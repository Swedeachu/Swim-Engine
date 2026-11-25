#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"

#include <algorithm>
#include <stdexcept>

namespace Engine
{

	namespace R = Swim::Render;
	namespace S = Swim::Rhi;

	std::array<float, 3> RenderFeatureView::ProjectDirection(const std::array<float, 3>& direction) const
	{
		// Directions project like points at infinity: clip = VP * (d, 0).
		const auto& m = ViewProjection;
		const float x = m[0] * direction[0] + m[1] * direction[1] + m[2] * direction[2];
		const float y = m[4] * direction[0] + m[5] * direction[1] + m[6] * direction[2];
		const float w = m[12] * direction[0] + m[13] * direction[1] + m[14] * direction[2];

		if (!(w > 1.0e-6f))
		{
			return { 0.5f, 0.5f, -1.0f };
		}

		return { x / w * 0.5f + 0.5f, 0.5f - y / w * 0.5f, w };
	}

	RenderFeatureContext::RenderFeatureContext(R::RenderGraph& graphValue, RenderFeatureStage stageValue,
		const RenderFeatureView& viewValue, const RenderSettings& settingsValue, R::GraphTexture colorValue, R::GraphTexture depthValue,
		Services servicesValue)
		: graph(graphValue), stage(stageValue), view(viewValue), settings(settingsValue), color(colorValue), depth(depthValue),
		  services(std::move(servicesValue))
	{
	}

	void RenderFeatureContext::SetColor(R::GraphTexture texture)
	{
		const auto& current = graph.GetDesc(color);
		const auto& next = graph.GetDesc(texture);

		if (next.PixelFormat != current.PixelFormat || next.Extent.Width != current.Extent.Width ||
			next.Extent.Height != current.Extent.Height)
		{
			throw std::invalid_argument("RenderFeatureContext::SetColor needs a texture with the scene color's format and size");
		}

		color = texture;
	}

	R::GraphTexture RenderFeatureContext::CreateTexture(S::Format format, std::uint32_t width, std::uint32_t height, std::string_view name)
	{
		S::TextureDesc desc;
		desc.Extent = { width ? width : view.Width, height ? height : view.Height, 1 };
		desc.PixelFormat = format;
		desc.Usage = S::TextureUsage::Sampled | S::TextureUsage::Storage | S::TextureUsage::TransferSource |
			(S::IsDepthFormat(format) ? S::TextureUsage::None : S::TextureUsage::ColorAttachment);
		desc.DebugName = name;
		return graph.CreateTexture(desc);
	}

	R::GraphTexture RenderFeatureContext::CreateColorTarget(std::string_view name)
	{
		const auto& desc = graph.GetDesc(color);
		return CreateTexture(desc.PixelFormat, desc.Extent.Width, desc.Extent.Height, name);
	}

	RenderFeatureComputePass RenderFeatureContext::Compute(std::string_view program)
	{
		if (!services.LoadCompute)
		{
			throw std::logic_error("RenderFeatureContext has no program loader");
		}

		return RenderFeatureComputePass(*this, std::string(program), services.LoadCompute(program));
	}

	R::GraphBuffer RenderFeatureContext::CreateBuffer(std::uint64_t size, std::string_view name)
	{
		S::BufferDesc desc;
		desc.Size = std::max<std::uint64_t>(size, 16);
		desc.Usage = S::BufferUsage::Storage | S::BufferUsage::Indirect | S::BufferUsage::TransferDestination;
		desc.DebugName = name;
		return graph.CreateBuffer(desc);
	}

	RenderFeatureComputePass::RenderFeatureComputePass(
		RenderFeatureContext& contextValue, std::string program, const RuntimeComputeProgram& compiledValue)
		: context(contextValue), programName(std::move(program)), compiled(compiledValue)
	{
	}

	RenderFeatureComputePass& RenderFeatureComputePass::Texture(std::string_view name, R::GraphTexture texture)
	{
		bindings.push_back({ std::string(name), Kind::Texture, texture, {}, {} });
		return *this;
	}

	RenderFeatureComputePass& RenderFeatureComputePass::Storage(std::string_view name, R::GraphTexture texture)
	{
		bindings.push_back({ std::string(name), Kind::Storage, texture, {}, {} });
		return *this;
	}

