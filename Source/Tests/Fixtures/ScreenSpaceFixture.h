#pragma once
// Analytic scenes for the screen-space effect tests (item 76): a ground plane and
// axis-aligned boxes, ray cast per pixel into the inputs Forward+ would produce
// (reverse-Z depth, world normal + roughness), under any ScreenSpaceView with jitter.
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceReference.h"
#include "Tests/Fixtures/ClusterFixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace Swim::Testing::ScreenSpaceScene
{
	using Float3 = std::array<float, 3>;
	namespace Ss = Render::ScreenSpace;

	struct Box
	{
		Float3 Min;
		Float3 Max;
	};

	struct Scene
	{
		bool Ground = true;
		float GroundHeight = 0.0f;
		std::vector<Box> Boxes;
	};

	struct Hit
	{
		float T = 0.0f;
		Float3 Position{};
		Float3 Normal{};
	};

	inline std::optional<Hit> Cast(const Scene& scene, const Float3& origin, const Float3& direction)
	{
		std::optional<Hit> best;
		const auto consider = [&](float t, const Float3& normal)
		{
			if (t > 1.0e-4f && (!best || t < best->T))
			{
				best = Hit{ t, { origin[0] + direction[0] * t, origin[1] + direction[1] * t, origin[2] + direction[2] * t }, normal };
			}
		};
		if (scene.Ground && std::abs(direction[1]) > 1.0e-8f)
		{
			consider((scene.GroundHeight - origin[1]) / direction[1], { 0, origin[1] > scene.GroundHeight ? 1.0f : -1.0f, 0 });
		}
		for (const auto& box : scene.Boxes)
		{
			float enter = -std::numeric_limits<float>::infinity();
			float exit = std::numeric_limits<float>::infinity();
			int axis = -1;
			float sign = 0.0f;
			bool miss = false;
			for (int a = 0; a < 3 && !miss; ++a)
			{
				if (std::abs(direction[a]) < 1.0e-12f)
				{
					miss = origin[a] < box.Min[a] || origin[a] > box.Max[a];
					continue;
				}
				float t0 = (box.Min[a] - origin[a]) / direction[a];
				float t1 = (box.Max[a] - origin[a]) / direction[a];
				float faceSign = -1.0f;
				if (t0 > t1)
				{
					std::swap(t0, t1);
					faceSign = 1.0f;
				}
				if (t0 > enter)
				{
					enter = t0;
					axis = a;
					sign = faceSign;
				}
				exit = std::min(exit, t1);
			}
			if (!miss && axis >= 0 && enter <= exit)
			{
				Float3 normal{ 0, 0, 0 };
				normal[axis] = sign;
				consider(enter, normal);
			}
		}
		return best;
	}

	struct Inputs
	{
		Ss::ScalarImage Depth;
		Ss::ColorImage Normal; // World normal, roughness in w.
		std::vector<Hit> Hits; // T = 0 where the ray hit nothing.
		Float3 Camera{};
	};

	// Each pixel centre sees the scene through NDC (centre - jitter), like a jittered raster.
	inline Inputs Render(
		const Scene& scene, const Render::ScreenSpaceView& view, std::uint32_t width, std::uint32_t height, float roughness = 0.5f)
	{
		const auto viewProjection = Render::MultiplyRowMajor(view.Projection, view.View);
		const auto inverse = *Ss::Inverse(viewProjection);
		const auto inverseView = *Ss::Inverse(view.View);
		Inputs inputs{ Ss::ScalarImage(width, height), Ss::ColorImage(width, height), std::vector<Hit>(std::size_t(width) * height), {} };
		inputs.Camera = { inverseView[3], inverseView[7], inverseView[11] };
		for (std::uint32_t y = 0; y < height; ++y)
		{
			for (std::uint32_t x = 0; x < width; ++x)
			{
				const float ndcX = 2.0f * (float(x) + 0.5f) / float(width) - 1.0f - view.Jitter[0];
				const float ndcY = 1.0f - 2.0f * (float(y) + 0.5f) / float(height) - view.Jitter[1];
				std::array<float, 4> near{};
				for (int r = 0; r < 4; ++r)
				{
					near[r] = inverse[r * 4] * ndcX + inverse[r * 4 + 1] * ndcY + inverse[r * 4 + 2] + inverse[r * 4 + 3];
				}
				const Float3 point{ near[0] / near[3], near[1] / near[3], near[2] / near[3] };
				const Float3 raw{ point[0] - inputs.Camera[0], point[1] - inputs.Camera[1], point[2] - inputs.Camera[2] };
				const float length = std::sqrt(raw[0] * raw[0] + raw[1] * raw[1] + raw[2] * raw[2]);
				const Float3 direction{ raw[0] / length, raw[1] / length, raw[2] / length };
				const auto hit = Cast(scene, inputs.Camera, direction);
				if (!hit)
				{
					continue; // Sky: depth 0, normal 0.
				}
				std::array<float, 4> clip{};
				for (int r = 0; r < 4; ++r)
				{
					clip[r] = viewProjection[r * 4] * hit->Position[0] + viewProjection[r * 4 + 1] * hit->Position[1] +
						viewProjection[r * 4 + 2] * hit->Position[2] + viewProjection[r * 4 + 3];
				}
				inputs.Depth.At(x, y) = clip[2] / clip[3];
				inputs.Normal.At(x, y) = { hit->Normal[0], hit->Normal[1], hit->Normal[2], roughness };
				inputs.Hits[std::size_t(y) * width + x] = *hit;
			}
		}
		return inputs;
	}

	inline Render::ScreenSpaceView View(const Float3& eye, const Float3& target, float aspect, float verticalFov = 1.0f)
	{
		Render::ScreenSpaceView view;
		view.View = ClusterScene::LookAt(eye, target, { 0, 1, 0 });
		view.Projection = Render::PerspectiveReverseZRowMajor(verticalFov, aspect, 0.1f);
		return view;
	}
} // namespace Swim::Testing::ScreenSpaceScene
