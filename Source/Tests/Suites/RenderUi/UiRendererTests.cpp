#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/UiRendering/UiRenderer.h"
#include "Tests/Fixtures/BindlessTableFixture.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <cstring>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	class MockGraphicsPipeline final : public Rhi::GraphicsPipeline
	{
	  public:
		std::uintptr_t GetNativeHandle() const override { return 31; }
	};

	// A mock device, a bindless table and the UI program's layout (space 0: the quad
	// buffer; space 1: the bindless arrays).
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

		std::unique_ptr<Rhi::Texture> Target(
			Rhi::Format format = Rhi::Format::RGBA8Unorm, std::uint32_t width = 64, std::uint32_t height = 32)
		{
			Rhi::TextureDesc desc;
			desc.Extent = { width, height, 1 };
			desc.PixelFormat = format;
			desc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource;
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

		UiRenderProgram Program() { return { &pipeline, &renderLayout }; }

		std::unique_ptr<Testing::BindlessTableFixture> bindlessFixture;
		std::unique_ptr<RenderGraphExecutor> executor;
		std::unique_ptr<BindlessResourceTable> table;
		Testing::MockPipelineLayout renderLayout;
		MockGraphicsPipeline pipeline;
	};
} // namespace

SWIM_TEST("Render.Ui.AtlasTextures", "UploadsWholePagesOnceThenOnlyChangedRows")
{
	UiWorld world;
	const auto font = Swim::Testing::LoadTextFontFixture();
	Text::GlyphAtlasDesc atlasDesc;
	atlasDesc.PageSize = 128;
	Text::GlyphAtlas atlas(atlasDesc);
	atlas.Get(font, font->GetGlyph(U'A'));
	UiAtlasTextures textures(world.Device(), *world.table);
	const auto texturesBefore = world.Device().TextureCreateCount;

	RenderGraph graph;
	const auto frame = textures.Update(graph, atlas);
	SWIM_REQUIRE_EQUAL(frame.Pages.size(), 1u);
	SWIM_CHECK_EQUAL(frame.UploadedRows, 128u); // The first upload initializes the whole page.
	SWIM_CHECK_EQUAL(frame.UploadedBytes, 128ull * 128 * 4);
	SWIM_CHECK(frame.TextureIndices[0] != BindlessResourceTable::FallbackIndex);
	SWIM_CHECK_EQUAL(frame.SamplerIndex, textures.GetSamplerIndex());
	SWIM_CHECK_THROWS(textures.Update(graph, atlas), std::logic_error); // Awaiting commit.
	world.Run(graph);
	textures.CommitFrame();
	SWIM_CHECK_EQUAL(world.Device().TextureCreateCount, texturesBefore + 1);
	SWIM_CHECK_EQUAL(world.Commands("CopyBufferToTexture").size(), std::size_t(1));
	// The GPU page holds the RGB distances with opaque alpha.
	const auto* pageView = static_cast<const Testing::MockTextureView*>(world.bindlessFixture->Table().Element(1, frame.TextureIndices[0]));
	auto* gpuPage = static_cast<Testing::MockTexture*>(&pageView->GetTexture());
	const auto view = atlas.GetPage(0);
	const auto& bytes = gpuPage->Bytes({});
	bool same = true;
	for (std::size_t texel = 0; texel < std::size_t(view.Size) * view.Size; ++texel)
	{
		for (int c = 0; c < 3; ++c)
		{
			same = same && std::to_integer<std::uint8_t>(bytes[texel * 4 + c]) == view.Pixels[texel * 3 + c];
		}
		same = same && std::to_integer<std::uint8_t>(bytes[texel * 4 + 3]) == 255u;
	}
	SWIM_CHECK(same);

	// Nothing new: no upload. A new glyph: only its shelf's rows.
	RenderGraph quiet;
	SWIM_CHECK_EQUAL(textures.Update(quiet, atlas).UploadedPages, 0u);
	world.Run(quiet);
	textures.CommitFrame();
	SWIM_CHECK(world.Commands("CopyBufferToTexture").empty());
	const auto entry = atlas.Get(font, font->GetGlyph(U'W'));
	RenderGraph partial;
	const auto update = textures.Update(partial, atlas);
	SWIM_CHECK_EQUAL(update.UploadedRows, atlas.GetChangedRows(0, 1).Height);
	SWIM_CHECK(update.UploadedRows < 128u);
	// An aborted frame is uploaded again next time.
	textures.AbortFrame();
	RenderGraph retry;
	SWIM_CHECK_EQUAL(textures.Update(retry, atlas).UploadedRows, update.UploadedRows);
	world.Run(retry);
	textures.CommitFrame();
	const auto& updated = gpuPage->Bytes({});
	const std::size_t probe = (std::size_t(entry.Y) * view.Size + entry.X + entry.Width / 2) * 4;
	const auto latest = atlas.GetPage(0);
	const std::uint8_t expected = latest.Pixels[probe / 4 * 3];
	SWIM_CHECK_EQUAL(std::to_integer<std::uint8_t>(updated[probe]), expected);
	SWIM_CHECK_EQUAL(textures.GetStats().Pages, 1u);
	SWIM_CHECK_EQUAL(textures.GetStats().UploadedBytes, 128ull * 128 * 4 + std::uint64_t(update.UploadedBytes));

	// Switching atlases requires a release; pages retire after their last use.
	Text::GlyphAtlas other(atlasDesc);
	RenderGraph wrong;
	SWIM_CHECK_THROWS(textures.Update(wrong, other), std::logic_error);
	Testing::MockTimeline timeline;
	textures.Release({ &timeline, 5 });
	SWIM_CHECK_EQUAL(textures.GetStats().RetiringPages, 1u);
	SWIM_CHECK_EQUAL(textures.Collect(), 0u);
	timeline.Complete(5);
	SWIM_CHECK_EQUAL(textures.Collect(), 1u);
	SWIM_CHECK(world.bindlessFixture->Table().Element(1, frame.TextureIndices[0]) == world.bindlessFixture->fallbackView.get());
	other.Get(font, font->GetGlyph(U'x'));
	RenderGraph fresh;
	SWIM_CHECK_EQUAL(textures.Update(fresh, other).UploadedRows, 128u);
	textures.AbortFrame();
}

