#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace Swim::Rhi
{

	enum class GraphicsApi : std::uint8_t
	{
		Vulkan,
		D3D12,
		Metal,
		Null,
		Count,
	};

	enum class Format : std::uint16_t
	{
		Undefined = 0,

		R8Unorm,
		R8Snorm,
		R8Uint,
		R8Sint,
		R16Unorm,
		R16Snorm,
		R16Uint,
		R16Sint,
		R16Float,
		R32Uint,
		R32Sint,
		R32Float,

		RG8Unorm,
		RG8Snorm,
		RG8Uint,
		RG8Sint,
		RG16Unorm,
		RG16Snorm,
		RG16Uint,
		RG16Sint,
		RG16Float,
		RG32Uint,
		RG32Sint,
		RG32Float,

		RGB32Uint,
		RGB32Sint,
		RGB32Float,

		RGBA8Unorm,
		RGBA8UnormSrgb,
		RGBA8Snorm,
		RGBA8Uint,
		RGBA8Sint,
		BGRA8Unorm,
		BGRA8UnormSrgb,
		RGBA16Unorm,
		RGBA16Snorm,
		RGBA16Uint,
		RGBA16Sint,
		RGBA16Float,
		RGBA32Uint,
		RGBA32Sint,
		RGBA32Float,

		RGB10A2Unorm,
		RGB10A2Uint,
		R11G11B10Float,
		RGB9E5Float,

		D16Unorm,
		D24UnormS8Uint,
		D32Float,
		D32FloatS8Uint,

		BC1RGBAUnorm,
		BC1RGBAUnormSrgb,
		BC3Unorm,
		BC3UnormSrgb,
		BC4Unorm,
		BC4Snorm,
		BC5Unorm,
		BC5Snorm,
		BC6HUfloat,
		BC6HSfloat,
		BC7Unorm,
		BC7UnormSrgb,

		ETC2RGB8Unorm,
		ETC2RGB8UnormSrgb,
		ETC2RGBA8Unorm,
		ETC2RGBA8UnormSrgb,

		ASTC4x4Unorm,
		ASTC4x4UnormSrgb,
		ASTC6x6Unorm,
		ASTC6x6UnormSrgb,
		ASTC8x8Unorm,
		ASTC8x8UnormSrgb,
	};

	constexpr bool IsDepthFormat(Format format)
	{
		switch (format)
		{
		case Format::D16Unorm:
		case Format::D24UnormS8Uint:
		case Format::D32Float:
		case Format::D32FloatS8Uint:
			return true;
		default:
			return false;
		}
	}

	constexpr bool HasStencil(Format format)
	{
		return format == Format::D24UnormS8Uint || format == Format::D32FloatS8Uint;
	}

	enum class TextureDimension : std::uint8_t
	{
		Texture1D,
		Texture2D,
		Texture3D,
		TextureCube,
	};

	enum class TextureViewDimension : std::uint8_t
	{
		Texture1D,
		Texture1DArray,
		Texture2D,
		Texture2DArray,
		Texture3D,
		TextureCube,
		TextureCubeArray,
	};

	enum class MemoryPreference : std::uint8_t
	{
		DeviceLocal,
		CpuToGpu,
		GpuToCpu,
	};

	enum class BufferUsage : std::uint32_t
	{
		None = 0,
		TransferSource = 1u << 0,
		TransferDestination = 1u << 1,
		Vertex = 1u << 2,
		Index = 1u << 3,
		Uniform = 1u << 4,
		Storage = 1u << 5,
		Indirect = 1u << 6,
		ShaderDeviceAddress = 1u << 7,
		AccelerationStructureStorage = 1u << 8,
		AccelerationStructureBuildInput = 1u << 9,
	};

	enum class TextureUsage : std::uint32_t
	{
		None = 0,
		TransferSource = 1u << 0,
		TransferDestination = 1u << 1,
		Sampled = 1u << 2,
		Storage = 1u << 3,
		ColorAttachment = 1u << 4,
		DepthStencilAttachment = 1u << 5,
		TransientAttachment = 1u << 6,
	};

	enum class ResourceState : std::uint32_t
	{
		Undefined = 0,
		Common = 1u << 0,
		CopySource = 1u << 1,
		CopyDestination = 1u << 2,
		VertexBuffer = 1u << 3,
		IndexBuffer = 1u << 4,
		UniformBuffer = 1u << 5,
		ShaderRead = 1u << 6,
		ShaderWrite = 1u << 7,
		IndirectArgument = 1u << 8,
		ColorAttachment = 1u << 9,
		DepthStencilRead = 1u << 10,
		DepthStencilWrite = 1u << 11,
		Present = 1u << 12,
		AccelerationStructureRead = 1u << 13,
		AccelerationStructureWrite = 1u << 14,
	};

	template <typename Enum>
	constexpr Enum EnumOr(Enum left, Enum right)
	{
		return static_cast<Enum>(
			static_cast<std::uint32_t>(left) |
			static_cast<std::uint32_t>(right));
	}

	template <typename Enum>
	constexpr Enum EnumAnd(Enum left, Enum right)
	{
		return static_cast<Enum>(
			static_cast<std::uint32_t>(left) &
			static_cast<std::uint32_t>(right));
	}

	constexpr BufferUsage operator|(BufferUsage left, BufferUsage right)
	{
		return EnumOr(left, right);
	}

	constexpr TextureUsage operator|(TextureUsage left, TextureUsage right)
	{
		return EnumOr(left, right);
	}

	constexpr ResourceState operator|(ResourceState left, ResourceState right)
	{
		return EnumOr(left, right);
	}

	constexpr bool HasAny(ResourceState value, ResourceState mask)
	{
		return EnumAnd(value, mask) != ResourceState::Undefined;
	}

	enum class DescriptorType : std::uint8_t
	{
		Sampler,
		SampledTexture,
		StorageTexture,
		UniformBuffer,
		StorageBuffer,
		ReadOnlyStorageBuffer,
		AccelerationStructure,
	};

	enum class ShaderStageMask : std::uint32_t
	{
		None = 0,
		Vertex = 1u << 0,
		Fragment = 1u << 1,
		Compute = 1u << 2,
		Geometry = 1u << 3,
		Hull = 1u << 4,
		Domain = 1u << 5,
		Mesh = 1u << 6,
		Task = 1u << 7,
		RayGeneration = 1u << 8,
		Intersection = 1u << 9,
		AnyHit = 1u << 10,
		ClosestHit = 1u << 11,
		Miss = 1u << 12,
		Callable = 1u << 13,
		AllGraphics =
			(1u << 0) | (1u << 1) | (1u << 3) | (1u << 4) |
			(1u << 5) | (1u << 6) | (1u << 7),
		All = 0x3fffu,
	};

	constexpr ShaderStageMask operator|(ShaderStageMask left, ShaderStageMask right)
	{
		return EnumOr(left, right);
	}

	enum class LoadOp : std::uint8_t
	{
		Load,
		Clear,
		Discard,
	};

	enum class StoreOp : std::uint8_t
	{
		Store,
		Discard,
	};

	enum class CompareOp : std::uint8_t
	{
		Never,
		Less,
		Equal,
		LessEqual,
		Greater,
		NotEqual,
		GreaterEqual,
		Always,
	};

	enum class StencilOp : std::uint8_t
	{
		Keep,
		Zero,
		Replace,
		IncrementClamp,
		DecrementClamp,
		Invert,
		IncrementWrap,
		DecrementWrap,
	};

	enum class BlendFactor : std::uint8_t
	{
		Zero,
		One,
		SourceColor,
		OneMinusSourceColor,
		DestinationColor,
		OneMinusDestinationColor,
		SourceAlpha,
		OneMinusSourceAlpha,
		DestinationAlpha,
		OneMinusDestinationAlpha,
	};

	enum class BlendOp : std::uint8_t
	{
		Add,
		Subtract,
		ReverseSubtract,
		Min,
		Max,
	};

	enum class ColorWriteMask : std::uint8_t
	{
		None = 0,
		Red = 1u << 0,
		Green = 1u << 1,
		Blue = 1u << 2,
		Alpha = 1u << 3,
		All = 0x0fu,
	};

	constexpr ColorWriteMask operator|(ColorWriteMask left, ColorWriteMask right)
	{
		return static_cast<ColorWriteMask>(
			static_cast<std::uint8_t>(left) |
			static_cast<std::uint8_t>(right));
	}

	enum class CullMode : std::uint8_t
	{
		None,
		Front,
		Back,
	};

	enum class FrontFace : std::uint8_t
	{
		CounterClockwise,
		Clockwise,
	};

	enum class PrimitiveTopology : std::uint8_t
	{
		PointList,
		LineList,
		LineStrip,
		TriangleList,
		TriangleStrip,
	};

	enum class Filter : std::uint8_t
	{
		Nearest,
		Linear,
	};

	enum class SamplerAddressMode : std::uint8_t
	{
		Repeat,
		MirroredRepeat,
		ClampToEdge,
		ClampToBorder,
	};

	enum class SampleCount : std::uint8_t
	{
		X1 = 1,
		X2 = 2,
		X4 = 4,
		X8 = 8,
	};

	struct BlendAttachmentState
	{
		bool Enabled = false;
		BlendFactor SourceColor = BlendFactor::One;
		BlendFactor DestinationColor = BlendFactor::Zero;
		BlendOp ColorOperation = BlendOp::Add;
		BlendFactor SourceAlpha = BlendFactor::One;
		BlendFactor DestinationAlpha = BlendFactor::Zero;
		BlendOp AlphaOperation = BlendOp::Add;
		ColorWriteMask WriteMask = ColorWriteMask::All;
	};

	struct StencilFaceState
	{
		StencilOp Fail = StencilOp::Keep;
		StencilOp DepthFail = StencilOp::Keep;
		StencilOp Pass = StencilOp::Keep;
		CompareOp Compare = CompareOp::Always;
		std::uint32_t CompareMask = 0xffffffffu;
		std::uint32_t WriteMask = 0xffffffffu;
		std::uint32_t Reference = 0;
	};

	struct RasterState
	{
		CullMode Cull = CullMode::Back;
		FrontFace Winding = FrontFace::CounterClockwise;
		bool DepthClamp = false;
		bool Wireframe = false;
	};

	struct DepthStencilState
	{
		bool DepthTest = true;
		bool DepthWrite = true;
		CompareOp DepthCompare = CompareOp::LessEqual;
		bool StencilTest = false;
		StencilFaceState Front{};
		StencilFaceState Back{};
	};

	struct Viewport
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;
		float MinDepth = 0.0f;
		float MaxDepth = 1.0f;
	};

	struct ScissorRect
	{
		std::int32_t X = 0;
		std::int32_t Y = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
	};

	struct DescriptorBindingDesc
	{
		std::uint32_t Binding = 0;
		DescriptorType Type = DescriptorType::SampledTexture;
		std::uint32_t Count = 1;
		ShaderStageMask Stages = ShaderStageMask::None;
		bool VariableCount = false;
		bool PartiallyBound = false;
	};

	struct DescriptorSchemaDesc
	{
		std::uint32_t Space = 0;
		std::vector<DescriptorBindingDesc> Bindings;
	};

	struct DescriptorLimits
	{
		std::uint32_t MaxSampledTexturesPerStage = 0;
		std::uint32_t MaxSamplersPerStage = 0;
		std::uint32_t MaxStorageTexturesPerStage = 0;
		std::uint32_t MaxUniformBuffersPerStage = 0;
		std::uint32_t MaxStorageBuffersPerStage = 0;
		std::uint32_t MaxBindlessSampledTextures = 0;
		std::uint32_t MaxBindlessSamplers = 0;
	};

	struct QueueCapabilities
	{
		bool DedicatedCompute = false;
		bool DedicatedTransfer = false;
		bool AsyncCompute = false;
	};

	struct GraphicsCapabilities
	{
		DescriptorLimits Descriptors;
		QueueCapabilities Queues;

		std::uint32_t MaxPushConstantBytes = 0;
		std::uint32_t MaxColorAttachments = 0;
		std::uint32_t MaxSamples = 1;
		std::uint32_t SubgroupSize = 0;
		std::uint64_t MinUniformBufferOffsetAlignment = 1;
		std::uint64_t MinStorageBufferOffsetAlignment = 1;
		std::uint64_t TimestampFrequency = 0;

		bool DescriptorIndexing = false;
		bool DescriptorBuffer = false;
		bool BufferDeviceAddress = false;
		bool IndirectCount = false;
		bool SubgroupOperations = false;
		bool MeshShaders = false;
		bool TaskShaders = false;
		bool TimestampQueries = false;
		bool RayQuery = false;
		bool RayTracingPipeline = false;
		bool SparseResidency = false;
		bool MemoryBudget = false;
		bool BcTextureCompression = false;
		bool Etc2TextureCompression = false;
		bool AstcTextureCompression = false;
		bool HdrSwapchain = false;
	};

} // namespace Swim::Rhi
