#include "Engine/Systems/Renderer/Runtime/ProceduralMeshes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Engine::ProceduralMeshes
{
	namespace
	{
		using Float3 = std::array<float, 3>;
		constexpr float Pi = 3.14159265358979f;

		Float3 Sub(const Float3& a, const Float3& b)
		{
			return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
		}

		Float3 Add(const Float3& a, const Float3& b)
		{
			return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
		}

		Float3 Scale(const Float3& a, float s)
		{
			return { a[0] * s, a[1] * s, a[2] * s };
		}

		float Dot(const Float3& a, const Float3& b)
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		}

		Float3 Cross(const Float3& a, const Float3& b)
		{
			return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
		}

		Float3 Normalize(const Float3& a, const Float3& fallback = { 1, 0, 0 })
		{
			const float length = std::sqrt(Dot(a, a));
			return length > 1e-12f ? Scale(a, 1.0f / length) : fallback;
		}

		// Any unit vector perpendicular to n.
		Float3 Perpendicular(const Float3& n)
		{
			const Float3 axis = std::abs(n[0]) < 0.9f ? Float3{ 1, 0, 0 } : Float3{ 0, 1, 0 };
			return Normalize(Cross(axis, n));
		}
	} // namespace

	std::uint32_t MeshData::AddVertex(
		const std::array<float, 3>& position, const std::array<float, 3>& normal, const std::array<float, 2>& uv)
	{
		Swim::Render::StandardVertex vertex;
		vertex.Position = position;
		vertex.Normal = normal;
		vertex.TexCoord0 = uv;
		Vertices.push_back(vertex);
		return static_cast<std::uint32_t>(Vertices.size() - 1);
	}

	void MeshData::AddTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c)
	{
		Indices.insert(Indices.end(), { a, b, c });
	}

	void MeshData::AddQuad(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d)
	{
		AddTriangle(a, b, c);
		AddTriangle(a, c, d);
	}

	std::array<float, 3> MeshData::BoundsMin() const
	{
		Float3 low{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
		for (const auto& vertex : Vertices)
		{
			for (int c = 0; c < 3; ++c)
			{
				low[c] = std::min(low[c], vertex.Position[c]);
			}
		}
		return Vertices.empty() ? Float3{ 0, 0, 0 } : low;
	}

	std::array<float, 3> MeshData::BoundsMax() const
	{
		Float3 high{ -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
		for (const auto& vertex : Vertices)
		{
			for (int c = 0; c < 3; ++c)
			{
				high[c] = std::max(high[c], vertex.Position[c]);
			}
		}
		return Vertices.empty() ? Float3{ 0, 0, 0 } : high;
	}

	void GenerateTangents(MeshData& mesh)
	{
		std::vector<Float3> tangents(mesh.Vertices.size(), Float3{ 0, 0, 0 });
		std::vector<Float3> bitangents(mesh.Vertices.size(), Float3{ 0, 0, 0 });
		for (std::size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
		{
			const std::uint32_t index[3]{ mesh.Indices[i], mesh.Indices[i + 1], mesh.Indices[i + 2] };
			const auto& v0 = mesh.Vertices[index[0]];
			const auto& v1 = mesh.Vertices[index[1]];
			const auto& v2 = mesh.Vertices[index[2]];
			const Float3 e1 = Sub(v1.Position, v0.Position);
			const Float3 e2 = Sub(v2.Position, v0.Position);
			const float du1 = v1.TexCoord0[0] - v0.TexCoord0[0];
			const float dv1 = v1.TexCoord0[1] - v0.TexCoord0[1];
			const float du2 = v2.TexCoord0[0] - v0.TexCoord0[0];
			const float dv2 = v2.TexCoord0[1] - v0.TexCoord0[1];
			const float det = du1 * dv2 - du2 * dv1;
			if (std::abs(det) < 1e-12f)
			{
				continue;
			}
			const float r = 1.0f / det;
			const Float3 dPdu = Scale(Sub(Scale(e1, dv2), Scale(e2, dv1)), r);
			const Float3 dPdv = Scale(Sub(Scale(e2, du1), Scale(e1, du2)), r);
			for (const auto k : index)
			{
				tangents[k] = Add(tangents[k], dPdu);
				bitangents[k] = Add(bitangents[k], dPdv);
			}
		}
		for (std::size_t v = 0; v < mesh.Vertices.size(); ++v)
		{
			auto& vertex = mesh.Vertices[v];
			const Float3 n = Normalize(vertex.Normal, { 0, 1, 0 });
			Float3 t = Sub(tangents[v], Scale(n, Dot(n, tangents[v])));
			t = Dot(t, t) > 1e-16f ? Normalize(t) : Perpendicular(n);
			// cross(N, T) * w must point toward decreasing v (up in the image).
			const float w = Dot(Cross(n, t), bitangents[v]) > 0.0f ? -1.0f : 1.0f;
			vertex.Tangent = { t[0], t[1], t[2], w };
		}
	}

	MeshData MakeBox(const std::array<float, 3>& h)
	{
		MeshData mesh;

		// normal, u axis, v axis (v grows down the face image).
		struct Face
		{
			Float3 Normal;
			Float3 U;
			Float3 V;
		};

		const std::array<Face, 6> faces{ { { { 1, 0, 0 }, { 0, 0, -1 }, { 0, -1, 0 } }, { { -1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 } },
			{ { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } }, { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
			{ { 0, 0, 1 }, { 1, 0, 0 }, { 0, -1, 0 } }, { { 0, 0, -1 }, { -1, 0, 0 }, { 0, -1, 0 } } } };
		for (const auto& face : faces)
		{
			const auto corner = [&](float su, float sv)
			{
				Float3 p{};
				for (int c = 0; c < 3; ++c)
				{
					p[c] = (face.Normal[c] + face.U[c] * su + face.V[c] * sv) * h[c];
				}
				return mesh.AddVertex(p, face.Normal, { (su + 1.0f) * 0.5f, (sv + 1.0f) * 0.5f });
			};
			// (-1,-1) top-left, (1,-1) top-right, (1,1) bottom-right, (-1,1) bottom-left.
			const auto a = corner(-1, 1);
			const auto b = corner(1, 1);
			const auto c = corner(1, -1);
			const auto d = corner(-1, -1);
			mesh.AddQuad(a, b, c, d);
		}
		GenerateTangents(mesh);
		return mesh;
	}

	MeshData MakePlane(float size, std::uint32_t subdivisions, float uvScale)
	{
		subdivisions = std::max(subdivisions, 1u);
		MeshData mesh;
		const float half = size * 0.5f;
		for (std::uint32_t z = 0; z <= subdivisions; ++z)
		{
			for (std::uint32_t x = 0; x <= subdivisions; ++x)
			{
				const float fx = float(x) / float(subdivisions);
				const float fz = float(z) / float(subdivisions);
				mesh.AddVertex({ -half + fx * size, 0.0f, -half + fz * size }, { 0, 1, 0 }, { fx * uvScale, fz * uvScale });
			}
		}
		const std::uint32_t row = subdivisions + 1;
		for (std::uint32_t z = 0; z < subdivisions; ++z)
		{
			for (std::uint32_t x = 0; x < subdivisions; ++x)
			{
				const std::uint32_t i = z * row + x;
				// Seen from +Y: (x, z+1) -> (x+1, z+1) -> (x+1, z) -> (x, z) is counter-clockwise.
				mesh.AddQuad(i + row, i + row + 1, i + 1, i);
			}
		}
		GenerateTangents(mesh);
		return mesh;
	}

	MeshData MakeSphere(float radius, std::uint32_t segments, std::uint32_t rings)
	{
		segments = std::max(segments, 3u);
		rings = std::max(rings, 2u);
		MeshData mesh;
		for (std::uint32_t r = 0; r <= rings; ++r)
		{
			const float v = float(r) / float(rings);
			const float theta = v * Pi; // 0 at the north pole.
			for (std::uint32_t s = 0; s <= segments; ++s)
			{
				const float u = float(s) / float(segments);
				const float phi = u * 2.0f * Pi;
				const Float3 n{ std::sin(theta) * std::sin(phi), std::cos(theta), std::sin(theta) * std::cos(phi) };
				mesh.AddVertex(Scale(n, radius), n, { u, v });
			}
		}
		const std::uint32_t row = segments + 1;
		for (std::uint32_t r = 0; r < rings; ++r)
		{
			for (std::uint32_t s = 0; s < segments; ++s)
			{
				const std::uint32_t i = r * row + s;
				if (r != 0)
				{
					mesh.AddTriangle(i, i + row, i + 1);
				}
				if (r != rings - 1)
				{
					mesh.AddTriangle(i + 1, i + row, i + row + 1);
				}
			}
		}
		GenerateTangents(mesh);
		return mesh;
	}

	namespace
	{
		// A disc cap at height y facing +Y (up) or -Y.
		void AddCap(MeshData& mesh, float radius, float y, bool up, std::uint32_t segments)
		{
			const Float3 normal{ 0, up ? 1.0f : -1.0f, 0 };
			const auto center = mesh.AddVertex({ 0, y, 0 }, normal, { 0.5f, 0.5f });
			const std::uint32_t first = static_cast<std::uint32_t>(mesh.Vertices.size());
			for (std::uint32_t s = 0; s <= segments; ++s)
			{
				const float phi = float(s) / float(segments) * 2.0f * Pi;
				const float x = std::sin(phi);
				const float z = std::cos(phi);
				mesh.AddVertex({ x * radius, y, z * radius }, normal, { 0.5f + 0.5f * x, 0.5f + (up ? 0.5f : -0.5f) * z });
			}
			for (std::uint32_t s = 0; s < segments; ++s)
			{
				if (up)
				{
					mesh.AddTriangle(center, first + s, first + s + 1);
				}
				else
				{
					mesh.AddTriangle(center, first + s + 1, first + s);
				}
			}
		}
	} // namespace

	MeshData MakeCylinder(float radius, float height, std::uint32_t segments)
	{
		segments = std::max(segments, 3u);
		MeshData mesh;
		const float half = height * 0.5f;
		for (std::uint32_t s = 0; s <= segments; ++s)
		{
			const float u = float(s) / float(segments);
			const float phi = u * 2.0f * Pi;
			const Float3 n{ std::sin(phi), 0, std::cos(phi) };
			mesh.AddVertex({ n[0] * radius, half, n[2] * radius }, n, { u, 0 });
			mesh.AddVertex({ n[0] * radius, -half, n[2] * radius }, n, { u, 1 });
		}
		for (std::uint32_t s = 0; s < segments; ++s)
		{
			const std::uint32_t i = s * 2;
			mesh.AddQuad(i + 1, i + 3, i + 2, i);
		}
		AddCap(mesh, radius, half, true, segments);
		AddCap(mesh, radius, -half, false, segments);
		GenerateTangents(mesh);
		return mesh;
	}

	MeshData MakeCone(float radius, float height, std::uint32_t segments)
	{
		segments = std::max(segments, 3u);
		MeshData mesh;
		const float half = height * 0.5f;
		const float slope = radius / height;
		for (std::uint32_t s = 0; s < segments; ++s)
		{
			const float u0 = float(s) / float(segments);
			const float u1 = float(s + 1) / float(segments);
			const float um = (u0 + u1) * 0.5f;
			const auto normalAt = [&](float u)
			{
				const float phi = u * 2.0f * Pi;
				return Normalize({ std::sin(phi), slope, std::cos(phi) });
			};
			const auto rim = [&](float u)
			{
				const float phi = u * 2.0f * Pi;
				return Float3{ std::sin(phi) * radius, -half, std::cos(phi) * radius };
			};
			const auto a = mesh.AddVertex(rim(u0), normalAt(u0), { u0, 1 });
			const auto b = mesh.AddVertex(rim(u1), normalAt(u1), { u1, 1 });
			const auto apex = mesh.AddVertex({ 0, half, 0 }, normalAt(um), { um, 0 });
			mesh.AddTriangle(a, b, apex);
		}
		AddCap(mesh, radius, -half, false, segments);
		GenerateTangents(mesh);
		return mesh;
	}

	MeshData MakeTorus(float majorRadius, float minorRadius, std::uint32_t segments, std::uint32_t sides)
	{
		segments = std::max(segments, 3u);
		sides = std::max(sides, 3u);
		MeshData mesh;
		for (std::uint32_t s = 0; s <= segments; ++s)
		{
			const float u = float(s) / float(segments);
			const float phi = u * 2.0f * Pi;
			const Float3 radial{ std::sin(phi), 0, std::cos(phi) };
			for (std::uint32_t k = 0; k <= sides; ++k)
			{
				const float v = float(k) / float(sides);
				const float theta = v * 2.0f * Pi;
				const Float3 n = Add(Scale(radial, std::cos(theta)), Float3{ 0, std::sin(theta), 0 });
				const Float3 p = Add(Scale(radial, majorRadius), Scale(n, minorRadius));
				mesh.AddVertex(p, n, { u, v });
			}
		}
		const std::uint32_t row = sides + 1;
		for (std::uint32_t s = 0; s < segments; ++s)
		{
			for (std::uint32_t k = 0; k < sides; ++k)
			{
				const std::uint32_t i = s * row + k;
				mesh.AddQuad(i, i + row, i + row + 1, i + 1);
			}
		}
		GenerateTangents(mesh);
		return mesh;
	}

	MeshData MakeCapsule(float radius, float height, std::uint32_t segments, std::uint32_t rings)
	{
		segments = std::max(segments, 3u);
		rings = std::max(rings, 2u);
		MeshData mesh;
		const float half = height * 0.5f;
		// Latitude rows: the north hemisphere (rings + 1 rows ending at the equator), then
		// the south one starting at the equator shifted down: the cylinder is the band
		// between the two equator rows.
		const std::uint32_t totalRows = 2 * (rings + 1);
		const float totalLength = height + Pi * radius;
		float traveled = 0.0f;
		for (std::uint32_t r = 0; r < totalRows; ++r)
		{
			const bool north = r <= rings;
			const std::uint32_t local = north ? r : r - (rings + 1);
			const float theta = north ? float(local) / float(rings) * Pi * 0.5f : Pi * 0.5f + float(local) / float(rings) * Pi * 0.5f;
			const float centerY = north ? half : -half;
			if (r > 0)
			{
				traveled += (r == rings + 1) ? height : radius * Pi * 0.5f / float(rings);
			}
			const float v = traveled / totalLength;
			for (std::uint32_t s = 0; s <= segments; ++s)
			{
				const float u = float(s) / float(segments);
				const float phi = u * 2.0f * Pi;
				const Float3 n{ std::sin(theta) * std::sin(phi), std::cos(theta), std::sin(theta) * std::cos(phi) };
				mesh.AddVertex({ n[0] * radius, centerY + n[1] * radius, n[2] * radius }, n, { u, v });
			}
		}
		const std::uint32_t row = segments + 1;
		for (std::uint32_t r = 0; r + 1 < totalRows; ++r)
		{
			for (std::uint32_t s = 0; s < segments; ++s)
			{
				const std::uint32_t i = r * row + s;
				if (r != 0)
				{
					mesh.AddTriangle(i, i + row, i + 1);
				}
				if (r + 2 != totalRows)
				{
					mesh.AddTriangle(i + 1, i + row, i + row + 1);
				}
			}
		}
		GenerateTangents(mesh);
		return mesh;
	}

	Swim::Assets::MeshAsset ToMeshAsset(const MeshData& mesh)
	{
		if (mesh.Vertices.empty() || mesh.Indices.empty() || mesh.Indices.size() % 3 != 0)
		{
			throw std::invalid_argument("ProceduralMeshes::ToMeshAsset needs a non-empty triangle list");
		}
		for (const auto index : mesh.Indices)
		{
			if (index >= mesh.Vertices.size())
			{
				throw std::invalid_argument("ProceduralMeshes::ToMeshAsset index out of range");
			}
		}
		using namespace Swim::Assets;
		MeshAsset asset;
		const std::uint64_t vertexBytes = mesh.Vertices.size() * sizeof(Swim::Render::StandardVertex);
		asset.VertexBytes.resize(static_cast<std::size_t>(vertexBytes));
		std::memcpy(asset.VertexBytes.data(), mesh.Vertices.data(), static_cast<std::size_t>(vertexBytes));
		asset.IndexBytes.resize(mesh.Indices.size() * sizeof(std::uint32_t));
		std::memcpy(asset.IndexBytes.data(), mesh.Indices.data(), asset.IndexBytes.size());
		asset.VertexStreams.push_back({ Swim::Render::StandardVertexStride, 0, vertexBytes });
		asset.VertexAttributes = { { VertexSemantic::Position, VertexElementFormat::Float32x3, 0, 0 },
			{ VertexSemantic::Normal, VertexElementFormat::Float32x3, 0, 12 },
			{ VertexSemantic::Tangent, VertexElementFormat::Float32x4, 0, 24 },
			{ VertexSemantic::TexCoord0, VertexElementFormat::Float32x2, 0, 40 } };
		asset.IndexFormat = IndexElementFormat::UInt32;
		MeshPrimitive primitive;
		primitive.FirstIndex = 0;
		primitive.IndexCount = static_cast<std::uint32_t>(mesh.Indices.size());
		primitive.Bounds.Min = mesh.BoundsMin();
		primitive.Bounds.Max = mesh.BoundsMax();
		asset.Primitives.push_back(primitive);
		asset.Lods.push_back({ 0, 1, 0.0f });
		asset.Bounds = primitive.Bounds;
		return asset;
	}

	SkinnedMeshData MakeSkinnedColumn(float radius, float height, std::uint32_t joints, std::uint32_t segments, std::uint32_t ringsPerJoint)
	{
		joints = std::clamp(joints, 1u, 64u);
		segments = std::max(segments, 3u);
		ringsPerJoint = std::max(ringsPerJoint, 1u);
		SkinnedMeshData result;
		result.JointCount = joints;
		result.Height = height;
		auto& mesh = result.Mesh;
		const std::uint32_t rows = joints * ringsPerJoint + 1;
		const float segmentLength = height / float(joints);
		const auto influence = [&](float y)
		{
			// Linear between the two joints around y (joint j sits at j * segmentLength).
			const float t = std::clamp(y / segmentLength - 0.5f, 0.0f, float(joints - 1));
			const auto j0 = static_cast<std::uint32_t>(std::floor(t));
			const std::uint32_t j1 = std::min(j0 + 1, joints - 1);
			const float w1 = j1 == j0 ? 0.0f : t - float(j0);
			Swim::Render::SkinInfluence skin;
			skin.Joints = { static_cast<std::uint16_t>(j0), static_cast<std::uint16_t>(j1), 0, 0 };
			skin.Weights = { 1.0f - w1, w1, 0.0f, 0.0f };
			return skin;
		};
		for (std::uint32_t r = 0; r < rows; ++r)
		{
			const float v = float(r) / float(rows - 1);
			const float y = v * height;
			// Taper toward the tip.
			const float ringRadius = radius * (1.0f - 0.6f * v);
			for (std::uint32_t s = 0; s <= segments; ++s)
			{
				const float u = float(s) / float(segments);
				const float phi = u * 2.0f * Pi;
				const Float3 n{ std::sin(phi), 0.6f * radius / height, std::cos(phi) };
				mesh.AddVertex({ std::sin(phi) * ringRadius, y, std::cos(phi) * ringRadius }, Normalize(n), { u, 1.0f - v });
				result.Influences.push_back(influence(y));
			}
		}
		const std::uint32_t row = segments + 1;
		for (std::uint32_t r = 0; r + 1 < rows; ++r)
		{
			for (std::uint32_t s = 0; s < segments; ++s)
			{
				const std::uint32_t i = r * row + s;
				mesh.AddQuad(i, i + 1, i + row + 1, i + row);
			}
		}
		// Tip cap (a fan to the top center).
		const auto tip = mesh.AddVertex({ 0, height, 0 }, { 0, 1, 0 }, { 0.5f, 0.0f });
		result.Influences.push_back(influence(height));
		const std::uint32_t top = (rows - 1) * row;
		for (std::uint32_t s = 0; s < segments; ++s)
		{
			mesh.AddTriangle(top + s, top + s + 1, tip);
		}
		GenerateTangents(mesh);
		return result;
	}
} // namespace Engine::ProceduralMeshes
