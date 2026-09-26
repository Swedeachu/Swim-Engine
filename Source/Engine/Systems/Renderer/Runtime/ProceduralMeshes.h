#pragma once

#include "Engine/Assets/MeshAsset.h"
#include "Engine/Systems/Renderer/ForwardPlus/StandardVertex.h"
#include "Engine/Systems/Renderer/Skinning/SkinningReference.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Engine::ProceduralMeshes
{
	// CPU geometry in the renderer's StandardVertex layout (48 bytes: position, normal,
	// tangent with the bitangent sign in w, uv), one triangle list, counter-clockwise
	// front faces seen from outside, +Y up. UV origin is the top left (v grows down).
	struct MeshData
	{
		std::vector<Swim::Render::StandardVertex> Vertices;
		std::vector<std::uint32_t> Indices;

		std::uint32_t AddVertex(const std::array<float, 3>& position, const std::array<float, 3>& normal, const std::array<float, 2>& uv);
		void AddTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c);
		void AddQuad(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d); // a b c d counter-clockwise.
		std::array<float, 3> BoundsMin() const;
		std::array<float, 3> BoundsMax() const;
	};

	// Recomputes every tangent from the UV layout (per-triangle accumulation, then
	// Gram-Schmidt against the normal). w is chosen so cross(N, T) * w points toward
	// decreasing v, the convention the Forward+ reference uses.
	void GenerateTangents(MeshData& mesh);

	// A box centered on the origin (24 vertices: flat faces, each face uv 0..1).
	MeshData MakeBox(const std::array<float, 3>& halfExtents = { 0.5f, 0.5f, 0.5f });
	// A plane on XZ facing +Y, size x size, subdivided, uv 0..uvScale.
	MeshData MakePlane(float size = 1.0f, std::uint32_t subdivisions = 1, float uvScale = 1.0f);
	// A UV sphere.
	MeshData MakeSphere(float radius = 0.5f, std::uint32_t segments = 32, std::uint32_t rings = 16);
	// A capped cylinder along Y from -height/2 to +height/2.
	MeshData MakeCylinder(float radius = 0.5f, float height = 1.0f, std::uint32_t segments = 32);
	// A capped cone along Y, base at -height/2.
	MeshData MakeCone(float radius = 0.5f, float height = 1.0f, std::uint32_t segments = 32);
	// A torus around Y.
	MeshData MakeTorus(float majorRadius = 0.4f, float minorRadius = 0.15f, std::uint32_t segments = 48, std::uint32_t sides = 24);
	// A capsule along Y: a cylinder of `height` between the hemisphere centers.
	MeshData MakeCapsule(float radius = 0.25f, float height = 0.5f, std::uint32_t segments = 24, std::uint32_t rings = 8);

	// The same mesh as a MeshAsset (one stream, one primitive, one LOD), ready to publish
	// in the AssetSystem and stream through AssetResidencyService.
	Swim::Assets::MeshAsset ToMeshAsset(const MeshData& mesh);

	// A segmented column for GPU skinning: `joints` bones stacked along +Y (joint j's
	// origin at y = j * height / joints, bind pose identity translations), each vertex
	// weighted linearly between the two nearest joints.
	struct SkinnedMeshData
	{
		MeshData Mesh;
		std::vector<Swim::Render::SkinInfluence> Influences;
		std::uint32_t JointCount = 0;
		float Height = 0.0f;
	};

	SkinnedMeshData MakeSkinnedColumn(
		float radius = 0.2f, float height = 2.0f, std::uint32_t joints = 4, std::uint32_t segments = 16, std::uint32_t ringsPerJoint = 4);
} // namespace Engine::ProceduralMeshes