	RenderFeatureComputePass& RenderFeatureComputePass::Buffer(std::string_view name, R::GraphBuffer buffer)
	{
		bindings.push_back({ std::string(name), Kind::Buffer, {}, buffer, {} });
		return *this;
	}

	RenderFeatureComputePass& RenderFeatureComputePass::StorageBuffer(std::string_view name, R::GraphBuffer buffer)
	{
		bindings.push_back({ std::string(name), Kind::StorageBuffer, {}, buffer, {} });
		return *this;
	}

	RenderFeatureComputePass& RenderFeatureComputePass::WriteBuffer(std::string_view name, R::GraphBuffer buffer)
	{
		bindings.push_back({ std::string(name), Kind::WriteBuffer, {}, buffer, {} });
		return *this;
	}

	RenderFeatureComputePass& RenderFeatureComputePass::Sampler(std::string_view name, std::string_view kind)
	{
		bindings.push_back({ std::string(name), Kind::Sampler, {}, {}, std::string(kind) });
		return *this;
	}

	R::GraphPass RenderFeatureComputePass::Dispatch(std::uint32_t width, std::uint32_t height, std::uint32_t depth)
	{
		using Type = S::DescriptorType;
		const auto label = "Feature " + programName;

		// Match every reflected descriptor to a supplied resource of a compatible kind.
		struct Resolved
		{
			const RuntimeBinding* Slot = nullptr;
			const Binding* Resource = nullptr;
		};

		std::vector<Resolved> resolved;

		for (const auto& slot : compiled.Bindings)
		{
			const auto found = std::find_if(bindings.begin(), bindings.end(),
				[&](const Binding& b)
				{
					return b.Name == slot.Name;
				});

			if (found == bindings.end())
			{
				throw std::invalid_argument(label + ": parameter '" + slot.Name + "' was not bound");
			}

			const bool compatible = (slot.Type == Type::SampledTexture && found->Type == Kind::Texture) ||
				(slot.Type == Type::StorageTexture && found->Type == Kind::Storage) ||
				(slot.Type == Type::ReadOnlyStorageBuffer && found->Type == Kind::Buffer) ||
				(slot.Type == Type::StorageBuffer &&
					(found->Type == Kind::StorageBuffer || found->Type == Kind::Buffer || found->Type == Kind::WriteBuffer)) ||
				(slot.Type == Type::Sampler && found->Type == Kind::Sampler);

			if (!compatible)
			{
				throw std::invalid_argument(label + ": parameter '" + slot.Name + "' was bound as the wrong kind of resource");
			}

			resolved.push_back({ &slot, &*found });
		}

		for (const auto& binding : bindings)
		{
			if (std::none_of(compiled.Bindings.begin(), compiled.Bindings.end(),
					[&](const RuntimeBinding& b)
					{
						return b.Name == binding.Name;
					}))
			{
				throw std::invalid_argument(label + ": the program has no parameter named '" + binding.Name + "'");
			}
		}

		auto& graph = context.graph;

		if (!compiled.Tweaks.empty() && !constants.empty() && context.services.OverrideConstants)
		{
			context.services.OverrideConstants(programName, compiled, constants);
		}

		// Formats of the bound textures (views are created with them; depth reads its depth aspect).
		struct TextureView
		{
			R::GraphTexture Texture;
			S::Format Format = S::Format::Undefined;
			bool IsDepth = false;
			std::uint32_t Binding = 0;
			std::uint32_t MipLevels = 1; // Sampled textures see every mip; storage images mip 0.
		};

		struct SamplerSlot
		{
			S::Sampler* Sampler = nullptr;
			std::uint32_t Binding = 0;
		};

		struct BufferSlot
		{
			R::GraphBuffer Buffer;
			std::uint32_t Binding = 0;
		};

		std::vector<TextureView> textures;
		std::vector<SamplerSlot> samplers;
		std::vector<BufferSlot> buffers;

		for (const auto& [slot, resource] : resolved)
		{
			switch (resource->Type)
			{
			case Kind::Texture:
			case Kind::Storage:
			{
				const auto format = graph.GetDesc(resource->TextureHandle).PixelFormat;
				const bool isDepth = format == S::Format::D32Float;

				if (resource->Type == Kind::Storage && slot->StorageFormat != S::Format::Undefined && slot->StorageFormat != format)
				{
					throw std::invalid_argument(
						label + ": storage image '" + slot->Name + "' has a different format than the shader declares");
				}

				textures.push_back({ resource->TextureHandle, format, isDepth, slot->Binding,
					resource->Type == Kind::Texture ? std::max(graph.GetDesc(resource->TextureHandle).MipLevels, 1u) : 1u });
				break;
			}
			case Kind::Sampler:
				samplers.push_back({ &context.services.GetSampler(resource->SamplerKind), slot->Binding });
				break;
			default:
				buffers.push_back({ resource->BufferHandle, slot->Binding });
				break;
			}
		}

		const auto groups = compiled.ThreadGroupSize;
		const auto count = [](std::uint32_t size, std::uint32_t group)
		{
			return (size + std::max(group, 1u) - 1) / std::max(group, 1u);
		};
		const std::array<std::uint32_t, 3> dispatch{ count(width, groups[0]), count(height, groups[1]), count(depth, groups[2]) };
		const auto* program = &compiled;
		auto resources = bindings;
		return graph.AddPass(
			label, S::QueueType::Compute,
			[&](R::RenderGraphBuilder& b)
			{
				for (const auto& resource : resources)
				{
					switch (resource.Type)
					{
					case Kind::Texture:
						b.Read(resource.TextureHandle, S::ResourceState::ShaderRead);
						break;
					case Kind::Storage:
						b.Write(resource.TextureHandle, S::ResourceState::ShaderWrite);
						break;
					case Kind::Buffer:
						b.Read(resource.BufferHandle, S::ResourceState::ShaderRead);
						break;
					case Kind::StorageBuffer:
						b.ReadWrite(resource.BufferHandle, S::ResourceState::ShaderRead | S::ResourceState::ShaderWrite);
						break;
					case Kind::WriteBuffer:
						b.Write(resource.BufferHandle, S::ResourceState::ShaderWrite);
						break;
					case Kind::Sampler:
						break;
					}
				}

			},
			[program, label, textures, samplers, buffers, constants = constants, dispatch](R::RenderCommandContext& c)
			{
				std::vector<S::DescriptorWrite> writes;

				for (const auto& texture : textures)
				{
					S::TextureViewDesc viewDesc;
					viewDesc.PixelFormat = texture.Format;
					viewDesc.MipLevelCount = texture.MipLevels;
					viewDesc.Aspect = texture.IsDepth ? S::TextureAspect::Depth : S::TextureAspect::Automatic;
					S::DescriptorWrite write{};
					write.Binding = texture.Binding;
					write.TextureResource = &c.CreateView(texture.Texture, viewDesc);
					writes.push_back(write);
				}

				for (const auto& sampler : samplers)
				{
					S::DescriptorWrite write{};
					write.Binding = sampler.Binding;
					write.SamplerResource = sampler.Sampler;
					writes.push_back(write);
				}

				for (const auto& buffer : buffers)
				{
					const auto range = c.GetRange(buffer.Buffer);
					S::DescriptorWrite write{};
					write.Binding = buffer.Binding;
					write.BufferResource = range.Buffer;
					write.BufferOffset = range.Offset;
					write.BufferRange = range.Size;
					writes.push_back(write);
				}

				auto& list = c.Commands();
				list.BindComputePipeline(*program->Pipeline);

				if (!writes.empty())
				{
					auto table = c.CreateDescriptorTable({ program->Layout.get(), program->Space, 0, label });

					if (!table)
					{
						throw std::runtime_error(label + " descriptor table could not be created");
					}

					table->Write(writes);
					auto& retained = static_cast<S::DescriptorTable&>(c.Retain(std::move(table)));
					list.BindDescriptorTable(program->Space, retained);
				}

				if (!constants.empty())
				{
					list.PushConstants(S::ShaderStageMask::Compute, 0, constants);
				}

				list.Dispatch(dispatch[0], dispatch[1], dispatch[2]);
			});
	}

