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
				(slot.Type == Type::StorageBuffer && (found->Type == Kind::StorageBuffer || found->Type == Kind::Buffer)) ||
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

		// Formats of the bound textures (views are created with them; depth reads its depth aspect).
		struct TextureView
		{
			R::GraphTexture Texture;
			S::Format Format = S::Format::Undefined;
			bool IsDepth = false;
			std::uint32_t Binding = 0;
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
				textures.push_back({ resource->TextureHandle, format, isDepth, slot->Binding });
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
					auto table = c.Device().CreateDescriptorTable({ program->Layout.get(), program->Space, 0, label });
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
} // namespace Engine
