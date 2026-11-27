#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Pipelines.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <stdexcept>

namespace Swim::RhiD3D12
{

	namespace
	{

		// DXIL program kinds in a container's DXIL part header.
		constexpr std::uint32_t PixelKind = 0;
		constexpr std::uint32_t VertexKind = 1;
		constexpr std::uint32_t ComputeKind = 5;

		std::uint32_t Read32(std::span<const std::byte> bytes, std::size_t offset)
		{
			std::uint32_t value = 0;
			std::memcpy(&value, bytes.data() + offset, sizeof(value));
			return value;
		}

		// The container of the given program kind in a run of concatenated DXIL containers.
		std::span<const std::byte> FindContainer(std::span<const std::byte> bytes, std::uint32_t kind)
		{
			std::size_t offset = 0;

			while (offset + 32 <= bytes.size() && std::memcmp(bytes.data() + offset, "DXBC", 4) == 0)
			{
				const auto size = Read32(bytes, offset + 24);
				const auto parts = Read32(bytes, offset + 28);

				if (size < 32 || offset + size > bytes.size() || 32 + std::size_t(parts) * 4 > size)
				{
					break;
				}

				const auto container = bytes.subspan(offset, size);

				for (std::uint32_t part = 0; part < parts; ++part)
				{
					const auto partOffset = Read32(container, 32 + part * 4);

					if (partOffset + 12 <= size && std::memcmp(container.data() + partOffset, "DXIL", 4) == 0 &&
						Read32(container, partOffset + 8) >> 16 == kind)
					{
						return container;
					}
				}

				offset += size;
			}

			return {};
		}

		D3D12_DESCRIPTOR_RANGE_TYPE ToRangeType(Rhi::NativeRegisterClass type)
		{
			switch (type)
			{
			case Rhi::NativeRegisterClass::ConstantBuffer:
				return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
			case Rhi::NativeRegisterClass::UnorderedAccess:
				return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			case Rhi::NativeRegisterClass::Sampler:
				return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			default:
				return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			}
		}

		// Where a binding lives without reflected registers: its own number and space.
		Rhi::NativeBinding DefaultNative(std::uint32_t space, const Rhi::DescriptorBindingDesc& binding)
		{
			Rhi::NativeBinding native;
			native.Space = space;
			native.Binding = binding.Binding;
			native.Register = binding.Binding;
			native.RegisterSpace = space;

			switch (binding.Type)
			{
			case Rhi::DescriptorType::Sampler:
				native.Class = Rhi::NativeRegisterClass::Sampler;
				break;
			case Rhi::DescriptorType::UniformBuffer:
				native.Class = Rhi::NativeRegisterClass::ConstantBuffer;
				break;
			case Rhi::DescriptorType::StorageTexture:
			case Rhi::DescriptorType::StorageBuffer:
				native.Class = Rhi::NativeRegisterClass::UnorderedAccess;
				break;
			default:
				native.Class = Rhi::NativeRegisterClass::ShaderResource;
				break;
			}

			return native;
		}

		// FNV-1a over everything a pipeline is built from: its name in the pipeline library.
		struct Hasher
		{
			std::uint64_t Value = 1469598103934665603ull;

			void Add(const void* data, std::size_t size)
			{
				const auto* bytes = static_cast<const unsigned char*>(data);

				for (std::size_t index = 0; index < size; ++index)
				{
					Value = (Value ^ bytes[index]) * 1099511628211ull;
				}
			}

			template <typename T> void AddValue(const T& value) { Add(&value, sizeof(value)); }

			std::wstring Name(const wchar_t* prefix) const
			{
				wchar_t text[40];
				swprintf(text, std::size(text), L"%s%016llx", prefix, static_cast<unsigned long long>(Value));
				return text;
			}
		};

		// Loads the pipeline from the library, or creates it and stores it there.
		template <typename Desc, typename Load, typename Create>
		ComPtr<ID3D12PipelineState> CachedPipeline(
			D3D12DeviceState& state, const std::wstring& name, const Desc& desc, Load load, Create create)
		{
			ComPtr<ID3D12PipelineState> pipeline;
			std::scoped_lock lock(state.PipelineLibraryMutex);

			if (state.PipelineLibrary && SUCCEEDED(load(state.PipelineLibrary.Get(), name.c_str(), &desc, &pipeline)))
			{
				return pipeline;
			}

			Check(create(&desc, &pipeline), "Create*PipelineState", &state);

			if (!state.PipelineLibrary && !state.PipelineLibraryLoaded)
			{
				// No cache was loaded: start an empty library so this run's pipelines are saved.
				state.Device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&state.PipelineLibrary));
				state.PipelineLibraryLoaded = true;
			}

