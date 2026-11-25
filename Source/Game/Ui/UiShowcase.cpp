#include "Game/Ui/UiShowcase.h"
#include "Engine/Runtime/UiRuntime.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Renderer/Runtime/MaterialLibrary.h"
#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"
#include "Engine/Systems/Renderer/Runtime/RenderServices.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Systems/Text/FontCollection.h"
#include "Engine/Systems/Text/GlyphAtlas.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace Game
{

	using namespace Swim::UI;

	namespace
	{

		// Row-major world-from-local: yaw, then pitch, of a box of `size` at `center`.
		std::array<float, 16> Box(std::array<float, 3> center, std::array<float, 3> size, float yaw, float pitch = 0.0f)
		{
			const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
			// R = Rx(pitch) * Ry(yaw).
			const float r[3][3] = { { cy, 0.0f, sy }, { sp * sy, cp, -sp * cy }, { -cp * sy, sp, cp * cy } };
			std::array<float, 16> m{};

			for (int row = 0; row < 3; ++row)
			{
				for (int column = 0; column < 3; ++column)
				{
					m[row * 4 + column] = r[row][column] * size[column];
				}

				m[row * 4 + 3] = r[row][0] * center[0] + r[row][1] * center[1] + r[row][2] * center[2];
			}

			m[15] = 1.0f;
			return m;
		}

		std::array<float, 16> Translation(float x, float y, float z)
		{
			return { 1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1 };
		}

	} // namespace

	UiShowcase::UiShowcase(Engine::Scene* sceneValue, entt::entity owner) : Behavior(sceneValue, owner)
	{
	}

	std::shared_ptr<const Swim::Text::FontFace> UiShowcase::MakePixelFont(const std::shared_ptr<const Swim::Text::FontFace>& vector)
	{
		constexpr std::uint32_t Cell = 8, Size = Cell * 16, Samples = 4;
		constexpr float PixelsPerEm = 8.5f, Baseline = 7.0f;
		Swim::Text::GlyphAtlas atlas({ 1024, 4, 64.0f, 6.0f });
		auto rgba = std::make_shared<std::vector<std::uint8_t>>(std::size_t(Size) * Size * 4, 0);

		for (char32_t code = 33; code < 127; ++code)
		{
			const auto glyph = atlas.Get(vector, vector->GetGlyph(code));

			if (glyph.Page == Swim::Text::NoAtlasPage || glyph.WidthEm <= 0.0f || glyph.HeightEm <= 0.0f)
			{
				continue;
			}

			const auto page = atlas.GetPage(glyph.Page);

			for (std::uint32_t py = 0; py < Cell; ++py)
			{
				for (std::uint32_t px = 0; px < Cell; ++px)
				{
					std::uint32_t covered = 0;

					for (std::uint32_t s = 0; s < Samples * Samples; ++s)
					{
						const float xEm = (float(px) + (float(s % Samples) + 0.5f) / Samples) / PixelsPerEm;
						const float yEm = (Baseline - float(py) - (float(s / Samples) + 0.5f) / Samples) / PixelsPerEm;
						const float tx = float(glyph.X) + (xEm - glyph.Left) / glyph.WidthEm * float(glyph.Width);
						const float ty = float(glyph.Y) + (glyph.Top - yEm) / glyph.HeightEm * float(glyph.Height);

						if (tx < float(glyph.X) || ty < float(glyph.Y) || tx >= float(glyph.X + glyph.Width) ||
							ty >= float(glyph.Y + glyph.Height))
						{
							continue;
						}

						const auto* t = &page.Pixels[(std::size_t(ty) * page.Size + std::size_t(tx)) * 3];
						const int median = std::max(std::min(t[0], t[1]), std::min(std::max(t[0], t[1]), t[2]));
						covered += median > 127 ? 1u : 0u;
					}

					if (covered * 8 >= Samples * Samples * 3)
					{
						const std::uint32_t x = (code % 16) * Cell + px, y = (code / 16) * Cell + py;
						auto* p = &(*rgba)[(std::size_t(y) * Size + x) * 4];
						p[0] = p[1] = p[2] = p[3] = 255;
					}
				}
			}
		}

		Swim::Text::BitmapFontDesc desc;
		desc.IntegerScale = false; // Sizes are picked in device pixels (BuildContent).
		desc.LoadPage = [rgba](std::uint32_t block) -> std::optional<Swim::Text::BitmapFontPage>
		{
			if (block != 0)
			{
				return std::nullopt;
			}

			return Swim::Text::BitmapFontPage{ Size, Size, *rgba, 0.0f };
		};
		return std::make_shared<Swim::Text::FontFace>(std::move(desc));
	}

	int UiShowcase::Init()
	{
		render = scene->GetRenderServices();

		if (!render || !render->Ui || !render->HasRenderer())
		{
			return 0;
		}

		Build();
		return 0;
	}

	int UiShowcase::Exit()
	{
		if (render && render->Ui && overlay)
		{
			render->Ui->RemoveOverlay(overlay);
		}

		if (render && render->Renderer)
		{
			auto& viewports = render->Renderer->GetViewports();
			viewports.Destroy(figure, render->Renderer->GetLastCompletion());
			viewports.Destroy(icons, render->Renderer->GetLastCompletion());
		}

		return 0;
	}

	void UiShowcase::Build()
	{
		auto& ui = *render->Ui;
		document = ui.CreateDocument();
		const auto vector = ui.GetFonts();
		pixelFonts = std::make_shared<const Swim::Text::FontCollection>(
			std::vector<std::shared_ptr<const Swim::Text::FontFace>>{ MakePixelFont(vector->GetFace(0)), vector->GetFace(0) });
		std::vector<std::uint8_t> pixels(24 * 24 * 4);

		for (std::uint32_t y = 0; y < 24; ++y)
		{
			for (std::uint32_t x = 0; x < 24; ++x)
			{
				const std::uint32_t edge = std::min(std::min(x, y), std::min(23 - x, 23 - y));
				auto* p = &pixels[(y * 24 + x) * 4];
				const bool rim = edge < 2, inner = edge >= 2 && edge < 8 && ((x + y) % 4 < 2);
				p[0] = rim ? 250 : inner ? 170 : 40;
				p[1] = rim ? 200 : inner ? 110 : 45;
				p[2] = rim ? 90 : inner ? 40 : 70;
				p[3] = 255;
			}
		}

		frameTexture = render->Meshes->RegisterTexture("Showcase/Frame", 24, 24, pixels, true, false);
		const std::array<std::uint8_t, 4> a{ 235, 190, 150, 255 }, b{ 70, 120, 200, 255 };
		skinTexture = render->Meshes->RegisterChecker("Showcase/Skin", 16, 4, a, b);
		// Opaque white plus, transparent elsewhere (premultiplied).
		std::vector<std::uint8_t> cross(16 * 16 * 4, 0);

		for (std::uint32_t y = 0; y < 16; ++y)
		{
			for (std::uint32_t x = 0; x < 16; ++x)
			{
				if (x == 7 || x == 8 || y == 7 || y == 8)
				{
					std::fill_n(&cross[(y * 16 + x) * 4], 4, std::uint8_t(255));
				}
			}
		}

		crosshairTexture = render->Meshes->RegisterTexture("Showcase/Crosshair", 16, 16, cross, true, false);
		auto& viewports = render->Renderer->GetViewports();
		figure = viewports.Create({ 240, 320, true, "Showcase figure" });
		icons = viewports.Create({ 128, 128, true, "Showcase icons" });
		DrawIcons();
		overlay = ui.AddOverlay(document, 50);
		ui.SetOverlayVisible(overlay, true);
	}

	// Sizes are device pixels (divided by the DPI scale), so pixel-font texels and the
	// viewport images map to whole device pixels.
	void UiShowcase::BuildContent(float dpiValue)
	{
		dpi = dpiValue;
		frameIndex = ~0u;
		crosshairIndex = ~0u;
		crosshairNodes.clear();
		auto& d = *document;
		const auto px = [this](float value)
		{
			return value / dpi;
		};
		const auto vector = render->Ui->GetFonts();
		const auto& pixel = pixelFonts;

		if (content)
		{
			d.Remove(content);
		}

		content = d.Create(d.GetRoot());
		UiStyle root;
		root.Flow = UiFlow::Row;
		root.Padding = { px(24), px(24), px(24), px(24) };
		root.Gap = px(24);
		root.AlignItems = UiAlign::Start;
		d.SetStyle(content, root);

		UiStyle column;
		column.Gap = px(14);
		column.Padding = { px(14), px(14), px(14), px(14) };
		column.Background = { 0.02f, 0.025f, 0.035f, 0.88f };
		column.CornerRadius = px(6);
		const auto left = d.Create(content);
		d.SetStyle(left, column);
		const auto right = d.Create(content);
		d.SetStyle(right, column);

		const auto heading = [&](UiNodeId parent, const std::string& text)
		{
			const auto node = d.Create(parent);
			UiStyle style;
			style.TextColor = { 0.6f, 0.75f, 1.0f, 1.0f };
			d.SetStyle(node, style);
			d.SetText(node, vector, text, px(20));
		};

		// Pixel-font paragraphs: one span per format.
		heading(left, "Pixel font, 1x / 2x / 3x (vector fallback for the accented letter)");

		for (const float scale : { 1.0f, 2.0f, 3.0f })
		{
			std::string text;
			UiTextOptions options;
			const auto add = [&](const std::string& part, UiColor color, UiTextFormat format)
			{
				options.Styles.push_back({ static_cast<std::uint32_t>(text.size()), color, format });
				text += part;
			};
			const UiColor white{ 1, 1, 1, 1 };
			add("Red ", { 1.0f, 0.18f, 0.12f, 1 }, UiTextFormat::None);
			add("Gold ", { 1.0f, 0.65f, 0.05f, 1 }, UiTextFormat::None);
			add("Bold ", white, UiTextFormat::Bold);
			add("Italic ", { 0.4f, 0.8f, 1.0f, 1 }, UiTextFormat::Italic);
			add("Under", white, UiTextFormat::Underline);
			add(" ", white, UiTextFormat::None);
			add("Strike", { 0.6f, 1.0f, 0.4f, 1 }, UiTextFormat::Strikethrough);
			add(" ", white, UiTextFormat::None);
			add("Obf", { 0.9f, 0.4f, 1.0f, 1 }, UiTextFormat::Obfuscated);
			add(" ", white, UiTextFormat::None);
			add("Shadow", white, UiTextFormat::Shadow);
			add(" caf\xC3\xA9", white, UiTextFormat::Shadow | UiTextFormat::Bold);
			const auto node = d.Create(left);
			d.SetText(node, pixel, text, px(8.0f * scale), options);
		}

		const auto wrapped = d.Create(left);
		UiStyle wrapStyle;
		wrapStyle.Width = UiLength::Pixels(px(420));
		wrapStyle.TextWrap = Swim::Text::TextWrap::Word;
		d.SetStyle(wrapped, wrapStyle);
		UiTextOptions wrapOptions;
		wrapOptions.Styles = { { 0, { 1, 1, 1, 1 }, UiTextFormat::Shadow },
			{ 37, { 1.0f, 0.85f, 0.3f, 1 }, UiTextFormat::Shadow | UiTextFormat::Bold } };
		d.SetText(wrapped, pixel,
			"Wrapped at 2x: measurement and drawing share one layout, so bold words, spans and fallback glyphs line up.", px(16),
			wrapOptions);

		// Nine-slice frame.
		heading(left, "Nine-slice frame (24 px source, 8 px borders)");
		{
			frameNode = d.Create(left);
			UiStyle style;
			style.Width = UiLength::Pixels(px(420));
			style.Height = UiLength::Pixels(px(110));
			d.SetStyle(frameNode, style);
		}

		// Clipped scroll view.
		heading(left, "Clipped scroll view (scrolled 46 px)");
		{
			const auto view = d.Create(left);
			UiStyle style;
			style.Width = UiLength::Pixels(px(420));
			style.Height = UiLength::Pixels(px(120));
			style.Clip = true;
			style.Padding = { px(8), px(8), px(8), px(8) };
			style.Gap = px(4);
			style.Background = { 0.08f, 0.09f, 0.12f, 1.0f };
			style.BorderWidth = px(1);
			style.BorderColor = { 0.4f, 0.45f, 0.6f, 1.0f };
			d.SetStyle(view, style);

			for (int i = 0; i < 12; ++i)
			{
				const auto row = d.Create(view);
				UiStyle rowStyle;
				rowStyle.Background = i % 2 ? UiColor{ 0.16f, 0.2f, 0.3f, 1 } : UiColor{ 0.12f, 0.14f, 0.2f, 1 };
				rowStyle.Padding = { px(6), px(2), px(6), px(2) };
				d.SetStyle(row, rowStyle);
				d.SetText(row, pixel, "Row " + std::to_string(i + 1) + ": clipped to the view's border box", px(16));
			}

			d.SetScroll(view, { 0, px(46) });
		}

		// Scene viewports.
		auto& viewports = render->Renderer->GetViewports();
		heading(right, "3D viewport (live)");
		figureNode = d.Create(right);
		{
			const auto image = viewports.GetImage(figure);
			UiStyle style;
			style.Width = UiLength::Pixels(px(240));
			style.Height = UiLength::Pixels(px(320));
			style.Background = { 0.1f, 0.12f, 0.16f, 1.0f };
			d.SetStyle(figureNode, style);
			d.SetImage(figureNode, { image.Texture, image.Sampler, { px(240), px(320) } });
		}

		heading(right, "Icons (drawn once)");
		iconsNode = d.Create(right);
		{
			const auto image = viewports.GetImage(icons);
			UiStyle style;
			style.Width = UiLength::Pixels(px(256));
			style.Height = UiLength::Pixels(px(256));
			d.SetStyle(iconsNode, style);
			d.SetImage(iconsNode, { image.Texture, image.Sampler, { px(256), px(256) } });
		}

		// Inverting crosshairs on the bar boundaries; the last one blends normally for contrast.
		heading(right, "Invert blend (last: normal)");
		{
			const auto strip = d.Create(right);
			UiStyle style;
			style.Flow = UiFlow::Row;
			style.Width = UiLength::Pixels(px(384));
			style.Height = UiLength::Pixels(px(72));
			d.SetStyle(strip, style);
			const std::array<UiColor, 6> bars{ { { 0.92f, 0.92f, 0.92f, 1 }, { 0.03f, 0.03f, 0.04f, 1 }, { 0.85f, 0.15f, 0.1f, 1 },
				{ 0.95f, 0.85f, 0.2f, 1 }, { 0.1f, 0.25f, 0.8f, 1 }, { 0.5f, 0.5f, 0.5f, 1 } } };

			for (const auto& bar : bars)
			{
				UiStyle barStyle;
				barStyle.Width = UiLength::Pixels(px(64));
				barStyle.Height = UiLength::Pixels(px(72));
				barStyle.Background = bar;
				d.SetStyle(d.Create(strip), barStyle);
			}

			for (int i = 1; i <= 6; ++i)
			{
				const auto node = d.Create(strip);
				UiStyle cross;
				cross.Absolute = true;
				cross.Width = UiLength::Pixels(px(32));
				cross.Height = UiLength::Pixels(px(32));
				cross.AnchorMin = cross.AnchorMax = { i < 6 ? float(i) / 6.0f : 1.0f / 12.0f, 0.5f };
				cross.Pivot = { 0.5f, 0.5f };
				d.SetStyle(node, cross);
				crosshairNodes.emplace_back(node, i < 6 ? UiBlend::Invert : UiBlend::Normal);
			}
		}
	}

	void UiShowcase::DrawFigure()
	{
		const auto cube = render->Meshes->Get(Engine::BuiltinMesh::Cube);
		Engine::SceneViewportScene s;
		s.View = Translation(0.0f, -1.0f, -4.4f);
		s.Projection = Swim::Render::PerspectiveReverseZRowMajor(0.62f, 240.0f / 320.0f, 0.1f);
		s.ClearColor = { 0.05f, 0.06f, 0.09f, 1.0f };
		const auto part = [&](std::array<float, 3> center, std::array<float, 3> size, std::array<float, 4> tint)
		{
			Engine::SceneViewportItem item;
			item.Mesh = cube;
			item.Texture = skinTexture;
			item.Transform = Box(center, size, angle, 0.12f);
			item.Tint = tint;
			s.Items.push_back(item);
		};
		part({ 0.0f, 1.75f, 0.0f }, { 0.5f, 0.5f, 0.5f }, { 1, 1, 1, 1 });
		part({ 0.0f, 1.125f, 0.0f }, { 0.5f, 0.75f, 0.25f }, { 0.4f, 0.8f, 0.5f, 1 });
		part({ -0.375f, 1.125f, 0.0f }, { 0.25f, 0.75f, 0.25f }, { 1, 1, 1, 1 });
		part({ 0.375f, 1.125f, 0.0f }, { 0.25f, 0.75f, 0.25f }, { 1, 1, 1, 1 });
		part({ -0.125f, 0.375f, 0.0f }, { 0.25f, 0.75f, 0.25f }, { 0.3f, 0.35f, 0.8f, 1 });
		part({ 0.125f, 0.375f, 0.0f }, { 0.25f, 0.75f, 0.25f }, { 0.3f, 0.35f, 0.8f, 1 });
		render->Renderer->GetViewports().Draw(figure, std::move(s));
	}

	void UiShowcase::DrawIcons()
	{
		const std::array<std::array<float, 4>, 4> tints{ { { 1, 1, 1, 1 }, { 0.9f, 0.3f, 0.25f, 1 }, { 0.3f, 0.85f, 0.4f, 1 },
			{ 1.0f, 0.8f, 0.2f, 1 } } };
		const std::array<Engine::BuiltinMesh, 4> meshes{ Engine::BuiltinMesh::Cube, Engine::BuiltinMesh::Cube, Engine::BuiltinMesh::Sphere,
			Engine::BuiltinMesh::Cylinder };

		for (std::uint32_t i = 0; i < 4; ++i)
		{
			Engine::SceneViewportScene s;
			s.View = Translation(0.0f, 0.0f, -5.0f);
			s.Projection = Swim::Render::OrthographicReverseZRowMajor(-0.9f, 0.9f, -0.9f, 0.9f, 0.1f, 20.0f);
			s.Region = { (i % 2) * 64, (i / 2) * 64, 64, 64 };
			s.LightDirection = { -0.3f, 0.9f, 0.5f };
			s.Ambient = { 0.45f, 0.45f, 0.5f };
			Engine::SceneViewportItem item;
			item.Mesh = render->Meshes->Get(meshes[i]);
			item.Texture = i == 0 ? skinTexture : Swim::Assets::AssetHandle<Swim::Assets::TextureAsset>{};
			item.Transform = Box({ 0, 0, 0 }, { 1, 1, 1 }, 0.785398f, 0.615f);
			item.Tint = tints[i];
			s.Items.push_back(item);
			render->Renderer->GetViewports().Draw(icons, std::move(s));
		}
	}

	void UiShowcase::Update(double dt)
	{
		if (!document)
		{
			return;
		}

		if (document->GetDpiScale() != dpi || !content)
		{
			BuildContent(document->GetDpiScale());
		}

		angle += static_cast<float>(dt) * 0.8f;
		DrawFigure();
		const auto index = render->Meshes->GetBindlessIndex(frameTexture);

		if (index != frameIndex && render->Meshes->IsResident(frameTexture))
		{
			frameIndex = index;
			UiImage image;
			image.Texture = index;
			image.Sampler = render->Materials->GetSamplerIndex(true);
			image.Size = { 420 / dpi, 110 / dpi };
			image.Slice = { 16 / dpi, 16 / dpi, 16 / dpi, 16 / dpi };
			image.SliceUv = { 8.0f / 24, 8.0f / 24, 8.0f / 24, 8.0f / 24 };
			document->SetImage(frameNode, image);
		}

		const auto cross = render->Meshes->GetBindlessIndex(crosshairTexture);

		if (cross != crosshairIndex && render->Meshes->IsResident(crosshairTexture))
		{
			crosshairIndex = cross;

			for (const auto& [node, blend] : crosshairNodes)
			{
				UiImage image;
				image.Texture = cross;
				image.Sampler = render->Materials->GetSamplerIndex(true);
				image.Size = { 32 / dpi, 32 / dpi };
				image.Blend = blend;
				document->SetImage(node, image);
			}
		}
	}

} // namespace Game
