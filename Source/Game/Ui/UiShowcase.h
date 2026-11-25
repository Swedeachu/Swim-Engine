#pragma once

#include "Engine/Systems/Entity/Behavior.h"
#include "Engine/Systems/Renderer/Runtime/SceneViewports.h"
#include "Engine/Systems/UI/UiDocument.h"

#include <memory>
#include <utility>
#include <vector>

namespace Engine
{

	struct RenderServices;

}

namespace Swim::Text
{

	class FontFace;
	class FontCollection;

} // namespace Swim::Text

namespace Game
{

	// A screen overlay ("sandbox.showcase 1|0") exercising the UI rendering paths: pixel-font
	// paragraphs at 1x/2x/3x with colour and format spans, a nine-slice frame, a clipped
	// scroll view, a live 3D figure in a scene viewport, four icons rendered once into
	// cells of one viewport texture, and inverting crosshairs over light and dark bars.
	class UiShowcase : public Engine::Behavior
	{

	  public:

		UiShowcase(Engine::Scene* scene, entt::entity owner);

		int Init() override;

		int Exit() override;

		void Update(double dt) override;

		bool UsesRealTime() const override { return true; }

		// A pixel font made from a vector face's outlines (8-pixel cells, printable ASCII).
		static std::shared_ptr<const Swim::Text::FontFace> MakePixelFont(const std::shared_ptr<const Swim::Text::FontFace>& vector);

	  private:

		void Build();

		void BuildContent(float dpi);

		void DrawFigure();

		void DrawIcons();

		Engine::RenderServices* render = nullptr;
		std::shared_ptr<Swim::UI::UiDocument> document;
		std::uint32_t overlay = 0;
		std::shared_ptr<const Swim::Text::FontCollection> pixelFonts;
		Swim::UI::UiNodeId content;
		float dpi = 0.0f;
		Engine::SceneViewportHandle figure;
		Engine::SceneViewportHandle icons;
		Swim::UI::UiNodeId frameNode;
		Swim::UI::UiNodeId figureNode;
		Swim::UI::UiNodeId iconsNode;
		std::vector<std::pair<Swim::UI::UiNodeId, Swim::UI::UiBlend>> crosshairNodes;
		Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> frameTexture;
		Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> skinTexture;
		Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> crosshairTexture;
		std::uint32_t frameIndex = ~0u;
		std::uint32_t crosshairIndex = ~0u;
		float angle = 0.6f;

	};

} // namespace Game