	RenderFeatureDrawContext::RenderFeatureDrawContext(
		R::ExternalDrawContext& drawValue, const RenderFeatureView& viewValue, const RenderFeatureContext::Services& servicesValue)
		: draw(drawValue), view(viewValue), services(servicesValue)
	{
	}

	RenderFeatureDraw RenderFeatureDrawContext::Program(std::string_view name, const RenderGeometryState& state)
	{
		if (!services.GetGeometryPipeline)
		{
			throw std::logic_error("RenderFeatureDrawContext has no pipeline cache");
		}

		const auto pipeline = services.GetGeometryPipeline(name, draw.Pass, draw.DepthPrepassed && !state.SkipsPrepass, state);

		if (!pipeline.Pipeline || !pipeline.Program)
		{
			throw std::runtime_error("Feature program " + std::string(name) + " has no pipeline for this pass");
		}

		return RenderFeatureDraw(*this, std::string(name), pipeline);
	}

	S::DescriptorTable& RenderFeatureDrawContext::FrameTable(S::PipelineLayout& layout)
	{
		for (const auto& [cached, table] : frameTables)
		{
			if (cached == &layout)
			{
				return *table;
			}
		}

		auto& table = draw.FrameTable(layout);
		frameTables.emplace_back(&layout, &table);
		return table;
	}

