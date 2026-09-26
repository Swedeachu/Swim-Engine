#include "Engine/Systems/Camera/Camera.h"
#include "Engine/Systems/Camera/FlyCameraController.h"
#include "Engine/Systems/Renderer/Runtime/ProceduralMeshes.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Game/Behaviors/TentacleAnimator.h"
#include "Tests/Framework/Test.h"

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

using Engine::Camera;
using Engine::FlyCameraController;
namespace Meshes = Engine::ProceduralMeshes;

namespace
{
	glm::vec3 ToVec(const std::array<float, 3>& value)
	{
		return { value[0], value[1], value[2] };
	}

	// Every triangle is counter-clockwise seen from the side its vertex normals face.
	bool WindingMatchesNormals(const Meshes::MeshData& mesh)
	{
		for (std::size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
		{
			const auto& a = mesh.Vertices[mesh.Indices[i]];
			const auto& b = mesh.Vertices[mesh.Indices[i + 1]];
			const auto& c = mesh.Vertices[mesh.Indices[i + 2]];
			const glm::vec3 face = glm::cross(ToVec(b.Position) - ToVec(a.Position), ToVec(c.Position) - ToVec(a.Position));
			if (glm::dot(face, face) < 1e-12f)
			{
				continue; // Degenerate (pole) triangles carry no orientation.
			}
			const glm::vec3 normal = ToVec(a.Normal) + ToVec(b.Normal) + ToVec(c.Normal);
			if (glm::dot(face, normal) <= 0.0f)
			{
				return false;
			}
		}
		return true;
	}

	bool IndicesInRange(const Meshes::MeshData& mesh)
	{
		for (const std::uint32_t index : mesh.Indices)
		{
			if (index >= mesh.Vertices.size())
			{
				return false;
			}
		}
		return mesh.Indices.size() % 3 == 0 && !mesh.Indices.empty();
	}

	bool TangentsAreUnitAndOrthogonal(const Meshes::MeshData& mesh)
	{
		for (const auto& vertex : mesh.Vertices)
		{
			const glm::vec3 n = ToVec(vertex.Normal);
			const glm::vec3 t(vertex.Tangent[0], vertex.Tangent[1], vertex.Tangent[2]);
			if (std::abs(glm::length(n) - 1.0f) > 1e-3f || std::abs(glm::length(t) - 1.0f) > 1e-3f)
			{
				return false;
			}
			if (std::abs(glm::dot(n, t)) > 1e-3f || std::abs(std::abs(vertex.Tangent[3]) - 1.0f) > 1e-6f)
			{
				return false;
			}
		}
		return true;
	}
} // namespace

SWIM_TEST("Engine.Camera", "ProjectionIsTheRenderersReverseZAndViewLooksDownMinusZ")
{
	Camera camera;
	camera.SetFieldOfView(60.0f);
	camera.SetClipPlanes(0.1f, 500.0f);
	camera.SetAspect(16.0f / 9.0f);
	const auto expected = Swim::Render::PerspectiveReverseZRowMajor(glm::radians(60.0f), 16.0f / 9.0f, 0.1f);
	const auto actual = camera.GetProjectionRowMajor();
	for (std::size_t i = 0; i < 16; ++i)
	{
		SWIM_CHECK_NEAR(actual[i], expected[i], 1e-5f);
	}

	// Reverse-Z: the near plane maps to depth 1, farther points toward 0.
	const glm::mat4 projection = camera.GetProjectionMatrix();
	const auto depthAt = [&](float distance)
	{
		const glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, -distance, 1.0f);
		return clip.z / clip.w;
	};
	SWIM_CHECK_NEAR(depthAt(0.1f), 1.0f, 1e-5f);
	SWIM_CHECK(depthAt(10.0f) < depthAt(1.0f));
	SWIM_CHECK(depthAt(1000.0f) > 0.0f);

