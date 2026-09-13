#pragma once

#include <cstdint>
#include <limits>

namespace Swim::Render
{
	// Handles are local to one graph definition, never native RHI pointers.
	enum class GraphKind : std::uint8_t
	{
		Buffer,
		Texture,
		Pass
	};

	template <GraphKind Kind> struct GraphHandle
	{
		std::uint64_t Graph = 0;
		std::uint32_t Index = std::numeric_limits<std::uint32_t>::max();
		bool operator==(const GraphHandle&) const = default;
	};

	using GraphBuffer = GraphHandle<GraphKind::Buffer>;
	using GraphTexture = GraphHandle<GraphKind::Texture>;
	using GraphPass = GraphHandle<GraphKind::Pass>;
	enum class GraphAccess : std::uint8_t
	{
		Read,
		Write,
		ReadWrite
	};
} // namespace Swim::Render
