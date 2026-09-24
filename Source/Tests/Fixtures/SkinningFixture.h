#pragma once
#include "Engine/Systems/Animation/Animator.h"
#include "Engine/Systems/Animation/SkeletonInstance.h"
#include "Engine/Systems/Renderer/Skinning/SkinningReference.h"
#include "Tests/Fixtures/AnimationFixture.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace Swim::Testing
{
	// A square tube along +y from y = 0 to y = 2 (Rings rings of four vertices, each
	// vertex duplicated per face for flat normals, so 8 per ring), skinned to the
	// chain skeleton of AnimationFixture.h: Root below y = 0.5, Mid around y = 1 and
	// Tip above y = 1.5, blended linearly in between. One morph target bulges the
	// middle outwards; a second only moves the top ring's normals.
	struct SkinnedStrip
	{
		static constexpr std::uint32_t JointCount = 3;

		std::vector<Render::StandardVertex> Vertices;
		std::vector<Render::SkinInfluence> Influences;
		std::vector<std::array<float, 3>> Bulge;	  // Position deltas, target 0.
		std::vector<std::array<float, 3>> TopNormals; // Normal deltas, target 1.
		std::vector<std::uint32_t> Indices;
		std::array<Render::SkinnedMorphTarget, 2> Targets{};

		explicit SkinnedStrip(std::uint32_t rings = 9, float halfWidth = 0.25f)
		{
			const std::array<std::array<float, 2>, 4> corners{ { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } } };
			for (std::uint32_t ring = 0; ring < rings; ++ring)
			{
				const float y = 2.0f * float(ring) / float(rings - 1);
				for (std::uint32_t face = 0; face < 4; ++face)
				{
					// Face normal between corners face and face + 1.
					const auto& a = corners[face];
					const auto& b = corners[(face + 1) % 4];
					const float nx = (a[0] + b[0]) * 0.5f, nz = (a[1] + b[1]) * 0.5f;
					for (const auto* corner : { &a, &b })
					{
						Render::StandardVertex v;
						v.Position = { (*corner)[0] * halfWidth, y, (*corner)[1] * halfWidth };
						v.Normal = { nx, 0.0f, nz };
						v.Tangent = { 0.0f, 1.0f, 0.0f, face % 2 ? -1.0f : 1.0f };
						v.TexCoord0 = { float(face) / 4.0f, y / 2.0f };
						Vertices.push_back(v);
						Influences.push_back(InfluenceAt(y));
						const float bulge = 0.5f * std::max(0.0f, 1.0f - std::abs(y - 1.0f));
						Bulge.push_back({ (*corner)[0] * bulge, 0.0f, (*corner)[1] * bulge });
						TopNormals.push_back(
							ring + 1 == rings ? std::array<float, 3>{ 0.0f, 0.5f, 0.0f } : std::array<float, 3>{ 0, 0, 0 });
					}
				}
			}
			for (std::uint32_t ring = 0; ring + 1 < rings; ++ring)
			{
				for (std::uint32_t face = 0; face < 4; ++face)
				{
					const std::uint32_t a = ring * 8 + face * 2, b = a + 1, c = a + 8, d = b + 8;
					Indices.insert(Indices.end(), { a, b, d, a, d, c });
				}
			}
			Targets[0].Positions = Bulge;
			Targets[1].Normals = TopNormals;
		}

		static Render::SkinInfluence InfluenceAt(float y)
		{
			Render::SkinInfluence influence;
			if (y <= 0.5f)
			{
				influence.Joints = { 0, 0, 0, 0 };
				influence.Weights = { 1, 0, 0, 0 };
			}
			else if (y < 1.5f)
			{
				// Root -> Mid over 0.5 .. 1, Mid -> Tip over 1 .. 1.5.
				const bool lower = y < 1.0f;
				const float t = lower ? (y - 0.5f) / 0.5f : (y - 1.0f) / 0.5f;
				influence.Joints = { std::uint16_t(lower ? 1 : 2), std::uint16_t(lower ? 0 : 1), 0, 0 };
				influence.Weights = { t, 1.0f - t, 0, 0 };
				if (t < 0.5f)
				{
					std::swap(influence.Joints[0], influence.Joints[1]);
					std::swap(influence.Weights[0], influence.Weights[1]);
				}
			}
			else
			{
				influence.Joints = { 2, 0, 0, 0 };
				influence.Weights = { 1, 0, 0, 0 };
			}
			return influence;
		}

		std::span<const std::byte> IndexBytes() const { return std::as_bytes(std::span(Indices)); }
	};

	// A waving chain: Mid bends about z and Tip about x, driven by an Animator.
	inline std::shared_ptr<const Animation::AnimationClip> MakeWaveClip()
	{
		Assets::AnimationClipAsset clip;
		clip.Name = "Wave";
		clip.Tracks.push_back(MakeRotationTrack("Mid", { 0, 0, 1 }, { 0.0f, 0.5f, 1.0f }, { 0.0f, 50.0f, 0.0f }));
		clip.Tracks.push_back(MakeRotationTrack("Tip", { 1, 0, 0 }, { 0.0f, 0.5f, 1.0f }, { 0.0f, -40.0f, 0.0f }));
		clip.Tracks.push_back(MakeVectorTrack("Root", Assets::AnimationPath::Translation, { 0.0f, 1.0f }, { 0, 0, 0, 0.5f, 0, 0 }));
		return std::make_shared<const Animation::AnimationClip>(clip);
	}

	inline std::vector<Render::SkinMatrix> Palette(std::span<const Animation::Matrix3x4> matrices)
	{
		return { matrices.begin(), matrices.end() };
	}
} // namespace Swim::Testing