	// Right-handed, +Y up, looking down -Z at yaw/pitch 0; the view matrix is the inverse pose.
	camera.SetPosition({ 1.0f, 2.0f, 3.0f });
	camera.SetYawPitch(0.0f, 0.0f);
	SWIM_CHECK_NEAR(camera.GetForward().z, -1.0f, 1e-5f);
	SWIM_CHECK_NEAR(camera.GetUp().y, 1.0f, 1e-5f);
	SWIM_CHECK_NEAR(camera.GetRight().x, 1.0f, 1e-5f);
	const glm::vec4 eye = camera.GetViewMatrix() * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f);
	SWIM_CHECK_NEAR(glm::length(glm::vec3(eye)), 0.0f, 1e-5f);
	SWIM_CHECK_THROWS(camera.SetFieldOfView(0.0f), std::invalid_argument);
	SWIM_CHECK_THROWS(camera.SetClipPlanes(1.0f, 0.5f), std::invalid_argument);
}

SWIM_TEST("Engine.FlyCamera", "LookTurnsByMouseDeltaTimesSensitivity")
{
	Camera camera;
	camera.SetYawPitch(10.0f, 5.0f);
	FlyCameraController::Settings settings;
	FlyCameraController::FrameInput input;
	input.MouseDeltaX = 20.0f;
	input.MouseDeltaY = -10.0f;

	FlyCameraController::Apply(camera, settings, input, 0.016f); // Not looking: nothing turns.
	SWIM_CHECK_NEAR(camera.GetYaw(), 10.0f, 1e-4f);
	SWIM_CHECK_NEAR(camera.GetPitch(), 5.0f, 1e-4f);

	input.Look = true;
	FlyCameraController::Apply(camera, settings, input, 0.016f);
	SWIM_CHECK_NEAR(camera.GetYaw(), 10.0f - 20.0f * settings.MouseSensitivity, 1e-4f);
	SWIM_CHECK_NEAR(camera.GetPitch(), 5.0f + 10.0f * settings.MouseSensitivity, 1e-4f);
}

SWIM_TEST("Engine.FlyCamera", "MovementFollowsTheViewAndWorldUp")
{
	Camera camera;
	camera.SetPosition({ 1.0f, 2.0f, 3.0f });
	camera.SetYawPitch(30.0f, -20.0f);
	FlyCameraController::Settings settings;
	settings.MoveSpeed = 4.0f;

	FlyCameraController::FrameInput input;
	input.Forward = true;
	const glm::vec3 start = camera.GetPosition();
	const glm::vec3 forward = camera.GetForward();
	FlyCameraController::Apply(camera, settings, input, 0.5f);
	const glm::vec3 moved = camera.GetPosition() - start;
	SWIM_CHECK_NEAR(glm::length(moved), 2.0f, 1e-4f);
	SWIM_CHECK(glm::dot(glm::normalize(moved), forward) > 0.9999f);

	// Up is world up whatever the pitch; boost multiplies the speed.
	input = {};
	input.Up = true;
	input.Boost = true;
	const glm::vec3 before = camera.GetPosition();
	FlyCameraController::Apply(camera, settings, input, 0.25f);
	SWIM_CHECK_NEAR(camera.GetPosition().y - before.y, 4.0f * settings.BoostMultiplier * 0.25f, 1e-4f);
	SWIM_CHECK_NEAR(camera.GetPosition().x, before.x, 1e-5f);

	// Opposite keys cancel; diagonals are normalized.
	input = {};
	input.Forward = input.Back = true;
	const glm::vec3 still = camera.GetPosition();
	FlyCameraController::Apply(camera, settings, input, 1.0f);
	SWIM_CHECK_NEAR(glm::length(camera.GetPosition() - still), 0.0f, 1e-6f);
	input = {};
	input.Forward = input.Right = true;
	FlyCameraController::Apply(camera, settings, input, 1.0f);
	SWIM_CHECK_NEAR(glm::length(camera.GetPosition() - still), 4.0f, 1e-4f);
}

SWIM_TEST("Engine.FlyCamera", "WheelScalesSpeedOnlyWhileLookingWithinLimits")
{
	Camera camera;
	FlyCameraController::Settings settings;
	FlyCameraController::FrameInput input;
	input.Wheel = 3.0f;
	FlyCameraController::Apply(camera, settings, input, 0.0f);
	SWIM_CHECK_NEAR(settings.MoveSpeed, 5.0f, 1e-6f);

	input.Look = true;
	FlyCameraController::Apply(camera, settings, input, 0.0f);
	SWIM_CHECK_NEAR(settings.MoveSpeed, 5.0f * std::pow(1.2f, 3.0f), 1e-4f);

	input.Wheel = 1000.0f;
	FlyCameraController::Apply(camera, settings, input, 0.0f);
	SWIM_CHECK_NEAR(settings.MoveSpeed, settings.MaxSpeed, 1e-4f);
	input.Wheel = -1000.0f;
	FlyCameraController::Apply(camera, settings, input, 0.0f);
	SWIM_CHECK_NEAR(settings.MoveSpeed, settings.MinSpeed, 1e-4f);
}

