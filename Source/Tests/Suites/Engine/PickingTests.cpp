#include "Engine/Components/Pickable.h"
#include "Engine/Components/Transform.h"
#include "Engine/Math/Ray.h"
#include "Engine/Systems/Camera/Camera.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Physics/RigidBody.h"
#include "Engine/Systems/Scene/Picking/ScenePicking.h"
#include "Tests/Framework/Test.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

using namespace Engine;
namespace Q = Engine::RayQueries;

namespace
{

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1.0e-4f)
	{
		return glm::length(a - b) <= tolerance;
	}

} // namespace

SWIM_TEST("Engine.Picking", "RayQueriesHitPlanesSpheresBoxesCapsulesAndTriangles")
{
	const Ray down{ { 0.0f, 5.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } };
	// Plane: either side, never behind the origin or parallel.
	auto hit = Q::Plane(down, { 0, 1, 0 }, { 0, 1, 0 });
	SWIM_REQUIRE(hit.has_value());
	SWIM_CHECK_NEAR(hit->T, 4.0f, 1e-5f);
	SWIM_CHECK(Near(hit->Normal, { 0, 1, 0 }));
	SWIM_CHECK(Q::Plane(down, { 0, 1, 0 }, { 0, -1, 0 }).has_value()); // The normal is flipped to face the ray.
	SWIM_CHECK(!Q::Plane(down, { 0, 9, 0 }, { 0, 1, 0 }).has_value()); // Behind.
	SWIM_CHECK(!Q::Plane(down, { 0, 1, 0 }, { 1, 0, 0 }).has_value()); // Parallel.

	// Sphere: front hit; from inside, the far wall; a miss.
	hit = Q::Sphere(down, { 0, 1, 0 }, 1.0f);
	SWIM_REQUIRE(hit.has_value());
	SWIM_CHECK_NEAR(hit->T, 3.0f, 1e-5f);
	SWIM_CHECK(Near(hit->Point, { 0, 2, 0 }));
	hit = Q::Sphere({ { 0, 1, 0 }, { 1, 0, 0 } }, { 0, 1, 0 }, 1.0f);
	SWIM_REQUIRE(hit.has_value());
	SWIM_CHECK_NEAR(hit->T, 1.0f, 1e-5f);
	SWIM_CHECK(Near(hit->Normal, { -1, 0, 0 })); // Facing back toward the origin.
	SWIM_CHECK(!Q::Sphere(down, { 3, 1, 0 }, 1.0f).has_value());

	// Axis-aligned and transformed boxes.
	hit = Q::Aabb(down, { -1, 0, -1 }, { 1, 2, 1 });
	SWIM_REQUIRE(hit.has_value());
	SWIM_CHECK_NEAR(hit->T, 3.0f, 1e-5f);
	const glm::mat4 world = glm::translate(glm::mat4(1.0f), { 5, 0, 0 }) * glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), { 0, 1, 0 }) *
		glm::scale(glm::mat4(1.0f), { 2, 1, 1 });
	// A ray along -x through the rotated, scaled box's centre line: the local |z| <= 1 face
	// limits it (local z = x / sqrt 2), at x = 5 + sqrt 2.
	hit = Q::Box({ { 20, 0.2f, 0 }, { -1, 0, 0 } }, world, glm::vec3(1.0f));
	SWIM_REQUIRE(hit.has_value());
	SWIM_CHECK_NEAR(hit->Point.x, 5.0f + std::sqrt(2.0f), 1e-3f);
	SWIM_CHECK(hit->Normal.x > 0.0f);

	// Transformed sphere (an ellipsoid when scaled) and capsule.
	hit = Q::TransformedSphere(down, glm::scale(glm::mat4(1.0f), { 1, 3, 1 }), 0.5f);
	SWIM_REQUIRE(hit.has_value());
	SWIM_CHECK_NEAR(hit->Point.y, 1.5f, 1e-4f);
	hit = Q::TransformedCapsule({ { 5, 0.4f, 0 }, { -1, 0, 0 } }, glm::mat4(1.0f), 0.25f, 0.5f);
	SWIM_REQUIRE(hit.has_value());
	SWIM_CHECK_NEAR(hit->Point.x, 0.25f, 1e-4f); // The cylinder part.
	hit = Q::TransformedCapsule(down, glm::mat4(1.0f), 0.25f, 0.5f);
	SWIM_REQUIRE(hit.has_value());
	SWIM_CHECK_NEAR(hit->Point.y, 0.75f, 1e-4f); // The top cap.

	// Triangle (both faces) and the closest point parameter.
	hit = Q::Triangle(down, { -1, 0, -1 }, { 1, 0, -1 }, { 0, 0, 1 });
	SWIM_REQUIRE(hit.has_value());
	SWIM_CHECK_NEAR(hit->T, 5.0f, 1e-5f);
	SWIM_CHECK(!Q::Triangle(down, { 2, 0, -1 }, { 3, 0, -1 }, { 2.5f, 0, 1 }).has_value());
	SWIM_CHECK_NEAR(Q::ClosestT(down, { 3, 2, 0 }), 3.0f, 1e-5f);
	SWIM_CHECK_EQUAL(Q::ClosestT(down, { 0, 9, 0 }), 0.0f);
}