	RenderFeatureDraw::RenderFeatureDraw(RenderFeatureDrawContext& contextValue, std::string program, RenderFeatureGeometryPipeline pipelineValue)
		: context(contextValue), programName(std::move(program)), pipeline(pipelineValue)
	{
	}

	RenderFeatureDraw& RenderFeatureDraw::Buffer(std::string_view name, R::GraphBuffer buffer)
	{
		bindings.push_back({ std::string(name), Kind::Buffer, buffer, {}, nullptr, nullptr, {} });
		return *this;
	}

	RenderFeatureDraw& RenderFeatureDraw::Texture(std::string_view name, R::GraphTexture texture)
	{
		bindings.push_back({ std::string(name), Kind::Texture, {}, texture, nullptr, nullptr, {} });
		return *this;
	}

	RenderFeatureDraw& RenderFeatureDraw::Texture(std::string_view name, S::TextureView& view)
	{
		bindings.push_back({ std::string(name), Kind::View, {}, {}, &view, nullptr, {} });
		return *this;
	}

	RenderFeatureDraw& RenderFeatureDraw::Sampler(std::string_view name, std::string_view kind)
	{
		bindings.push_back({ std::string(name), Kind::Sampler, {}, {}, nullptr, nullptr, std::string(kind) });
		return *this;
	}

	RenderFeatureDraw& RenderFeatureDraw::Sampler(std::string_view name, S::Sampler& sampler)
	{
		bindings.push_back({ std::string(name), Kind::SamplerObject, {}, {}, nullptr, &sampler, {} });
		return *this;
	}

