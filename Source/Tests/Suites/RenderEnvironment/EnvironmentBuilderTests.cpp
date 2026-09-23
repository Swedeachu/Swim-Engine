#include "Engine/Systems/Renderer/Environment/EnvironmentBuilder.h"
#include "Engine/Systems/Renderer/Environment/CubeImage.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Framework/Test.h"

#include <cstring>
#include <memory>

using namespace Swim;
using namespace Swim::Render;
namespace Env = Swim::Render::Environment;

namespace
{
	struct EnvironmentWorld
	{
		EnvironmentWorld()
		{
			device.CreateTextures = true;
			executor = std::make_unique<RenderGraphExecutor>(device);
			const auto schema =
				[](Testing::MockPipelineLayout& layout, std::initializer_list<std::pair<std::uint32_t, Rhi::DescriptorType>> bindings)
			{
				Rhi::DescriptorSchemaDesc space{ 0, {} };
				for (const auto& [binding, type] : bindings)
				{
					space.Bindings.push_back({ binding, type, 1, Rhi::ShaderStageMask::Compute });
				}
				layout.program.Interface.DescriptorSchemas = { space };
			};
			using T = Rhi::DescriptorType;
			schema(skyLayout, { { EnvironmentSkyBindings::Destination, T::StorageTexture } });
			schema(downsampleLayout,
				{ { EnvironmentDownsampleBindings::Source, T::SampledTexture },
					{ EnvironmentDownsampleBindings::Destination, T::StorageTexture } });
			schema(prefilterLayout,
				{ { EnvironmentPrefilterBindings::Source, T::SampledTexture }, { EnvironmentPrefilterBindings::Sampler, T::Sampler },
					{ EnvironmentPrefilterBindings::Destination, T::StorageTexture } });
			schema(irradianceLayout,
				{ { EnvironmentIrradianceBindings::Source, T::SampledTexture },
					{ EnvironmentIrradianceBindings::Output, T::StorageBuffer } });
			schema(lutLayout, { { EnvironmentBrdfLutBindings::Destination, T::StorageTexture } });
			sampler = device.CreateSampler({});
		}

		EnvironmentBuilderDesc Desc()
		{
			EnvironmentBuilderDesc desc;
			desc.Sky = { &skyPipeline, &skyLayout, 0 };
			desc.Downsample = { &downsamplePipeline, &downsampleLayout, 0 };
			desc.Prefilter = { &prefilterPipeline, &prefilterLayout, 0 };
			desc.Irradiance = { &irradiancePipeline, &irradianceLayout, 0 };
			desc.BrdfLut = { &lutPipeline, &lutLayout, 0 };
			desc.Sampler = sampler.get();
			desc.DebugName = "Test environment";
			return desc;
		}

		std::vector<Testing::MockCommand> Commands(const std::string& kind) const
		{
			std::vector<Testing::MockCommand> result;
			for (const auto& command : *device.Commands)
			{
				if (command.Kind == kind)
				{
					result.push_back(command);
				}
			}
			return result;
		}

		void Run(RenderGraph& graph)
		{
			device.Commands->clear();
			executor->Execute(graph.Compile());
			executor->Wait();
		}

		template <typename T> static T Read(const Testing::MockCommand& command)
		{
			T value{};
			SWIM_REQUIRE_EQUAL(command.Data.size(), sizeof(T));
			std::memcpy(&value, command.Data.data(), sizeof(T));
			return value;
		}

		Testing::MockDevice device;
		Testing::MockPipelineLayout skyLayout, downsampleLayout, prefilterLayout, irradianceLayout, lutLayout;
		Testing::MockComputePipeline skyPipeline, downsamplePipeline, prefilterPipeline, irradiancePipeline, lutPipeline;
		std::unique_ptr<Rhi::Sampler> sampler;
		std::unique_ptr<RenderGraphExecutor> executor;
	};
} // namespace

