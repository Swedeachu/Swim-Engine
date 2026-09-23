#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibility.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityReference.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#if defined(SWIM_GPU_VISIBILITY_SPIRV_PATH) && defined(SWIM_RHI_GPU_DRIVEN_DRAW_SPIRV_PATH)
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#define SWIM_GPU_VISIBILITY_SMOKE_AVAILABLE 1
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string_view>
#include <tuple>
#include <vector>

namespace
{
#ifdef SWIM_GPU_VISIBILITY_SMOKE_AVAILABLE
	std::vector<std::byte> ReadBytes(const char* path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		SWIM_REQUIRE(file && !bytes.empty());
		return bytes;
	}

	Swim::ShaderCompiler::ShaderRhiInterfaceResult LoadInterface(const char* path)
	{
		const auto reflection = Swim::ShaderCompiler::LoadSlangReflectionJson(path);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		auto converted = Swim::ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		return converted;
	}
#endif

	// Critical-path items 49 and 52-55 end to end on a real device, with no CPU
	// visibility feedback: a 64x64 grid of GPU Scene objects (a two-LOD quad in a
	// GeometryHeap) is culled, LOD-selected, binned and compacted by
	// GpuVisibility.slang; an orthographic view then draws every bin with
	// DrawIndexedIndirectCount using vertex pulling. Over three frames (camera
	// move + scene edits, then a camera cut) the commands, draw records, counts
	// and statistics match RunVisibilityReference, and every drawn object's
	// pixels carry its id while nothing culled, hidden or destroyed appears.
	void RunGpuVisibilitySmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_GPU_VISIBILITY_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "GPU visibility smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;
		using S = Rhi::ResourceState;

		const auto cullInterface = LoadInterface(SWIM_GPU_VISIBILITY_REFLECTION_PATH);
		const auto drawInterface = LoadInterface(SWIM_RHI_GPU_DRIVEN_DRAW_REFLECTION_PATH);
		const auto cullBytes = ReadBytes(SWIM_GPU_VISIBILITY_SPIRV_PATH);
		const auto drawBytes = ReadBytes(SWIM_RHI_GPU_DRIVEN_DRAW_SPIRV_PATH);

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "GPU visibility smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		SWIM_REQUIRE(graphics->GetAdapter(0).GetInfo().Capabilities.IndirectCount);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		// Culling program.
		const auto& cull = cullInterface.Interface;
		const Rhi::ShaderStageArtifact cullStage{ Rhi::ShaderStageMask::Compute, "computeMain", cullBytes };
		auto cullProgram = device->CreateShaderProgram(
			{ { &cullStage, 1 }, { cull.DescriptorSchemas, cull.PushConstants, cull.ComputeThreadGroupSize }, "GPU visibility" });
		SWIM_REQUIRE(cullProgram);
		auto cullLayout = device->CreatePipelineLayout({ cullProgram.get(), "GPU visibility layout" });
		SWIM_REQUIRE(cullLayout);
		auto cullPipeline = device->CreateComputePipeline({ cullProgram.get(), cullLayout.get(), {}, "GPU visibility" });
		SWIM_REQUIRE(cullPipeline);

