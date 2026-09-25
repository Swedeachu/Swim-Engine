#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/UiRendering/UiRenderSurfaces.h"
#include "Engine/Systems/UI/UiCanvas.h"
#include "Tests/Fixtures/BindlessTableFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	class MockGraphicsPipeline final : public Rhi::GraphicsPipeline
	{
	  public:
		std::uintptr_t GetNativeHandle() const override { return 41; }
	};

	struct UiWorld
	{
		UiWorld()
		{
			bindlessFixture = std::make_unique<Testing::BindlessTableFixture>(16, 4);
			executor = std::make_unique<RenderGraphExecutor>(Device());
			table = std::make_unique<BindlessResourceTable>(Device(), bindlessFixture->Desc());
			renderLayout.program.Interface.DescriptorSchemas = {
				Rhi::DescriptorSchemaDesc{ 0,
					{ { UiRenderBindings::Quads, Rhi::DescriptorType::ReadOnlyStorageBuffer, 1,
						Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment } } },
				Testing::BindlessTableFixture::MakeSpace(16, 4)
			};
		}

		Testing::MockDevice& Device() { return bindlessFixture->device; }

		std::unique_ptr<Rhi::Texture> Texture(Rhi::Format format, std::uint32_t width, std::uint32_t height, std::uint32_t mips = 1)
		{
			Rhi::TextureDesc desc;
			desc.Extent = { width, height, 1 };
			desc.PixelFormat = format;
			desc.MipLevels = mips;
			desc.Usage = Rhi::IsDepthFormat(format) ? Rhi::TextureUsage::DepthStencilAttachment
													: Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::Sampled;
			return Device().CreateTexture(desc);
		}

		void Run(RenderGraph& graph)
		{
			Device().Commands->clear();
			executor->Execute(graph.Compile());
			executor->Wait();
		}

		std::vector<Testing::MockCommand> Commands(const std::string& kind) const
		{
			std::vector<Testing::MockCommand> result;
			for (const auto& command : *bindlessFixture->device.Commands)
			{
				if (command.Kind == kind)
				{
					result.push_back(command);
				}
			}
			return result;
		}

		UiRenderProgram Program(bool depth = true) { return { &pipeline, &renderLayout, depth ? &depthPipeline : nullptr }; }

		std::unique_ptr<Testing::BindlessTableFixture> bindlessFixture;
		std::unique_ptr<RenderGraphExecutor> executor;
		std::unique_ptr<BindlessResourceTable> table;
		Testing::MockPipelineLayout renderLayout;
		MockGraphicsPipeline pipeline;
		MockGraphicsPipeline depthPipeline;
	};

	GpuUiQuad Quad(std::uint32_t kind, float x0, float y0, float x1, float y1)
	{
		GpuUiQuad quad;
		quad.Kind = kind;
		quad.Rect[0] = quad.Clip[0] = x0;
		quad.Rect[1] = quad.Clip[1] = y0;
		quad.Rect[2] = quad.Clip[2] = x1;
		quad.Rect[3] = quad.Clip[3] = y1;
		quad.Uv[2] = quad.Uv[3] = 1.0f;
		for (int c = 0; c < 4; ++c)
		{
			quad.Color[c] = c == 3 ? 1.0f : 0.25f * float(c + 1);
		}
		return quad;
	}

	// A distance-like MSDF (median = 0.5 on a circle of radius 0.3 around the UV centre)
	// and an image gradient.
	Ui::Float4 Sample(std::uint32_t texture, std::uint32_t, float u, float v)
	{
		if (texture == 1)
		{
			const float d = 0.3f - std::sqrt((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f));
			const float m = 0.5f + d;
			return { m, m + 0.05f, m - 0.05f, 1.0f };
		}
		return { u, v, 0.5f, 1.0f };
	}

	std::vector<GpuUiQuad> Scene()
	{
		auto solid = Quad(UiQuadSolid, 3.25f, 2.5f, 40.5f, 27.75f);
		solid.Radius = 6.0f;
		solid.Border = 2.0f;
		solid.BorderColor[0] = solid.BorderColor[3] = 1.0f;
		solid.Clip[2] = 36.0f; // Clipped on the right.
		auto glyph = Quad(UiQuadGlyph, 20.0f, 4.0f, 44.0f, 28.0f);
		glyph.Texture = 1;
		glyph.Uv[0] = 0.0f;
		glyph.Uv[2] = 1.0f;
		glyph.UnitRange = 4.0f / 64.0f;
		glyph.PixelRange = std::max(1.0f, 4.0f * 24.0f / 64.0f); // DistanceRange 4 over a 64-texel page.
		auto image = Quad(UiQuadImage, 46.0f, 1.0f, 62.0f, 31.0f);
		image.Texture = 2;
		return { solid, glyph, image };
	}
} // namespace

