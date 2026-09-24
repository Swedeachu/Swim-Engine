#pragma once
// CPU shadow maps for the Phase 16 tests: every texel of a shadow view is ray-cast
// against Tests/Fixtures/ForwardPlusFixture.h's analytic shapes, so the expected
// depth of each texel center is exact (what the GPU rasterizer interpolates there).
#include "Engine/Systems/Renderer/Shadows/ShadowMath.h"
#include "Tests/Fixtures/ForwardPlusFixture.h"

#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace Swim::Testing::ShadowScene
{
	using Matrix = Render::Shadows::Matrix;
	using Float3 = std::array<float, 3>;

	// General 4x4 inverse (row-major), Gauss-Jordan with partial pivoting.
	inline std::optional<Matrix> Inverse(const Matrix& m)
	{
		std::array<double, 32> a{};
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				a[r * 8 + c] = m[r * 4 + c];
			}
			a[r * 8 + 4 + r] = 1.0;
		}
		for (int column = 0; column < 4; ++column)
		{
			int pivot = column;
			for (int r = column + 1; r < 4; ++r)
			{
				pivot = std::abs(a[r * 8 + column]) > std::abs(a[pivot * 8 + column]) ? r : pivot;
			}
			if (std::abs(a[pivot * 8 + column]) < 1.0e-300)
			{
				return std::nullopt;
			}
			for (int c = 0; c < 8; ++c)
			{
				std::swap(a[column * 8 + c], a[pivot * 8 + c]);
			}
			const double scale = 1.0 / a[column * 8 + column];
			for (int c = 0; c < 8; ++c)
			{
				a[column * 8 + c] *= scale;
			}
			for (int r = 0; r < 4; ++r)
			{
				if (r == column)
				{
					continue;
				}
				const double factor = a[r * 8 + column];
				for (int c = 0; c < 8; ++c)
				{
					a[r * 8 + c] -= factor * a[column * 8 + c];
				}
			}
		}
		Matrix inverse{};
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				inverse[r * 4 + c] = float(a[r * 8 + 4 + c]);
			}
		}
		return inverse;
	}

	inline Float3 Unproject(const Matrix& inverse, float x, float y, float z)
	{
		double out[4]{};
		for (int r = 0; r < 4; ++r)
		{
			out[r] = double(inverse[r * 4]) * x + double(inverse[r * 4 + 1]) * y + double(inverse[r * 4 + 2]) * z + inverse[r * 4 + 3];
		}
		return { float(out[0] / out[3]), float(out[1] / out[3]), float(out[2] / out[3]) };
	}

	// Renders one view's tile of `atlas` (reverse-Z, cleared texels keep their value).
	// `casts[i]` says whether object i casts (non-casters, masked-away and blended
	// objects are skipped). `signature` (optional, atlas-sized) receives object * 8 +
	// face + 1 of the surface each texel saw, 0 for none.
	inline void RenderView(const std::vector<ForwardScene::Object>& objects, const std::vector<bool>& casts,
		const Render::GpuShadowView& view, Render::Shadows::ShadowAtlasImage& atlas, std::vector<std::uint32_t>* signature = nullptr)
	{
		Matrix viewProjection{};
		std::copy(std::begin(view.ViewProjection), std::end(view.ViewProjection), viewProjection.begin());
		const auto inverse = Inverse(viewProjection);
		if (!inverse)
		{
			return;
		}
		const auto x0 = std::uint32_t(view.AtlasRect[0]);
		const auto y0 = std::uint32_t(view.AtlasRect[1]);
		const auto size = std::uint32_t(view.AtlasRect[2]);
		for (std::uint32_t y = 0; y < size; ++y)
		{
			for (std::uint32_t x = 0; x < size; ++x)
			{
				const float ndcX = (float(x) + 0.5f) / float(size) * 2.0f - 1.0f;
				const float ndcY = 1.0f - (float(y) + 0.5f) / float(size) * 2.0f;
				const auto nearPoint = Unproject(*inverse, ndcX, ndcY, 1.0f);
				const auto midPoint = Unproject(*inverse, ndcX, ndcY, 0.5f);
				const Float3 direction{ midPoint[0] - nearPoint[0], midPoint[1] - nearPoint[1], midPoint[2] - nearPoint[2] };
				const std::size_t texel = std::size_t(y0 + y) * atlas.Size + (x0 + x);
				for (const auto& hit : ForwardScene::CastRay(objects, nearPoint, direction))
				{
					if (!casts[hit.Object])
					{
						continue;
					}
					if (const auto projected = Render::Shadows::ProjectToShadowView(view, hit.Position))
					{
						atlas.Depth[texel] = projected->Depth;
						if (signature)
						{
							(*signature)[texel] = hit.Object * 8 + hit.Face + 1;
						}
					}
					break;
				}
			}
		}
	}
} // namespace Swim::Testing::ShadowScene
