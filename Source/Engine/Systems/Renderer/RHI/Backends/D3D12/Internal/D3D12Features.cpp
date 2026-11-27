#include "Engine/Systems/Renderer/RHI/Backends/D3D12/Internal/D3D12DeviceState.h"

#include <sstream>

namespace Swim::RhiD3D12
{

	namespace
	{

		template <typename T> bool Query(ID3D12Device* device, D3D12_FEATURE feature, T& data)
		{
			return SUCCEEDED(device->CheckFeatureSupport(feature, &data, sizeof(T)));
		}

		D3D_SHADER_MODEL HighestShaderModel(ID3D12Device* device)
		{
			// The runtime rejects models newer than it knows, so step down from the newest.
			for (int model = D3D_SHADER_MODEL_6_9; model >= D3D_SHADER_MODEL_6_0; --model)
			{
				D3D12_FEATURE_DATA_SHADER_MODEL data{ static_cast<D3D_SHADER_MODEL>(model) };

				if (Query(device, D3D12_FEATURE_SHADER_MODEL, data))
				{
					return data.HighestShaderModel;
				}
			}

			return D3D_SHADER_MODEL_5_1;
		}

		D3D_FEATURE_LEVEL HighestFeatureLevel(ID3D12Device* device)
		{
			const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
			D3D12_FEATURE_DATA_FEATURE_LEVELS data{ static_cast<UINT>(std::size(levels)), levels, D3D_FEATURE_LEVEL_12_0 };
			return Query(device, D3D12_FEATURE_FEATURE_LEVELS, data) ? data.MaxSupportedFeatureLevel : D3D_FEATURE_LEVEL_12_0;
		}

		const char* ShaderModelName(D3D_SHADER_MODEL model)
		{
			static const char* const names[] = { "6.0", "6.1", "6.2", "6.3", "6.4", "6.5", "6.6", "6.7", "6.8", "6.9" };
			const int index = int(model) - int(D3D_SHADER_MODEL_6_0);
			return index >= 0 && index < int(std::size(names)) ? names[index] : "5.1";
		}

	} // namespace

	D3D12Features QueryFeatures(ID3D12Device* device, IDXGIFactory6* factory)
	{
		D3D12Features features;
		features.FeatureLevel = HighestFeatureLevel(device);
		features.ShaderModel = HighestShaderModel(device);

		if (D3D12_FEATURE_DATA_D3D12_OPTIONS options{}; Query(device, D3D12_FEATURE_D3D12_OPTIONS, options))
		{
			features.BindingTier = options.ResourceBindingTier;
			features.HeapTier = options.ResourceHeapTier;
			features.TypedUavLoadAdditionalFormats = options.TypedUAVLoadAdditionalFormats;
		}

		if (D3D12_FEATURE_DATA_D3D12_OPTIONS1 options{}; Query(device, D3D12_FEATURE_D3D12_OPTIONS1, options))
		{
			features.WaveOps = options.WaveOps;
			features.WaveLaneCountMin = options.WaveLaneCountMin;
			features.WaveLaneCountMax = options.WaveLaneCountMax;
		}

		if (D3D12_FEATURE_DATA_D3D12_OPTIONS3 options{}; Query(device, D3D12_FEATURE_D3D12_OPTIONS3, options))
		{
			features.CopyQueueTimestamps = options.CopyQueueTimestampQueriesSupported;
		}

		if (D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{}; Query(device, D3D12_FEATURE_D3D12_OPTIONS5, options))
		{
			features.RaytracingTier = options.RaytracingTier;
		}

		if (D3D12_FEATURE_DATA_D3D12_OPTIONS7 options{}; Query(device, D3D12_FEATURE_D3D12_OPTIONS7, options))
		{
			features.MeshShaderTier = options.MeshShaderTier;
		}

		if (D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{}; Query(device, D3D12_FEATURE_D3D12_OPTIONS12, options))
		{
			features.EnhancedBarriers = options.EnhancedBarriersSupported;
		}

		if (D3D12_FEATURE_DATA_D3D12_OPTIONS13 options{}; Query(device, D3D12_FEATURE_D3D12_OPTIONS13, options))
		{
			features.UnrestrictedCopyPitch = options.UnrestrictedBufferTextureCopyPitchSupported;
		}

		D3D12_FEATURE_DATA_ROOT_SIGNATURE signature{ D3D_ROOT_SIGNATURE_VERSION_1_1 };
		features.RootSignature11 =
			Query(device, D3D12_FEATURE_ROOT_SIGNATURE, signature) && signature.HighestVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1;
		BOOL tearing = FALSE;
		features.Tearing =
			factory && SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing;
		return features;
	}

