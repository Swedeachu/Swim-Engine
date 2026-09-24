#include "Tests/Framework/Test.h"

#if defined(SWIM_FORWARD_OPAQUE_SPIRV_PATH) && defined(SWIM_FORWARD_TRANSPARENT_SPIRV_PATH) &&                                             \
	defined(SWIM_FORWARD_TRANSPARENT_SORT_SPIRV_PATH) && defined(SWIM_SHADOW_DEPTH_SPIRV_PATH) && defined(SWIM_SHADOW_MASKED_SPIRV_PATH)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	// GPU-assisted validation instruments every load, store and atomic through an access
	// chain into a Uniform or StorageBuffer descriptor, and warns
	// (GPUAV-Compile-time-general-buffer) when one module has more than 75. The native
	// smokes treat that warning as a failure, so every renderer program stays below it.
	// This counts conservatively: the layer can skip some accesses, never add any.
	constexpr std::uint32_t GpuAvGeneralBufferLimit = 75;

	std::vector<std::uint32_t> ReadWords(const char* path)
	{
		std::ifstream file(path, std::ios::binary);
		const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		std::vector<std::uint32_t> words(bytes.size() / 4);
		if (!words.empty())
		{
			std::memcpy(words.data(), bytes.data(), words.size() * 4);
		}
		return words;
	}

	std::uint32_t InstrumentedBufferAccesses(const std::vector<std::uint32_t>& words)
	{
		constexpr std::uint32_t OpVariable = 59, OpLoad = 61, OpStore = 62, OpAccessChain = 65, OpInBoundsAccessChain = 66,
								OpPtrAccessChain = 67, OpAtomicLoad = 227, OpAtomicStore = 228, OpAtomicLast = 242;
		constexpr std::uint32_t Uniform = 2, StorageBuffer = 12;
		std::unordered_map<std::uint32_t, std::uint32_t> storage; // Variable -> storage class.
		std::unordered_map<std::uint32_t, std::uint32_t> chains;  // Access chain -> base.
		std::vector<std::uint32_t> pointers;
		for (std::size_t at = 5; at < words.size();)
		{
			const std::uint32_t count = words[at] >> 16;
			const std::uint32_t opcode = words[at] & 0xffffu;
			if (count == 0 || at + count > words.size())
			{
				break;
			}
			if (opcode == OpVariable && count >= 4)
			{
				storage[words[at + 2]] = words[at + 3];
			}
			else if ((opcode == OpAccessChain || opcode == OpInBoundsAccessChain || opcode == OpPtrAccessChain) && count >= 4)
			{
				chains[words[at + 2]] = words[at + 3];
			}
			else if (opcode == OpLoad && count >= 4)
			{
				pointers.push_back(words[at + 3]);
			}
			else if ((opcode == OpStore || opcode == OpAtomicStore) && count >= 3)
			{
				pointers.push_back(words[at + 1]);
			}
			else if (opcode >= OpAtomicLoad && opcode <= OpAtomicLast && count >= 4)
			{
				pointers.push_back(words[at + 3]);
			}
			at += count;
		}
		std::uint32_t accesses = 0;
		for (auto pointer : pointers)
		{
			if (!chains.contains(pointer))
			{
				continue;
			}
			for (int depth = 0; depth < 64 && chains.contains(pointer); ++depth)
			{
				pointer = chains.at(pointer);
			}
			const auto found = storage.find(pointer);
			accesses += found != storage.end() && (found->second == Uniform || found->second == StorageBuffer) ? 1u : 0u;
		}
		return accesses;
	}
} // namespace

SWIM_TEST("ShaderCompiler.GpuAvBudget", "RendererProgramsStayBelowTheGpuAvInstrumentationWarning")
{
	const struct
	{
		const char* Name;
		const char* Path;
	} programs[]{
		{ "SwimForwardOpaque", SWIM_FORWARD_OPAQUE_SPIRV_PATH },
		{ "SwimForwardTransparent", SWIM_FORWARD_TRANSPARENT_SPIRV_PATH },
		{ "SwimForwardTransparentSort", SWIM_FORWARD_TRANSPARENT_SORT_SPIRV_PATH },
		{ "SwimShadowDepth", SWIM_SHADOW_DEPTH_SPIRV_PATH },
		{ "SwimShadowMasked", SWIM_SHADOW_MASKED_SPIRV_PATH },
	};

	for (const auto& program : programs)
	{
		const auto words = ReadWords(program.Path);
		SWIM_REQUIRE_MESSAGE(words.size() > 5 && words[0] == 0x07230203u, std::string("missing SPIR-V: ") + program.Path);
		const auto accesses = InstrumentedBufferAccesses(words);
		std::printf("             [GPU-AV budget] %-28s %3u buffer accesses (limit %u)\n", program.Name, accesses, GpuAvGeneralBufferLimit);
		SWIM_CHECK_MESSAGE(accesses > 0u, std::string(program.Name) + " has no buffer accesses: the counter is broken");
		SWIM_CHECK_MESSAGE(accesses <= GpuAvGeneralBufferLimit,
			std::string(program.Name) + " would trigger GPUAV-Compile-time-general-buffer under GPU-assisted validation");
	}
}
#endif