SWIM_TEST("Render.EnvironmentBuilder", "RecordsSkyMipsPrefilterAndIrradiancePasses")
{
	EnvironmentWorld world;
	const EnvironmentBuilder builder(world.Desc());
	EnvironmentMapDesc map;
	map.SourceSize = 32;
	map.PrefilteredSize = 16;
	map.PrefilteredMipCount = 4;
	map.PrefilterSampleCount = 48;
	map.IrradianceFaceSize = 8;
	Env::ProceduralSky sky;
	sky.Intensity = 1.5f;

	RenderGraph graph;
	const auto environment = builder.Record(graph, sky, map);
	SWIM_CHECK_EQUAL(environment.SourceSize, 32u);
	SWIM_CHECK_EQUAL(environment.SourceMipCount, 4u); // 32, 16, 8, 4.
	SWIM_CHECK_EQUAL(environment.PrefilteredMipCount, 4u);
	// Sky + 3 mips + 4 prefilter mips + irradiance.
	SWIM_CHECK_EQUAL(environment.Passes.size(), std::size_t(9));
	const auto source = graph.GetDesc(environment.Source);
	SWIM_CHECK(source.Dimension == Rhi::TextureDimension::TextureCube);
	SWIM_CHECK(source.ArrayLayers == 6 && source.MipLevels == 4 && source.PixelFormat == Rhi::Format::RGBA16Float);
	const auto prefiltered = graph.GetDesc(environment.Prefiltered);
	SWIM_CHECK(prefiltered.Dimension == Rhi::TextureDimension::TextureCube);
	SWIM_CHECK(prefiltered.Extent.Width == 16 && prefiltered.MipLevels == 4 && prefiltered.ArrayLayers == 6);
	SWIM_CHECK_EQUAL(graph.GetDesc(environment.Irradiance).Size, EnvironmentIrradianceBindings::OutputBytes);
	graph.Export(environment.Prefiltered, Rhi::ResourceState::ShaderRead);
	graph.Export(environment.Irradiance, Rhi::ResourceState::ShaderRead);
	world.Run(graph);

	// Dispatches: 6 sky faces (32 / 8 = 4 groups), 6 per source mip, 6 per
	// prefiltered mip, then one irradiance group.
	const auto dispatches = world.Commands("Dispatch");
	SWIM_REQUIRE_EQUAL(dispatches.size(), std::size_t(6 + 18 + 24 + 1));
	SWIM_CHECK(dispatches[0].SourceOffset == 4 && dispatches[0].DestinationOffset == 4 && dispatches[0].Size == 1);
	SWIM_CHECK(dispatches[6].SourceOffset == 2 && dispatches[6].DestinationOffset == 2);   // Mip 1: 16 texels.
	SWIM_CHECK(dispatches[18].SourceOffset == 1 && dispatches[18].DestinationOffset == 1); // Mip 3: 4 texels.
	SWIM_CHECK(dispatches[24].SourceOffset == 2);										   // Prefilter mip 0: 16 texels.
	SWIM_CHECK(dispatches[42].SourceOffset == 1);										   // Prefilter mip 3: 2 texels.
	SWIM_CHECK(dispatches.back().SourceOffset == 1 && dispatches.back().DestinationOffset == 1 && dispatches.back().Size == 1);

	// Push constants: the sky per face, then sizes, then the prefilter's roughness ladder.
	const auto constants = world.Commands("PushConstants");
	SWIM_REQUIRE_EQUAL(constants.size(), dispatches.size());
	for (std::uint32_t face = 0; face < 6; ++face)
	{
		const auto skyConstants = EnvironmentWorld::Read<Env::ProceduralSkyConstants>(constants[face]);
		SWIM_CHECK_EQUAL(skyConstants.Face, face);
		SWIM_CHECK_EQUAL(skyConstants.Size, 32u);
		SWIM_CHECK_EQUAL(skyConstants.Zenith[3], 1.5f);
	}
	SWIM_CHECK((EnvironmentWorld::Read<std::array<std::uint32_t, 4>>(constants[6])[0]) == 16u);
	SWIM_CHECK((EnvironmentWorld::Read<std::array<std::uint32_t, 4>>(constants[23])[0]) == 4u);
	for (std::uint32_t mip = 0; mip < 4; ++mip)
	{
		for (std::uint32_t face = 0; face < 6; ++face)
		{
			const auto prefilter = EnvironmentWorld::Read<EnvironmentPrefilterConstants>(constants[24 + mip * 6 + face]);
			SWIM_CHECK_EQUAL(prefilter.Face, face);
			SWIM_CHECK_EQUAL(prefilter.Size, 16u >> mip);
			SWIM_CHECK_EQUAL(prefilter.Roughness, Env::PrefilterMipRoughness(mip, 4));
			SWIM_CHECK_EQUAL(prefilter.SampleCount, 48u);
			SWIM_CHECK_EQUAL(prefilter.SourceSize, 32u);
			SWIM_CHECK_EQUAL(prefilter.SourceMipCount, 4u);
		}
	}
	SWIM_CHECK((EnvironmentWorld::Read<std::array<std::uint32_t, 4>>(constants.back())[0]) == 8u);

	// The last table is the irradiance projection: a 6-layer array view of the 8x8 mip.
	const auto* table = world.device.LastDescriptorTable;
	SWIM_REQUIRE(table != nullptr);
	const auto* view = static_cast<const Rhi::TextureView*>(table->Element(EnvironmentIrradianceBindings::Source, 0));
	SWIM_REQUIRE(view != nullptr);
	SWIM_CHECK(view->GetDesc().Dimension == Rhi::TextureViewDimension::Texture2DArray);
	SWIM_CHECK_EQUAL(view->GetDesc().BaseMipLevel, 2u);
	SWIM_CHECK_EQUAL(view->GetDesc().ArrayLayerCount, 6u);
	SWIM_CHECK(table->Element(EnvironmentIrradianceBindings::Output, 0) != nullptr);
}