	Rhi::GraphicsCapabilities ToCapabilities(const D3D12Features& features, ID3D12Device* device)
	{
		Rhi::GraphicsCapabilities caps;
		const bool bindless = features.BindingTier >= D3D12_RESOURCE_BINDING_TIER_3;
		caps.Descriptors.MaxSampledTexturesPerStage = bindless ? 1000000u : 128u;
		caps.Descriptors.MaxSamplersPerStage = 2048;
		caps.Descriptors.MaxStorageTexturesPerStage = bindless ? 1000000u : 64u;
		caps.Descriptors.MaxUniformBuffersPerStage = 14;
		caps.Descriptors.MaxStorageBuffersPerStage = bindless ? 1000000u : 64u;
		caps.Descriptors.MaxBindlessSampledTextures = bindless ? 500000u : 0u;
		caps.Descriptors.MaxBindlessSamplers = bindless ? 2048u : 0u;
		caps.Queues = { true, true, true };
		caps.VertexInput = { D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT,
			D3D12_REQ_MULTI_ELEMENT_STRUCTURE_SIZE_IN_BYTES, 2047 };
		caps.MaxPushConstantBytes = 128;
		caps.Compute.MaxGroupCount = { D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION, D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION,
			D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION };
		caps.Compute.MaxGroupSize = { D3D12_CS_THREAD_GROUP_MAX_X, D3D12_CS_THREAD_GROUP_MAX_Y, D3D12_CS_THREAD_GROUP_MAX_Z };
		caps.Compute.MaxInvocations = D3D12_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP;
		caps.MaxColorAttachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
		caps.MaxTextureArrayLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
		// Counts every Forward+ target format (colour, object id, velocity, depth) renders at.
		caps.SampleCounts = 1;

		for (UINT count = 2; count <= 16; count *= 2)
		{
			bool supported = true;

			for (const auto format : { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_D32_FLOAT })
			{
				D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{ format, count };
				supported = supported && Query(device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, levels) && levels.NumQualityLevels > 0;
			}

			if (supported)
			{
				caps.SampleCounts |= count;
				caps.MaxSamples = count;
			}
		}
		caps.SubgroupSize = features.WaveLaneCountMin;
		caps.MinUniformBufferOffsetAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
		caps.MinStorageBufferOffsetAlignment = 256;
		ComPtr<ID3D12CommandQueue> queue;
		D3D12_COMMAND_QUEUE_DESC queueDesc{ D3D12_COMMAND_LIST_TYPE_DIRECT };
		UINT64 frequency = 0;

		if (SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))) && SUCCEEDED(queue->GetTimestampFrequency(&frequency)))
		{
			caps.TimestampFrequency = frequency;
		}

		caps.DescriptorIndexing = bindless;
		caps.BufferDeviceAddress = true; // GPU virtual addresses.
		caps.IndirectCount = true;		 // ExecuteIndirect takes a count buffer.
		caps.SubgroupOperations = features.WaveOps;
		caps.MeshShaders = features.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
		caps.TaskShaders = caps.MeshShaders; // Amplification shaders come with the mesh tier.
		caps.TimestampQueries = true;
		caps.RayQuery = features.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
		caps.RayTracingPipeline = features.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
		caps.MemoryBudget = true;
		caps.BcTextureCompression = true;
		caps.HdrSwapchain = true;
		caps.SampledCubeArray = true;
		caps.StorageImageExtendedFormats = features.TypedUavLoadAdditionalFormats;
		caps.BindlessDescriptors = bindless && features.ShaderModel >= D3D_SHADER_MODEL_6_6;
		return caps;
	}

	std::string DescribeFeatures(const D3D12Features& features)
	{
		std::ostringstream out;
		out << "feature level " << ((features.FeatureLevel >> 12) & 0xf) << "_" << ((features.FeatureLevel >> 8) & 0xf) << ", shader model "
			<< ShaderModelName(features.ShaderModel) << ", binding tier " << int(features.BindingTier) << ", heap tier "
			<< int(features.HeapTier) << ", enhanced barriers " << (features.EnhancedBarriers ? "yes" : "no")
			<< ", unrestricted copy pitch " << (features.UnrestrictedCopyPitch ? "yes" : "no") << ", mesh tier "
			<< int(features.MeshShaderTier) << ", raytracing tier " << (features.RaytracingTier / 10) << "."
			<< (features.RaytracingTier % 10) << ", wave ops " << (features.WaveOps ? "yes" : "no") << " (" << features.WaveLaneCountMin
			<< "-" << features.WaveLaneCountMax << "), typed UAV loads " << (features.TypedUavLoadAdditionalFormats ? "yes" : "no")
			<< ", tearing " << (features.Tearing ? "yes" : "no");
		return out.str();
	}

} // namespace Swim::RhiD3D12