SWIM_TEST("Render.Ui.Reference", "WorldCanvasesThroughTheScreenMappingMatchTheScreenRasterizer")
{
	UiCompositionSettings settings;
	const auto screen = Ui::BuildDrawConstants(64, 32, settings);
	SWIM_CHECK_EQUAL(screen.Flags, 0u);
	// The renderer's screen mapping is UI::ScreenClipFromCanvas.
	const auto expected = UI::ScreenClipFromCanvas(64.0f, 32.0f);
	for (int i = 0; i < 16; ++i)
	{
		SWIM_CHECK_NEAR(screen.ClipFromCanvas[i], expected[i], 1e-7f);
	}
	std::array<float, 16> matrix{};
	std::copy(std::begin(screen.ClipFromCanvas), std::end(screen.ClipFromCanvas), matrix.begin());
	const auto world = Ui::BuildCanvasDrawConstants(64, 32, settings, matrix);
	SWIM_CHECK_EQUAL(world.Flags, UiDrawWorld);
	const auto quads = Scene();
	Ui::Canvas a{ 64, 32, std::vector<Ui::Float4>(64 * 32, Ui::Float4{ 0.1f, 0.1f, 0.1f, 1.0f }) };
	Ui::Canvas b = a;
	Ui::Rasterize(a, quads, screen, Sample);
	Ui::RasterizeProjected(b, quads, world, Sample);
	float worst = 0.0f;
	for (std::size_t i = 0; i < a.Texels.size(); ++i)
	{
		for (int c = 0; c < 4; ++c)
		{
			worst = std::max(worst, std::abs(a.Texels[i][c] - b.Texels[i][c]));
		}
	}
	SWIM_CHECK(worst < 1e-4f); // Derivative footprints of a 1:1 mapping are one pixel.
	const auto sample = Ui::CanvasAt(world, 10.5f, 20.5f);
	SWIM_REQUIRE(sample.has_value());
	SWIM_CHECK_NEAR(sample->X, 10.5f, 1e-4f);
	SWIM_CHECK_NEAR(sample->FootprintX, 1.0f, 1e-4f);
}

