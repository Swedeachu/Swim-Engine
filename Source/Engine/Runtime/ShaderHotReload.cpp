#include "Engine/Runtime/ShaderHotReload.h"

#include "Engine/Platform/Process.h"
#include "Engine/Platform/Thread.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Engine
{

	namespace
	{

		namespace fs = std::filesystem;

		std::vector<std::string> Split(std::string_view text, char separator)
		{
			std::vector<std::string> parts;
			std::size_t start = 0;

			while (start <= text.size())
			{
				const auto end = std::min(text.find(separator, start), text.size());
				parts.emplace_back(text.substr(start, end - start));
				start = end + 1;
			}

			return parts;
		}

		// A make-style depfile: "target: dep dep ...", with '\' escaping the next character
		// (':', ' ', '\') and a backslash-newline continuing the line.
		std::vector<fs::path> ReadDepfile(const fs::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			std::vector<fs::path> files;

			if (!file)
			{
				return files;
			}

			const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			std::string token;
			bool targets = true;

			const auto flush = [&]
			{
				if (!token.empty() && !targets)
				{
					files.emplace_back(fs::path(token));
				}

				token.clear();
			};

			for (std::size_t i = 0; i < text.size(); ++i)
			{
				const char c = text[i];

				if (c == '\\' && i + 1 < text.size())
				{
					const char next = text[++i];

					if (next == '\n' || next == '\r')
					{
						flush();
					}
					else
					{
						token.push_back(next);
					}
				}
				else if (c == ':' && targets && (i + 1 == text.size() || std::isspace(static_cast<unsigned char>(text[i + 1]))))
				{
					token.clear();
					targets = false;
				}
				else if (std::isspace(static_cast<unsigned char>(c)))
				{
					flush();
				}
				else
				{
					token.push_back(c);
				}
			}

			flush();
			return files;
		}

		struct Hasher
		{
			std::uint64_t Value = 1469598103934665603ull;

			void Add(const void* data, std::size_t size)
			{
				const auto* bytes = static_cast<const unsigned char*>(data);

				for (std::size_t i = 0; i < size; ++i)
				{
					Value = (Value ^ bytes[i]) * 1099511628211ull;
				}
			}

			void Add(std::string_view text)
			{
				Add(text.data(), text.size());
				Add("\0", 1);
			}

			void AddFile(const fs::path& path)
			{
				Add(path.generic_string());
				std::ifstream file(path, std::ios::binary);
				char buffer[16384];

				while (file)
				{
					file.read(buffer, sizeof(buffer));
					Add(buffer, static_cast<std::size_t>(file.gcount()));
				}
			}
		};

	} // namespace

	struct ShaderHotReload::Impl
	{
		struct Program
		{
			std::string Name;
			fs::path Source;
			std::string Entry;
			std::vector<std::string> Includes;
			std::vector<std::string> Defines;
			std::vector<fs::path> Files; // The source and everything it includes.
			std::vector<fs::file_time_type> Times;
		};

		struct Result
		{
			std::string Name;
			bool Compiled = false;
			fs::path Bytecode;
			fs::path Reflection;
			std::string Log;
		};

		ShaderHotReloadDesc desc;
		Swim::Commands::CommandRegistry& commands;
		FrameRenderer& renderer;
		Swim::Commands::ConsoleVariable* watch = nullptr;
		Swim::Commands::ConsoleVariable::SubscriptionId watchSubscription{};
		std::vector<Program> programs; // The worker's once it runs.
		std::vector<std::string> names;
		std::mutex mutex;
		std::condition_variable wake;
		bool stop = false;
		std::vector<std::string> requests;
		std::vector<Result> results;
		std::atomic<bool> watching{ true };
		std::thread worker;

		Impl(ShaderHotReloadDesc descValue, Swim::Commands::CommandRegistry& commandsValue, FrameRenderer& rendererValue)
			: desc(std::move(descValue)), commands(commandsValue), renderer(rendererValue)
		{
		}

		bool ReadManifest()
		{
			std::ifstream file(desc.Sources, std::ios::binary);
			std::string line;

			while (file && std::getline(file, line))
			{
				if (!line.empty() && line.back() == '\r')
				{
					line.pop_back();
				}

				const auto fields = Split(line, '\t');

				if (fields.size() < 6 || fields[0].empty())
				{
					continue;
				}

				Program program;
				program.Name = fields[0];
				program.Source = fs::path(fields[1]);
				program.Entry = fields[2];

				for (auto& include : Split(fields[3], '|'))
				{
					if (!include.empty())
					{
						program.Includes.push_back(std::move(include));
					}
				}

				for (auto& define : Split(fields[4], '|'))
				{
					if (!define.empty())
					{
						program.Defines.push_back(std::move(define));
					}
				}

				program.Files = ReadDepfile(fs::path(fields[5]));

				if (std::find(program.Files.begin(), program.Files.end(), program.Source) == program.Files.end())
				{
					program.Files.insert(program.Files.begin(), program.Source);
				}

				names.push_back(program.Name);
				programs.push_back(std::move(program));
			}

			return !programs.empty();
		}

		static void Snapshot(Program& program)
		{
			program.Times.clear();

			for (const auto& file : program.Files)
			{
				std::error_code error;
				const auto time = fs::last_write_time(file, error);
				program.Times.push_back(error ? fs::file_time_type::min() : time);
			}
		}

		// stamps: this poll's write times, so a file shared by many programs (an include) is
		// read once per poll.
		static bool Changed(const Program& program, std::unordered_map<fs::path::string_type, fs::file_time_type>& stamps)
		{
			for (std::size_t i = 0; i < program.Files.size(); ++i)
			{
				auto [found, added] = stamps.try_emplace(program.Files[i].native());

				if (added)
				{
					std::error_code error;
					const auto time = fs::last_write_time(program.Files[i], error);
					found->second = error ? fs::file_time_type::min() : time;
				}

				if (found->second != program.Times[i])
				{
					return true;
				}
			}

			return false;
		}

		Result Compile(Program& program) const
		{
			Result result;
			result.Name = program.Name;
			std::vector<std::string> arguments{ desc.Compiler.string(), program.Source.string(), "-lang", "slang", "-target", "spirv",
				"-profile", "spirv_1_5", "-matrix-layout-column-major", "-preserve-params", "-fvk-use-entrypoint-name",
				"-default-image-format-unknown" };

			// Matches the SWIM_SHADER_DEBUG_INFO build option for reloaded programs.
			static const bool debugInfo = []
			{
				const char* value = std::getenv("SWIM_SHADER_DEBUG_INFO");
				return value != nullptr && std::string_view(value) != "0" && std::string_view(value) != "";
			}();

			if (debugInfo)
			{
				arguments.insert(arguments.end(), { "-g2", "-debug-info-include-source" });
			}

			if (!program.Entry.empty())
			{
				arguments.insert(arguments.end(), { "-entry", program.Entry });
			}

			for (const auto& include : program.Includes)
			{
				arguments.insert(arguments.end(), { "-I", include });
			}

			for (const auto& define : program.Defines)
			{
				arguments.push_back("-D" + define);
			}

			Hasher hash;

			for (const auto& argument : arguments)
			{
				hash.Add(argument);
			}

			for (const auto& file : program.Files)
			{
				hash.AddFile(file);
			}

			char hex[17];
			std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash.Value));
			std::error_code error;
			fs::create_directories(desc.CacheRoot, error);
			const auto base = desc.CacheRoot / (program.Name + "-" + hex);
			const auto spv = fs::path(base).concat(".spv");
			const auto json = fs::path(base).concat(".reflection.json");
			const auto dep = fs::path(base).concat(".d");

			if (!fs::is_regular_file(spv, error) || !fs::is_regular_file(json, error))
			{
				const auto tmpSpv = fs::path(base).concat(".tmp.spv");
				const auto tmpJson = fs::path(base).concat(".tmp.json");
				const auto tmpDep = fs::path(base).concat(".tmp.d");
				arguments.insert(arguments.end(),
					{ "-reflection-json", tmpJson.string(), "-depfile", tmpDep.string(), "-o", tmpSpv.string() });
				const auto run = Swim::Platform::RunProcess(arguments);

				if (!run.Started || run.ExitCode != 0 || !fs::is_regular_file(tmpSpv, error) || !fs::is_regular_file(tmpJson, error))
				{
					result.Log = run.Started ? run.Output : "slangc did not start: " + run.Error;
					fs::remove(tmpSpv, error);
					fs::remove(tmpJson, error);
					fs::remove(tmpDep, error);
					return result;
				}

				fs::rename(tmpDep, dep, error);
				fs::rename(tmpJson, json, error);
				fs::rename(tmpSpv, spv, error);

				if (error)
				{
					result.Log = "cannot write " + spv.string() + ": " + error.message();
					return result;
				}
			}

			if (auto files = ReadDepfile(dep); !files.empty())
			{
				if (std::find(files.begin(), files.end(), program.Source) == files.end())
				{
					files.insert(files.begin(), program.Source);
				}

				program.Files = std::move(files);
			}

			result.Compiled = true;
			result.Bytecode = spv;
			result.Reflection = json;
			return result;
		}

		void Run()
		{
			Swim::Platform::SetCurrentThreadName("Shader hot reload");

			for (auto& program : programs)
			{
				Snapshot(program);
			}

			std::unique_lock lock(mutex);

			while (!stop)
			{
				wake.wait_for(lock, std::chrono::seconds(1),
					[&]
					{
						return stop || !requests.empty();
					});

				if (stop)
				{
					break;
				}

				const auto asked = std::move(requests);
				requests.clear();
				lock.unlock();
				const bool all = std::find(asked.begin(), asked.end(), "all") != asked.end();
				std::vector<Program*> due;
				std::unordered_map<fs::path::string_type, fs::file_time_type> stamps;

				for (auto& program : programs)
				{
					const bool requested = all || std::find(asked.begin(), asked.end(), program.Name) != asked.end();

					if (requested || (watching && Changed(program, stamps)))
					{
						due.push_back(&program);
					}
				}

				if (!due.empty() && asked.empty())
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Let the editor finish writing.
				}

				for (auto* program : due)
				{
					auto result = Compile(*program);
					Snapshot(*program);
					std::lock_guard guard(mutex);
					results.push_back(std::move(result));
				}

				lock.lock();
			}
		}
	};

	ShaderHotReload::ShaderHotReload(ShaderHotReloadDesc desc, Swim::Commands::CommandRegistry& commands, FrameRenderer& renderer)
		: impl(std::make_unique<Impl>(std::move(desc), commands, renderer))
	{
		std::error_code error;

		if (!fs::is_regular_file(impl->desc.Compiler, error) || !impl->ReadManifest())
		{
			return;
		}

		impl->watch = &commands.AddBool("shader_hot_reload", true, "Recompiles runtime shaders when their files change", "Developer/Shaders");
		impl->watching = impl->watch->GetBool();
		impl->watchSubscription = impl->watch->Subscribe(
			[impl = impl.get()](const Swim::Commands::ConsoleVariable& variable)
			{
				impl->watching = variable.GetBool();
			});
		commands.Register({ "shader_reload", "Recompiles a runtime shader program, or all of them", "Developer/Shaders", "<name|all>" },
			[this](const std::vector<std::string>& args)
			{
				if (args.size() != 1)
				{
					throw std::invalid_argument("usage: shader_reload <name|all>");
				}

				if (!Reload(args[0]))
				{
					throw std::invalid_argument("no runtime shader program named " + args[0]);
				}
			});
		impl->worker = std::thread(
			[impl = impl.get()]
			{
				impl->Run();
			});
	}

	ShaderHotReload::~ShaderHotReload()
	{
		if (!impl->worker.joinable())
		{
			return;
		}

		{
			std::lock_guard guard(impl->mutex);
			impl->stop = true;
		}

		impl->wake.notify_all();
		impl->worker.join();
		impl->watch->Unsubscribe(impl->watchSubscription);
		impl->commands.Unregister("shader_reload");
	}

	bool ShaderHotReload::IsActive() const
	{
		return impl->worker.joinable();
	}

	bool ShaderHotReload::Reload(std::string_view name)
	{
		if (!IsActive() || (name != "all" && std::find(impl->names.begin(), impl->names.end(), name) == impl->names.end()))
		{
			return false;
		}

		{
			std::lock_guard guard(impl->mutex);
			impl->requests.emplace_back(name);
		}

		impl->wake.notify_all();
		return true;
	}

	void ShaderHotReload::Update()
	{
		std::vector<Impl::Result> finished;

		{
			std::lock_guard guard(impl->mutex);
			finished.swap(impl->results);
		}

		for (auto& result : finished)
		{
			if (!result.Compiled)
			{
				std::cerr << "[Shaders] " << result.Name << " did not compile; the previous program stays.\n" << result.Log;

				if (!result.Log.empty() && result.Log.back() != '\n')
				{
					std::cerr << '\n';
				}

				continue;
			}

			RuntimeShaderDesc program;
			program.Name = result.Name;
			program.Bytecode = result.Bytecode;
			program.Reflection = result.Reflection;
			impl->renderer.ReplaceProgram(std::move(program),
				[name = result.Name](std::string_view error)
				{
					if (error.empty())
					{
						std::cout << "[Shaders] Reloaded " << name << '\n';
					}
					else
					{
						std::cerr << "[Shaders] " << name << " was not swapped in: " << error << '\n';
					}
				});
		}
	}

} // namespace Engine
