#pragma once
// Analytic geometry for the Clustered Forward+ tests (items 66-67): StandardVertex
// cube and quad meshes with exact per-face normals and tangents, and a CPU ray
// caster over the same shapes under GPU Scene transforms, so every pixel's surface
// (object, face, position, frame) is known exactly.
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusReference.h"
#include "Engine/Systems/Renderer/ForwardPlus/StandardVertex.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace Swim::Testing::ForwardScene
{
	using Float3 = std::array<float, 3>;

	inline Float3 Cross(const Float3& a, const Float3& b)
	{
		return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
	}

	inline float Dot(const Float3& a, const Float3& b)
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	}

	struct Mesh
	{
		std::vector<Render::StandardVertex> Vertices;
		std::vector<std::uint32_t> Indices;
	};

	// Cube face f: outward normal and tangent (the +u direction); bitangent = N x T.
	inline constexpr std::array<std::array<Float3, 2>, 6> CubeFaces{ { { Float3{ 1, 0, 0 }, Float3{ 0, 0, -1 } },
		{ Float3{ -1, 0, 0 }, Float3{ 0, 0, 1 } }, { Float3{ 0, 1, 0 }, Float3{ 1, 0, 0 } }, { Float3{ 0, -1, 0 }, Float3{ 1, 0, 0 } },
		{ Float3{ 0, 0, 1 }, Float3{ 1, 0, 0 } }, { Float3{ 0, 0, -1 }, Float3{ -1, 0, 0 } } } };

	// One square face: corners center + s T + r B for (s, r) in CCW order seen from
	// outside (T x B = N), two triangles.
	inline void AddFace(Mesh& mesh, const Float3& center, const Float3& normal, const Float3& tangent)
	{
		const auto bitangent = Cross(normal, tangent);
		const auto base = static_cast<std::uint32_t>(mesh.Vertices.size());
		constexpr std::array<std::array<float, 2>, 4> corners{ { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } } };
		for (const auto& [s, r] : corners)
		{
			Render::StandardVertex vertex;
			for (int c = 0; c < 3; ++c)
			{
				vertex.Position[c] = center[c] + s * tangent[c] + r * bitangent[c];
				vertex.Normal[c] = normal[c];
				vertex.Tangent[c] = tangent[c];
			}
			vertex.Tangent[3] = 1.0f;
			vertex.TexCoord0 = { (s + 1.0f) * 0.5f, (1.0f - r) * 0.5f };
			mesh.Vertices.push_back(vertex);
		}
		for (const auto index : { 0u, 1u, 2u, 0u, 2u, 3u })
		{
			mesh.Indices.push_back(base + index);
		}
	}

	// [-1, 1]^3, 24 vertices, 36 indices.
	inline Mesh MakeCube()
	{
		Mesh mesh;
		for (const auto& face : CubeFaces)
		{
			AddFace(mesh, face[0], face[0], face[1]);
		}
		return mesh;
	}

	// [-1, 1]^2 at z = 0 facing +Z.
	inline Mesh MakeQuad()
	{
		Mesh mesh;
		AddFace(mesh, { 0, 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 });
		return mesh;
	}

	enum class Shape
	{
		Cube,
		Quad,
	};

	struct Object
	{
		Shape Kind = Shape::Cube;
		Render::GpuTransformRecord Transform; // Current rows.
	};

	struct Hit
	{
		float T = 0.0f;
		std::uint32_t Object = 0;
		std::uint32_t Face = 0;			// Cube face index (0 for quads).
		Float3 Position{};				// World.
		Float3 Normal{};				// World, outward (unnormalized cofactor transform).
		std::array<float, 4> Tangent{}; // World tangent; w is the bitangent sign after mirroring.
		bool FrontFacing = true;		// The outward side faces the ray origin.
		bool Mirrored = false;
	};

	// Inverse of the 3x3 linear part (row-major 9 floats).
	inline std::array<float, 9> InverseLinear(const float (&rows)[12])
	{
		const Float3 r0{ rows[0], rows[1], rows[2] };
		const Float3 r1{ rows[4], rows[5], rows[6] };
		const Float3 r2{ rows[8], rows[9], rows[10] };
		const auto c0 = Cross(r1, r2);
		const auto c1 = Cross(r2, r0);
		const auto c2 = Cross(r0, r1);
		const float det = Dot(r0, c0);
		// inverse columns are c0, c1, c2 divided by det.
		return { c0[0] / det, c1[0] / det, c2[0] / det, c0[1] / det, c1[1] / det, c2[1] / det, c0[2] / det, c1[2] / det, c2[2] / det };
	}

	// Every intersection of the ray origin + t * direction (t > 0) with the objects,
	// nearest first. Cubes report their entry face, quads either side.
	inline std::vector<Hit> CastRay(const std::vector<Object>& objects, const Float3& origin, const Float3& direction)
	{
		namespace Fp = Render::ForwardPlus;
		std::vector<Hit> hits;
		for (std::uint32_t index = 0; index < objects.size(); ++index)
		{
			const auto& object = objects[index];
			const auto& rows = object.Transform.Current;
			const auto inverse = InverseLinear(rows);
			const Float3 relative{ origin[0] - rows[3], origin[1] - rows[7], origin[2] - rows[11] };
			Float3 o{}, d{};
			for (int r = 0; r < 3; ++r)
			{
				o[r] = inverse[r * 3] * relative[0] + inverse[r * 3 + 1] * relative[1] + inverse[r * 3 + 2] * relative[2];
				d[r] = inverse[r * 3] * direction[0] + inverse[r * 3 + 1] * direction[1] + inverse[r * 3 + 2] * direction[2];
			}
			std::optional<float> t;
			Float3 localNormal{}, localTangent{};
			std::uint32_t face = 0;
			if (object.Kind == Shape::Quad)
			{
				if (std::abs(d[2]) > 1.0e-12f)
				{
					const float candidate = -o[2] / d[2];
					const float x = o[0] + candidate * d[0];
					const float y = o[1] + candidate * d[1];
					if (candidate > 1.0e-4f && std::abs(x) <= 1.0f && std::abs(y) <= 1.0f)
					{
						t = candidate;
						localNormal = { 0, 0, 1 };
						localTangent = { 1, 0, 0 };
					}
				}
			}
			else
			{
				float tNear = -std::numeric_limits<float>::infinity();
				float tFar = std::numeric_limits<float>::infinity();
				int axis = 0;
				bool miss = false;
				for (int a = 0; a < 3 && !miss; ++a)
				{
					if (std::abs(d[a]) < 1.0e-12f)
					{
						miss = std::abs(o[a]) > 1.0f;
						continue;
					}
					float t0 = (-1.0f - o[a]) / d[a];
					float t1 = (1.0f - o[a]) / d[a];
					if (t0 > t1)
					{
						std::swap(t0, t1);
					}
					if (t0 > tNear)
					{
						tNear = t0;
						axis = a;
					}
					tFar = std::min(tFar, t1);
				}
				if (!miss && tNear <= tFar && tNear > 1.0e-4f)
				{
					t = tNear;
					// The entry face faces against the ray.
					const bool positive = d[axis] < 0.0f;
					for (std::uint32_t f = 0; f < 6; ++f)
					{
						if (CubeFaces[f][0][axis] == (positive ? 1.0f : -1.0f))
						{
							face = f;
						}
					}
					localNormal = CubeFaces[face][0];
					localTangent = CubeFaces[face][1];
				}
			}
			if (!t)
			{
				continue;
			}
			Hit hit;
			hit.T = *t;
			hit.Object = index;
			hit.Face = face;
			for (int c = 0; c < 3; ++c)
			{
				hit.Position[c] = origin[c] + *t * direction[c];
			}
			hit.Normal = Fp::TransformNormal(rows, localNormal);
			hit.Mirrored = Fp::Determinant(rows) < 0.0f;
			const auto tangent = Fp::TransformDirection(rows, localTangent);
			hit.Tangent = { tangent[0], tangent[1], tangent[2], hit.Mirrored ? -1.0f : 1.0f };
			hit.FrontFacing = Dot(hit.Normal, direction) < 0.0f;
			hits.push_back(hit);
		}
		std::sort(hits.begin(), hits.end(),
			[](const Hit& a, const Hit& b)
			{
				return a.T < b.T;
			});
		return hits;
	}

	// A row-major affine: translation * rotation(Y) * rotation(X) * scale.
	inline Render::GpuTransformRecord MakeTransform(const Float3& translation, float yaw, float pitch, const Float3& scale)
	{
		const float cy = std::cos(yaw), sy = std::sin(yaw), cx = std::cos(pitch), sx = std::sin(pitch);
		// Ry * Rx.
		const float r[9]{ cy, sy * sx, sy * cx, 0, cx, -sx, -sy, cy * sx, cy * cx };
		Render::GpuTransformRecord transform;
		for (int row = 0; row < 3; ++row)
		{
			for (int column = 0; column < 3; ++column)
			{
				transform.Current[row * 4 + column] = r[row * 3 + column] * scale[column];
			}
			transform.Current[row * 4 + 3] = translation[row];
		}
		for (int i = 0; i < 12; ++i)
		{
			transform.Previous[i] = transform.Current[i];
		}
		return transform;
	}
} // namespace Swim::Testing::ForwardScene