	void RenderFeatureDraw::Bind()
	{
		using Type = S::DescriptorType;
		const auto label = "Feature " + programName;
		auto& c = context.Commands();
		auto& list = c.Commands();
		auto& layout = *pipeline.Program->Layout;
		const auto& reflected = pipeline.Program->Bindings;

		for (const auto& binding : bindings)
		{
			if (std::none_of(reflected.begin(), reflected.end(),
					[&](const RuntimeBinding& b)
					{
						return b.Name == binding.Name && b.Space >= 2;
					}))
			{
				throw std::invalid_argument(label + ": the program has no feature parameter named '" + binding.Name + "'");
			}
		}

		list.BindGraphicsPipeline(*pipeline.Pipeline);
		list.BindDescriptorTable(0, context.FrameTable(layout));

		if (context.draw.Bindless)
		{
			list.BindDescriptorTable(1, *context.draw.Bindless);
		}

		std::vector<std::uint32_t> spaces;

		for (const auto& slot : reflected)
		{
			if (slot.Space >= 2 && std::find(spaces.begin(), spaces.end(), slot.Space) == spaces.end())
			{
				spaces.push_back(slot.Space);
			}
		}

		for (const auto space : spaces)
		{
			std::vector<S::DescriptorWrite> writes;

			for (const auto& slot : reflected)
			{
				if (slot.Space != space)
				{
					continue;
				}

				const auto found = std::find_if(bindings.begin(), bindings.end(),
					[&](const Binding& b)
					{
						return b.Name == slot.Name;
					});

				if (found == bindings.end())
				{
					throw std::invalid_argument(label + ": parameter '" + slot.Name + "' was not bound");
				}

				const bool isBuffer = slot.Type == Type::ReadOnlyStorageBuffer || slot.Type == Type::StorageBuffer;
				const bool compatible = (isBuffer && found->Type == Kind::Buffer) ||
					(slot.Type == Type::SampledTexture && (found->Type == Kind::Texture || found->Type == Kind::View)) ||
					(slot.Type == Type::Sampler && (found->Type == Kind::Sampler || found->Type == Kind::SamplerObject));

				if (!compatible)
				{
					throw std::invalid_argument(label + ": parameter '" + slot.Name + "' was bound as the wrong kind of resource");
				}

				S::DescriptorWrite write{};
				write.Binding = slot.Binding;

				switch (found->Type)
				{
				case Kind::Buffer:
				{
					const auto range = c.GetRange(found->BufferHandle);
					write.BufferResource = range.Buffer;
					write.BufferOffset = range.Offset;
					write.BufferRange = range.Size;
					break;
				}
				case Kind::Texture:
				{
					const auto& desc = c.Get(found->TextureHandle).GetDesc();
					S::TextureViewDesc viewDesc;
					viewDesc.PixelFormat = desc.PixelFormat;
					viewDesc.Dimension = desc.ArrayLayers > 1 ? S::TextureViewDimension::Texture2DArray : S::TextureViewDimension::Texture2D;

					// The shader's declaration decides (a one-layer Texture2DArray is still an array).
					for (const auto& schema : layout.GetInterface().DescriptorSchemas)
					{
						for (const auto& declared : schema.Bindings)
						{
							if (schema.Space == slot.Space && declared.Binding == slot.Binding)
							{
								viewDesc.Dimension = declared.SampledDimension;
							}
						}
					}

					viewDesc.MipLevelCount = desc.MipLevels;
					viewDesc.ArrayLayerCount = desc.ArrayLayers;
					viewDesc.Aspect = S::IsDepthFormat(desc.PixelFormat) ? S::TextureAspect::Depth : S::TextureAspect::Automatic;
					write.TextureResource = &c.CreateView(found->TextureHandle, viewDesc);
					break;
				}
				case Kind::View:
					write.TextureResource = found->ViewObject;
					break;
				case Kind::Sampler:
					write.SamplerResource = &context.services.GetSampler(found->SamplerKind);
					break;
				case Kind::SamplerObject:
					write.SamplerResource = found->SamplerObject;
					break;
				}

				writes.push_back(write);
			}

			auto table = c.CreateDescriptorTable({ &layout, space, 0, label });

			if (!table)
			{
				throw std::runtime_error(label + " descriptor table could not be created");
			}

			table->Write(writes);
			list.BindDescriptorTable(space, static_cast<S::DescriptorTable&>(c.Retain(std::move(table))));
		}

		const auto& ranges = layout.GetInterface().PushConstants;

		if (!constants.empty())
		{
			if (ranges.empty())
			{
				throw std::invalid_argument(label + ": the program declares no push constants");
			}

			list.PushConstants(ranges.front().Stages, 0, constants);
		}
	}

	void RenderFeatureDraw::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance)
	{
		Bind();
		context.Commands().Commands().Draw(vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void RenderFeatureDraw::DrawIndirect(R::GraphBuffer arguments, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride)
	{
		Bind();
		auto& c = context.Commands();
		const auto range = c.GetRange(arguments);
		c.Commands().DrawIndirect(*range.Buffer, range.Offset + offset, drawCount, stride);
	}

	void RenderFeatureDraw::DrawIndirectCount(R::GraphBuffer arguments, std::uint64_t offset, R::GraphBuffer count, std::uint64_t countOffset,
		std::uint32_t maxDrawCount, std::uint32_t stride)
	{
		Bind();
		auto& c = context.Commands();
		const auto range = c.GetRange(arguments);
		const auto counter = c.GetRange(count);
		c.Commands().DrawIndirectCount(*range.Buffer, range.Offset + offset, *counter.Buffer, counter.Offset + countOffset, maxDrawCount, stride);
	}

} // namespace Engine