SWIM_TEST("Render.Ui.Renderer", "RecordsOneInstancedDrawWithBindlessTablesAndConstants")
{
	UiWorld world;
	const auto font = Swim::Testing::LoadTextFontFixture();
	Text::GlyphAtlas atlas;
	UI::UiDocument document;
	const auto label = document.Create(document.GetRoot());
	UI::UiStyle style;
	style.Background = { 0.1f, 0.2f, 0.3f, 1.0f };
	style.CornerRadius = 4.0f;
	document.SetStyle(label, style);
	document.SetText(label, font, "Hi!", 16.0f);
	document.Layout({ 64, 32 }, 1.0f);
	const auto& paint = document.Paint(atlas);
	SWIM_REQUIRE_EQUAL(paint.size(), 4u); // Background + three glyphs.

	UiAtlasTextures textures(world.Device(), *world.table);
	UiRenderer renderer;
	auto target = world.Target();
	RenderGraph graph;
	const auto atlasFrame = textures.Update(graph, atlas);
	const auto color = graph.ImportTexture(*target, Rhi::ResourceState::ColorAttachment);
	graph.Export(color, Rhi::ResourceState::ColorAttachment);
	UiRenderFrame frame;
	frame.Paint = paint;
	frame.Target = color;
	frame.Atlas = &atlasFrame;
	const auto pass = renderer.Record(graph, frame, world.Program(), world.table->GetTable());
	SWIM_REQUIRE(pass.has_value());
	world.Run(graph);
	textures.CommitFrame();
	SWIM_CHECK_EQUAL(renderer.GetStats().Quads, 4u);
	SWIM_CHECK_EQUAL(renderer.GetStats().Glyphs, 3u);
	const auto draws = world.Commands("Draw");
	SWIM_REQUIRE_EQUAL(draws.size(), std::size_t(1));
	SWIM_CHECK(draws[0].SourceOffset == 6u && draws[0].DestinationOffset == 4u);
	const auto tables = world.Commands("BindDescriptorTable");
	SWIM_REQUIRE_EQUAL(tables.size(), std::size_t(2));
	SWIM_CHECK(tables[0].SourceOffset == 0u && tables[1].SourceOffset == UiRenderBindings::BindlessSpace);
	SWIM_CHECK(tables[1].Source == &world.table->GetTable());
	const auto pushes = world.Commands("PushConstants");
	SWIM_REQUIRE_EQUAL(pushes.size(), std::size_t(1));
	GpuUiDrawConstants constants{};
	SWIM_REQUIRE_EQUAL(pushes[0].Data.size(), sizeof(constants));
	std::memcpy(&constants, pushes[0].Data.data(), sizeof(constants));
	SWIM_CHECK(constants.TargetSize[0] == 64.0f && constants.TargetSize[1] == 32.0f);
	SWIM_CHECK_EQUAL(constants.Encoding, 0u);
	// Glyph instances sample the atlas page through its bindless element.
	const auto& quads = renderer.GetLastQuads();
	SWIM_CHECK_EQUAL(quads[0].Kind, UiQuadSolid);
	SWIM_CHECK_NEAR(quads[0].Radius, 4.0f, 1e-6f);
	for (std::size_t i = 1; i < quads.size(); ++i)
	{
		SWIM_CHECK_EQUAL(quads[i].Kind, UiQuadGlyph);
		SWIM_CHECK_EQUAL(quads[i].Texture, atlasFrame.TextureIndices[0]);
		SWIM_CHECK_EQUAL(quads[i].Sampler, atlasFrame.SamplerIndex);
	}
	// The upload precedes the draw (the pass declares the page as a sampled read).
	std::size_t upload = 0, draw = 0, index = 0;
	for (const auto& command : *world.Device().Commands)
	{
		upload = command.Kind == "CopyBufferToTexture" ? index : upload;
		draw = command.Kind == "Draw" ? index : draw;
		++index;
	}
	SWIM_CHECK(upload < draw);
}