SWIM_TEST("Render.EnvironmentBuilder", "PrefilterReadsTheWholeCubeAndWritesOneFacePerDispatch")
{
	EnvironmentWorld world;
	const EnvironmentBuilder builder(world.Desc());
	EnvironmentMapDesc map;
	map.SourceSize = 16;
	map.PrefilteredSize = 8;
	map.PrefilteredMipCount = 2;
	map.IrradianceFaceSize = 4;

	// An external source cube (for example an uploaded HDR environment) with its own mips.
	RenderGraph graph;
	auto sourceDesc = EnvironmentBuilder::SourceCubeDesc(16);
	sourceDesc.DebugName = "Uploaded environment";
	const auto source = graph.CreateTexture(sourceDesc);
	graph.AddPass(
		"Upload", Rhi::QueueType::Compute,
		[&](RenderGraphBuilder& b)
		{
			b.Write(source, Rhi::ResourceState::ShaderWrite, { 0, 1, 0, 6 });
		},
		[](RenderCommandContext&)
		{
		});
	const auto mipPasses = builder.RecordMips(graph, source);
	SWIM_CHECK_EQUAL(mipPasses.size(), std::size_t(2));
	// Persistent targets can be supplied.
	auto targetDesc = EnvironmentBuilder::PrefilteredCubeDesc(map);
	targetDesc.DebugName = "Persistent prefiltered";
	const auto target = graph.CreateTexture(targetDesc);
	const auto environment = builder.RecordFromSource(graph, source, map, { target, std::nullopt });
	SWIM_CHECK(environment.Prefiltered == target);
	SWIM_CHECK_EQUAL(environment.Passes.size(), std::size_t(3));
	graph.Export(environment.Prefiltered, Rhi::ResourceState::ShaderRead);
	graph.Export(environment.Irradiance, Rhi::ResourceState::ShaderRead);

	world.Run(graph);
	SWIM_CHECK_EQUAL(world.Commands("Dispatch").size(), std::size_t(12 + 12 + 1));

	// Only the prefiltered cube is consumed: the irradiance pass is culled and the last
	// table is the prefilter's (mip 1, face 5).
	RenderGraph single;
	const auto singleSource = single.CreateTexture(sourceDesc);
	single.AddPass(
		"Upload", Rhi::QueueType::Compute,
		[&](RenderGraphBuilder& b)
		{
			b.Write(singleSource, Rhi::ResourceState::ShaderWrite);
		},
		[](RenderCommandContext&)
		{
		});
	const auto only = builder.RecordFromSource(single, singleSource, map);
	single.Export(only.Prefiltered, Rhi::ResourceState::ShaderRead);
	world.Run(single);
	SWIM_CHECK_EQUAL(world.Commands("Dispatch").size(), std::size_t(12));
	const auto* table = world.device.LastDescriptorTable;
	SWIM_REQUIRE(table != nullptr);
	const auto* sourceView = static_cast<const Rhi::TextureView*>(table->Element(EnvironmentPrefilterBindings::Source, 0));
	const auto* destinationView = static_cast<const Rhi::TextureView*>(table->Element(EnvironmentPrefilterBindings::Destination, 0));
	SWIM_REQUIRE(sourceView != nullptr && destinationView != nullptr);
	SWIM_CHECK(sourceView->GetDesc().Dimension == Rhi::TextureViewDimension::TextureCube);
	SWIM_CHECK_EQUAL(sourceView->GetDesc().MipLevelCount, 3u);
	SWIM_CHECK_EQUAL(sourceView->GetDesc().ArrayLayerCount, 6u);
	SWIM_CHECK(destinationView->GetDesc().Dimension == Rhi::TextureViewDimension::Texture2D);
	SWIM_CHECK_EQUAL(destinationView->GetDesc().BaseMipLevel, 1u);
	SWIM_CHECK_EQUAL(destinationView->GetDesc().BaseArrayLayer, 5u);
	SWIM_CHECK(table->Element(EnvironmentPrefilterBindings::Sampler, 0) == world.sampler.get());
}

