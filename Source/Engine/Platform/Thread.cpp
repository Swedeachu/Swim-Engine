#include "Thread.h"
#include <algorithm>
#include <string>

#if defined(_WIN32)
	#include "Internal/WindowsApi.h"
#elif defined(__linux__)
	#include <pthread.h>
	#include <sched.h>
#endif

namespace Swim::Platform
{

	bool SetCurrentThreadName(std::string_view name)
	{
	#if defined(_WIN32)
		if (name.empty())
		{
			return false;
		}

		int required = MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
		if (required <= 0)
		{
			return false;
		}

		std::wstring wide(static_cast<size_t>(required), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), wide.data(), required);
		return SUCCEEDED(SetThreadDescription(GetCurrentThread(), wide.c_str()));
	#elif defined(__linux__)
		std::string truncated(name.substr(0, 15));
		return pthread_setname_np(pthread_self(), truncated.c_str()) == 0;
	#else
		(void)name;
		return false;
	#endif
	}

	bool SetCurrentThreadAffinity(uint64_t affinityMask)
	{
		if (affinityMask == 0)
		{
			return false;
		}

	#if defined(_WIN32)
		return SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(affinityMask)) != 0;
	#elif defined(__linux__)
		cpu_set_t cpuSet;
		CPU_ZERO(&cpuSet);
		for (uint32_t cpu = 0; cpu < 64; ++cpu)
		{
			if ((affinityMask & (uint64_t{ 1 } << cpu)) != 0)
			{
				CPU_SET(cpu, &cpuSet);
			}
		}
		return pthread_setaffinity_np(pthread_self(), sizeof(cpuSet), &cpuSet) == 0;
	#else
		(void)affinityMask;
		return false;
	#endif
	}

}
