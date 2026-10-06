#include "Engine/Systems/Renderer/Reflections/PlanarReflections.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <vector>

using namespace Swim::Render;
namespace PR = Swim::Render::PlanarReflections;

namespace
{

	using F3 = PR::Float3;

	F3 Sub(const F3& a, const F3& b)
	{
		return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
	}

	F3 Add(const F3& a, const F3& b)
	{
		return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
	}

	F3 Mul(const F3& a, float s)
	{
		return { a[0] * s, a[1] * s, a[2] * s };
	}

	float Dot(const F3& a, const F3& b)
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	}

	F3 Cross(const F3& a, const F3& b)
	{
		return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
	}

	F3 Normalize(const F3& a)
	{
		return Mul(a, 1.0f / std::sqrt(Dot(a, a)));
	}

	float Distance(const F3& a, const F3& b)
	{
		const auto d = Sub(a, b);
		return std::sqrt(Dot(d, d));
	}

	// A 1920 x 1080, 60-degree camera at `position` looking at `target`.
	PR::ViewCamera Camera(const F3& position, const F3& target)
	{
		const F3 forward = Normalize(Sub(target, position));
		const F3 right = Normalize(Cross(forward, { 0.0f, 1.0f, 0.0f }));
		const F3 up = Cross(right, forward);
		PR::ViewCamera camera;
		camera.Position = position;
		camera.Forward = forward;
		camera.VerticalFov = 1.0471976f;
		camera.ViewportWidth = 1920.0f;
		camera.ViewportHeight = 1080.0f;
		camera.ViewProjection =
			MultiplyRowMajor(PerspectiveReverseZRowMajor(camera.VerticalFov, 1920.0f / 1080.0f, 0.1f), PR::ViewMatrix(position, right, up, forward));
		return camera;
	}

	// A mirror floor (y = 0), 4 m across.
	PlanarReflectorDesc Floor(std::uint64_t key = 1)
	{
		PlanarReflectorDesc floor;
		floor.Key = key;
		floor.OwnerObjectId = std::uint32_t(key) + 1u;
		floor.Shape = PlanarReflectorShape::Plane;
		floor.PlaneHalfExtents = { 2.0f, 2.0f };
		return floor;
	}

	PlanarReflectorDesc Box(std::uint64_t key, const F3& center)
	{
		PlanarReflectorDesc box;
		box.Key = key;
		box.OwnerObjectId = std::uint32_t(key) + 1u;
		box.Shape = PlanarReflectorShape::Box;
		box.World = { 1, 0, 0, center[0], 0, 1, 0, center[1], 0, 0, 1, center[2] };
		return box;
	}

	// The world ray of a capture texel coordinate.
	F3 CaptureRay(const PR::Capture& capture, const std::array<float, 2>& uv)
	{
		const float ndcX = uv[0] * 2.0f - 1.0f;
		const float ndcY = 1.0f - uv[1] * 2.0f;
		const auto& f = capture.FrustumScale;
		const float x = (ndcX + f[2]) / f[0];
		const float y = (ndcY + f[3]) / f[1];
		return Normalize(Add(Add(Mul(capture.Right, x), Mul(capture.Up, y)), capture.Forward));
	}

	// The scene the captures "see": a wall at z = -3 facing +z (sky elsewhere).
	float WallDistance(const F3& origin, const F3& direction)
	{
		return direction[2] < -1.0e-4f ? (-3.0f - origin[2]) / direction[2] : PR::DistanceSky;
	}

	F3 Reflect(const F3& v, const F3& n)
	{
		return Sub(v, Mul(n, 2.0f * Dot(v, n)));
	}

} // namespace

// The capture through a mirror maps a point of the mirror to what its reflection shows: the
// lookup hits the same wall point as the true mirror ray, and still does (reprojected through
// the captured distances) after the camera moved.
SWIM_TEST("Render.PlanarReflections", "LookupMatchesTheMirrorRayAndReprojectsAfterCameraMotion")
{
	PR::Planner planner;
	PlanarReflectionSettings settings;
	const std::vector<PlanarReflectorDesc> reflectors{ Floor() };
	const auto cameraA = Camera({ 0.0f, 2.0f, 5.0f }, { 0.0f, 0.0f, 0.0f });
	const auto& plan = planner.Update(reflectors, cameraA, 1, 0.0, settings);
	SWIM_REQUIRE_EQUAL(plan.Captures.size(), std::size_t{ 1 });
	SWIM_REQUIRE_EQUAL(plan.Records.size(), std::size_t{ 1 });
	const auto capture = plan.Captures[0];
	const auto record = plan.Records[0];
	SWIM_CHECK_NEAR(capture.Position[1], -2.0f, 1e-4f); // The camera mirrored in the floor.
	SWIM_CHECK_NEAR(capture.NearClip, 2.0f + 0.002f, 1e-4f);
	SWIM_CHECK_EQUAL(record.Camera[3], 0.0f); // Planes match pixels by geometry.
	const auto distance = [&](const std::array<float, 2>& uv)
	{
		return WallDistance(capture.Position, CaptureRay(capture, uv));
	};
	const F3 n{ 0.0f, 1.0f, 0.0f };
	int checked = 0;

	for (const auto& camera : { cameraA, Camera({ 0.6f, 2.4f, 4.5f }, { 0.0f, 0.0f, 0.0f }) })
	{
		for (float x = -1.0f; x <= 1.0f; x += 0.5f)
		{
			for (float z = -1.5f; z <= 1.0f; z += 0.5f)
			{
				const F3 p{ x, 0.0f, z };
				const F3 r = Reflect(Normalize(Sub(p, camera.Position)), n);
				const float t = WallDistance(p, r);

				if (t >= PR::DistanceSky)
				{
					continue;
				}

				const F3 truth = Add(p, Mul(r, t));
				const auto uv = PR::LookupUv(record, p, r, distance);
				const F3 ray = CaptureRay(capture, uv);
				const F3 found = Add(capture.Position, Mul(ray, WallDistance(capture.Position, ray)));
				SWIM_CHECK_MESSAGE(Distance(found, truth) < 0.03f, "lookup at " + std::to_string(x) + ", " + std::to_string(z));
				++checked;
			}
		}
	}

	SWIM_CHECK(checked > 20);
}