SWIM_TEST("Render.Ui.Reference", "ProjectedCanvasesFollowPerspectiveScaleOpacityAndTheCamera")
{
	UI::UiCameraView camera;
	camera.View = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -4, 0, 0, 0, 1 };
	camera.Projection = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.1f, 0, 0, -1, 0 };
	camera.ViewportWidth = 64.0f;
	camera.ViewportHeight = 64.0f;
	UiCompositionSettings settings;
	const auto coverage = [&](float z, float opacity, float* alpha = nullptr)
	{
		UI::UiWorldPlacement placement;
		placement.UnitsPerPixel = 0.02f;
		placement.Transform = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, z };
		const auto m = UI::CanvasToWorld(UI::UiCanvasMode::WorldPanel, placement, { 50, 50 });
		const auto clip = UI::ClipFromCanvas(m, camera);
		const auto constants = Ui::BuildCanvasDrawConstants(64, 64, settings, clip, opacity);
		auto quad = Quad(UiQuadSolid, 0, 0, 50, 50);
		quad.Color[0] = quad.Color[1] = quad.Color[2] = quad.Color[3] = 1.0f;
		Ui::Canvas canvas{ 64, 64, std::vector<Ui::Float4>(64 * 64) };
		Ui::RasterizeProjected(canvas, std::span(&quad, 1), constants, Sample);
		int covered = 0;
		for (const auto& texel : canvas.Texels)
		{
			covered += texel[3] > 0.25f ? 1 : 0;
			if (alpha && texel[3] > 0.0f)
			{
				*alpha = std::max(*alpha, texel[3]);
			}
		}
		return covered;
	};
	// A 1 m panel 2 m and 4 m from the camera: a quarter of the pixels at twice the distance.
	const int nearPixels = coverage(2.0f, 1.0f);
	const int farPixels = coverage(0.0f, 1.0f);
	SWIM_CHECK_NEAR(float(nearPixels), 16.0f * 16.0f, 40.0f);
	SWIM_CHECK_NEAR(float(farPixels), 8.0f * 8.0f, 20.0f);
	float alpha = 0.0f;
	coverage(2.0f, 0.5f, &alpha);
	SWIM_CHECK_NEAR(alpha, 0.5f, 1e-5f);	   // Canvas opacity.
	SWIM_CHECK_EQUAL(coverage(6.0f, 1.0f), 0); // Behind the camera.

	// Glyph ranges follow the screen scale: a canvas pixel covering two screen pixels has a
	// footprint of one half and twice the range.
	UI::UiWorldPlacement placement;
	placement.UnitsPerPixel = 8.0f / 64.0f; // One canvas pixel = two screen pixels at 2 m.
	placement.Transform = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 2 };
	const auto clip = UI::ClipFromCanvas(UI::CanvasToWorld(UI::UiCanvasMode::WorldPanel, placement, { 32, 32 }), camera);
	const auto constants = Ui::BuildCanvasDrawConstants(64, 64, settings, clip);
	const auto sample = Ui::CanvasAt(constants, 32.5f, 32.5f);
	SWIM_REQUIRE(sample.has_value());
	SWIM_CHECK_NEAR(sample->FootprintX, 0.5f, 1e-3f);
	SWIM_CHECK_NEAR(sample->X, 16.25f, 1e-3f);
	auto glyph = Quad(UiQuadGlyph, 0, 0, 32, 32);
	glyph.UnitRange = 4.0f / 64.0f;
	glyph.Texture = 1;
	Ui::Footprint footprint{ sample->FootprintX, sample->FootprintY, sample->FootprintX / 32.0f, sample->FootprintY / 32.0f };
	// At the circle edge + 1/16 UV the msd median is 0.5 - 1/16: coverage 0.5 - range / 16.
	const auto shaded = Ui::ShadeQuad(glyph, 32.0f * (0.5f + 0.3f + 1.0f / 16.0f), 16.0f, Sample, &footprint);
	const float range = 0.5f * 2.0f * (4.0f / 64.0f) / (0.5f / 32.0f); // = 4: 2 screen px per canvas px.
	SWIM_CHECK_NEAR(shaded[3], std::clamp(0.5f - range / 16.0f, 0.0f, 1.0f), 1e-4f);

	std::array<float, 16> bad{};
	bad[0] = std::numeric_limits<float>::infinity();
	SWIM_CHECK_THROWS(Ui::BuildCanvasDrawConstants(64, 64, settings, bad), std::invalid_argument);
	SWIM_CHECK_THROWS(Ui::BuildCanvasDrawConstants(64, 64, settings, clip, 1.5f), std::invalid_argument);
	SWIM_CHECK_THROWS(Ui::BuildDrawConstants(64, 64, settings, -0.1f), std::invalid_argument);
}

