#pragma once
#include <cstdint>

namespace Swim::Render
{

	// Per-object flags stored in GpuInstanceRecord::Flags. The low 16 bits belong
	// to producers; GpuScene owns the high bits and keeps them consistent.
	enum class RenderObjectFlags : std::uint32_t
	{
		None = 0,
		Visible = 1u << 0,
		CastShadows = 1u << 1,
		Static = 1u << 2, // Hint: the transform rarely changes.
		Selectable = 1u << 3,
		// The first-person overlay layer: drawn only in the main camera view, never culled
		// there, never in shadow, probe or capture views, projected with the overlay's field
		// of view and near plane into a depth range in front of the whole scene (it never
		// clips into nearby geometry). Lit and shadowed at its world position.
		Overlay = 1u << 4,

		// GpuScene-owned: the row holds a live object.
		Live = 1u << 30,
		// GpuScene-owned: MeshIndex/MeshGeneration name a mesh. A GPU renderer
		// draws a row only when Live, HasMesh and Visible are all set.
		HasMesh = 1u << 31,

		ProducerMask = 0xffffu,
		Default = Visible | CastShadows,
	};

	constexpr RenderObjectFlags operator|(RenderObjectFlags a, RenderObjectFlags b)
	{
		return static_cast<RenderObjectFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
	}

	constexpr RenderObjectFlags operator&(RenderObjectFlags a, RenderObjectFlags b)
	{
		return static_cast<RenderObjectFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
	}

	constexpr bool HasAny(RenderObjectFlags value, RenderObjectFlags mask)
	{
		return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(mask)) != 0;
	}

} // namespace Swim::Render