SWIM_TEST("Engine.ProceduralMeshes", "EveryShapeIsClosedWoundOutwardAndTangentSpaceIsValid")
{
	const std::array<Meshes::MeshData, 7> shapes{ Meshes::MakeBox(), Meshes::MakePlane(2.0f, 4, 2.0f), Meshes::MakeSphere(),
		Meshes::MakeCylinder(), Meshes::MakeCone(), Meshes::MakeTorus(), Meshes::MakeCapsule() };
	const std::array<const char*, 7> names{ "box", "plane", "sphere", "cylinder", "cone", "torus", "capsule" };
	for (std::size_t i = 0; i < shapes.size(); ++i)
	{
		SWIM_CHECK_MESSAGE(IndicesInRange(shapes[i]), names[i]);
		SWIM_CHECK_MESSAGE(WindingMatchesNormals(shapes[i]), names[i]);
		SWIM_CHECK_MESSAGE(TangentsAreUnitAndOrthogonal(shapes[i]), names[i]);
	}
}

SWIM_TEST("Engine.ProceduralMeshes", "ShapesHaveTheDocumentedBoundsAndCounts")
{
	const auto box = Meshes::MakeBox({ 1.0f, 2.0f, 3.0f });
	SWIM_CHECK_EQUAL(box.Vertices.size(), std::size_t{ 24 });
	SWIM_CHECK_EQUAL(box.Indices.size(), std::size_t{ 36 });
	SWIM_CHECK_NEAR(box.BoundsMax()[1], 2.0f, 1e-6f);
	SWIM_CHECK_NEAR(box.BoundsMin()[2], -3.0f, 1e-6f);

	const auto plane = Meshes::MakePlane(10.0f, 2, 1.0f);
	SWIM_CHECK_EQUAL(plane.Vertices.size(), std::size_t{ 9 });
	SWIM_CHECK_EQUAL(plane.Indices.size(), std::size_t{ 2 * 2 * 6 });
	SWIM_CHECK_NEAR(plane.BoundsMax()[0], 5.0f, 1e-6f);
	SWIM_CHECK_NEAR(plane.BoundsMax()[1], 0.0f, 1e-6f);
	for (const auto& vertex : plane.Vertices)
	{
		SWIM_CHECK_NEAR(vertex.Normal[1], 1.0f, 1e-6f);
	}

	const auto sphere = Meshes::MakeSphere(0.5f);
	for (const auto& vertex : sphere.Vertices)
	{
		SWIM_CHECK_NEAR(glm::length(ToVec(vertex.Position)), 0.5f, 1e-4f);
	}

	const auto capsule = Meshes::MakeCapsule(0.25f, 0.5f);
	SWIM_CHECK_NEAR(capsule.BoundsMax()[1], 0.5f, 1e-4f);
	SWIM_CHECK_NEAR(capsule.BoundsMin()[1], -0.5f, 1e-4f);
}

SWIM_TEST("Engine.ProceduralMeshes", "TangentsPointAlongIncreasingU")
{
	// On the +Y plane u grows along +X, so the tangent is +X.
	const auto plane = Meshes::MakePlane(1.0f, 1, 1.0f);
	for (const auto& vertex : plane.Vertices)
	{
		SWIM_CHECK(vertex.Tangent[0] > 0.999f);
	}
}

SWIM_TEST("Engine.ProceduralMeshes", "MeshAssetsCarryOneStandardVertexStream")
{
	const auto sphere = Meshes::MakeSphere(0.5f, 16, 8);
	const auto asset = Meshes::ToMeshAsset(sphere);
	SWIM_REQUIRE_EQUAL(asset.VertexStreams.size(), std::size_t{ 1 });
	SWIM_CHECK_EQUAL(asset.VertexBytes.size(), sphere.Vertices.size() * Swim::Render::StandardVertexStride);
	SWIM_CHECK_EQUAL(asset.IndexBytes.size(), sphere.Indices.size() * sizeof(std::uint32_t));
	SWIM_REQUIRE_EQUAL(asset.Primitives.size(), std::size_t{ 1 });
	SWIM_REQUIRE_EQUAL(asset.Lods.size(), std::size_t{ 1 });
	SWIM_CHECK_NEAR(asset.Bounds.Max[1], 0.5f, 1e-4f);
}