SWIM_TEST("Render.PlanarReflections", "CullsBackFacingTinyAndScreenSpaceFriendlyReflectors")
{
	PlanarReflectionSettings settings;
	const std::vector<PlanarReflectorDesc> reflectors{ Floor() };
	{
		PR::Planner planner; // Below the mirror: it faces away.
		const auto& plan = planner.Update(reflectors, Camera({ 0.0f, -2.0f, 5.0f }, { 0.0f, 0.0f, 0.0f }), 1, 0.0, settings);
		SWIM_CHECK(plan.Captures.empty() && plan.Records.empty());
		SWIM_CHECK_EQUAL(plan.Culled, 1u);
	}
	{
		PR::Planner planner; // 400 m away: a few pixels, left to SSR and probes.
		const auto& plan = planner.Update(reflectors, Camera({ 0.0f, 50.0f, 400.0f }, { 0.0f, 0.0f, 0.0f }), 1, 0.0, settings);
		SWIM_CHECK(plan.Captures.empty());
	}
	{
		PR::Planner planner; // Grazing: the mirror ray stays on screen, where SSR shows it.
		const auto& plan = planner.Update(reflectors, Camera({ 0.0f, 0.3f, 6.0f }, { 0.0f, 0.0f, -20.0f }), 1, 0.0, settings);
		SWIM_CHECK(plan.Captures.empty());
	}
	{
		PR::Planner planner; // Behind the camera.
		const auto& plan = planner.Update(reflectors, Camera({ 0.0f, 2.0f, 5.0f }, { 0.0f, 2.0f, 10.0f }), 1, 0.0, settings);
		SWIM_CHECK(plan.Captures.empty());
	}
}

SWIM_TEST("Render.PlanarReflections", "CoplanarFacesShareOneCaptureSizedByTheirScreenFootprint")
{
	PlanarReflectionSettings settings;
	// Two unit cubes side by side: their front faces share a plane (and one capture); the
	// faces seen edge-on are culled.
	const std::vector<PlanarReflectorDesc> boxes{ Box(1, { 0.0f, 0.5f, 0.0f }), Box(2, { 1.02f, 0.5f, 0.0f }) };
	PR::Planner planner;
	const auto& plan = planner.Update(boxes, Camera({ 0.51f, 0.5f, 4.0f }, { 0.51f, 0.5f, 0.0f }), 1, 0.0, settings);
	SWIM_CHECK_EQUAL(plan.Groups, 1u);
	SWIM_REQUIRE_EQUAL(plan.Captures.size(), std::size_t{ 1 });
	SWIM_CHECK(plan.Captures[0].Width > plan.Captures[0].Height); // A 2 x 1 window.
	SWIM_CHECK_NEAR(plan.Captures[0].Forward[2], 1.0f, 1e-5f);	  // Looks through the faces.

	// LOD: twice as far, about half the texels.
	PR::Planner near;
	PR::Planner far;
	const std::vector<PlanarReflectorDesc> floor{ Floor() };
	const auto nearWidth = near.Update(floor, Camera({ 0.0f, 2.0f, 4.0f }, { 0.0f, 0.0f, 0.0f }), 1, 0.0, settings).Captures.at(0).Width;
	const auto farWidth = far.Update(floor, Camera({ 0.0f, 4.0f, 8.0f }, { 0.0f, 0.0f, 0.0f }), 1, 0.0, settings).Captures.at(0).Width;
	SWIM_CHECK(farWidth < nearWidth);
	SWIM_CHECK(farWidth >= settings.MinResolution && nearWidth <= settings.AtlasResolution);
	SWIM_CHECK_EQUAL(nearWidth % 8u, 0u);
}