SWIM_TEST("Render.EnvironmentBuilder", "BrdfLutAndContractViolations")
{
	EnvironmentWorld world;
	SWIM_CHECK_THROWS(EnvironmentBuilder(EnvironmentBuilderDesc{}), std::invalid_argument);
	auto missingSampler = world.Desc();
	missingSampler.Sampler = nullptr;
	SWIM_CHECK_THROWS(EnvironmentBuilder(missingSampler), std::invalid_argument);
	auto missingProgram = world.Desc();
	missingProgram.Irradiance.Layout = nullptr;
	SWIM_CHECK_THROWS(EnvironmentBuilder(missingProgram), std::invalid_argument);
	const EnvironmentBuilder builder(world.Desc());

	RenderGraph graph;
	const auto lut = builder.RecordBrdfLut(graph, 32, 128);
	const auto lutDesc = graph.GetDesc(lut);
	SWIM_CHECK(lutDesc.Extent.Width == 32 && lutDesc.Extent.Height == 32 && lutDesc.PixelFormat == Rhi::Format::RGBA16Float);
	graph.Export(lut, Rhi::ResourceState::ShaderRead);
	world.Run(graph);
	const auto dispatches = world.Commands("Dispatch");
	SWIM_REQUIRE_EQUAL(dispatches.size(), std::size_t(1));
	SWIM_CHECK(dispatches[0].SourceOffset == 4 && dispatches[0].DestinationOffset == 4);
	const auto constants = EnvironmentWorld::Read<std::array<std::uint32_t, 4>>(world.Commands("PushConstants")[0]);
	SWIM_CHECK(constants[0] == 32 && constants[1] == 128);

	// Map contract.
	EnvironmentMapDesc map;
	EnvironmentBuilder::Validate(map);
	const auto rejects = [&](auto mutate)
	{
		auto bad = map;
		mutate(bad);
		RenderGraph badGraph;
		SWIM_CHECK_THROWS(builder.Record(badGraph, {}, bad), std::invalid_argument);
	};
	rejects(
		[](EnvironmentMapDesc& m)
		{
			m.SourceSize = 96;
		});
	rejects(
		[](EnvironmentMapDesc& m)
		{
			m.SourceSize = 8;
		});
	rejects(
		[](EnvironmentMapDesc& m)
		{
			m.PrefilteredSize = 48;
		});
	rejects(
		[](EnvironmentMapDesc& m)
		{
			m.PrefilteredMipCount = 1;
		});
	rejects(
		[](EnvironmentMapDesc& m)
		{
			m.PrefilteredMipCount = 8;
		}); // 64 has 7 mips.
	rejects(
		[](EnvironmentMapDesc& m)
		{
			m.PrefilterSampleCount = 0;
		});
	rejects(
		[](EnvironmentMapDesc& m)
		{
			m.IrradianceFaceSize = 256;
		});
	rejects(
		[](EnvironmentMapDesc& m)
		{
			m.IrradianceFaceSize = 2;
		});

	// Source and target contracts.
	RenderGraph bad;
	Rhi::TextureDesc flat = EnvironmentBuilder::BrdfLutDesc(128);
	SWIM_CHECK_THROWS(builder.RecordFromSource(bad, bad.CreateTexture(flat), map), std::invalid_argument);
	auto wrongMips = EnvironmentBuilder::SourceCubeDesc(128);
	wrongMips.MipLevels = 2;
	SWIM_CHECK_THROWS(builder.RecordFromSource(bad, bad.CreateTexture(wrongMips), map), std::invalid_argument);
	auto wrongFormat = EnvironmentBuilder::SourceCubeDesc(128);
	wrongFormat.PixelFormat = Rhi::Format::RGBA32Float;
	SWIM_CHECK_THROWS(builder.RecordFromSource(bad, bad.CreateTexture(wrongFormat), map), std::invalid_argument);
	SWIM_CHECK_THROWS(builder.RecordMips(bad, bad.CreateTexture(wrongFormat)), std::invalid_argument);
	auto sampledOnly = EnvironmentBuilder::SourceCubeDesc(64);
	sampledOnly.Usage = Rhi::TextureUsage::Sampled;
	SWIM_CHECK_THROWS(builder.RecordMips(bad, bad.CreateTexture(sampledOnly)), std::invalid_argument);
	const auto goodSource = bad.CreateTexture(EnvironmentBuilder::SourceCubeDesc(128));
	auto wrongTarget = EnvironmentBuilder::PrefilteredCubeDesc(map);
	wrongTarget.MipLevels = 3;
	SWIM_CHECK_THROWS(
		builder.RecordFromSource(bad, goodSource, map, { bad.CreateTexture(wrongTarget), std::nullopt }), std::invalid_argument);
	const auto tinyBuffer = bad.CreateBuffer({ 64, Rhi::BufferUsage::Storage, Rhi::MemoryPreference::DeviceLocal, "Tiny" });
	SWIM_CHECK_THROWS(builder.RecordFromSource(bad, goodSource, map, { std::nullopt, tinyBuffer }), std::invalid_argument);
	SWIM_CHECK_THROWS(builder.RecordBrdfLut(bad, 2, 16), std::invalid_argument);
	SWIM_CHECK_THROWS(builder.RecordBrdfLut(bad, 32, 0), std::invalid_argument);
	SWIM_CHECK_THROWS(builder.RecordBrdfLut(bad, 32, 16, bad.CreateTexture(EnvironmentBuilder::BrdfLutDesc(16))), std::invalid_argument);
}