SWIM_TEST("Render.Ui.Renderer", "WorldFramesPushTheCanvasMatrixTestSceneDepthAndDrawMips")
{
	UiWorld world;
	UiRenderer renderer;
	auto color = world.Texture(Rhi::Format::RGBA16Float, 64, 32, 3);
	auto depth = world.Texture(Rhi::Format::D32Float, 64, 32);
	auto smallDepth = world.Texture(Rhi::Format::D32Float, 16, 8);
	UI::UiPaintQuad paint;
	paint.Bounds = { 0, 0, 10, 10 };
	paint.Clip = { 0, 0, 100, 100 };
	paint.Color = { 1, 1, 1, 1 };
	const std::array<float, 16> matrix{ 0.1f, 0, 0, -0.5f, 0, -0.1f, 0, 0.5f, 0, 0, 0, 0.5f, 0, 0, 0, 1 };

	RenderGraph graph;
	const auto target = graph.ImportTexture(*color, Rhi::ResourceState::ColorAttachment);
	const auto sceneDepth = graph.ImportTexture(*depth, Rhi::ResourceState::DepthStencilWrite);
	graph.Export(target, Rhi::ResourceState::ColorAttachment);
	graph.Export(sceneDepth, Rhi::ResourceState::DepthStencilWrite);
	UiRenderFrame frame;
	frame.Paint = std::span(&paint, 1);
	frame.Target = target;
	frame.ClipFromCanvas = matrix;
	frame.Depth = sceneDepth;
	frame.Opacity = 0.75f;
	frame.Composition.Encoding = UiOutputEncoding::Linear;
	SWIM_REQUIRE(renderer.Record(graph, frame, world.Program(), world.table->GetTable()).has_value());
	world.Run(graph);
	const auto pushes = world.Commands("PushConstants");
	SWIM_REQUIRE_EQUAL(pushes.size(), std::size_t(1));
	GpuUiDrawConstants constants{};
	SWIM_REQUIRE_EQUAL(pushes[0].Data.size(), sizeof(constants));
	std::memcpy(&constants, pushes[0].Data.data(), sizeof(constants));
	SWIM_CHECK_EQUAL(constants.Flags, UiDrawWorld);
	SWIM_CHECK_NEAR(constants.Opacity, 0.75f, 1e-7f);
	SWIM_CHECK_NEAR(constants.ClipFromCanvas[3], -0.5f, 1e-7f);
	const auto pipelines = world.Commands("BindGraphicsPipeline");
	SWIM_REQUIRE_EQUAL(pipelines.size(), std::size_t(1));
	SWIM_CHECK(pipelines[0].Source == &world.depthPipeline);
	const auto rendering = world.Commands("BeginRendering");
	SWIM_REQUIRE_EQUAL(rendering.size(), std::size_t(1));
	SWIM_CHECK(rendering[0].Destination != nullptr); // The depth attachment.
	SWIM_CHECK(rendering[0].SourceOffset == 64u && rendering[0].DestinationOffset == 32u);

	// Mip 2 of the target: a quarter-size render area through a view of that level.
	RenderGraph mips;
	const auto mipTarget = mips.ImportTexture(*color, Rhi::ResourceState::ColorAttachment);
	mips.Export(mipTarget, Rhi::ResourceState::ColorAttachment);
	UiRenderFrame mipFrame;
	mipFrame.Paint = std::span(&paint, 1);
	mipFrame.Target = mipTarget;
	mipFrame.TargetMip = 2;
	mipFrame.Clear = true;
	SWIM_REQUIRE(renderer.Record(mips, mipFrame, world.Program(false), world.table->GetTable()).has_value());
	world.Run(mips);
	const auto mipRendering = world.Commands("BeginRendering");
	SWIM_REQUIRE_EQUAL(mipRendering.size(), std::size_t(1));
	SWIM_CHECK(mipRendering[0].SourceOffset == 16u && mipRendering[0].DestinationOffset == 8u);
	const auto* view = static_cast<const Rhi::TextureView*>(mipRendering[0].Source);
	SWIM_CHECK_EQUAL(view->GetDesc().BaseMipLevel, 2u);
	SWIM_CHECK(mipRendering[0].Destination == nullptr);
	SWIM_CHECK(world.Commands("BindGraphicsPipeline")[0].Source == &world.pipeline);
	SWIM_CHECK(renderer.GetLastConstants().TargetSize[0] == 16.0f);

	// Validation.
	RenderGraph invalid;
	const auto t = invalid.ImportTexture(*color, Rhi::ResourceState::ColorAttachment);
	const auto d = invalid.ImportTexture(*depth, Rhi::ResourceState::DepthStencilWrite);
	const auto small = invalid.ImportTexture(*smallDepth, Rhi::ResourceState::DepthStencilWrite);
	UiRenderFrame bad;
	bad.Paint = std::span(&paint, 1);
	bad.Target = t;
	bad.Depth = d; // Without a canvas matrix.
	SWIM_CHECK_THROWS(renderer.Record(invalid, bad, world.Program(), world.table->GetTable()), std::invalid_argument);
	bad.ClipFromCanvas = matrix;
	SWIM_CHECK_THROWS(renderer.Record(invalid, bad, world.Program(false), world.table->GetTable()), std::invalid_argument);
	bad.Depth = small; // Not the drawn extent.
	SWIM_CHECK_THROWS(renderer.Record(invalid, bad, world.Program(), world.table->GetTable()), std::invalid_argument);
	bad.TargetMip = 2; // Now it is.
	SWIM_CHECK(renderer.Record(invalid, bad, world.Program(), world.table->GetTable()).has_value());
	bad.TargetMip = 3;
	SWIM_CHECK_THROWS(renderer.Record(invalid, bad, world.Program(), world.table->GetTable()), std::invalid_argument);
	bad.TargetMip = 0;
	bad.Depth.reset();
	bad.OffsetX = 4.0f;
	SWIM_CHECK_THROWS(renderer.Record(invalid, bad, world.Program(), world.table->GetTable()), std::invalid_argument);
	bad.OffsetX = 0.0f;
	bad.Opacity = 2.0f;
	SWIM_CHECK_THROWS(renderer.Record(invalid, bad, world.Program(), world.table->GetTable()), std::invalid_argument);

	Testing::MockShaderProgram program;
	const auto tested = UiRenderer::PipelineDesc(Rhi::Format::RGBA16Float, program, world.renderLayout, Rhi::Format::D32Float);
	SWIM_CHECK(tested.DepthStencil.DepthTest && !tested.DepthStencil.DepthWrite);
	SWIM_CHECK(tested.DepthStencil.DepthCompare == Rhi::CompareOp::GreaterEqual);
	SWIM_CHECK(tested.DepthStencilFormat == Rhi::Format::D32Float);
	SWIM_CHECK(tested.Raster.Cull == Rhi::CullMode::None);
	SWIM_CHECK(!UiRenderer::PipelineDesc(Rhi::Format::RGBA16Float, program, world.renderLayout).DepthStencil.DepthTest);
	SWIM_CHECK_THROWS(
		UiRenderer::PipelineDesc(Rhi::Format::RGBA16Float, program, world.renderLayout, Rhi::Format::RGBA8Unorm), std::invalid_argument);
}