SWIM_TEST("Render.PlanarReflections", "CapturesAreReusedUntilMotionMoversOrAgeCallForThem")
{
	PlanarReflectionSettings settings;
	settings.MaxAgeSeconds = 0.5f;
	const std::vector<PlanarReflectorDesc> reflectors{ Floor() };
	const auto camera = Camera({ 0.0f, 2.0f, 5.0f }, { 0.0f, 0.0f, 0.0f });
	PR::Planner planner;
	SWIM_CHECK_EQUAL(planner.Update(reflectors, camera, 1, 0.0, settings).Captures.size(), std::size_t{ 1 });

	// Still: reused (the record stays), nothing rendered.
	const auto& still = planner.Update(reflectors, camera, 2, 0.1, settings);
	SWIM_CHECK(still.Captures.empty());
	SWIM_CHECK_EQUAL(still.Records.size(), std::size_t{ 1 });

	// A small camera move stays within the motion tolerance (the lookup reprojects it).
	SWIM_CHECK(planner.Update(reflectors, Camera({ 0.0005f, 2.0f, 5.0f }, { 0.0f, 0.0f, 0.0f }), 3, 0.2, settings).Captures.empty());

	// Something moving in the reflection, a big camera move, or age: re-rendered.
	const ReflectionProbeMover mover{ { 0.0f, 1.0f, -2.0f }, 0.5f };
	SWIM_CHECK_EQUAL(planner.Update(reflectors, camera, 4, 0.3, settings, std::span(&mover, 1)).Captures.size(), std::size_t{ 1 });
	SWIM_CHECK_EQUAL(planner.Update(reflectors, Camera({ 1.0f, 2.5f, 5.0f }, { 0.0f, 0.0f, 0.0f }), 5, 0.35, settings).Captures.size(),
		std::size_t{ 1 });
	SWIM_CHECK(planner.Update(reflectors, Camera({ 1.0f, 2.5f, 5.0f }, { 0.0f, 0.0f, 0.0f }), 6, 0.4, settings).Captures.empty());
	SWIM_CHECK_EQUAL(planner.Update(reflectors, Camera({ 1.0f, 2.5f, 5.0f }, { 0.0f, 0.0f, 0.0f }), 7, 1.0, settings).Captures.size(),
		std::size_t{ 1 });

	// The budget: three separate mirrors, one capture a frame, the rest wait their turn.
	settings.CapturesPerFrame = 1;
	PR::Planner budgeted;
	const std::vector<PlanarReflectorDesc> three{ Box(1, { -2.0f, 0.5f, 0.0f }), Box(2, { 0.0f, 0.5f, -1.0f }), Box(3, { 2.0f, 0.5f, -2.0f }) };
	const auto view = Camera({ 0.0f, 0.6f, 6.0f }, { 0.0f, 0.5f, 0.0f });
	const auto& first = budgeted.Update(three, view, 1, 0.0, settings);
	SWIM_CHECK_EQUAL(first.Captures.size(), std::size_t{ 1 });
	SWIM_CHECK_EQUAL(first.Records.size(), std::size_t{ 1 }); // Only captured slots are shaded with.
	const auto& second = budgeted.Update(three, view, 2, 0.01, settings);
	SWIM_CHECK_EQUAL(second.Captures.size(), std::size_t{ 1 });
	SWIM_CHECK(second.Records.size() >= 2u);
}

SWIM_TEST("Render.PlanarReflections", "SphereCapsLookFromTheCentreTowardsTheCamera")
{
	PlanarReflectionSettings settings;
	PlanarReflectorDesc sphere;
	sphere.Key = 9;
	sphere.OwnerObjectId = 10;
	sphere.Shape = PlanarReflectorShape::Sphere;
	PR::Planner planner;
	const std::vector<PlanarReflectorDesc> reflectors{ sphere };
	const auto& plan = planner.Update(reflectors, Camera({ 0.0f, 0.0f, 4.0f }, { 0.0f, 0.0f, 0.0f }), 1, 0.0, settings);
	SWIM_REQUIRE_EQUAL(plan.Captures.size(), std::size_t{ 1 });
	const auto& capture = plan.Captures[0];
	SWIM_CHECK_EQUAL(capture.ExcludedObjectId, 10u);
	SWIM_CHECK_NEAR(capture.Forward[2], 1.0f, 1e-5f);
	SWIM_CHECK_EQUAL(capture.Width, capture.Height);
	SWIM_CHECK(capture.NearClip > 0.5f);
	SWIM_CHECK_EQUAL(plan.Records[0].Camera[3], 10.0f); // Matched by object.
	// The cap's centre reflects straight back towards the camera: the middle of the capture.
	const auto uv = PR::LookupUv(plan.Records[0], { 0.0f, 0.0f, 0.5f }, { 0.0f, 0.0f, 1.0f },
		[](const std::array<float, 2>&)
		{
			return 20.0f;
		});
	SWIM_CHECK_NEAR(uv[0], 0.5f, 1e-3f);
	SWIM_CHECK_NEAR(uv[1], 0.5f, 1e-3f);
}
