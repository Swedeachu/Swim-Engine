#pragma once

#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"
#include "Tests/Fixtures/RhiFrameCapture.h"

#include <memory>

namespace Swim::Testing
{

	// A mock device plus a layout whose space 1 is a bindless sampler array
	// (binding 0) and texture array (binding 1), with fallback resources.
	struct BindlessTableFixture
	{
		explicit BindlessTableFixture(std::uint32_t textures = 4, std::uint32_t samplers = 3)
		{
			device.CreateTextures = true;
			layout.program.Interface.DescriptorSchemas.push_back(MakeSpace(textures, samplers));
			Rhi::TextureDesc desc{};
			desc.Extent = { 1, 1, 1 };
			desc.PixelFormat = Rhi::Format::RGBA8Unorm;
			desc.Usage = Rhi::TextureUsage::Sampled;
			fallbackTexture = std::make_unique<MockTexture>(desc);
			fallbackView = std::make_unique<MockTextureView>(*fallbackTexture, Rhi::TextureViewDesc{});
			fallbackSampler = std::make_unique<MockSampler>(Rhi::SamplerDesc{});
		}

		static Rhi::DescriptorSchemaDesc MakeSpace(std::uint32_t textures, std::uint32_t samplers)
		{
			Rhi::DescriptorSchemaDesc space{ 1,
				{ { 0, Rhi::DescriptorType::Sampler, samplers, Rhi::ShaderStageMask::Fragment },
					{ 1, Rhi::DescriptorType::SampledTexture, textures, Rhi::ShaderStageMask::Fragment } } };
			for (auto& binding : space.Bindings)
			{
				binding.PartiallyBound = binding.UpdateAfterBind = true;
			}
			return space;
		}

		Render::BindlessTableDesc Desc()
		{
			Render::BindlessTableDesc desc;
			desc.Layout = &layout;
			desc.Space = 1;
			desc.FallbackTexture = fallbackView.get();
			desc.FallbackSampler = fallbackSampler.get();
			return desc;
		}

		MockDescriptorTable& Table() { return *device.LastDescriptorTable; }

		MockDevice device;
		MockPipelineLayout layout;
		std::unique_ptr<MockTexture> fallbackTexture;
		std::unique_ptr<MockTextureView> fallbackView;
		std::unique_ptr<MockSampler> fallbackSampler;
	};

} // namespace Swim::Testing