SWIM_TEST("Engine.Picking", "ScreenAndWorldRoundTripThroughTheCamera")
{
	Camera camera;
	camera.SetAspect(16.0f / 9.0f);
	camera.SetFieldOfView(60.0f);
	camera.LookAt({ 1, 2, 8 }, { 0, 0.5f, 0 });
	constexpr float width = 1600.0f;
	constexpr float height = 900.0f;
	// The centre pixel looks along the forward axis.
	const auto centre = camera.ScreenPointToRay(width * 0.5f, height * 0.5f, width, height);
	SWIM_CHECK(glm::dot(centre.Direction, camera.GetForward()) > 0.99999f);
	// A world point projects to a pixel whose ray passes through it, and back.
	const glm::vec3 point{ -1.5f, 1.2f, -2.0f };
	const auto screen = camera.WorldToScreen(point, width, height);
	SWIM_REQUIRE(screen.has_value());
	const auto ray = camera.ScreenPointToRay(screen->x, screen->y, width, height);
	const float t = Q::ClosestT(ray, point);
	SWIM_CHECK(Near(ray.At(t), point, 1e-3f));
	SWIM_CHECK(Near(camera.ScreenToWorldAtDepth(screen->x, screen->y, screen->z, width, height), point, 1e-3f));
	SWIM_CHECK(Near(camera.ScreenToWorld(screen->x, screen->y, glm::length(point - camera.GetPosition()), width, height), point, 1e-3f));
	// Behind the camera: nothing.
	SWIM_CHECK(!camera.WorldToScreen(camera.GetPosition() - camera.GetForward(), width, height).has_value());
	// Pixels and NDC.
	SWIM_CHECK(Near(glm::vec3(Camera::ScreenToNdc(0.0f, 0.0f, width, height), 0.0f), { -1, 1, 0 }));
	SWIM_CHECK(Near(glm::vec3(Camera::NdcToScreen({ 1.0f, -1.0f }, width, height), 0.0f), { width, height, 0 }));
	// The camera system uses the render surface.
	CameraSystem cameras;
	cameras.SetSurfaceSize(1600, 900);
	cameras.GetCamera() = camera;
	const auto fromSystem = cameras.WorldToScreen(point);
	SWIM_REQUIRE(fromSystem.has_value());
	SWIM_CHECK(Near(*fromSystem, *screen, 1e-3f));
}

