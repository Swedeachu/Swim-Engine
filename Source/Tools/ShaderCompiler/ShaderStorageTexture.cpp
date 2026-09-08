#include "Tools/ShaderCompiler/ShaderStorageTexture.h"

#include <array>

namespace Swim::ShaderCompiler
{

	Rhi::Format GetRhiStorageTextureFormat(const ShaderBindingReflection& parameter)
	{
		struct FormatEntry
		{
			std::string_view Name;
			Rhi::Format Format;
			std::string_view Scalar;
			std::uint32_t Components;
		};
		// Exact Slang format qualifiers; never infer image storage format from float4.
		static constexpr std::array<FormatEntry, 13> formats{{
			{ "r32f", Rhi::Format::R32Float, "float32", 1 },
			{ "r32ui", Rhi::Format::R32Uint, "uint32", 1 },
			{ "r32i", Rhi::Format::R32Sint, "int32", 1 },
			{ "rgba32f", Rhi::Format::RGBA32Float, "float32", 4 },
			{ "rgba32ui", Rhi::Format::RGBA32Uint, "uint32", 4 },
			{ "rgba32i", Rhi::Format::RGBA32Sint, "int32", 4 },
			{ "rgba16f", Rhi::Format::RGBA16Float, "float32", 4 },
			{ "rgba16ui", Rhi::Format::RGBA16Uint, "uint32", 4 },
			{ "rgba16i", Rhi::Format::RGBA16Sint, "int32", 4 },
			{ "rgba8", Rhi::Format::RGBA8Unorm, "float32", 4 },
			{ "rgba8_snorm", Rhi::Format::RGBA8Snorm, "float32", 4 },
			{ "rgba8ui", Rhi::Format::RGBA8Uint, "uint32", 4 },
			{ "rgba8i", Rhi::Format::RGBA8Sint, "int32", 4 }
		}};
		for (const auto& format : formats)
		{
			if (parameter.ResourceFormat == format.Name && parameter.ResourceScalarType == format.Scalar &&
				parameter.ResourceComponentCount == format.Components)
			{
				return format.Format;
			}
		}
		return Rhi::Format::Undefined;
	}

} // namespace Swim::ShaderCompiler
