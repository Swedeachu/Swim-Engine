// SwimStackSampler: a CPU sampling profiler that needs no administrator rights (no ETW).
//
// It attaches to a running process of the same user, and every interval suspends each thread
// that used CPU since the last sample (QueryThreadCycleTime), walks its stack (StackWalk64 over
// the x64 unwind data) and resumes it. At the end it symbolizes the stacks with DbgHelp (the
// PDBs next to the modules, or --symbols) and writes:
//
//   <out>.folded      one line per unique stack: "thread;outer;...;leaf weight" (flame graphs)
//   <out>.txt         top functions by exclusive and inclusive CPU share, and the share per thread
//
// Each sample is weighted by the CPU cycles its thread used since its previous sample, so a
// thread that ran briefly and is now waiting counts little: the shares estimate CPU time.
//
// Usage: SwimStackSampler --pid <id> [--seconds 30] [--interval-ms 2] [--delay 0] [--out prefix]
//                         [--symbols path] [--top 40]
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dbghelp.h>
#include <timeapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{

	struct Options
	{
		DWORD Pid = 0;
		double Seconds = 30.0;
		double Delay = 0.0;
		int IntervalMs = 2;
		int Top = 40;
		std::string Out = "stacks";
		std::string Symbols;
	};

	struct ThreadState
	{
		HANDLE Handle = nullptr;
		ULONG64 Cycles = 0;
		std::string Name;
	};

	struct StackKey
	{
		DWORD Thread = 0;
		std::vector<DWORD64> Frames; // Leaf first.

		bool operator<(const StackKey& other) const { return Thread != other.Thread ? Thread < other.Thread : Frames < other.Frames; }
	};

	bool ParseOptions(int argc, char** argv, Options& options)
	{
		for (int i = 1; i < argc; ++i)
		{
			const std::string arg = argv[i];
			const char* value = i + 1 < argc ? argv[i + 1] : nullptr;

			if (!value)
			{
				return false;
			}

			if (arg == "--pid")
			{
				options.Pid = static_cast<DWORD>(std::stoul(value));
			}
			else if (arg == "--seconds")
			{
				options.Seconds = std::stod(value);
			}
			else if (arg == "--delay")
			{
				options.Delay = std::stod(value);
			}
			else if (arg == "--interval-ms")
			{
				options.IntervalMs = std::max(1, std::stoi(value));
			}
			else if (arg == "--top")
			{
				options.Top = std::max(1, std::stoi(value));
			}
			else if (arg == "--out")
			{
				options.Out = value;
			}
			else if (arg == "--symbols")
			{
				options.Symbols = value;
			}
			else
			{
				return false;
			}

			++i;
		}

		return options.Pid != 0;
	}

	std::string Narrow(const wchar_t* text)
	{
		const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
		std::string result(size > 0 ? size - 1 : 0, '\0');

		if (size > 1)
		{
			WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
		}

		return result;
	}

	std::string ThreadName(HANDLE thread, DWORD id)
	{
		using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
		static const auto getDescription =
			reinterpret_cast<GetThreadDescriptionFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
		std::string name;
		PWSTR description = nullptr;

		if (getDescription && SUCCEEDED(getDescription(thread, &description)) && description)
		{
			name = Narrow(description);
			LocalFree(description);
		}

		return name.empty() ? "thread " + std::to_string(id) : name;
	}

	// Opens threads that appeared since the last refresh; forgets exited ones.
	void RefreshThreads(DWORD pid, std::unordered_map<DWORD, ThreadState>& threads)
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

		if (snapshot == INVALID_HANDLE_VALUE)
		{
			return;
		}

		std::unordered_set<DWORD> alive;
		THREADENTRY32 entry{ sizeof(entry) };

		for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry))
		{
			if (entry.th32OwnerProcessID != pid)
			{
				continue;
			}

			alive.insert(entry.th32ThreadID);

			if (threads.contains(entry.th32ThreadID))
			{
				continue;
			}

			HANDLE handle =
				OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);

			if (!handle)
			{
				continue;
			}

			ThreadState state;
			state.Handle = handle;
			QueryThreadCycleTime(handle, &state.Cycles);
			state.Name = ThreadName(handle, entry.th32ThreadID);
			threads.emplace(entry.th32ThreadID, std::move(state));
		}

		CloseHandle(snapshot);

		for (auto it = threads.begin(); it != threads.end();)
		{
			if (!alive.contains(it->first))
			{
				CloseHandle(it->second.Handle);
				it = threads.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	bool WalkStack(HANDLE process, HANDLE thread, std::vector<DWORD64>& frames)
	{
		frames.clear();
		CONTEXT context{};
		context.ContextFlags = CONTEXT_FULL;

		if (!GetThreadContext(thread, &context))
		{
			return false;
		}

		STACKFRAME64 frame{};
		frame.AddrPC.Offset = context.Rip;
		frame.AddrPC.Mode = AddrModeFlat;
		frame.AddrFrame.Offset = context.Rbp;
		frame.AddrFrame.Mode = AddrModeFlat;
		frame.AddrStack.Offset = context.Rsp;
		frame.AddrStack.Mode = AddrModeFlat;

		while (frames.size() < 96 &&
			StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
				nullptr))
		{
			if (frame.AddrPC.Offset == 0)
			{
				break;
			}

			frames.push_back(frame.AddrPC.Offset);
		}

		return !frames.empty();
	}

	class Symbolizer
	{

	  public:
		explicit Symbolizer(HANDLE process) : process(process) {}

		const std::string& Name(DWORD64 address)
		{
			if (const auto it = names.find(address); it != names.end())
			{
				return it->second;
			}

			alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
			auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = MAX_SYM_NAME;
			DWORD64 displacement = 0;
			IMAGEHLP_MODULE64 module{ sizeof(module) };
			std::string moduleName = SymGetModuleInfo64(process, address, &module) ? module.ModuleName : "?";
			std::string name = moduleName + "!";

			if (SymFromAddr(process, address, &displacement, symbol))
			{
				name += symbol->Name;
			}
			else
			{
				char hex[32];
				std::snprintf(hex, sizeof(hex), "0x%llx", static_cast<unsigned long long>(address - module.BaseOfImage));
				name += hex;
			}

			return names.emplace(address, std::move(name)).first->second;
		}

	  private:
		HANDLE process;
		std::unordered_map<DWORD64, std::string> names;
	};

	std::string Sanitize(std::string text)
	{
		std::replace(text.begin(), text.end(), ';', ':');
		std::replace(text.begin(), text.end(), ' ', '_');
		return text;
	}

} // namespace

