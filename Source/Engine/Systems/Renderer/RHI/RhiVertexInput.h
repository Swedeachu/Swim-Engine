#pragma once

#include "Engine/Systems/Renderer/RHI/RhiTypes.h"

namespace Swim::Rhi
{

	enum class VertexInputRate : std::uint8_t
	{
		Vertex,
		Instance, // One element per instance; no custom divisor.
	};

	// Attribute offsets, strides and bound offsets require component alignment.
	// Every attribute must fit a nonzero stride; zero stride repeats one record.
	struct VertexBindingDesc
	{
		std::uint32_t Slot = 0;
		std::uint32_t Stride = 0; // Zero repeats the same record for every element.
		VertexInputRate Rate = VertexInputRate::Vertex;
	};

	struct VertexAttributeDesc
	{
		std::uint32_t Location = 0; // Shader location, independent of the buffer slot.
		std::uint32_t Slot = 0;
		Format DataFormat = Format::Undefined;
		std::uint32_t Offset = 0;
	};

} // namespace Swim::Rhi
