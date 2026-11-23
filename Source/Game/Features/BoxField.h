#pragma once

#include "Engine/Systems/Renderer/Resources/StreamingBuffer.h"
#include "Engine/Systems/Renderer/Resources/TextureArray.h"
#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace Game
{

	// A field of bobbing boxes drawn entirely on the GPU through the render feature
	// geometry hooks: a compute pass generates the boxes into a storage buffer, another
	// culls them against the camera and every shadow tile into indirect draw arguments,
	// and the depth prepass, opaque and shadow passes draw them with DrawIndirectCount,
	// pulling vertices from the storage buffer (Source/Game/Shaders/BoxField*.slang).
	// Each row's colours live in a range of a StreamingBuffer; a few times a second a row
	// is recoloured into a new range and spacer ranges come and go, so the buffer keeps
	// fragmenting and defragmenting while the field must stay coherent. The boxes' faces
	// sample a four-layer TextureArray: three generated patterns (one repainted every two
	// seconds) and FaceImage, loaded in the background and reloaded when the file changes.
	// The camera view culls in two phases: the prepass draws the boxes visible last frame,
	// CullGeometry tests the rest against the HZB, and the opaque pass draws the newly
	// visible ones.
	class BoxField : public Engine::RenderFeature
	{

	  public:

		std::string_view GetName() const override { return "BoxField"; }

		std::uint32_t GetGeometryPasses() const override;

		Engine::RenderGeometryResources PrepareGeometry(Engine::RenderFeatureContext& context) override;

		bool UsesOcclusion() const override { return true; }

		void CullGeometry(Engine::RenderFeatureContext& context, const Engine::RenderFeatureOcclusion& occlusion) override;

		void DrawGeometry(Engine::RenderFeatureDrawContext& context) override;

		std::array<float, 3> Origin{ 34.0f, 0.0f, -20.0f };
		float Spacing = 0.9f;
		std::uint32_t Columns = 32;
		std::uint32_t Rows = 24;
		std::filesystem::path FaceImage; // Layer 3 (a generated pattern when empty).

	  private:

		Swim::Render::GraphBuffer boxes{};
		Swim::Render::GraphBuffer counts{};
		Swim::Render::GraphBuffer arguments{};
		Swim::Render::GraphBuffer views{};
		Swim::Render::GraphBuffer visibility{};
		std::unique_ptr<Swim::Rhi::Buffer> history; // Per box: visible last frame.
		bool cut = true;
		std::uint32_t boxCount = 0;
		std::uint32_t viewCount = 0;
		std::vector<int> shadowSlots; // Shadow view row -> cull view (-1: not drawn).
		float previousTime = -1.0f;
		std::unique_ptr<Swim::Render::StreamingBuffer> colors;
		std::vector<Swim::Render::StreamingRange> rowRanges;
		std::vector<Swim::Render::StreamingRange> spacers;
		float nextChurn = 0.0f;
		std::uint32_t seed = 12345u;
		std::unique_ptr<Swim::Render::TextureArray> faces;
		Swim::Render::GraphTexture faceTexture{};
		Swim::Render::GraphBuffer faceMinLod{};
		float nextRepaint = 0.0f;
		std::uint32_t repaints = 0;

		void WriteRow(std::uint32_t row);

		void PaintLayer(std::uint32_t layer, std::uint32_t variant);

		std::uint32_t Random();

	};

} // namespace Game