SWIM_TEST("Render.Ui.Surfaces", "SurfacesRegisterBindlessTexturesAndDrawEveryMipOnlyWhenPaintChanges")
{
	UiWorld world;
	UiRenderer renderer;
	UiRenderSurfaces surfaces(world.Device(), *world.table);
	UiRenderSurfaceDesc desc;
	desc.Width = 64;
	desc.Height = 32;
	const auto surface = surfaces.Create(desc);
	SWIM_CHECK(surfaces.IsValid(surface));
	UI::UiPaintQuad paint;
	paint.Bounds = { 4, 4, 20, 10 };
	paint.Clip = { 0, 0, 64, 32 };
	paint.Color = { 1, 0, 0, 1 };
	UiSurfaceContent content;
	content.Paint = std::span(&paint, 1);
	content.PaintRevision = 7;

	const auto record = [&](const UiSurfaceContent& c)
	{
		RenderGraph graph;
		const auto frame = surfaces.Record(graph, surface, renderer, world.Program(false), world.table->GetTable(), c);
		world.Run(graph);
		surfaces.CommitFrame();
		return frame;
	};
	auto frame = record(content);
	SWIM_CHECK(frame.Drawn);
	SWIM_CHECK_EQUAL(frame.MipLevels, 7u); // 64 x 32: the full chain.
	SWIM_CHECK_EQUAL(world.Commands("Draw").size(), std::size_t(7));
	const auto areas = world.Commands("BeginRendering");
	SWIM_REQUIRE_EQUAL(areas.size(), std::size_t(7));
	SWIM_CHECK(areas[0].SourceOffset == 64u && areas[0].DestinationOffset == 32u);
	SWIM_CHECK(areas[6].SourceOffset == 1u && areas[6].DestinationOffset == 1u);
	SWIM_CHECK(frame.TextureIndex != 0u); // A registered element (0 is the fallback).
	// Unchanged content: imported and exported, nothing drawn.
	frame = record(content);
	SWIM_CHECK(!frame.Drawn);
	SWIM_CHECK(world.Commands("Draw").empty());
	// A new revision or Force draws again; an aborted frame draws again next time.
	content.PaintRevision = 8;
	SWIM_CHECK(record(content).Drawn);
	content.Force = true;
	SWIM_CHECK(record(content).Drawn);
	content.Force = false;
	content.PaintRevision = 9;
	{
		RenderGraph graph;
		SWIM_CHECK(surfaces.Record(graph, surface, renderer, world.Program(false), world.table->GetTable(), content).Drawn);
		SWIM_CHECK_THROWS(
			surfaces.Record(graph, surface, renderer, world.Program(false), world.table->GetTable(), content), std::logic_error);
		surfaces.AbortFrame();
	}
	SWIM_CHECK(record(content).Drawn);
	const auto stats = surfaces.GetStats();
	SWIM_CHECK_EQUAL(stats.Surfaces, 1u);
	SWIM_CHECK_EQUAL(stats.DrawnSurfaces, 4u);
	SWIM_CHECK_EQUAL(stats.SkippedSurfaces, 1u);

	// The panel paint of a world quad showing the surface.
	const auto panel = UiRenderSurfaces::PanelPaint(frame, { 320, 160 });
	SWIM_REQUIRE_EQUAL(panel.size(), std::size_t(1));
	SWIM_CHECK(panel[0].Kind == UI::UiPaintKind::Image);
	SWIM_CHECK_EQUAL(panel[0].Texture, frame.TextureIndex);
	SWIM_CHECK_EQUAL(panel[0].Sampler, frame.SamplerIndex);
	SWIM_CHECK_NEAR(panel[0].Bounds.Width, 320.0f, 1e-6f);

	// Release, reuse and validation.
	SWIM_CHECK(surfaces.Release(surface));
	SWIM_CHECK(!surfaces.IsValid(surface));
	SWIM_CHECK(!surfaces.Release(surface));
	surfaces.Collect();
	const auto reused = surfaces.Create(desc);
	SWIM_CHECK(reused.Index == surface.Index && reused.Generation != surface.Generation);
	RenderGraph graph;
	SWIM_CHECK_THROWS(
		surfaces.Record(graph, surface, renderer, world.Program(false), world.table->GetTable(), content), std::invalid_argument);
	UiRenderSurfaceDesc bad = desc;
	bad.Width = 0;
	SWIM_CHECK_THROWS(surfaces.Create(bad), std::invalid_argument);
	bad = desc;
	bad.Format = Rhi::Format::D32Float;
	SWIM_CHECK_THROWS(surfaces.Create(bad), std::invalid_argument);
	bad = desc;
	bad.MipLevels = 8;
	SWIM_CHECK_THROWS(surfaces.Create(bad), std::invalid_argument);
}