		// Draw program (vertex pulling, object id output).
		const auto& draw = drawInterface.Interface;
		const std::array<Rhi::ShaderStageArtifact, 2> drawStages{ { { Rhi::ShaderStageMask::Vertex, "vertexMain", drawBytes },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", drawBytes } } };
		auto drawProgram = device->CreateShaderProgram({ drawStages, { draw.DescriptorSchemas, draw.PushConstants }, "GPU-driven draw" });
		SWIM_REQUIRE(drawProgram);
		auto drawLayout = device->CreatePipelineLayout({ drawProgram.get(), "GPU-driven draw layout" });
		SWIM_REQUIRE(drawLayout);
		const Rhi::Format format = Rhi::Format::RGBA8Unorm;
		Rhi::GraphicsPipelineDesc drawDesc{};
		drawDesc.Program = drawProgram.get();
		drawDesc.Layout = drawLayout.get();
		drawDesc.ColorFormats = { &format, 1 };
		drawDesc.DepthStencil.DepthTest = drawDesc.DepthStencil.DepthWrite = false;
		drawDesc.Raster.Cull = Rhi::CullMode::None;
		drawDesc.DebugName = "GPU-driven draw";
		auto drawPipeline = device->CreateGraphicsPipeline(drawDesc);
		SWIM_REQUIRE(drawPipeline);
		const auto drawSpace = draw.DescriptorSchemas[0].Space;

		RenderGraphExecutor executor(*device);
		{
			// A quad (LOD0: two triangles) and its coarse LOD1 (one triangle); both cover
			// the sample point at local (-0.5, -0.5).
			GeometryHeapDesc heapDesc;
			heapDesc.VertexPageSize = 64 * 1024;
			heapDesc.IndexPageSize = 64 * 1024;
			heapDesc.MeshletPageSize = 4096;
			heapDesc.MaxMeshes = 8;
			heapDesc.MaxSubmeshes = 16;
			heapDesc.MaxPages = 8;
			GeometryHeap heap(*device, heapDesc);
			const std::array<float, 12> positions{ -1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0 };
			const std::array<std::uint32_t, 9> indices{ 0, 1, 2, 0, 2, 3, 0, 1, 3 };
			const std::array<GeometrySubmesh, 2> submeshes{ { { 0, 6, 0, 0 }, { 6, 3, 0, 0 } } };
			const std::array<GeometryLodRange, 2> lods{ { { 0, 1, 0.0f }, { 1, 1, 1.0f } } };
			GeometryMeshDesc meshDesc;
			meshDesc.VertexStride = 12;
			meshDesc.Vertices = std::as_bytes(std::span(positions));
			meshDesc.IndexFormat = Rhi::IndexType::Uint32;
			meshDesc.Indices = std::as_bytes(std::span(indices));
			meshDesc.Submeshes = submeshes;
			meshDesc.Lods = lods;
			meshDesc.DebugName = "Visibility quad";
			const auto quad = heap.CreateMesh(meshDesc);
			const auto& quadMeta = *heap.GetMetadata(quad);

			constexpr std::uint32_t side = 64;
			GpuScene scene(*device, { side * side, "Visibility scene" });
			std::vector<RenderObjectHandle> objects;
			for (std::uint32_t j = 0; j < side; ++j)
			{
				for (std::uint32_t i = 0; i < side; ++i)
				{
					RenderObjectDesc desc;
					desc.Transform = RenderAffine::Translation(float(i) * 2.5f, float(j) * 2.5f, 0.0f);
					desc.Mesh = quad;
					desc.LocalBounds = RenderBounds::FromMinMax({ -1, -1, 0 }, { 1, 1, 0 });
					desc.MaterialSet = (i + j) % 13 == 0 ? 1u : 0u;
					desc.ObjectId = j * side + i;
					objects.push_back(scene.Create(desc));
				}
			}

			GpuVisibilityDesc visibilityDesc;
			visibilityDesc.CullPipeline = cullPipeline.get();
			visibilityDesc.Layout = cullLayout.get();
			visibilityDesc.Space = cull.DescriptorSchemas[0].Space;
			visibilityDesc.MaxObjects = side * side;
			visibilityDesc.MaxMaterialSets = 4;
			visibilityDesc.MaterialBinCapacities = { 4096, 8 }; // Material bin 1 overflows on purpose.
			visibilityDesc.IndexPageSlots = 1;
			GpuVisibility visibility(*device, visibilityDesc);
			visibility.SetMaterialBin(1, 1);
			const auto& bins = visibility.GetBins();
			const VisibilityBinLayout roomy(std::vector<std::uint32_t>{ 4096, 4096 }, 1); // Reference candidates without drops.
			const std::vector<std::uint32_t> materialBins{ 0, 1, 0, 0 };
			const std::vector<std::uint32_t> pages{ quadMeta.IndexPage };
			std::vector<GpuLodState> referenceLods;

			constexpr std::uint32_t size = 256;
			const auto frame = [&](float cameraX, float cameraY, std::uint32_t flags)
			{
				RenderViewDesc viewDesc;
				const std::array<float, 16> view{ 1, 0, 0, -cameraX, 0, 1, 0, -cameraY, 0, 0, 1, -10, 0, 0, 0, 1 };
				viewDesc.ViewProjection = MultiplyRowMajor(OrthographicRowMajor(-20, 20, -20, 20, 0.1f, 100.0f), view);
				viewDesc.CameraPosition = { cameraX, cameraY, 10.0f };
				viewDesc.LodScale = 18.0f;
				viewDesc.LodPixelError = 1.0f;
				viewDesc.LodHysteresis = 0.25f;
				viewDesc.Flags = flags;
				viewDesc.Depth = DepthConvention::Forward; // OrthographicRowMajor; no depth buffer here.
				VisibilityFrameDesc frameDesc;
				frameDesc.View = BuildGpuViewRecord(viewDesc);
				frameDesc.IndexPages = pages;

				RenderGraph graph;
				const auto sceneResources = scene.Import(graph);
				const auto geometry = heap.Import(graph);
				const auto visible = visibility.Record(graph, sceneResources, geometry, frameDesc);
				const auto viewUpload =
					graph.CreateUpload(std::as_bytes(std::span(&frameDesc.View, 1)), "Draw view", Rhi::BufferUsage::Storage, 16);
				const auto target = graph.CreateTexture({ Rhi::TextureDimension::Texture2D, { size, size, 1 }, Rhi::Format::RGBA8Unorm,
					Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource, 1, 1, Rhi::SampleCount::X1,
					"Visibility target" });
				const auto vertexPage = geometry.Pages[quadMeta.VertexPage];
				const auto indexPage = geometry.Pages[quadMeta.IndexPage];
				graph.AddPass(
					"GPU-driven draw", Rhi::QueueType::Graphics,
					[&](RenderGraphBuilder& b)
					{
						b.Read(visible.Commands, S::IndirectArgument);
						b.Read(visible.Counts, S::IndirectArgument);
						b.Read(visible.DrawRecords, S::ShaderRead);
						b.Read(sceneResources.Instances, S::ShaderRead);
						b.Read(sceneResources.Transforms, S::ShaderRead);
						b.Read(vertexPage, S::ShaderRead);
						b.Read(indexPage, S::IndexBuffer);
						b.Read(viewUpload, S::ShaderRead);
						b.Write(target, S::ColorAttachment);
					},
					[&](RenderCommandContext& c)
					{
						auto table = c.Device().CreateDescriptorTable({ drawLayout.get(), drawSpace, 0, "GPU-driven draw table" });
						SWIM_REQUIRE(table);
						std::array<Rhi::DescriptorWrite, 5> writes{};
						for (std::uint32_t binding = 0; binding < writes.size(); ++binding)
						{
							writes[binding].Binding = binding;
						}
						writes[0].BufferResource = &c.Get(sceneResources.Instances);
						writes[1].BufferResource = &c.Get(sceneResources.Transforms);
						writes[2].BufferResource = &c.Get(visible.DrawRecords);
						writes[3].BufferResource = &c.Get(vertexPage);
						const auto viewRange = c.GetRange(viewUpload);
						writes[4].BufferResource = viewRange.Buffer;
						writes[4].BufferOffset = viewRange.Offset;
						writes[4].BufferRange = viewRange.Size;
						table->Write(writes);
						auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
						Rhi::RenderingAttachmentDesc attachment{};
						attachment.View = &c.CreateView(target);
						attachment.Load = Rhi::LoadOp::Clear;
						auto& commands = c.Commands();
						commands.BeginRendering({ { &attachment, 1 }, nullptr, { size, size } });
						commands.BindGraphicsPipeline(*drawPipeline);
						commands.BindDescriptorTable(drawSpace, retained);
						commands.SetViewport({ 0, 0, float(size), float(size) });
						commands.SetScissor({ 0, 0, size, size });
						commands.BindIndexBuffer(c.Get(indexPage), 0, Rhi::IndexType::Uint32);
						for (std::uint32_t bin = 0; bin < bins.GetBinCount(); ++bin)
						{
							const auto& range = bins.GetRange(bin);
							commands.DrawIndexedIndirectCount(c.Get(visible.Commands),
								std::uint64_t(range.First) * sizeof(Rhi::DrawIndexedIndirectCommand), c.Get(visible.Counts),
								std::uint64_t(bin) * sizeof(std::uint32_t), range.Capacity);
						}
						commands.EndRendering();
					});
				const auto capacity = bins.GetTotalCapacity();
				const auto commandReadback = AddBufferReadback(
					graph, "Commands readback", visible.Commands, 0, std::uint64_t(capacity) * sizeof(Rhi::DrawIndexedIndirectCommand));
				const auto recordReadback =
					AddBufferReadback(graph, "Records readback", visible.DrawRecords, 0, std::uint64_t(capacity) * sizeof(GpuDrawRecord));
				const auto countReadback =
					AddBufferReadback(graph, "Counts readback", visible.Counts, 0, bins.GetBinCount() * sizeof(std::uint32_t));
				const auto imageReadback = AddTextureReadback(graph, "Image readback", target, { 0, {}, {}, { size, size, 1 } });
				const auto completion = executor.Execute(graph.Compile());
				scene.CommitUploads();
				heap.CommitUploads(completion);
				executor.Wait();

				std::vector<Rhi::DrawIndexedIndirectCommand> commands(capacity);
				std::vector<GpuDrawRecord> records(capacity);
				std::vector<std::uint32_t> counts(bins.GetBinCount());
				std::vector<std::uint8_t> pixels(std::size_t(size) * size * 4);
				VisibilityStats stats;
				SWIM_REQUIRE(executor.TryReadback(commandReadback.Buffer, std::as_writable_bytes(std::span(commands))) ==
					Rhi::ReadbackStatus::Ready);
				SWIM_REQUIRE(
					executor.TryReadback(recordReadback.Buffer, std::as_writable_bytes(std::span(records))) == Rhi::ReadbackStatus::Ready);
				SWIM_REQUIRE(
					executor.TryReadback(countReadback.Buffer, std::as_writable_bytes(std::span(counts))) == Rhi::ReadbackStatus::Ready);
				SWIM_REQUIRE(
					executor.TryReadback(imageReadback.Buffer, std::as_writable_bytes(std::span(pixels))) == Rhi::ReadbackStatus::Ready);
				SWIM_REQUIRE(visible.StatsReadback.has_value());
				SWIM_REQUIRE(executor.TryReadback(visible.StatsReadback->Buffer, std::as_writable_bytes(std::span(&stats, 1))) ==
					Rhi::ReadbackStatus::Ready);

				// The CPU definition over the same rows. The candidate run (no capacity
				// limit) uses a copy of the LOD history so both see the same history.
				std::vector<GpuInstanceRecord> instances;
				std::vector<GpuTransformRecord> transforms;
				for (std::uint32_t row = 0; row < sceneResources.RowCount; ++row)
				{
					instances.push_back(scene.GetInstanceRow(row));
					transforms.push_back(scene.GetTransformRow(row));
				}
				std::vector<GpuMeshMetadata> meshes(quad.Index + 1);
				meshes[quad.Index] = quadMeta;
				const auto quadSubmeshes = heap.GetSubmeshes(quad);
				std::vector<GpuSubmeshRecord> submeshRows(quadMeta.FirstSubmesh + quadSubmeshes.size());
				std::copy(quadSubmeshes.begin(), quadSubmeshes.end(), submeshRows.begin() + quadMeta.FirstSubmesh);
				auto candidateLods = referenceLods;
				const VisibilityReferenceInputs inputs{ instances, transforms, meshes, submeshRows, frameDesc.View, materialBins, pages,
					&bins };
				const auto expected = RunVisibilityReference(inputs, referenceLods);
				auto roomyInputs = inputs;
				roomyInputs.Bins = &roomy;
				const auto candidates = RunVisibilityReference(roomyInputs, candidateLods);

				SWIM_CHECK(std::memcmp(&stats, &expected.Stats, sizeof(stats)) == 0);
				SWIM_CHECK_EQUAL(stats.Tested, expected.Stats.Tested);
				SWIM_CHECK_EQUAL(stats.FrustumCulled, expected.Stats.FrustumCulled);
				SWIM_CHECK_EQUAL(stats.Draws, expected.Stats.Draws);
				SWIM_CHECK_EQUAL(stats.Dropped, expected.Stats.Dropped);
				SWIM_CHECK(stats.Dropped > 0);
				SWIM_CHECK(stats.LodCounts[0] > 0 && stats.LodCounts[1] > 0);

				std::map<std::uint32_t, std::uint32_t> drawn; // Object id -> instance row.
				for (std::uint32_t bin = 0; bin < bins.GetBinCount(); ++bin)
				{
					const auto& range = bins.GetRange(bin);
					const auto written = std::min(counts[bin], range.Capacity);
					SWIM_CHECK_EQUAL(counts[bin], std::uint32_t(candidates.Bins[bin].size())); // Attempts, before clamping.
					SWIM_CHECK_EQUAL(written, std::uint32_t(expected.Bins[bin].size()));
					using Key = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t>;
					std::set<Key> allowed;
					for (const auto& draw : candidates.Bins[bin])
					{
						allowed.insert({ draw.Record.InstanceRow, draw.Record.SubmeshRow, draw.Command.IndexCount, draw.Command.FirstIndex,
							draw.Command.VertexOffset });
					}
					std::set<Key> seen;
					for (std::uint32_t slot = 0; slot < written; ++slot)
					{
						const auto& command = commands[range.First + slot];
						const auto& record = records[range.First + slot];
						SWIM_CHECK_EQUAL(command.FirstInstance, range.First + slot);
						SWIM_CHECK_EQUAL(command.InstanceCount, 1u);
						const Key key{ record.InstanceRow, record.SubmeshRow, command.IndexCount, command.FirstIndex,
							command.VertexOffset };
						SWIM_CHECK(allowed.contains(key));
						SWIM_CHECK(seen.insert(key).second);
						drawn.emplace(instances[record.InstanceRow].ObjectId, record.InstanceRow);
					}
					if (range.Capacity >= candidates.Bins[bin].size())
					{
						SWIM_CHECK(seen == allowed); // Unclamped bins hold exactly the expected set.
					}
				}

				// Pixels: every drawn object shows its id at its sample point; no other id appears.
				const auto decode = [&](std::uint32_t x, std::uint32_t y)
				{
					const auto* p = pixels.data() + (std::size_t(y) * size + x) * 4;
					return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16);
				};
				std::uint32_t checkedSamples = 0;
				for (const auto& [id, row] : drawn)
				{
					const auto& transform = transforms[instances[row].TransformIndex];
					RenderAffine world;
					std::copy(std::begin(transform.Current), std::end(transform.Current), world.Rows.begin());
					const auto point = world.TransformPoint({ -0.5f, -0.5f, 0.0f });
					const auto& m = frameDesc.View.ViewProjection;
					const float cx = m[0] * point[0] + m[1] * point[1] + m[2] * point[2] + m[3];
					const float cy = m[4] * point[0] + m[5] * point[1] + m[6] * point[2] + m[7];
					const float cw = m[12] * point[0] + m[13] * point[1] + m[14] * point[2] + m[15];
					const float px = (cx / cw * 0.5f + 0.5f) * float(size);
					// RHI NDC is +Y up (the Vulkan viewport is flipped), so framebuffer row 0 is NDC y = +1.
					const float py = (0.5f - cy / cw * 0.5f) * float(size);
					if (px < 0.0f || py < 0.0f || px >= float(size) || py >= float(size))
					{
						continue;
					}
					SWIM_CHECK_EQUAL(decode(std::uint32_t(px), std::uint32_t(py)), id + 1);
					++checkedSamples;
				}
				SWIM_CHECK(checkedSamples > 100);
				std::uint32_t strays = 0;
				for (std::uint32_t y = 0; y < size; ++y)
				{
					for (std::uint32_t x = 0; x < size; ++x)
					{
						const auto value = decode(x, y);
						strays += value != 0 && !drawn.contains(value - 1);
					}
				}
				SWIM_CHECK_EQUAL(strays, 0u);
			};

			// Frame 1: fresh history.
			frame(40.3f, 40.7f, 0);

			// Frame 2: camera moves, scene edits (hidden, destroyed, moved away, re-binned).
			for (std::uint32_t i = 100; i < 103; ++i)
			{
				scene.SetFlags(objects[1040 + i], RenderObjectFlags::CastShadows);
			}
			scene.Destroy(objects[1100]);
			scene.Destroy(objects[1101]);
			scene.SetTransform(objects[1102], RenderAffine::Translation(1000.0f, 0.0f, 0.0f));
			scene.SetMaterialSet(objects[1103], 1);
			scene.SetMaterialSet(objects[1104], 1);
			frame(43.9f, 41.2f, 0);

			// Frame 3: camera cut resets LOD history.
			frame(41.1f, 39.6f, std::uint32_t(GpuViewFlags::ResetLodHistory));

			scene.Collect();
			scene.Drain();
			heap.Drain();
		}
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "GpuVisibilityCullsBinsAndDrawsIndirect", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunGpuVisibilitySmoke);
				} });
		}
		return true;
	}();

} // namespace