SWIM_TEST("Engine.Picking", "ScenePickingReturnsTheClosestEntityAndHonoursFilters")
{
	entt::registry registry;
	const auto add = [&](const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f))
	{
		const auto e = registry.create();
		registry.emplace<Transform>(e, Transform(position, scale));
		return e;
	};
	// Along -z from z = 10: a pickable sphere at z = 0, a box collider at z = -4 (behind it),
	// a scaled capsule collider at z = 4 (in front), off to the side a sphere collider.
	const auto sphere = add({ 0, 0, 0 });
	registry.emplace<Pickable>(sphere);
	const auto box = add({ 0, 0, -4 }, { 2, 2, 2 });
	Rigidbody boxBody;
	boxBody.collider.type = ColliderType::Box;
	registry.emplace<Rigidbody>(box, boxBody);
	const auto capsule = add({ 0, 0, 4 });
	Rigidbody capsuleBody;
	capsuleBody.collider.type = ColliderType::Capsule;
	capsuleBody.collider.capsule = { 0.3f, 0.5f };
	registry.emplace<Rigidbody>(capsule, capsuleBody);
	const auto aside = add({ 5, 0, 0 });
	Rigidbody asideBody;
	asideBody.collider.type = ColliderType::Sphere;
	registry.emplace<Rigidbody>(aside, asideBody);

	const Ray ray{ { 0, 0, 10 }, { 0, 0, -2 } }; // Any length: distances come back in metres.
	auto all = ScenePicking::PickAll(registry, ray);
	SWIM_REQUIRE_EQUAL(all.size(), std::size_t{ 3 });
	SWIM_CHECK(all[0].Entity == capsule && all[1].Entity == sphere && all[2].Entity == box);
	SWIM_CHECK_NEAR(all[0].Distance, 5.7f, 1e-4f);
	SWIM_CHECK_NEAR(all[1].Distance, 9.5f, 1e-4f);
	SWIM_CHECK_NEAR(all[2].Distance, 13.0f, 1e-4f);
	SWIM_CHECK(Near(all[1].Normal, { 0, 0, 1 }));

	// Filters: an entity filter, colliders off, a layer mask, the max distance.
	PickOptions options;
	options.Filter = [capsule](entt::entity e)
	{
		return e != capsule;
	};
	auto closest = ScenePicking::PickClosest(registry, ray, options);
	SWIM_REQUIRE(closest.has_value());
	SWIM_CHECK(closest->Entity == sphere);
	options = {};
	options.Colliders = false;
	closest = ScenePicking::PickClosest(registry, ray, options);
	SWIM_REQUIRE(closest.has_value());
	SWIM_CHECK(closest->Entity == sphere);
	options.LayerMask = 2u;
	SWIM_CHECK(!ScenePicking::PickClosest(registry, ray, options).has_value());
	options = {};
	options.MaxDistance = 5.0f;
	SWIM_CHECK(!ScenePicking::PickClosest(registry, ray, options).has_value());

	// A Pickable replaces the collider shape: a big sphere around the box is hit earlier.
	Pickable wide;
	wide.Radius = 1.5f;
	registry.emplace<Pickable>(box, wide);
	all = ScenePicking::PickAll(registry, ray);
	SWIM_REQUIRE_EQUAL(all.size(), std::size_t{ 3 });
	SWIM_CHECK_NEAR(all[2].Distance, 11.0f, 1e-4f); // Radius 1.5 x scale 2 = 3 m in front of z = -4.

	// Picking at a screen pixel through the camera system.
	CameraSystem cameras;
	cameras.SetSurfaceSize(800, 600);
	cameras.GetCamera().LookAt({ 0, 0, 10 }, { 0, 0, 0 });
	closest = ScenePicking::PickAtScreen(registry, cameras, 400.0f, 300.0f);
	SWIM_REQUIRE(closest.has_value());
	SWIM_CHECK(closest->Entity == capsule);
	SWIM_CHECK(!ScenePicking::PickAtScreen(registry, cameras, 5.0f, 5.0f).has_value());
}
