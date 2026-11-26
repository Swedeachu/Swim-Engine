#pragma once

#include "Engine/Systems/Renderer/Features/Wind.h"
#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Engine
{

	// Block-style fire: flame cards textured with a flipbook from the game's texture array
	// (consecutive layers, or rows inside one layer), swaying with the wind like foliage and
	// emissive, so bloom gives the glow. Fires sit on the floor, under a ceiling or on any
	// wall of their cell, or stand alone as small flames (torches, candles, campfires) and
	// around burning entities. The transparent pass adds a glow halo and stateless GPU
	// particles per fire: embers, sparks and light smoke. A fire draws in the frame it is
	// added. Lights are not drawn here; Flicker gives the curve the cards use, so a light
	// can follow it (VfxDirector does). Programs: BlockFire, BlockFireProcedural,
	// BlockFireGlow.
	class BlockFire final : public RenderFeature
	{

	  public:
		static constexpr std::uint32_t MaxFires = 16384;

		using Float3 = std::array<float, 3>;

		enum class Attachment : std::uint8_t
		{
			Floor,	 // Burning on the block below.
			Ceiling, // Burning on the underside of the block above.
			North,	 // On the wall at the cell's min z.
			South,	 // On the wall at the cell's max z.
			West,	 // On the wall at the cell's min x.
			East,	 // On the wall at the cell's max x.
		};

		enum class Shape : std::uint8_t
		{
			Block,	// Cards filling the cell (by Attachment).
			Small,	// Two crossed cards: torch, candle and campfire flames.
			Entity, // Four cards around a burning entity.
		};

		struct Fire
		{
			Float3 Position{ 0.0f, 0.0f, 0.0f }; // Block: the cell's min corner. Small, Entity: the centre of the flame's base.
			Attachment Attach = Attachment::Floor;
			Shape Kind = Shape::Block;
			std::uint32_t Layer = 0;		 // First texture-array layer of the flipbook.
			std::uint32_t Frames = 1;		 // Flipbook frames.
			std::uint32_t Rows = 1;			 // > 1: the frames are rows inside each layer (top first); 1: one layer per frame.
			float FrameRate = 16.0f;		 // Frames per second.
			float Width = 1.0f;				 // Small, Entity: card width (m).
			float Height = 1.0f;			 // Card height (m).
			float Intensity = 1.0f;			 // Emission and particle brightness.
			Float3 Tint{ 1.0f, 1.0f, 1.0f }; // Emission tint (e.g. a blue flame's texture can stay white-tinted).
			float Seed = 0.0f;				 // Decorrelates neighbours (frame offset, flicker, particles).
			std::uint32_t Embers = 4;		 // 0..6.
			std::uint32_t Sparks = 1;		 // 0..2.
			std::uint32_t Smoke = 1;		 // 0..3.
		};

		// Sorted nearest first by whoever fills it (VfxDirector does): the first
		// ParticleFires get the halo and particles.
		std::vector<Fire> Fires;
		// The game's block Texture2DArray (sRGB). Null draws a procedural flame instead.
		Swim::Rhi::TextureView* Texture = nullptr;
		// Used instead of Texture when set; once it expires the procedural flame draws
		// (TextureArray::GetSharedView).
		std::weak_ptr<Swim::Rhi::TextureView> SharedTexture;
		WindSettings Wind;

		struct Look
		{
			float Emission = 3.0f;		// Card emission scale (HDR).
			float Glow = 1.0f;			// Halo strength.
			float Sway = 0.06f;			// Lean per m/s of wind at the card top.
			float FlickerAmount = 1.0f; // 0..1.
			float SmokeOpacity = 0.12f; // Light smoke puffs.
			std::uint32_t ParticleFires = 192;
		};

		Look Settings;

		// The brightness multiplier the cards flicker with (about 0.75..1.2 at amount 1).
		static float Flicker(float time, float seed, float amount);

		std::string_view GetName() const override { return "Block fire"; }

		std::uint32_t GetGeometryPasses() const override;

		RenderGeometryResources PrepareGeometry(RenderFeatureContext& context) override;

		void DrawGeometry(RenderFeatureDrawContext& context) override;

	  private:
		Swim::Render::GraphBuffer params{};
		Swim::Render::GraphBuffer fires{};
		std::uint32_t count = 0;
		std::uint32_t particleCount = 0;
		std::shared_ptr<Swim::Rhi::TextureView> heldTexture; // Keeps SharedTexture alive until drawn.
		Swim::Rhi::TextureView* frameTexture = nullptr;
		float previousTime = 0.0f;
	};

} // namespace Engine