SWIM_TEST("Render.Ui.Renderer", "ValidatesTargetsEncodingsAndEmptyFrames")
{
	UiWorld world;
	UiRenderer renderer;
	auto target = world.Target();
	auto srgb = world.Target(Rhi::Format::RGBA8UnormSrgb);
	RenderGraph graph;
	const auto color = graph.ImportTexture(*target, Rhi::ResourceState::ColorAttachment);
	UiRenderFrame frame;
	frame.Target = color;
	SWIM_CHECK(!renderer.Record(graph, frame, world.Program(), world.table->GetTable()).has_value()); // Nothing to draw.
	frame.Clear = true;
	frame.ClearColor = { 0, 0, 0, 1 };
	SWIM_CHECK(renderer.Record(graph, frame, world.Program(), world.table->GetTable()).has_value());
	graph.Export(color, Rhi::ResourceState::ColorAttachment);
	world.Run(graph);
	SWIM_CHECK(world.Commands("Draw").empty()); // A clear-only pass.

	RenderGraph invalid;
	const auto srgbTarget = invalid.ImportTexture(*srgb, Rhi::ResourceState::ColorAttachment);
	frame.Target = srgbTarget;
	SWIM_CHECK_THROWS(renderer.Record(invalid, frame, world.Program(), world.table->GetTable()), std::invalid_argument);
	frame.Composition.Encoding = UiOutputEncoding::Linear;
	SWIM_CHECK(renderer.Record(invalid, frame, world.Program(), world.table->GetTable()).has_value());
	SWIM_CHECK_THROWS(renderer.Record(invalid, frame, {}, world.table->GetTable()), std::invalid_argument);
	frame.Composition.PaperWhiteNits = -1.0f;
	SWIM_CHECK_THROWS(renderer.Record(invalid, frame, world.Program(), world.table->GetTable()), std::invalid_argument);

	// Per-format pipeline descriptions keep their format storage alive.
	Testing::MockShaderProgram program;
	const auto a = UiRenderer::PipelineDesc(Rhi::Format::RGBA8Unorm, program, world.renderLayout);
	const auto b = UiRenderer::PipelineDesc(Rhi::Format::RGBA16Float, program, world.renderLayout);
	SWIM_CHECK(a.ColorFormats[0] == Rhi::Format::RGBA8Unorm && b.ColorFormats[0] == Rhi::Format::RGBA16Float);
	SWIM_CHECK(a.BlendAttachments[0].Enabled && a.BlendAttachments[0].DestinationColor == Rhi::BlendFactor::OneMinusSourceAlpha &&
		a.BlendAttachments[0].SourceColor == Rhi::BlendFactor::One);
	SWIM_CHECK_THROWS(UiRenderer::PipelineDesc(Rhi::Format::D32Float, program, world.renderLayout), std::invalid_argument);
	SWIM_CHECK_THROWS(UiRenderer(UiRendererDesc{ 0 }), std::invalid_argument);
}
