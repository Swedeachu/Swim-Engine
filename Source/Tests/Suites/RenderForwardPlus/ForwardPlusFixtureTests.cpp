#include "Tests/Fixtures/ForwardPlusFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <optional>
#include <random>

using namespace Swim;
using namespace Swim::Render;
namespace Fs = Swim::Testing::ForwardScene;
namespace Fp = Swim::Render::ForwardPlus;

namespace
{
	Fs::Float3 Sub(const Fs::Float3& a, const Fs::Float3& b)
	{
		return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
	}

	// Moller-Trumbore; t > 0 only.
	std::optional<float> IntersectTriangle(
		const Fs::Float3& o, const Fs::Float3& d, const Fs::Float3& a, const Fs::Float3& b, const Fs::Float3& c)
	{
		const auto e1 = Sub(b, a);
		const auto e2 = Sub(c, a);
		const auto p = Fs::Cross(d, e2);
		const float det = Fs::Dot(e1, p);
		if (std::abs(det) < 1.0e-12f)
		{
			return std::nullopt;
		}
		const auto s = Sub(o, a);
		const float u = Fs::Dot(s, p) / det;
		const auto q = Fs::Cross(s, e1);
		const float v = Fs::Dot(d, q) / det;
		const float t = Fs::Dot(e2, q) / det;
		if (u < 0.0f || v < 0.0f || u + v > 1.0f || t <= 0.0f)
		{
			return std::nullopt;
		}
		return t;
	}
} // namespace

// The analytic shapes the native Forward+ smoke ray-casts are exactly the meshes it
// uploads: counter-clockwise outward triangles whose vertex normals and tangents
// equal the face frames, and ray hits that agree with triangle intersection under
// rotated, non-uniformly scaled and mirrored transforms.
SWIM_TEST("Render.ForwardPlus.Fixture", "MeshesMatchTheAnalyticRayCaster")
{
	for (const auto& mesh : { Fs::MakeCube(), Fs::MakeQuad() })
	{
		SWIM_REQUIRE_EQUAL(mesh.Indices.size() % 3, std::size_t(0));
		for (std::size_t i = 0; i < mesh.Indices.size(); i += 3)
		{
			const auto& a = mesh.Vertices[mesh.Indices[i]];
			const auto& b = mesh.Vertices[mesh.Indices[i + 1]];
			const auto& c = mesh.Vertices[mesh.Indices[i + 2]];
			const auto geometric = Fs::Cross(Sub(b.Position, a.Position), Sub(c.Position, a.Position));
			SWIM_CHECK(Fs::Dot(geometric, a.Normal) > 0.0f); // CCW seen from the outside.
			SWIM_CHECK(a.Normal == b.Normal && b.Normal == c.Normal);
			// The tangent is dp/du on the face (along an edge of constant v).
			const float du = b.TexCoord0[0] - a.TexCoord0[0];
			if (du != 0.0f && b.TexCoord0[1] == a.TexCoord0[1])
			{
				const auto dp = Sub(b.Position, a.Position);
				const float scale = 1.0f / du;
				for (int k = 0; k < 3; ++k)
				{
					SWIM_CHECK(std::abs(dp[k] * scale * 0.5f - a.Tangent[k]) < 1.0e-6f);
				}
			}
		}
	}
	SWIM_CHECK_EQUAL(Fs::MakeCube().Vertices.size(), std::size_t(24));

	std::mt19937 random(68);
	std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
	const auto cube = Fs::MakeCube();
	const auto quad = Fs::MakeQuad();
	std::uint32_t hits = 0;
	for (int trial = 0; trial < 300; ++trial)
	{
		const bool mirrored = trial % 3 == 0;
		const auto transform = Fs::MakeTransform({ unit(random), unit(random), unit(random) }, 3.0f * unit(random), 3.0f * unit(random),
			{ (mirrored ? -1.0f : 1.0f) * (1.2f + 0.5f * unit(random)), 1.0f + 0.5f * unit(random), 0.8f + 0.3f * unit(random) });
		const bool isQuad = trial % 2 == 1;
		const std::vector<Fs::Object> objects{ { isQuad ? Fs::Shape::Quad : Fs::Shape::Cube, transform } };
		const Fs::Float3 origin{ 6.0f * unit(random), 6.0f * unit(random), 8.0f };
		const Fs::Float3 target{ 0.8f * unit(random), 0.8f * unit(random), 0.8f * unit(random) };
		const auto direction = Sub(target, origin);
		const auto cast = Fs::CastRay(objects, origin, direction);
		// Brute force over the transformed triangles.
		const auto& mesh = isQuad ? quad : cube;
		std::optional<float> nearest;
		Fs::Float3 nearestNormal{};
		for (std::size_t i = 0; i < mesh.Indices.size(); i += 3)
		{
			const auto p = [&](std::size_t k)
			{
				return Fp::TransformPoint(transform.Current, mesh.Vertices[mesh.Indices[i + k]].Position);
			};
			const auto t = IntersectTriangle(origin, direction, p(0), p(1), p(2));
			if (t && (!nearest || *t < *nearest))
			{
				nearest = t;
				nearestNormal = Fp::TransformNormal(transform.Current, mesh.Vertices[mesh.Indices[i]].Normal);
			}
		}
		SWIM_REQUIRE_EQUAL(cast.empty(), !nearest.has_value());
		if (!nearest)
		{
			continue;
		}
		++hits;
		SWIM_CHECK(std::abs(cast[0].T - *nearest) < 1.0e-4f * (1.0f + *nearest));
		const float alignment = Fs::Dot(cast[0].Normal, nearestNormal) /
			std::sqrt(Fs::Dot(cast[0].Normal, cast[0].Normal) * Fs::Dot(nearestNormal, nearestNormal));
		SWIM_CHECK(alignment > 0.9999f);
		SWIM_CHECK(cast[0].Mirrored == mirrored);
		if (!isQuad)
		{
			SWIM_CHECK(cast[0].FrontFacing); // A closed cube is entered through a front face.
		}
	}
	SWIM_CHECK(hits > 150u);
}