			if (state.PipelineLibrary)
			{
				state.PipelineLibrary->StorePipeline(name.c_str(), pipeline.Get());
			}

			return pipeline;
		}

		D3D12_BLEND ToBlend(Rhi::BlendFactor factor)
		{
			switch (factor)
			{
			case Rhi::BlendFactor::Zero:
				return D3D12_BLEND_ZERO;
			case Rhi::BlendFactor::One:
				return D3D12_BLEND_ONE;
			case Rhi::BlendFactor::SourceColor:
				return D3D12_BLEND_SRC_COLOR;
			case Rhi::BlendFactor::OneMinusSourceColor:
				return D3D12_BLEND_INV_SRC_COLOR;
			case Rhi::BlendFactor::DestinationColor:
				return D3D12_BLEND_DEST_COLOR;
			case Rhi::BlendFactor::OneMinusDestinationColor:
				return D3D12_BLEND_INV_DEST_COLOR;
			case Rhi::BlendFactor::SourceAlpha:
				return D3D12_BLEND_SRC_ALPHA;
			case Rhi::BlendFactor::OneMinusSourceAlpha:
				return D3D12_BLEND_INV_SRC_ALPHA;
			case Rhi::BlendFactor::DestinationAlpha:
				return D3D12_BLEND_DEST_ALPHA;
			case Rhi::BlendFactor::OneMinusDestinationAlpha:
				return D3D12_BLEND_INV_DEST_ALPHA;
			}

			return D3D12_BLEND_ONE;
		}

		// Alpha blend factors may not name colour channels.
		D3D12_BLEND ToAlphaBlend(Rhi::BlendFactor factor)
		{
			switch (factor)
			{
			case Rhi::BlendFactor::SourceColor:
				return D3D12_BLEND_SRC_ALPHA;
			case Rhi::BlendFactor::OneMinusSourceColor:
				return D3D12_BLEND_INV_SRC_ALPHA;
			case Rhi::BlendFactor::DestinationColor:
				return D3D12_BLEND_DEST_ALPHA;
			case Rhi::BlendFactor::OneMinusDestinationColor:
				return D3D12_BLEND_INV_DEST_ALPHA;
			default:
				return ToBlend(factor);
			}
		}

		D3D12_BLEND_OP ToBlendOp(Rhi::BlendOp op)
		{
			switch (op)
			{
			case Rhi::BlendOp::Subtract:
				return D3D12_BLEND_OP_SUBTRACT;
			case Rhi::BlendOp::ReverseSubtract:
				return D3D12_BLEND_OP_REV_SUBTRACT;
			case Rhi::BlendOp::Min:
				return D3D12_BLEND_OP_MIN;
			case Rhi::BlendOp::Max:
				return D3D12_BLEND_OP_MAX;
			default:
				return D3D12_BLEND_OP_ADD;
			}
		}

		D3D12_STENCIL_OP ToStencilOp(Rhi::StencilOp op)
		{
			switch (op)
			{
			case Rhi::StencilOp::Zero:
				return D3D12_STENCIL_OP_ZERO;
			case Rhi::StencilOp::Replace:
				return D3D12_STENCIL_OP_REPLACE;
			case Rhi::StencilOp::IncrementClamp:
				return D3D12_STENCIL_OP_INCR_SAT;
			case Rhi::StencilOp::DecrementClamp:
				return D3D12_STENCIL_OP_DECR_SAT;
			case Rhi::StencilOp::Invert:
				return D3D12_STENCIL_OP_INVERT;
			case Rhi::StencilOp::IncrementWrap:
				return D3D12_STENCIL_OP_INCR;
			case Rhi::StencilOp::DecrementWrap:
				return D3D12_STENCIL_OP_DECR;
			default:
				return D3D12_STENCIL_OP_KEEP;
			}
		}

		D3D12_DEPTH_STENCILOP_DESC ToStencilFace(const Rhi::StencilFaceState& face)
		{
			return { ToStencilOp(face.Fail), ToStencilOp(face.DepthFail), ToStencilOp(face.Pass), ToComparison(face.Compare) };
		}

	} // namespace

	// Program

	std::unique_ptr<D3D12ShaderProgram> D3D12ShaderProgram::Create(
		std::shared_ptr<D3D12DeviceState> state, const Rhi::ShaderProgramDesc& desc)
	{
		auto program = std::unique_ptr<D3D12ShaderProgram>(new D3D12ShaderProgram());
		program->state = std::move(state);
		const auto& in = desc.Interface;
		program->programInterface.DescriptorSchemas.assign(in.DescriptorSchemas.begin(), in.DescriptorSchemas.end());
		program->programInterface.PushConstants.assign(in.PushConstants.begin(), in.PushConstants.end());
		program->programInterface.ComputeThreadGroupSize = in.ComputeThreadGroupSize;
		program->programInterface.NativeBindings.assign(in.NativeBindings.begin(), in.NativeBindings.end());
		program->programInterface.NativeVertexInputs.assign(in.NativeVertexInputs.begin(), in.NativeVertexInputs.end());

		for (const auto& artifact : desc.Stages)
		{
			const auto kind = artifact.Stage == Rhi::ShaderStageMask::Vertex ? VertexKind
				: artifact.Stage == Rhi::ShaderStageMask::Fragment			 ? PixelKind
				: artifact.Stage == Rhi::ShaderStageMask::Compute			 ? ComputeKind
																			 : UINT32_MAX;
			const auto container = kind == UINT32_MAX ? std::span<const std::byte>{} : FindContainer(artifact.Bytecode, kind);

			if (container.empty())
			{
				return nullptr;
			}

			program->stages.push_back({ artifact.Stage, std::string(artifact.EntryPoint), { container.begin(), container.end() } });
		}

		return program->stages.empty() ? nullptr : std::move(program);
	}

	const D3D12ShaderProgram::Stage* D3D12ShaderProgram::FindStage(Rhi::ShaderStageMask mask) const
	{
		const auto found = std::find_if(stages.begin(), stages.end(),
			[&](const Stage& stage)
			{
				return stage.Mask == mask;
			});
		return found == stages.end() ? nullptr : &*found;
	}

	// Layout

	D3D12PipelineLayout::D3D12PipelineLayout(std::shared_ptr<D3D12DeviceState> stateValue, D3D12ShaderProgram& programValue)
		: state(std::move(stateValue)), program(programValue)
	{
	}

	const D3D12PipelineLayout::Space* D3D12PipelineLayout::FindSpace(std::uint32_t index) const
	{
		const auto found = std::find_if(spaces.begin(), spaces.end(),
			[&](const Space& space)
			{
				return space.Index == index;
			});
		return found == spaces.end() ? nullptr : &*found;
	}

	std::unique_ptr<D3D12PipelineLayout> D3D12PipelineLayout::Create(
		std::shared_ptr<D3D12DeviceState> state, const Rhi::PipelineLayoutDesc& desc)
	{
		auto* program = dynamic_cast<D3D12ShaderProgram*>(desc.Program);

		if (!program || program->GetState() != state)
		{
			return nullptr;
		}

		auto layout = std::unique_ptr<D3D12PipelineLayout>(new D3D12PipelineLayout(state, *program));
		const auto& programInterface = program->GetInterface();
		layout->compute = program->FindStage(Rhi::ShaderStageMask::Compute) != nullptr;

		// Reflected spaces, each replaced by an explicit one of the same index.
		std::map<std::uint32_t, std::vector<Rhi::DescriptorBindingDesc>> schemas;

		for (const auto& schema : programInterface.DescriptorSchemas)
		{
			schemas[schema.Space] = schema.Bindings;
		}

		for (const auto& schema : desc.DescriptorSpaces)
		{
			schemas[schema.Space] = schema.Bindings;
		}

		layout->layoutInterface = programInterface;
		layout->layoutInterface.DescriptorSchemas.clear();

		for (const auto& [index, bindings] : schemas)
		{
			layout->layoutInterface.DescriptorSchemas.push_back({ index, bindings });
		}

		std::map<std::pair<std::uint32_t, std::uint32_t>, Rhi::NativeBinding> natives;
		std::optional<Rhi::NativeBinding> pushNative;

		for (const auto& native : programInterface.NativeBindings)
		{
			if (native.Space == Rhi::NativeBinding::PushConstantSpace)
			{
				pushNative = native;
			}
			else
			{
				natives[{ native.Space, native.Binding }] = native;
			}
		}

		const bool reflected = !programInterface.NativeBindings.empty();
		std::vector<D3D12_ROOT_PARAMETER1> parameters;
		std::vector<std::vector<D3D12_DESCRIPTOR_RANGE1>> ranges; // Kept alive until serialization.
		ranges.reserve(schemas.size() * 2);

		if (!programInterface.PushConstants.empty())
		{
			std::uint32_t bytes = 0;

			for (const auto& range : programInterface.PushConstants)
			{
				bytes = std::max(bytes, range.Offset + range.Size);
			}

			D3D12_ROOT_PARAMETER1 parameter{};
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			parameter.Constants.ShaderRegister = pushNative ? pushNative->Register : 0u;
			parameter.Constants.RegisterSpace = pushNative ? pushNative->RegisterSpace : FallbackPushConstantSpace;
			parameter.Constants.Num32BitValues = (bytes + 3) / 4;
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			layout->pushParameter = static_cast<int>(parameters.size());
			layout->pushDwords = parameter.Constants.Num32BitValues;
			parameters.push_back(parameter);
		}

		if (!layout->compute)
		{
			D3D12_ROOT_PARAMETER1 parameter{};
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			parameter.Constants.ShaderRegister = 0;
			parameter.Constants.RegisterSpace = DrawBaseRegisterSpace;
			parameter.Constants.Num32BitValues = 2;
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
			layout->drawBaseParameter = static_cast<int>(parameters.size());
			parameters.push_back(parameter);
		}

		// Single structured buffers become root descriptors while the root signature has room
		// (64 DWORDs: two per root descriptor, one per table, up to two tables per space).
		// Root descriptors carry no size, so buffers whose element count the shaders read
		// keep their table range.
		std::uint32_t rootDwords = 2u * static_cast<std::uint32_t>(schemas.size());

		for (const auto& parameter : parameters)
		{
			rootDwords += parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS ? parameter.Constants.Num32BitValues : 1u;
		}

		for (auto& [index, bindings] : schemas)
		{
			Space space;
			space.Index = index;
			std::sort(bindings.begin(), bindings.end(),
				[](const auto& left, const auto& right)
				{
					return left.Binding < right.Binding;
				});
			std::vector<D3D12_DESCRIPTOR_RANGE1> viewRanges;
			std::vector<D3D12_DESCRIPTOR_RANGE1> samplerRanges;

			for (const auto& binding : bindings)
			{
				if (binding.Count == 0)
				{
					return nullptr; // Runtime-sized arrays need an explicit capacity.
				}

				Slot slot;
				slot.Binding = binding;
				slot.Sampler = binding.Type == Rhi::DescriptorType::Sampler;
				auto& count = slot.Sampler ? space.SamplerCount : space.ViewCount;
				slot.Offset = count;
				count += binding.Count;

				if (const auto found = natives.find({ index, binding.Binding }); found != natives.end())
				{
					slot.Native = found->second;
				}
				else if (!reflected)
				{
					slot.Native = DefaultNative(index, binding);
				}

				const bool structured = slot.Native && slot.Native->StructureStride != 0 && !slot.Native->SizeQueried &&
					binding.Count == 1 &&
					(binding.Type == Rhi::DescriptorType::ReadOnlyStorageBuffer || binding.Type == Rhi::DescriptorType::StorageBuffer);

				if (structured && rootDwords + 2 <= D3D12_MAX_ROOT_COST)
				{
					D3D12_ROOT_PARAMETER1 parameter{};
					const bool write = binding.Type == Rhi::DescriptorType::StorageBuffer;
					parameter.ParameterType = write ? D3D12_ROOT_PARAMETER_TYPE_UAV : D3D12_ROOT_PARAMETER_TYPE_SRV;
					parameter.Descriptor = { slot.Native->Register, slot.Native->RegisterSpace, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE };
					parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
					slot.RootParameter = static_cast<int>(parameters.size());
					parameters.push_back(parameter);
					rootDwords += 2;
				}
				// Only bindings the shaders declare get a register range.
				else if (slot.Native)
				{
					D3D12_DESCRIPTOR_RANGE1 range{};
					range.RangeType = ToRangeType(slot.Native->Class);
					range.NumDescriptors = binding.Count;
					range.BaseShaderRegister = slot.Native->Register;
					range.RegisterSpace = slot.Native->RegisterSpace;
					range.Flags = slot.Sampler
						? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
						: D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
					range.OffsetInDescriptorsFromTableStart = slot.Offset;
					(slot.Sampler ? samplerRanges : viewRanges).push_back(range);
				}

				space.Slots.push_back(slot);
			}

			for (auto* list : { &viewRanges, &samplerRanges })
			{
				if (list->empty())
				{
					continue;
				}

				ranges.push_back(std::move(*list));
				D3D12_ROOT_PARAMETER1 parameter{};
				parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				parameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(ranges.back().size());
				parameter.DescriptorTable.pDescriptorRanges = ranges.back().data();
				parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
				(list == &viewRanges ? space.ViewParameter : space.SamplerParameter) = static_cast<int>(parameters.size());
				parameters.push_back(parameter);
			}

			layout->spaces.push_back(std::move(space));
		}

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC root{};
		root.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		root.Desc_1_1.NumParameters = static_cast<UINT>(parameters.size());
		root.Desc_1_1.pParameters = parameters.data();
		root.Desc_1_1.Flags =
			layout->compute ? D3D12_ROOT_SIGNATURE_FLAG_NONE : D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		ComPtr<ID3DBlob> serialized;
		ComPtr<ID3DBlob> errors;

		if (FAILED(D3D12SerializeVersionedRootSignature(&root, &serialized, &errors)))
		{
			const std::string message =
				errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "";
			throw std::invalid_argument("D3D12: root signature of " + std::string(desc.DebugName) + ": " + message);
		}

		const auto* bytes = static_cast<const std::byte*>(serialized->GetBufferPointer());
		layout->blob.assign(bytes, bytes + serialized->GetBufferSize());
		Check(state->Device->CreateRootSignature(0, layout->blob.data(), layout->blob.size(), IID_PPV_ARGS(&layout->rootSignature)),
			"CreateRootSignature", state.get());
		SetDebugName(layout->rootSignature.Get(), desc.DebugName);
		return layout;
	}

	// Pipelines

	std::unique_ptr<D3D12ComputePipeline> D3D12ComputePipeline::Create(
		std::shared_ptr<D3D12DeviceState> state, const Rhi::ComputePipelineDesc& desc)
	{
		auto* program = dynamic_cast<D3D12ShaderProgram*>(desc.Program);
		auto* layout = dynamic_cast<D3D12PipelineLayout*>(desc.Layout);
		const auto* stage = program ? program->FindStage(Rhi::ShaderStageMask::Compute) : nullptr;

		if (!stage || !layout || &layout->GetProgram() != program || (!desc.EntryPoint.empty() && desc.EntryPoint != stage->EntryPoint))
		{
			return nullptr;
		}

		auto pipeline = std::unique_ptr<D3D12ComputePipeline>(new D3D12ComputePipeline(*layout));
		D3D12_COMPUTE_PIPELINE_STATE_DESC native{};
		native.pRootSignature = layout->GetRootSignature();
		native.CS = { stage->Bytecode.data(), stage->Bytecode.size() };
		Hasher hash;
		hash.Add(stage->Bytecode.data(), stage->Bytecode.size());
		hash.Add(layout->GetRootSignatureBlob().data(), layout->GetRootSignatureBlob().size());
		auto* device = state->Device.Get();
		pipeline->pipeline = CachedPipeline(
			*state, hash.Name(L"C"), native,
			[](ID3D12PipelineLibrary1* library, const wchar_t* name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* d,
				ComPtr<ID3D12PipelineState>* out)
			{
				return library->LoadComputePipeline(name, d, IID_PPV_ARGS(out->ReleaseAndGetAddressOf()));
			},
			[device](const D3D12_COMPUTE_PIPELINE_STATE_DESC* d, ComPtr<ID3D12PipelineState>* out)
			{
				return device->CreateComputePipelineState(d, IID_PPV_ARGS(out->ReleaseAndGetAddressOf()));
			});
		SetDebugName(pipeline->pipeline.Get(), desc.DebugName);
		return pipeline;
	}

	std::unique_ptr<D3D12GraphicsPipeline> D3D12GraphicsPipeline::Create(
		std::shared_ptr<D3D12DeviceState> state, const Rhi::GraphicsPipelineDesc& desc)
	{
		auto* program = dynamic_cast<D3D12ShaderProgram*>(desc.Program);
		auto* layout = dynamic_cast<D3D12PipelineLayout*>(desc.Layout);
		const auto* vertex = program ? program->FindStage(Rhi::ShaderStageMask::Vertex) : nullptr;
		const auto* pixel = program ? program->FindStage(Rhi::ShaderStageMask::Fragment) : nullptr;

		if (!vertex || !layout || &layout->GetProgram() != program || desc.ColorFormats.size() > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT)
		{
			return nullptr;
		}

		// Vertex attributes: the input signature names each location by semantic.
		std::vector<D3D12_INPUT_ELEMENT_DESC> elements;

		for (const auto& attribute : desc.VertexAttributes)
		{
			const auto& inputs = program->GetInterface().NativeVertexInputs;
			const auto input = std::find_if(inputs.begin(), inputs.end(),
				[&](const Rhi::NativeVertexInput& candidate)
				{
					return candidate.Location == attribute.Location;
				});

			if (input == inputs.end())
			{
				throw std::invalid_argument("D3D12: vertex attribute location " + std::to_string(attribute.Location) +
					" has no input semantic in the program (NativeVertexInputs)");
			}

			const auto binding = std::find_if(desc.VertexBindings.begin(), desc.VertexBindings.end(),
				[&](const Rhi::VertexBindingDesc& candidate)
				{
					return candidate.Slot == attribute.Slot;
				});
			const bool instanced = binding != desc.VertexBindings.end() && binding->Rate == Rhi::VertexInputRate::Instance;
			elements.push_back({ input->SemanticName.c_str(), input->SemanticIndex, ToDxgiFormat(attribute.DataFormat), attribute.Slot,
				attribute.Offset, instanced ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
				instanced ? 1u : 0u });
		}

		auto pipeline = std::unique_ptr<D3D12GraphicsPipeline>(new D3D12GraphicsPipeline(*layout));
		D3D12_GRAPHICS_PIPELINE_STATE_DESC native{};
		native.InputLayout = { elements.data(), static_cast<UINT>(elements.size()) };
		native.pRootSignature = layout->GetRootSignature();
		native.VS = { vertex->Bytecode.data(), vertex->Bytecode.size() };

		if (pixel)
		{
			native.PS = { pixel->Bytecode.data(), pixel->Bytecode.size() };
		}

		native.BlendState.IndependentBlendEnable = TRUE;

		for (std::size_t index = 0; index < desc.ColorFormats.size(); ++index)
		{
			const auto blend = index < desc.BlendAttachments.size() ? desc.BlendAttachments[index] : Rhi::BlendAttachmentState{};
			auto& target = native.BlendState.RenderTarget[index];
			target.BlendEnable = blend.Enabled;
			target.SrcBlend = ToBlend(blend.SourceColor);
			target.DestBlend = ToBlend(blend.DestinationColor);
			target.BlendOp = ToBlendOp(blend.ColorOperation);
			target.SrcBlendAlpha = ToAlphaBlend(blend.SourceAlpha);
			target.DestBlendAlpha = ToAlphaBlend(blend.DestinationAlpha);
			target.BlendOpAlpha = ToBlendOp(blend.AlphaOperation);
			target.LogicOp = D3D12_LOGIC_OP_NOOP;
			target.RenderTargetWriteMask = static_cast<UINT8>(blend.WriteMask);
			native.RTVFormats[index] = ToDxgiFormat(desc.ColorFormats[index]);
		}

		native.NumRenderTargets = static_cast<UINT>(desc.ColorFormats.size());
		native.SampleMask = UINT_MAX;
		native.BlendState.AlphaToCoverageEnable = desc.AlphaToCoverage && desc.Samples != Rhi::SampleCount::X1;
		auto& raster = native.RasterizerState;
		raster.FillMode = desc.Raster.Wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
		raster.CullMode = desc.Raster.Cull == Rhi::CullMode::Back ? D3D12_CULL_MODE_BACK
			: desc.Raster.Cull == Rhi::CullMode::Front			  ? D3D12_CULL_MODE_FRONT
																  : D3D12_CULL_MODE_NONE;
		raster.FrontCounterClockwise = desc.Raster.Winding == Rhi::FrontFace::CounterClockwise;
		raster.DepthClipEnable = !desc.Raster.DepthClamp;
		raster.DepthBias = std::isfinite(desc.Raster.DepthBiasConstant)
			? static_cast<INT>(std::lround(std::clamp(desc.Raster.DepthBiasConstant, -1.0e6f, 1.0e6f)))
			: 0;
		raster.SlopeScaledDepthBias = std::isfinite(desc.Raster.DepthBiasSlope) ? desc.Raster.DepthBiasSlope : 0.0f;
		raster.DepthBiasClamp = 0.0f;
		const auto& depth = desc.DepthStencil;
		const bool hasDepth = desc.DepthStencilFormat != Rhi::Format::Undefined;
		auto& ds = native.DepthStencilState;
		ds.DepthEnable = hasDepth && (depth.DepthTest || depth.DepthWrite);
		ds.DepthWriteMask = hasDepth && depth.DepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		ds.DepthFunc = depth.DepthTest ? ToComparison(depth.DepthCompare) : D3D12_COMPARISON_FUNC_ALWAYS;
		ds.StencilEnable = hasDepth && depth.StencilTest;
		ds.StencilReadMask = static_cast<UINT8>(depth.Front.CompareMask);
		ds.StencilWriteMask = static_cast<UINT8>(depth.Front.WriteMask);
		ds.FrontFace = ToStencilFace(depth.Front);
		ds.BackFace = ToStencilFace(depth.Back);
		native.DSVFormat = hasDepth ? ToDxgiFormat(desc.DepthStencilFormat) : DXGI_FORMAT_UNKNOWN;
		native.SampleDesc.Count = static_cast<UINT>(desc.Samples);

		switch (desc.Topology)
		{
		case Rhi::PrimitiveTopology::PointList:
			native.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
			pipeline->topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
			break;
		case Rhi::PrimitiveTopology::LineList:
		case Rhi::PrimitiveTopology::LineStrip:
			native.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
			pipeline->topology =
				desc.Topology == Rhi::PrimitiveTopology::LineList ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
			break;
		default:
			native.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			pipeline->topology = desc.Topology == Rhi::PrimitiveTopology::TriangleStrip ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
																						: D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			break;
		}

		pipeline->stencilReference = depth.Front.Reference;

		for (const auto& binding : desc.VertexBindings)
		{
			pipeline->strides.resize(std::max<std::size_t>(pipeline->strides.size(), binding.Slot + 1));
			pipeline->strides[binding.Slot] = binding.Stride;
		}

		Hasher hash;
		hash.Add(vertex->Bytecode.data(), vertex->Bytecode.size());

		if (pixel)
		{
			hash.Add(pixel->Bytecode.data(), pixel->Bytecode.size());
		}

		hash.Add(layout->GetRootSignatureBlob().data(), layout->GetRootSignatureBlob().size());
		hash.AddValue(native.BlendState);
		hash.AddValue(native.RasterizerState);
		hash.AddValue(native.DepthStencilState);
		hash.AddValue(native.RTVFormats);
		hash.AddValue(native.NumRenderTargets);
		hash.AddValue(native.DSVFormat);
		hash.AddValue(native.SampleDesc);
		hash.AddValue(native.PrimitiveTopologyType);

		for (const auto& element : elements)
		{
			hash.Add(element.SemanticName, std::strlen(element.SemanticName));
			hash.AddValue(element.SemanticIndex);
			hash.AddValue(element.Format);
			hash.AddValue(element.InputSlot);
			hash.AddValue(element.AlignedByteOffset);
			hash.AddValue(element.InputSlotClass);
		}
		auto* device = state->Device.Get();
		pipeline->pipeline = CachedPipeline(
			*state, hash.Name(L"G"), native,
			[](ID3D12PipelineLibrary1* library, const wchar_t* name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d,
				ComPtr<ID3D12PipelineState>* out)
			{
				return library->LoadGraphicsPipeline(name, d, IID_PPV_ARGS(out->ReleaseAndGetAddressOf()));
			},
			[device](const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ComPtr<ID3D12PipelineState>* out)
			{
				return device->CreateGraphicsPipelineState(d, IID_PPV_ARGS(out->ReleaseAndGetAddressOf()));
			});
		SetDebugName(pipeline->pipeline.Get(), desc.DebugName);
		return pipeline;
	}

	// Pipeline cache: a small header (magic, adapter LUID, driver version) then the library.

	namespace
	{

		struct CacheHeader
		{
			char Magic[8] = { 'S', 'W', 'D', '3', 'D', 'P', 'L', '1' };
			LUID Adapter{};
			std::uint64_t Driver = 0;
			std::uint64_t Size = 0;
		};

		CacheHeader MakeHeader(const D3D12DeviceState& state)
		{
			CacheHeader header;
			header.Adapter = state.Device->GetAdapterLuid();
			LARGE_INTEGER driver{};
			state.Adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver);
			header.Driver = static_cast<std::uint64_t>(driver.QuadPart);
			return header;
		}

	} // namespace

	Rhi::PipelineCacheLoadStatus LoadD3D12PipelineCache(D3D12DeviceState& state, std::span<const std::byte> data)
	{
		std::scoped_lock lock(state.PipelineLibraryMutex);

		if (state.PipelineLibraryLoaded)
		{
			return Rhi::PipelineCacheLoadStatus::AlreadyInitialized;
		}

		state.PipelineLibraryLoaded = true;

		if (data.empty())
		{
			state.Device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&state.PipelineLibrary));
			return Rhi::PipelineCacheLoadStatus::Empty;
		}

		CacheHeader header;
		const auto expected = MakeHeader(state);

		if (data.size() < sizeof(header))
		{
			state.Device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&state.PipelineLibrary));
			return Rhi::PipelineCacheLoadStatus::InvalidData;
		}

		std::memcpy(&header, data.data(), sizeof(header));

		if (std::memcmp(header.Magic, expected.Magic, sizeof(header.Magic)) != 0 || header.Size != data.size() - sizeof(header))
		{
			state.Device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&state.PipelineLibrary));
			return Rhi::PipelineCacheLoadStatus::InvalidData;
		}

		if (std::memcmp(&header.Adapter, &expected.Adapter, sizeof(LUID)) != 0 || header.Driver != expected.Driver)
		{
			state.Device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&state.PipelineLibrary));
			return Rhi::PipelineCacheLoadStatus::Incompatible;
		}

		state.PipelineLibraryBlob.assign(data.begin() + sizeof(header), data.end());
		const HRESULT result = state.Device->CreatePipelineLibrary(
			state.PipelineLibraryBlob.data(), state.PipelineLibraryBlob.size(), IID_PPV_ARGS(&state.PipelineLibrary));

		if (FAILED(result))
		{
			state.PipelineLibraryBlob.clear();
			state.Device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&state.PipelineLibrary));
			return result == D3D12_ERROR_DRIVER_VERSION_MISMATCH || result == D3D12_ERROR_ADAPTER_NOT_FOUND
				? Rhi::PipelineCacheLoadStatus::Incompatible
				: Rhi::PipelineCacheLoadStatus::InvalidData;
		}

		return Rhi::PipelineCacheLoadStatus::Loaded;
	}

	Rhi::PipelineCacheData GetD3D12PipelineCacheData(D3D12DeviceState& state)
	{
		Rhi::PipelineCacheData data;
		std::scoped_lock lock(state.PipelineLibraryMutex);

		if (!state.PipelineLibrary)
		{
			data.Status = Rhi::PipelineCacheDataStatus::Empty;
			return data;
		}

		auto header = MakeHeader(state);
		header.Size = state.PipelineLibrary->GetSerializedSize();

		if (header.Size + sizeof(header) > Rhi::MaxPipelineCacheDataBytes)
		{
			data.Status = Rhi::PipelineCacheDataStatus::TooLarge;
			return data;
		}

		data.Bytes.resize(sizeof(header) + header.Size);
		std::memcpy(data.Bytes.data(), &header, sizeof(header));

		if (FAILED(state.PipelineLibrary->Serialize(data.Bytes.data() + sizeof(header), header.Size)))
		{
			data.Bytes.clear();
			data.Status = Rhi::PipelineCacheDataStatus::Failed;
			return data;
		}

		data.Status = Rhi::PipelineCacheDataStatus::Ready;
		return data;
	}

} // namespace Swim::RhiD3D12