int main(int argc, char** argv)
{
	Options options;

	if (!ParseOptions(argc, argv, options))
	{
		std::cerr << "usage: SwimStackSampler --pid <id> [--seconds 30] [--interval-ms 2] [--delay 0] [--out prefix] [--symbols path] "
					 "[--top 40]\n";
		return 2;
	}

	HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, options.Pid);

	if (!process)
	{
		std::cerr << "cannot open process " << options.Pid << " (error " << GetLastError() << ")\n";
		return 1;
	}

	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);

	if (!SymInitialize(process, options.Symbols.empty() ? nullptr : options.Symbols.c_str(), TRUE))
	{
		std::cerr << "SymInitialize failed (error " << GetLastError() << ")\n";
		return 1;
	}

	if (options.Delay > 0.0)
	{
		std::this_thread::sleep_for(std::chrono::duration<double>(options.Delay));
	}

	timeBeginPeriod(1);
	const DWORD self = GetCurrentThreadId();
	std::unordered_map<DWORD, ThreadState> threads;
	std::map<StackKey, double> stacks;
	double cyclesTotal = 0.0;
	std::uint64_t ticks = 0;
	std::uint64_t samples = 0;
	std::uint64_t idle = 0;
	std::vector<DWORD64> frames;
	StackKey key;
	using Clock = std::chrono::steady_clock;
	const auto start = Clock::now();
	auto lastRefresh = start - std::chrono::seconds(1);
	DWORD exitCode = STILL_ACTIVE;

	while (std::chrono::duration<double>(Clock::now() - start).count() < options.Seconds && GetExitCodeProcess(process, &exitCode) &&
		exitCode == STILL_ACTIVE)
	{
		if (Clock::now() - lastRefresh > std::chrono::milliseconds(250))
		{
			RefreshThreads(options.Pid, threads);
			SymRefreshModuleList(process);
			lastRefresh = Clock::now();
		}

		++ticks;

		for (auto& [id, thread] : threads)
		{
			ULONG64 cycles = 0;

			if (id == self || !QueryThreadCycleTime(thread.Handle, &cycles))
			{
				continue;
			}

			if (cycles == thread.Cycles)
			{
				++idle; // Waited the whole interval: not CPU time.
				continue;
			}

			const double used = double(cycles - thread.Cycles);
			thread.Cycles = cycles;

			if (SuspendThread(thread.Handle) == static_cast<DWORD>(-1))
			{
				continue;
			}

			const bool walked = WalkStack(process, thread.Handle, frames);
			ResumeThread(thread.Handle);

			if (walked)
			{
				key.Thread = id;
				key.Frames = frames;
				stacks[key] += used;
				cyclesTotal += used;
				++samples;
			}
		}

		Sleep(static_cast<DWORD>(options.IntervalMs));
	}

	timeEndPeriod(1);
	const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();

	// Symbolize: one name per frame (inlined frames show as their caller).
	Symbolizer symbols(process);
	std::unordered_map<std::string, double> exclusive;
	std::unordered_map<std::string, double> inclusive;
	std::map<std::string, double> perThread;
	std::unordered_map<DWORD, std::string> threadNames;

	for (const auto& [id, thread] : threads)
	{
		threadNames[id] = thread.Name;
	}

	std::ofstream folded(options.Out + ".folded");
	std::unordered_set<std::string> seen;

	for (const auto& [stack, count] : stacks)
	{
		const auto nameIt = threadNames.find(stack.Thread);
		const std::string thread = nameIt != threadNames.end() ? nameIt->second : "thread " + std::to_string(stack.Thread);
		perThread[thread] += count;
		std::string line = Sanitize(thread);
		seen.clear();

		for (auto it = stack.Frames.rbegin(); it != stack.Frames.rend(); ++it)
		{
			const std::string& name = symbols.Name(*it);
			line += ';';
			line += Sanitize(name);

			if (seen.insert(name).second)
			{
				inclusive[name] += count;
			}
		}

		exclusive[symbols.Name(stack.Frames.front())] += count;
		// Weight in units of 1e5 cycles (about 30 us at 3 GHz), at least 1.
		folded << line << ' ' << std::max<std::uint64_t>(1, static_cast<std::uint64_t>(count / 1.0e5)) << '\n';
	}

	auto sorted = [](const auto& table)
	{
		std::vector<std::pair<std::string, double>> rows(table.begin(), table.end());
		std::sort(rows.begin(), rows.end(),
			[](const auto& a, const auto& b)
			{
				return a.second != b.second ? a.second > b.second : a.first < b.first;
			});
		return rows;
	};

	std::ofstream report(options.Out + ".txt");
	auto both = [&](const std::string& text)
	{
		report << text;
		std::cout << text;
	};
	char line[1024];
	std::snprintf(line, sizeof(line),
		"Sampled pid %lu for %.1f s every %d ms: %llu ticks, %llu CPU samples (%llu idle thread checks skipped), %zu unique stacks\n\n",
		static_cast<unsigned long>(options.Pid), elapsed, options.IntervalMs, static_cast<unsigned long long>(ticks),
		static_cast<unsigned long long>(samples), static_cast<unsigned long long>(idle), stacks.size());
	both(line);
	const double total = std::max(1.0, cyclesTotal);
	std::snprintf(
		line, sizeof(line), "%.3g G cycles sampled (the shares below are of these)\n\nCPU share per thread\n", cyclesTotal / 1.0e9);
	both(line);

	for (const auto& [name, count] : sorted(perThread))
	{
		std::snprintf(line, sizeof(line), "  %6.2f %%  %s\n", 100.0 * count / total, name.c_str());
		both(line);
	}

	for (const auto& [title, table] : { std::pair{ "exclusive", &exclusive }, std::pair{ "inclusive", &inclusive } })
	{
		both(std::string("\nTop functions by ") + title + " CPU share\n");
		int shown = 0;

		for (const auto& [name, count] : sorted(*table))
		{
			if (shown++ == options.Top)
			{
				break;
			}

			std::snprintf(line, sizeof(line), "  %6.2f %%  %s\n", 100.0 * count / total, name.substr(0, 900).c_str());
			both(line);
		}
	}

	for (const auto& [id, thread] : threads)
	{
		CloseHandle(thread.Handle);
	}

	SymCleanup(process);
	CloseHandle(process);
	return samples > 0 ? 0 : 1;
}