SWIM_TEST("Engine.ProceduralMeshes", "SkinnedColumnWeightsAreNormalizedAndFollowHeight")
{
	const auto column = Meshes::MakeSkinnedColumn(0.2f, 2.0f, 4, 12, 3);
	SWIM_CHECK_EQUAL(column.JointCount, 4u);
	SWIM_REQUIRE_EQUAL(column.Influences.size(), column.Mesh.Vertices.size());
	for (std::size_t i = 0; i < column.Influences.size(); ++i)
	{
		const auto& influence = column.Influences[i];
		float sum = 0.0f;
		for (std::size_t k = 0; k < 4; ++k)
		{
			sum += influence.Weights[k];
			SWIM_CHECK(influence.Joints[k] < column.JointCount);
		}
		SWIM_CHECK_NEAR(sum, 1.0f, 1e-4f);
	}
	// The bottom ring belongs to joint 0 only; the top ring to the last joint.
	float lowest = 1e9f;
	float highest = -1e9f;
	std::size_t bottom = 0;
	std::size_t top = 0;
	for (std::size_t i = 0; i < column.Mesh.Vertices.size(); ++i)
	{
		const float y = column.Mesh.Vertices[i].Position[1];
		if (y < lowest)
		{
			lowest = y;
			bottom = i;
		}
		if (y > highest)
		{
			highest = y;
			top = i;
		}
	}
	SWIM_CHECK_NEAR(highest - lowest, 2.0f, 1e-3f);
	const auto dominant = [](const Swim::Render::SkinInfluence& influence)
	{
		std::size_t best = 0;
		for (std::size_t k = 1; k < 4; ++k)
		{
			best = influence.Weights[k] > influence.Weights[best] ? k : best;
		}
		return influence.Joints[best];
	};
	SWIM_CHECK_EQUAL(dominant(column.Influences[bottom]), std::uint16_t{ 0 });
	SWIM_CHECK_EQUAL(dominant(column.Influences[top]), std::uint16_t{ 3 });
}

SWIM_TEST("Game.TentacleAnimator", "PaletteIsARigidChainOfEqualSegments")
{
	constexpr std::uint32_t joints = 5;
	constexpr float height = 2.0f;
	constexpr float segment = height / joints;
	const auto palette = Game::TentacleAnimator::ComputePalette(joints, height, 0.7f, 0.3f);
	SWIM_REQUIRE_EQUAL(palette.size(), std::size_t{ joints });
	glm::vec3 previous(0.0f);
	for (std::uint32_t j = 0; j < joints; ++j)
	{
		const auto& m = palette[j];
		// Row-major 3x4: the 3x3 part is a rotation (orthonormal rows).
		for (std::size_t r = 0; r < 3; ++r)
		{
			SWIM_CHECK_NEAR(glm::length(glm::vec3(m[r * 4 + 0], m[r * 4 + 1], m[r * 4 + 2])), 1.0f, 1e-3f);
		}
		// Where the joint's bind origin (0, j * segment, 0) ends up.
		const float y = segment * static_cast<float>(j);
		const glm::vec3 origin(m[1] * y + m[3], m[5] * y + m[7], m[9] * y + m[11]);
		if (j == 0)
		{
			SWIM_CHECK_NEAR(glm::length(origin), 0.0f, 1e-5f); // The root stays planted.
		}
		else
		{
			SWIM_CHECK_NEAR(glm::length(origin - previous), segment, 1e-4f);
		}
		previous = origin;
	}
	// The pose moves over time.
	const auto later = Game::TentacleAnimator::ComputePalette(joints, height, 1.7f, 0.3f);
	float difference = 0.0f;
	for (std::size_t j = 0; j < palette.size(); ++j)
	{
		for (std::size_t k = 0; k < 12; ++k)
		{
			difference += std::abs(palette[j][k] - later[j][k]);
		}
	}
	SWIM_CHECK(difference > 1e-3f);
	SWIM_CHECK(Game::TentacleAnimator::ComputePalette(0, height, 0.0f, 0.0f).empty());
}
