#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusBindings.h"
#include "Engine/Systems/Renderer/Particles/ParticleSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"
#include "Tests/Fixtures/ParticleFixture.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#if defined(SWIM_PARTICLE_SIMULATE_SPIRV_PATH) && defined(SWIM_PARTICLE_EMIT_SPIRV_PATH) && defined(SWIM_PARTICLE_COMPACT_SPIRV_PATH) &&   \
	defined(SWIM_PARTICLE_FINALIZE_SPIRV_PATH) && defined(SWIM_PARTICLE_RENDER_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_SKY_SPIRV_PATH) &&  \
	defined(SWIM_ENVIRONMENT_DOWNSAMPLE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_PREFILTER_SPIRV_PATH) &&                                   \
	defined(SWIM_ENVIRONMENT_IRRADIANCE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_BRDF_LUT_SPIRV_PATH)
#include "Tests/Fixtures/VulkanEnvironmentFixture.h"
#define SWIM_PARTICLE_SMOKE_AVAILABLE 1
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
#ifdef SWIM_PARTICLE_SMOKE_AVAILABLE
	namespace Smoke = Swim::Testing::EnvironmentSmoke;
	namespace P = Swim::Render::Particles;
	namespace Scene = Swim::Testing::ParticleScene;

	double PassMilliseconds(const std::vector<Swim::Render::GraphPassTiming>& timings, std::string_view prefix)
	{
		double total = 0.0;
		double previousEnd = 0.0;
		for (const auto& timing : timings)
		{
			if (!timing.EndOffsetNanoseconds)
			{
				continue;
			}
			const double end = *timing.EndOffsetNanoseconds;
			if (timing.Name.starts_with(prefix))
			{
				total += std::max(end - previousEnd, 0.0) * 1.0e-6;
			}
			previousEnd = std::max(previousEnd, end);
		}
		return total;
	}

	bool Close(float actual, float expected, float relative, float absolute)
	{
		return std::abs(actual - expected) <= absolute + relative * std::abs(expected);
	}

	// A 4 x 2 flipbook of 8 x 8 cells: each cell a distinct color, fading toward its
	// centre so bilinear filtering matters.
	constexpr std::uint32_t AtlasWidth = 32;
	constexpr std::uint32_t AtlasHeight = 16;

	std::vector<std::array<std::uint8_t, 4>> MakeAtlas()
	{
		std::vector<std::array<std::uint8_t, 4>> texels(AtlasWidth * AtlasHeight);
		for (std::uint32_t y = 0; y < AtlasHeight; ++y)
		{
			for (std::uint32_t x = 0; x < AtlasWidth; ++x)
			{
				const std::uint32_t cell = (y / 8) * 4 + x / 8;
				const float dx = (float(x % 8) + 0.5f) / 8.0f - 0.5f;
				const float dy = (float(y % 8) + 0.5f) / 8.0f - 0.5f;
				const float fade = std::clamp(1.0f - 2.5f * (dx * dx + dy * dy), 0.0f, 1.0f);
				texels[y * AtlasWidth + x] = { std::uint8_t(40 + 25 * cell), std::uint8_t(255 - 30 * cell),
					std::uint8_t(120 + (cell % 3) * 60), std::uint8_t(std::lround(255.0f * fade)) };
			}
		}
		return texels;
	}

	// Bilinear, clamp to edge, at uv (Vulkan's texel-centre convention).
	Scene::Float4 SampleAtlas(const std::vector<std::array<std::uint8_t, 4>>& atlas, const Scene::Float2& uv)
	{
		const float fx = uv[0] * float(AtlasWidth) - 0.5f;
		const float fy = uv[1] * float(AtlasHeight) - 0.5f;
		const int x0 = int(std::floor(fx));
		const int y0 = int(std::floor(fy));
		const float tx = fx - float(x0);
		const float ty = fy - float(y0);
		const auto texel = [&](int x, int y)
		{
			x = std::clamp(x, 0, int(AtlasWidth) - 1);
			y = std::clamp(y, 0, int(AtlasHeight) - 1);
			const auto& t = atlas[std::size_t(y) * AtlasWidth + std::size_t(x)];
			return Scene::Float4{ t[0] / 255.0f, t[1] / 255.0f, t[2] / 255.0f, t[3] / 255.0f };
		};
		const auto a = texel(x0, y0), b = texel(x0 + 1, y0), c = texel(x0, y0 + 1), d = texel(x0 + 1, y0 + 1);
		Scene::Float4 result{};
		for (int k = 0; k < 4; ++k)
		{
			result[k] = (a[k] * (1 - tx) + b[k] * tx) * (1 - ty) + (c[k] * (1 - tx) + d[k] * tx) * ty;
		}
		return result;
	}

	// SwimParticleRender with the bindless space, and one pipeline per blend mode.
	struct RenderProgram
	{
		std::unique_ptr<Swim::Rhi::ShaderProgram> Program;
		std::unique_ptr<Swim::Rhi::PipelineLayout> Layout;
		std::unique_ptr<Swim::Rhi::GraphicsPipeline> Additive;
		std::unique_ptr<Swim::Rhi::GraphicsPipeline> AlphaBlend;
	};

	RenderProgram LoadRender(Swim::Rhi::Device& device, const Swim::Rhi::DescriptorSchemaDesc& bindlessSpace)
	{
		using namespace Swim;
		const auto reflected = Smoke::ReflectProgram(SWIM_PARTICLE_RENDER_REFLECTION_PATH);
		const auto bytes = Smoke::ReadSpirv(SWIM_PARTICLE_RENDER_SPIRV_PATH);
		const auto& draw = reflected.Interface;
		const std::array<Rhi::ShaderStageArtifact, 2> stages{ { { Rhi::ShaderStageMask::Vertex, "vertexMain", bytes },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", bytes } } };
		RenderProgram program;
		program.Program = device.CreateShaderProgram({ stages, { draw.DescriptorSchemas, draw.PushConstants }, "Particle render" });
		SWIM_REQUIRE(program.Program);
		program.Layout = device.CreatePipelineLayout({ program.Program.get(), "Particle render", { &bindlessSpace, 1 } });
		SWIM_REQUIRE(program.Layout);
		program.Additive = device.CreateGraphicsPipeline(
			Render::ParticleSystem::PipelineDesc(Render::ParticleBlendMode::Additive, *program.Program, *program.Layout));
		program.AlphaBlend = device.CreateGraphicsPipeline(
			Render::ParticleSystem::PipelineDesc(Render::ParticleBlendMode::AlphaBlend, *program.Program, *program.Layout));
		SWIM_REQUIRE(program.Additive && program.AlphaBlend);
		return program;
	}
#endif

	// Critical-path item 77 on a real device. Three emitters (a bouncing world-space
	// fountain, a moving local-space alpha-blended smoke and textured flipbook bursts)
	// run for many frames. Every frame the whole pool, the counters, the draw lists and
	// the indirect arguments are read back and compared with Particles:: stepped from the
	// GPU's own previous state; the last frame is drawn and compared pixel by pixel with
	// the CPU rasterizer. A large emitter then times the passes.
	void RunParticleSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_PARTICLE_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "Particle smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Particle smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		SWIM_REQUIRE_MESSAGE(
			graphics->GetAdapter(0).GetInfo().Capabilities.BindlessDescriptors, "Adapter lacks bindless descriptor support");
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		const auto simulate =
			Smoke::MakeCompute(*device, SWIM_PARTICLE_SIMULATE_SPIRV_PATH, SWIM_PARTICLE_SIMULATE_REFLECTION_PATH, "Particle simulate");
		const auto emit = Smoke::MakeCompute(*device, SWIM_PARTICLE_EMIT_SPIRV_PATH, SWIM_PARTICLE_EMIT_REFLECTION_PATH, "Particle emit");
		const auto compact =
			Smoke::MakeCompute(*device, SWIM_PARTICLE_COMPACT_SPIRV_PATH, SWIM_PARTICLE_COMPACT_REFLECTION_PATH, "Particle compact");
		const auto finalize =
			Smoke::MakeCompute(*device, SWIM_PARTICLE_FINALIZE_SPIRV_PATH, SWIM_PARTICLE_FINALIZE_REFLECTION_PATH, "Particle finalize");
		const auto bindlessSpace = ForwardPlusBindlessSpace(8, 4); // The table layout shared with Forward+.
		const auto render = LoadRender(*device, bindlessSpace);
		RenderGraphExecutor executor(*device);

		// Bindless: a white fallback, the atlas and a linear clamp sampler.
		const auto atlasTexels = MakeAtlas();
		const auto makeTexture = [&](std::uint32_t width, std::uint32_t height, const char* name)
		{
			Rhi::TextureDesc desc;
			desc.Extent = { width, height, 1 };
			desc.PixelFormat = Rhi::Format::RGBA8Unorm;
			desc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			desc.DebugName = name;
			auto texture = device->CreateTexture(desc);
			SWIM_REQUIRE(texture);
			return texture;
		};
		auto white = makeTexture(1, 1, "Particle fallback");
		auto atlas = makeTexture(AtlasWidth, AtlasHeight, "Particle atlas");
		Rhi::TextureViewDesc viewDesc;
		viewDesc.PixelFormat = Rhi::Format::RGBA8Unorm;
		auto whiteView = device->CreateTextureView(*white, viewDesc);
		auto atlasView = device->CreateTextureView(*atlas, viewDesc);
		Rhi::SamplerDesc samplerDesc{};
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = Rhi::SamplerAddressMode::ClampToEdge;
		samplerDesc.MipFilter = Rhi::Filter::Nearest;
		auto sampler = device->CreateSampler(samplerDesc);
		SWIM_REQUIRE(whiteView && atlasView && sampler);
		{
			RenderGraph uploads;
			const auto whiteTexture = uploads.ImportTexture(*white, Rhi::ResourceState::Undefined);
			const auto atlasTexture = uploads.ImportTexture(*atlas, Rhi::ResourceState::Undefined);
			const std::array<std::uint8_t, 4> opaque{ 255, 255, 255, 255 };
			AddTextureUpload(uploads, "White upload", std::as_bytes(std::span(opaque)), whiteTexture, { 0, {}, {}, { 1, 1, 1 } });
			AddTextureUpload(uploads, "Atlas upload", std::as_bytes(std::span(atlasTexels)), atlasTexture,
				{ 0, {}, {}, { AtlasWidth, AtlasHeight, 1 } });
			uploads.Export(whiteTexture, Rhi::ResourceState::ShaderRead);
			uploads.Export(atlasTexture, Rhi::ResourceState::ShaderRead);
			executor.Execute(uploads.Compile());
			executor.Wait();
		}
		BindlessTableDesc bindlessDesc;
		bindlessDesc.Layout = render.Layout.get();
		bindlessDesc.Space = ParticleRenderBindings::BindlessSpace;
		bindlessDesc.FallbackTexture = whiteView.get();
		bindlessDesc.FallbackSampler = sampler.get();
		BindlessResourceTable bindless(*device, bindlessDesc);
		const auto atlasHandle = bindless.RegisterTexture(*atlasView);
		const auto samplerHandle = bindless.RegisterSampler(*sampler);

		ParticleSystemDesc systemDesc;
		systemDesc.Capacity = 65536;
		systemDesc.MaxEmitters = 8;
		systemDesc.Simulate = { simulate.Pipeline.get(), simulate.Layout.get() };
		systemDesc.Emit = { emit.Pipeline.get(), emit.Layout.get() };
		systemDesc.Compact = { compact.Pipeline.get(), compact.Layout.get() };
		systemDesc.Finalize = { finalize.Pipeline.get(), finalize.Layout.get() };
		systemDesc.DebugName = "Particles";
		ParticleSystem system(*device, systemDesc);
		const ParticleRenderProgram renderProgram{ render.Additive.get(), render.AlphaBlend.get(), render.Layout.get() };

		auto sparks = Scene::Sparks();
		sparks.TextureIndex = bindless.GetIndex(atlasHandle);
		sparks.SamplerIndex = bindless.GetIndex(samplerHandle);
		std::vector<ParticleEmitterHandle> handles{ system.CreateEmitter(Scene::Fountain(), Scene::Translation(-1.5f, 0.0f, 0.0f)),
			system.CreateEmitter(Scene::Smoke(), Scene::Translation(1.5f, 1.0f, 0.0f)),
			system.CreateEmitter(sparks, Scene::Translation(0.0f, 0.5f, 1.0f)) };
		std::map<std::uint32_t, P::ReferenceEmitter> mirrors;
		for (const auto handle : handles)
		{
			mirrors.emplace(handle.Index, P::ReferenceEmitter(system.GetRange(handle)->second));
		}

		constexpr std::uint32_t width = 256;
		constexpr std::uint32_t height = 144;
		const auto view = Scene::View(float(width) / float(height), { 0.0f, 2.5f, 7.0f }, { 0.0f, 1.2f, 0.0f });
		const float dt = 1.0f / 30.0f;
		constexpr int Frames = 30;
		// The scene depth plane: reverse-Z (near / view depth) of a surface 7 m along the
		// view axis, through the emitters, so the particles behind it are hidden.
		const float sceneDepth = 0.1f / 7.0f;
		Rhi::TextureDesc colorDesc;
		colorDesc.Extent = { width, height, 1 };
		colorDesc.PixelFormat = Rhi::Format::RGBA16Float;
		colorDesc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::Sampled;
		colorDesc.DebugName = "Particle color";
		Rhi::TextureDesc depthDesc = colorDesc;
		depthDesc.PixelFormat = Rhi::Format::D32Float;
		depthDesc.Usage = Rhi::TextureUsage::DepthStencilAttachment;
		depthDesc.DebugName = "Particle depth";
		const Scene::Float4 clearColor{ 0.02f, 0.03f, 0.05f, 1.0f };

		std::vector<GpuParticle> pool(system.GetCapacity());
		std::uint32_t totalIdMismatches = 0, totalFieldOutliers = 0, totalCompared = 0, sortInversions = 0, sortedChecked = 0;
		std::uint32_t pixelOutliers = 0, pixelsCompared = 0, pixelsLit = 0, hiddenBehindScene = 0;
		float worstField = 0.0f;
		for (int frameIndex = 0; frameIndex < Frames; ++frameIndex)
		{
			// The smoke's emitter drifts: local-space particles follow it.
			system.SetTransform(handles[1], Scene::Translation(1.5f - 0.05f * float(frameIndex), 1.0f, 0.0f));
			const bool drawFrame = frameIndex == Frames - 1;
			RenderGraph graph;
			const auto resources = system.Simulate(graph, view, dt);
			SWIM_REQUIRE_EQUAL(resources.Emitters.size(), handles.size());
			const auto poolReadback =
				AddBufferReadback(graph, "Pool", *resources.Particles, 0, std::uint64_t(pool.size()) * sizeof(GpuParticle));
			const auto countersReadback = AddBufferReadback(graph, "Counters", *resources.Counters, 0, 8 * sizeof(GpuParticleCounters));
			const auto drawListReadback = AddBufferReadback(graph, "Draw list", *resources.DrawList, 0, std::uint64_t(pool.size()) * 4);
			const auto argsReadback = AddBufferReadback(graph, "Draw arguments", *resources.DrawArgs, 0, 8 * 20);
			std::optional<GraphReadback> colorReadback;
			if (drawFrame)
			{
				const auto color = graph.CreateTexture(colorDesc);
				const auto depth = graph.CreateTexture(depthDesc);
				graph.AddPass(
					"Particle target clear", Rhi::QueueType::Graphics,
					[&](RenderGraphBuilder& b)
					{
						b.Write(color, Rhi::ResourceState::ColorAttachment);
						b.Write(depth, Rhi::ResourceState::DepthStencilWrite);
					},
					[color, depth, clearColor, sceneDepth](RenderCommandContext& c)
					{
						std::array<Rhi::RenderingAttachmentDesc, 1> colors{};
						colors[0].View = &c.CreateView(color);
						colors[0].Load = Rhi::LoadOp::Clear;
						colors[0].Clear.Value = clearColor;
						Rhi::TextureViewDesc depthView;
						depthView.PixelFormat = Rhi::Format::D32Float;
						const Rhi::DepthStencilAttachmentDesc depthAttachment{ &c.CreateView(depth, depthView), Rhi::LoadOp::Clear,
							Rhi::StoreOp::Store, sceneDepth, 0 };
						c.Commands().BeginRendering({ colors, &depthAttachment, { width, height } });
						c.Commands().EndRendering();
					});
				SWIM_REQUIRE(system.Draw(graph, resources, renderProgram, { color, depth }, bindless.GetTable()).has_value());
				colorReadback = AddTextureReadback(graph, "Particle color", color, { 0, {}, {}, { width, height, 1 } });
			}
			executor.Execute(graph.Compile());
			system.CommitFrame();
			const auto timings = executor.ReadTimings();
			const auto read = [&](const GraphReadback& readback, auto& target)
			{
				SWIM_REQUIRE(
					executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(target))) == Rhi::ReadbackStatus::Ready);
			};
			std::array<GpuParticleCounters, 8> counters{};
			std::vector<std::uint32_t> drawList(pool.size());
			std::array<std::uint32_t, 40> args{};
			read(poolReadback, pool);
			read(countersReadback, counters);
			read(drawListReadback, drawList);
			read(argsReadback, args);

			std::vector<Scene::DrawBatch> batches;
			std::uint32_t frameMismatches = 0, frameOutliers = 0, frameCompared = 0;
			for (const auto& entry : resources.Emitters)
			{
				const auto& record = entry.Record;
				auto& mirror = mirrors.at(record.Row);
				mirror.Step(record, dt);
				std::map<std::uint32_t, GpuParticle> gpu;
				for (std::uint32_t slot = record.FirstSlot; slot < record.FirstSlot + record.Capacity; ++slot)
				{
					if (pool[slot].Lifetime > 0.0f)
					{
						gpu[pool[slot].Id] = pool[slot];
					}
				}
				// 1. The live particles, matched by id (slot assignment is concurrent).
				std::uint32_t matched = 0;
				for (const auto& expected : mirror.Particles())
				{
					const auto found = gpu.find(expected.Id);
					if (found == gpu.end())
					{
						++frameMismatches;
						continue;
					}
					++matched;
					const auto& actual = found->second;
					bool outlier = false;
					const auto compare = [&](float a, float e, float relative, float absolute)
					{
						worstField = std::max(worstField, std::abs(a - e) / (std::abs(e) + 1.0f));
						outlier = outlier || !Close(a, e, relative, absolute);
					};
					for (int c = 0; c < 3; ++c)
					{
						compare(actual.Position[c], expected.Position[c], 1.0e-4f, 1.0e-4f);
						compare(actual.Velocity[c], expected.Velocity[c], 1.0e-4f, 1.0e-4f);
					}
					compare(actual.Age, expected.Age, 1.0e-5f, 1.0e-6f);
					compare(actual.Lifetime, expected.Lifetime, 1.0e-5f, 1.0e-6f);
					compare(actual.Size, expected.Size, 1.0e-5f, 1.0e-6f);
					compare(actual.Rotation, expected.Rotation, 1.0e-4f, 1.0e-4f);
					frameOutliers += outlier ? 1u : 0u;
				}
				frameMismatches += std::uint32_t(gpu.size()) - matched;
				frameCompared += std::uint32_t(mirror.Particles().size());
				// 2. Counters, draw list and indirect arguments.
				const auto& counter = counters[record.Row];
				SWIM_CHECK_EQUAL(counter.Alive, std::uint32_t(gpu.size()));
				SWIM_CHECK_EQUAL(counter.Free + counter.Alive, record.Capacity);
				SWIM_CHECK_EQUAL(counter.Dropped, 0u);
				SWIM_CHECK(args[record.Row * 5] == 6u && args[record.Row * 5 + 1] == counter.Alive && args[record.Row * 5 + 2] == 0u);
				std::vector<GpuParticle> drawn;
				for (std::uint32_t i = 0; i < counter.Alive; ++i)
				{
					const auto slot = drawList[record.FirstSlot + i];
					SWIM_REQUIRE(slot >= record.FirstSlot && slot < record.FirstSlot + record.Capacity);
					SWIM_CHECK(pool[slot].Lifetime > 0.0f);
					drawn.push_back(pool[slot]);
				}
				std::vector<std::uint32_t> ids;
				for (const auto& p : drawn)
				{
					ids.push_back(p.Id);
				}
				std::sort(ids.begin(), ids.end());
				SWIM_CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end()); // Each live slot once.
				// 3. Blended emitters draw back to front (depths from the GPU's own positions).
				if ((record.Flags & ParticleFlagSorted) != 0)
				{
					for (std::size_t i = 1; i < drawn.size(); ++i)
					{
						const float before = P::ViewDepth(resources.FrameRecord, record, drawn[i - 1]);
						const float after = P::ViewDepth(resources.FrameRecord, record, drawn[i]);
						++sortedChecked;
						sortInversions +=
							P::DrawsBefore(after, drawn[i].Id, before, drawn[i - 1].Id) && std::abs(after - before) > 1.0e-5f ? 1u : 0u;
					}
				}
				// Continue from the GPU's state, so every frame compares one step.
				std::vector<GpuParticle> live;
				for (const auto& [id, particle] : gpu)
				{
					live.push_back(particle);
				}
				mirror.Assign(std::move(live));
				batches.push_back({ &entry.Record, entry.Blend, std::move(drawn) });
			}
			totalIdMismatches += frameMismatches;
			totalFieldOutliers += frameOutliers;
			totalCompared += frameCompared;
			if (frameIndex % 10 == 9)
			{
				std::printf("             [particles frame %d] %u particles: %u id mismatches, %u field outliers\n", frameIndex,
					frameCompared, frameMismatches, frameOutliers);
			}

			// 4. The drawn frame against the CPU rasterizer over the GPU's particles, in the
			// system's draw order (additive, then blended back to front by emitter origin).
			if (drawFrame)
			{
				std::vector<std::uint16_t> halves(std::size_t(width) * height * 4);
				read(*colorReadback, halves);
				std::vector<std::size_t> order(batches.size());
				for (std::size_t i = 0; i < order.size(); ++i)
				{
					order[i] = i;
				}
				std::stable_sort(order.begin(), order.end(),
					[&](std::size_t a, std::size_t b)
					{
						const auto& ea = resources.Emitters[a];
						const auto& eb = resources.Emitters[b];
						if (ea.Blend != eb.Blend)
						{
							return ea.Blend == ParticleBlendMode::Additive;
						}
						return ea.Blend == ParticleBlendMode::AlphaBlend && ea.OriginDepth > eb.OriginDepth;
					});
				std::vector<Scene::DrawBatch> ordered;
				for (const auto index : order)
				{
					ordered.push_back(batches[index]);
				}
				Scene::Image expected{ width, height, std::vector<Scene::Float4>(std::size_t(width) * height, clearColor), {} };
				std::vector<float> depthPlane(expected.Texels.size(), sceneDepth);
				Scene::Rasterize(expected, depthPlane, resources.FrameRecord, ordered,
					[&](const GpuParticleEmitter&, const Scene::Float2& uv)
					{
						return SampleAtlas(atlasTexels, uv);
					});
				Scene::Image unoccluded{ width, height, std::vector<Scene::Float4>(std::size_t(width) * height, clearColor), {} };
				std::vector<float> noDepth(expected.Texels.size(), 0.0f);
				Scene::Rasterize(unoccluded, noDepth, resources.FrameRecord, ordered,
					[&](const GpuParticleEmitter&, const Scene::Float2& uv)
					{
						return SampleAtlas(atlasTexels, uv);
					});
				for (std::size_t i = 0; i < expected.Texels.size(); ++i)
				{
					if (expected.Ambiguous[i] || unoccluded.Ambiguous[i])
					{
						continue;
					}
					++pixelsCompared;
					bool outlier = false;
					bool lit = false;
					for (int c = 0; c < 4; ++c)
					{
						const float actual = Smoke::HalfToFloat(halves[i * 4 + c]);
						outlier = outlier || !Close(actual, expected.Texels[i][c], 0.01f, 0.01f);
						lit = lit || std::abs(expected.Texels[i][c] - clearColor[c]) > 0.01f;
					}
					pixelsLit += lit ? 1u : 0u;
					bool hidden = false;
					for (int c = 0; c < 4; ++c)
					{
						hidden = hidden || std::abs(unoccluded.Texels[i][c] - expected.Texels[i][c]) > 0.01f;
					}
					hiddenBehindScene += hidden ? 1u : 0u;
					pixelOutliers += outlier ? 1u : 0u;
				}
				std::printf(
					"             [particles draw] %u pixels compared (%u lit, %u with particles behind the scene depth), %u outliers\n",
					pixelsCompared, pixelsLit, hiddenBehindScene, pixelOutliers);
				SWIM_CHECK(pixelsLit > pixelsCompared / 50);
				SWIM_CHECK(hiddenBehindScene > 0u);
				SWIM_CHECK(pixelOutliers <= pixelsCompared / 200);
				std::printf(
					"             [particles] GPU: simulate %.3f ms, emit %.3f ms, compact %.3f ms, finalize %.3f ms, draw %.3f ms\n",
					PassMilliseconds(timings, "Particles simulate"), PassMilliseconds(timings, "Particles emit"),
					PassMilliseconds(timings, "Particles compact"), PassMilliseconds(timings, "Particles finalize"),
					PassMilliseconds(timings, "Particles draw"));
			}
		}
		std::printf(
			"             [particles] %d frames, %u particle-steps: %u id mismatches, %u field outliers (worst %.2e); %u sorted pairs, "
			"%u inversions\n",
			Frames, totalCompared, totalIdMismatches, totalFieldOutliers, double(worstField), sortedChecked, sortInversions);
		SWIM_CHECK(totalCompared > 3000u);
		SWIM_CHECK(totalIdMismatches <= totalCompared / 1000);
		SWIM_CHECK(totalFieldOutliers <= totalCompared / 200);
		SWIM_CHECK(sortedChecked > 500u && sortInversions == 0u);

		// Timing: 60,000 additive particles.
		executor.Wait();
		for (const auto handle : handles)
		{
			system.Release(handle); // Nothing in flight after the wait.
		}
		system.Collect();
		auto mist = Scene::Fountain();
		mist.Capacity = 60000;
		mist.Rate = 200000.0f;
		mist.Collision = false;
		mist.LifetimeMin = 0.2f;
		mist.LifetimeMax = 0.3f;
		system.CreateEmitter(mist);
		for (int frameIndex = 0; frameIndex < 6; ++frameIndex)
		{
			RenderGraph graph;
			const auto resources = system.Simulate(graph, view, dt);
			const auto color = graph.CreateTexture(colorDesc);
			const auto depth = graph.CreateTexture(depthDesc);
			graph.AddPass(
				"Particle target clear", Rhi::QueueType::Graphics,
				[&](RenderGraphBuilder& b)
				{
					b.Write(color, Rhi::ResourceState::ColorAttachment);
					b.Write(depth, Rhi::ResourceState::DepthStencilWrite);
				},
				[color, depth](RenderCommandContext& c)
				{
					std::array<Rhi::RenderingAttachmentDesc, 1> colors{};
					colors[0].View = &c.CreateView(color);
					colors[0].Load = Rhi::LoadOp::Clear;
					Rhi::TextureViewDesc depthView;
					depthView.PixelFormat = Rhi::Format::D32Float;
					const Rhi::DepthStencilAttachmentDesc depthAttachment{ &c.CreateView(depth, depthView), Rhi::LoadOp::Clear,
						Rhi::StoreOp::Store, 0.0f, 0 };
					c.Commands().BeginRendering({ colors, &depthAttachment, { width, height } });
					c.Commands().EndRendering();
				});
			system.Draw(graph, resources, renderProgram, { color, depth }, bindless.GetTable());
			graph.Export(color, Rhi::ResourceState::ShaderRead);
			executor.Execute(graph.Compile());
			system.CommitFrame();
			executor.Wait();
			if (frameIndex == 5)
			{
				const auto timings = executor.ReadTimings();
				std::printf(
					"             [particles 60k] GPU: simulate %.3f ms, emit %.3f ms, compact %.3f ms, finalize %.3f ms, draw %.3f ms\n",
					PassMilliseconds(timings, "Particles simulate"), PassMilliseconds(timings, "Particles emit"),
					PassMilliseconds(timings, "Particles compact"), PassMilliseconds(timings, "Particles finalize"),
					PassMilliseconds(timings, "Particles draw"));
			}
		}
		executor.Wait();
		system.Drain();
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "GpuParticlesMatchTheCpuReference", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunParticleSmoke);
				} });
		}
		return true;
	}();
} // namespace
