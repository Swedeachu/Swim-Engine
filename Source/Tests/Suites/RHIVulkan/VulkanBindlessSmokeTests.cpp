#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"
#include "Engine/Systems/Renderer/Resources/GpuSamplerCache.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#ifdef SWIM_RHI_BINDLESS_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{

	// Runtime-sized reflected arrays, a shared bindless space supplied by the
	// pipeline layout, BindlessResourceTable/GpuSamplerCache element management,
	// an element written while earlier work is pending, and timeline-retired
	// element reuse with the fallback rewrite observable from a shader.
	void RunBindlessSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_BINDLESS_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "Bindless smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using S = Rhi::ResourceState;
		const auto reflection = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_BINDLESS_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		const auto& interface = converted.Interface;
		std::ifstream file(SWIM_RHI_BINDLESS_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		std::vector<std::byte> bytecode(static_cast<std::size_t>(file.tellg()));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
		SWIM_REQUIRE(file && !bytecode.empty());

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Bindless smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto& adapter = graphics->GetAdapter(0);
		SWIM_REQUIRE_MESSAGE(
			adapter.GetInfo().Capabilities.BindlessDescriptors, "Adapter lacks update-unused-while-pending bindless support");
		auto device = adapter.CreateDevice();
		SWIM_REQUIRE(device);

		// One shared space for every stage; the compute program only reflects compute.
		constexpr std::uint32_t textureCapacity = 64;
		constexpr std::uint32_t samplerCapacity = 8;
		Rhi::DescriptorSchemaDesc shared{ 1,
			{ { 0, Rhi::DescriptorType::Sampler, samplerCapacity, Rhi::ShaderStageMask::Compute },
				{ 1, Rhi::DescriptorType::SampledTexture, textureCapacity, Rhi::ShaderStageMask::Compute } } };
		for (auto& binding : shared.Bindings)
		{
			binding.Stages = Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment | Rhi::ShaderStageMask::Compute;
			binding.PartiallyBound = binding.UpdateAfterBind = true;
		}
		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytecode };
		auto program = device->CreateShaderProgram(
			{ { &stage, 1 }, { interface.DescriptorSchemas, interface.PushConstants, interface.ComputeThreadGroupSize }, "Bindless" });
		SWIM_REQUIRE(program);
		SWIM_CHECK(!device->CreatePipelineLayout({ program.get(), "Unsized bindless layout" })); // Needs the explicit capacity.
		auto layout = device->CreatePipelineLayout({ program.get(), "Bindless layout", { &shared, 1 } });
		SWIM_REQUIRE(layout);
		auto pipeline = device->CreateComputePipeline({ program.get(), layout.get(), {}, "Bindless compute" });
		SWIM_REQUIRE(pipeline);

		// Six 2x1 textures (the first is the 1x1 fallback); sampled at u = 0.5, nearest
		// returns texel 1 and linear the average of both texels.
		const std::array<std::array<std::uint8_t, 8>, 6> texels{ { { 0, 0, 255, 255, 0, 0, 255, 255 }, { 255, 0, 0, 255, 0, 255, 0, 255 },
			{ 0, 0, 0, 255, 255, 255, 255, 255 }, { 255, 0, 255, 255, 255, 255, 0, 255 }, { 10, 20, 30, 255, 40, 50, 60, 255 },
			{ 200, 100, 50, 255, 0, 0, 0, 255 } } };
		std::array<std::unique_ptr<Rhi::Texture>, 6> textures;
		std::array<std::unique_ptr<Rhi::TextureView>, 6> views;
		auto staging =
			device->CreateBuffer({ sizeof(texels), Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, "Bindless texels" });
		SWIM_REQUIRE(staging);
		staging->Write(0, std::as_bytes(std::span(texels)));
		for (std::size_t index = 0; index < textures.size(); ++index)
		{
			Rhi::TextureDesc desc{};
			desc.Extent = { index == 0 ? 1u : 2u, 1, 1 };
			desc.PixelFormat = Rhi::Format::RGBA8Unorm;
			desc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			textures[index] = device->CreateTexture(desc);
			SWIM_REQUIRE(textures[index]);
			Rhi::TextureViewDesc view{};
			view.PixelFormat = desc.PixelFormat;
			views[index] = device->CreateTextureView(*textures[index], view);
			SWIM_REQUIRE(views[index]);
		}
		Rhi::SamplerDesc nearestDesc{};
		nearestDesc.MinFilter = nearestDesc.MagFilter = nearestDesc.MipFilter = Rhi::Filter::Nearest;
		nearestDesc.AddressU = nearestDesc.AddressV = Rhi::SamplerAddressMode::ClampToEdge;
		Rhi::SamplerDesc linearDesc = nearestDesc;
		linearDesc.MinFilter = linearDesc.MagFilter = Rhi::Filter::Linear;
		auto fallbackSampler = device->CreateSampler(nearestDesc);
		SWIM_REQUIRE(fallbackSampler);

		Render::BindlessTableDesc bindlessDesc;
		bindlessDesc.Layout = layout.get();
		bindlessDesc.Space = 1;
		bindlessDesc.FallbackTexture = views[0].get();
		bindlessDesc.FallbackSampler = fallbackSampler.get();
		Render::BindlessResourceTable bindless(*device, bindlessDesc);
		Render::GpuSamplerCache samplerCache(*device, &bindless);
		const auto nearest = samplerCache.Acquire(nearestDesc);
		const auto linear = samplerCache.Acquire(linearDesc);
		auto renamed = nearestDesc;
		renamed.DebugName = "Same nearest sampler";
		SWIM_CHECK(samplerCache.Acquire(renamed) == nearest);
		const auto nearestIndex = samplerCache.GetBindlessIndex(nearest);
		const auto linearIndex = samplerCache.GetBindlessIndex(linear);
		SWIM_CHECK_EQUAL(nearestIndex, 1u);
		SWIM_CHECK_EQUAL(linearIndex, 2u);

		constexpr std::uint32_t maxRows = 8;
		constexpr std::size_t dispatches = 5;
		std::array<std::unique_ptr<Rhi::Buffer>, dispatches> lookups, results, readbacks;
		std::array<std::unique_ptr<Rhi::DescriptorTable>, dispatches> tables;
		for (std::size_t index = 0; index < dispatches; ++index)
		{
			lookups[index] =
				device->CreateBuffer({ maxRows * 8, Rhi::BufferUsage::Storage, Rhi::MemoryPreference::CpuToGpu, "Bindless lookups" });
			results[index] = device->CreateBuffer({ maxRows * 16, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource,
				Rhi::MemoryPreference::DeviceLocal, "Bindless results" });
			readbacks[index] = device->CreateBuffer(
				{ maxRows * 16, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::GpuToCpu, "Bindless readback" });
			tables[index] = device->CreateDescriptorTable({ layout.get(), 0, 0, "Bindless dispatch" });
			SWIM_REQUIRE(lookups[index] && results[index] && readbacks[index] && tables[index]);
			std::array<Rhi::DescriptorWrite, 2> writes{};
			writes[0].Binding = 0;
			writes[0].BufferResource = lookups[index].get();
			writes[1].Binding = 1;
			writes[1].BufferResource = results[index].get();
			tables[index]->Write(writes);
		}

		// Destroyed first: drains every submission before the table and resources go.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Compute, 2 });
		SWIM_REQUIRE(frames);

		struct Row
		{
			std::uint32_t Texture;
			std::uint32_t Sampler;
		};

		const auto submit = [&](std::size_t slot, const std::vector<Row>& rows, bool upload)
		{
			lookups[slot]->Write(0, std::as_bytes(std::span(rows)));
			frames->BeginFrame();
			auto& commands = frames->CreateCommandList();
			commands.Begin();
			if (upload)
			{
				commands.Transition(*staging, S::HostWrite, S::CopySource);
				for (std::size_t index = 0; index < textures.size(); ++index)
				{
					commands.Transition(*textures[index], S::Undefined, S::CopyDestination);
					commands.CopyBufferToTexture(*staging, *textures[index], { index * 8, {}, {}, { index == 0 ? 1u : 2u, 1, 1 } });
					commands.Transition(*textures[index], S::CopyDestination, S::ShaderRead);
				}
			}
			commands.Transition(*lookups[slot], S::HostWrite, S::ShaderRead);
			commands.Transition(*results[slot], S::Undefined, S::ShaderRead | S::ShaderWrite);
			commands.BindComputePipeline(*pipeline);
			commands.BindDescriptorTable(0, *tables[slot]);
			commands.BindDescriptorTable(1, bindless.GetTable());
			const std::array<std::uint32_t, 4> push{ static_cast<std::uint32_t>(rows.size()), 0, 0, 0 };
			commands.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(push)));
			commands.Dispatch(1, 1, 1);
			commands.Transition(*results[slot], S::ShaderRead | S::ShaderWrite, S::CopySource);
			commands.Transition(*readbacks[slot], S::Undefined, S::CopyDestination);
			commands.CopyBuffer(*results[slot], *readbacks[slot], { 0, 0, rows.size() * 16 });
			commands.Transition(*readbacks[slot], S::CopyDestination, S::HostRead);
			commands.End();
			frames->SubmitCurrent();
			return frames->GetLastSubmittedPoint();
		};
		// texture: texel set; sampler: bindless sampler index (fallback nearest at 0).
		const auto expect = [&](std::size_t slot, std::size_t row, std::size_t texture, bool linearFilter)
		{
			std::array<float, 4> actual{};
			readbacks[slot]->Read(row * 16, std::as_writable_bytes(std::span(actual)));
			for (std::size_t channel = 0; channel < 4; ++channel)
			{
				const float first = texels[texture][channel] / 255.0f;
				const float second = texels[texture][4 + channel] / 255.0f;
				const float wanted = texture == 0 ? first : linearFilter ? (first + second) * 0.5f : second;
				SWIM_CHECK(std::abs(actual[channel] - wanted) <= 2.0f / 255.0f);
			}
		};

		// Frame 0: two registered textures, both samplers and the fallback elements.
		auto first = bindless.RegisterTexture(*views[1]);
		auto second = bindless.RegisterTexture(*views[2]);
		SWIM_CHECK_EQUAL(bindless.GetIndex(first), 1u);
		SWIM_CHECK_EQUAL(bindless.GetIndex(second), 2u);
		submit(0,
			{ { 1, nearestIndex }, { 1, linearIndex }, { 2, nearestIndex }, { 2, linearIndex },
				{ Render::BindlessResourceTable::FallbackIndex, Render::BindlessResourceTable::FallbackIndex } },
			true);
		frames->Drain();
		expect(0, 0, 1, false);
		expect(0, 1, 1, true);
		expect(0, 2, 2, false);
		expect(0, 3, 2, true);
		expect(0, 4, 0, false);

		// Frame A reads elements 1 and 2. While it may still be pending, element 3 is
		// written (it is unused by A) and element 2 is released against A's point.
		const auto pointA = submit(1, { { 1, linearIndex }, { 2, nearestIndex } }, false);
		auto third = bindless.RegisterTexture(*views[3]);
		SWIM_CHECK_EQUAL(bindless.GetIndex(third), 3u);
		SWIM_CHECK(bindless.Release(second, pointA));
		// Element 2 is not reusable before Collect observes A's completion.
		auto fourth = bindless.RegisterTexture(*views[4]);
		SWIM_CHECK_EQUAL(bindless.GetIndex(fourth), 4u);
		submit(2, { { 3, nearestIndex }, { 3, linearIndex }, { 4, linearIndex }, { 1, nearestIndex } }, false);
		frames->Drain();
		expect(1, 0, 1, true);
		expect(1, 1, 2, false);
		expect(2, 0, 3, false);
		expect(2, 1, 3, true);
		expect(2, 2, 4, true);
		expect(2, 3, 1, false);

		// After retirement the element points at the fallback, so a stale index is safe.
		SWIM_CHECK_EQUAL(bindless.Collect(), 1u);
		SWIM_CHECK_EQUAL(bindless.GetIndex(second), Render::BindlessResourceTable::FallbackIndex);
		submit(3, { { 2, nearestIndex }, { 4, nearestIndex } }, false);
		frames->Drain();
		expect(3, 0, 0, false);
		expect(3, 1, 4, false);

		// The retired element is reused for a new texture with a new generation.
		auto fifth = bindless.RegisterTexture(*views[5]);
		SWIM_CHECK_EQUAL(bindless.GetIndex(fifth), 2u);
		SWIM_CHECK(fifth.Generation != second.Generation);
		const auto last = submit(4, { { 2, nearestIndex }, { 2, linearIndex }, { 3, linearIndex } }, false);
		frames->Drain();
		expect(4, 0, 5, false);
		expect(4, 1, 5, true);
		expect(4, 2, 3, true);

		for (auto handle : { first, third, fourth, fifth })
		{
			SWIM_CHECK(bindless.Release(handle, last));
		}
		for (auto handle : { nearest, nearest, linear })
		{
			SWIM_CHECK(samplerCache.Release(handle, last));
		}
		// Elements point back at the fallbacks before the samplers are destroyed.
		SWIM_CHECK_EQUAL(bindless.Collect(), 6u);
		SWIM_CHECK_EQUAL(samplerCache.Collect(), 2u);
		const auto stats = bindless.GetStats();
		SWIM_CHECK_EQUAL(stats.LiveTextures, 1u);
		SWIM_CHECK_EQUAL(stats.LiveSamplers, 1u);
		SWIM_CHECK_EQUAL(stats.TextureCapacity, textureCapacity);
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "BindlessTableTimelineSafeReuse", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunBindlessSmoke);
				} });
		}
		return true;
	}();

} // namespace
