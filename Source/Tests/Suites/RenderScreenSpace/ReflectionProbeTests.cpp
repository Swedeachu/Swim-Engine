#include "Engine/Systems/Renderer/Environment/EnvironmentMath.h"
#include "Engine/Systems/Renderer/Reflections/ReflectionProbes.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

using namespace Swim::Render;
namespace RP = Swim::Render::ReflectionProbes;
namespace Env = Swim::Render::Environment;

namespace
{

	float Dot(const RP::Float3& a, const RP::Float3& b)
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	}

	GpuReflectionProbeRecord Record(RP::Float3 position, float radius, float blend, float owner = 0.0f, float slot = 0.0f)
	{
		GpuReflectionProbeRecord record;
		record.PositionRadius[0] = position[0];
		record.PositionRadius[1] = position[1];
		record.PositionRadius[2] = position[2];
		record.PositionRadius[3] = radius;
		record.Params[0] = blend;
		record.Params[1] = slot;
		record.Params[3] = owner;
		return record;
	}

	ReflectionProbeDesc Probe(std::uint64_t key, RP::Float3 position, bool dynamic = true)
	{
		ReflectionProbeDesc desc;
		desc.Key = key;
		desc.Position = position;
		desc.Dynamic = dynamic;
		return desc;
	}

} // namespace

SWIM_TEST("Render.ReflectionProbes", "CaptureViewsSeeTheCubeTexelsTheyFill")
{
	// Every face's capture view, projected, puts the cube texel direction (x, y) at capture
	// pixel (CaptureColumn(x), y): the resolve pass relies on it.
	constexpr std::uint32_t size = 8;
	const RP::Float3 origin{ 1.0f, 2.0f, -3.0f };
	const auto projection = RP::CubeFaceProjection(0.1f);

	for (std::uint32_t face = 0; face < 6; ++face)
	{
		const auto view = RP::CubeFaceView(face, origin);
		const auto viewProjection = MultiplyRowMajor(projection, view);
		// Rigid and right-handed (determinant +1 of the rotation).
		const RP::Float3 x{ view[0], view[1], view[2] };
		const RP::Float3 y{ view[4], view[5], view[6] };
		const RP::Float3 z{ view[8], view[9], view[10] };
		const RP::Float3 xy{ x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0] };
		SWIM_CHECK(std::abs(Dot(xy, z) - 1.0f) < 1.0e-6f);

		for (std::uint32_t ty = 0; ty < size; ++ty)
		{
			for (std::uint32_t tx = 0; tx < size; ++tx)
			{
				const auto d = Env::CubeTexelDirection(face, tx, ty, size);
				const std::array<float, 4> p{ origin[0] + d[0] * 5.0f, origin[1] + d[1] * 5.0f, origin[2] + d[2] * 5.0f, 1.0f };
				float clip[4];

				for (int r = 0; r < 4; ++r)
				{
					clip[r] = viewProjection[r * 4] * p[0] + viewProjection[r * 4 + 1] * p[1] + viewProjection[r * 4 + 2] * p[2] +
						viewProjection[r * 4 + 3];
				}

				SWIM_REQUIRE(clip[3] > 0.0f);
				const float px = (clip[0] / clip[3] * 0.5f + 0.5f) * float(size);
				const float py = (0.5f - clip[1] / clip[3] * 0.5f) * float(size);
				SWIM_CHECK(std::abs(px - (float(RP::CaptureColumn(tx, size)) + 0.5f)) < 1.0e-3f);
				SWIM_CHECK(std::abs(py - (float(ty) + 0.5f)) < 1.0e-3f);
				// And the stored distance of that point is its true distance.
				const float depth = clip[2] / clip[3];
				SWIM_CHECK(std::abs(RP::CaptureDistance(depth, 0.1f, clip[0] / clip[3], clip[1] / clip[3]) - 5.0f) < 1.0e-3f);
			}
		}
	}

	SWIM_CHECK_EQUAL(RP::CaptureDistance(0.0f, 0.1f, 0.0f, 0.0f), RP::DistanceSky);
}

SWIM_TEST("Render.ReflectionProbes", "ObjectProbesServeOnlyTheirOwnerAndAreaProbesBlendByInfluence")
{
	const std::vector<GpuReflectionProbeRecord> records{
		Record({ 0, 1, 0 }, 1.6f, 0.4f, 7.0f), // Object probe of object 6 (id + 1 = 7).
		Record({ 2, 1, 0 }, 1.6f, 0.4f, 9.0f), // Object probe of its neighbour.
		Record({ 0, 1, 0 }, 10.0f, 2.0f),	   // Area probe.
		Record({ 6, 1, 0 }, 10.0f, 2.0f),	   // Another area probe.
	};
	// A pixel of object 6 touching its neighbour uses its own probe only, fully.
	auto selection = RP::Select(records, { 1.0f, 1.0f, 0.0f }, 7.0f);
	SWIM_CHECK_EQUAL(selection.First, 0);
	SWIM_CHECK_EQUAL(selection.Second, -1);
	SWIM_CHECK_EQUAL(selection.Coverage, 1.0f);
	// The neighbour's pixel at the same point uses the neighbour's.
	selection = RP::Select(records, { 1.0f, 1.0f, 0.0f }, 9.0f);
	SWIM_CHECK_EQUAL(selection.First, 1);
	// The floor (no probe of its own) never uses object probes: the two area probes blend,
	// the nearer one dominating.
	selection = RP::Select(records, { 1.0f, 0.0f, 0.0f }, 1.0f);
	SWIM_CHECK_EQUAL(selection.First, 2);
	SWIM_CHECK_EQUAL(selection.Second, 3);
	SWIM_CHECK(selection.FirstShare > selection.SecondShare);
	SWIM_CHECK(std::abs(selection.FirstShare + selection.SecondShare - 1.0f) < 1.0e-6f);
	SWIM_CHECK_EQUAL(selection.Coverage, 1.0f);
	// Coverage fades over the blend distance, and out of range nothing is selected.
	selection = RP::Select(records, { -9.0f, 1.0f, 0.0f }, 0.0f);
	SWIM_CHECK_EQUAL(selection.First, 2);
	SWIM_CHECK(std::abs(selection.Coverage - 0.5f) < 1.0e-5f);
	selection = RP::Select(records, { -40.0f, 1.0f, 0.0f }, 0.0f);
	SWIM_CHECK_EQUAL(selection.First, -1);
	SWIM_CHECK_EQUAL(selection.Coverage, 0.0f);
}

SWIM_TEST("Render.ReflectionProbes", "ParallaxCorrectionFindsWhatTheSurfaceReallyReflects")
{
	// A probe at the origin inside a room: a wall at x = 4 (distance 4 / cos along x).
	const RP::Float3 probe{ 0, 0, 0 };
	const auto wall = [](const RP::Float3& d)
	{
		return d[0] > 1.0e-4f ? 4.0f / d[0] : RP::DistanceSky;
	};
	// A surface point off the probe looking diagonally reflects the wall point (4, 5, 0);
	// from the probe that point lies along normalize(4, 5, 0), not along the ray.
	const RP::Float3 position{ 0.0f, 1.0f, 0.0f };
	const float s = 1.0f / std::sqrt(2.0f);
	const RP::Float3 direction{ s, s, 0.0f };
	const auto corrected = RP::ParallaxDirection(position, direction, probe, wall);
	const float expected = 1.0f / std::sqrt(41.0f);
	SWIM_CHECK(std::abs(corrected[0] - 4.0f * expected) < 1.0e-2f);
	SWIM_CHECK(std::abs(corrected[1] - 5.0f * expected) < 1.0e-2f);
	// Constant distance (a sphere around the probe).
	const auto sphere = [](const RP::Float3&)
	{
		return 10.0f;
	};
	const auto exact = RP::ParallaxDirection({ 3.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, probe, sphere);
	const float t = std::sqrt(100.0f - 9.0f);
	SWIM_CHECK(std::abs(exact[0] - 3.0f / 10.0f) < 1.0e-2f);
	SWIM_CHECK(std::abs(exact[1] - t / 10.0f) < 1.0e-2f);
	// An occluder in front of a far wall: the nearest crossing wins (a fixed-point iteration
	// would flip between the two at the occluder's edge).
	const auto occluded = RP::ParallaxDirection({ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, probe,
		[](const RP::Float3& d)
		{
			return d[0] > 0.99f ? 2.0f : 20.0f;
		});
	SWIM_CHECK(occluded[0] > 0.99f);
	// A ray passing behind a near object, as the probe sees it: from (1, 0, 0) straight up
	// under a ceiling at y = 30, while an object 1.2 m from the probe covers the directions
	// 18..26 degrees from +y. The samples there are "beyond" only because the object hides
	// them; the march must not stop at its silhouette (the step-banded smear beside the
	// mirror cube) but go on to the ceiling at (1, 30, 0).
	const auto behind = RP::ParallaxDirection({ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, probe,
		[](const RP::Float3& d)
		{
			const float angle = std::acos(std::clamp(d[1], -1.0f, 1.0f)) * 57.2957795f;

			if (angle >= 18.0f && angle <= 26.0f)
			{
				return 1.2f;
			}

			return d[1] > 1.0e-3f ? 30.0f / d[1] : RP::DistanceSky;
		});
	const float ceiling = 1.0f / std::sqrt(901.0f);
	SWIM_CHECK(std::abs(behind[0] - ceiling) < 5.0e-3f);
	SWIM_CHECK(std::abs(behind[1] - 30.0f * ceiling) < 1.0e-3f);
	// A ray that hits a side of an object the probe cannot see, with only sky beyond: the
	// object's front face at x = 2 covers the directions within 0.2 rad of +x. The ray from
	// (0, 1.5, 0) enters those directions at x ~ 2.5, half a metre behind the face, and
	// leaves them into the sky. It takes the object (where it went behind it), not the sky
	// along the ray, which cut bright slices through the orbiting block in chrome balls.
	const float downLength = std::sqrt(1.16f);
	const RP::Float3 down{ 1.0f / downLength, -0.4f / downLength, 0.0f };
	const auto side = RP::ParallaxDirection({ 0.0f, 1.5f, 0.0f }, down, probe,
		[](const RP::Float3& d)
		{
			return std::acos(std::clamp(d[0], -1.0f, 1.0f)) < 0.2f ? 2.0f / d[0] : RP::DistanceSky;
		});
	SWIM_CHECK(std::acos(std::clamp(side[0], -1.0f, 1.0f)) < 0.21f);
	SWIM_CHECK(side[1] > 0.15f); // Where the ray entered the object's directions (from above).
	// The same object far behind the ray's path (more than ParallaxHidden): passed, the sky.
	const auto passed = RP::ParallaxDirection({ 0.0f, 1.5f, 0.0f }, down, probe,
		[](const RP::Float3& d)
		{
			return std::acos(std::clamp(d[0], -1.0f, 1.0f)) < 0.2f ? 0.5f / d[0] : RP::DistanceSky;
		});
	SWIM_CHECK(passed[0] * down[0] + passed[1] * down[1] + passed[2] * down[2] > 0.999f);
	// The same at any probe resolution (the steps follow the texel angle).
	for (const float jitter : { RP::ProbeTexelAngle(64), RP::ProbeTexelAngle(256), RP::ProbeTexelAngle(512) })
	{
		const auto jittered = RP::ParallaxDirection({ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, probe,
			[](const RP::Float3& d)
			{
				const float angle = std::acos(std::clamp(d[1], -1.0f, 1.0f)) * 57.2957795f;
				return angle >= 18.0f && angle <= 26.0f ? 1.2f : (d[1] > 1.0e-3f ? 30.0f / d[1] : RP::DistanceSky);
			},
			jitter);
		SWIM_CHECK(std::abs(jittered[0] - ceiling) < 5.0e-3f);
		const auto wallJittered = RP::ParallaxDirection(position, direction, probe, wall, jitter);
		SWIM_CHECK(std::abs(wallJittered[0] - 4.0f * expected) < 1.0e-2f);
	}

	// The sky (far away) leaves the direction unchanged.
	const auto sky = RP::ParallaxDirection(position, direction, probe,
		[](const RP::Float3&)
		{
			return RP::DistanceSky;
		});
	SWIM_CHECK(std::abs(sky[0] - s) < 1.0e-3f && std::abs(sky[1] - s) < 1.0e-3f);
}

SWIM_TEST("Render.ReflectionProbes", "FacesThatSeeMovingObjectsAreRecapturedFirst")
{
	// Face frusta: +X sees a sphere along +X and one near its edge (within its radius), not one behind.
	SWIM_CHECK(RP::FaceSees(0, { 5.0f, 0.0f, 0.0f }, 0.5f));
	SWIM_CHECK(RP::FaceSees(0, { 5.0f, 5.3f, 0.0f }, 0.5f));
	SWIM_CHECK(!RP::FaceSees(0, { -5.0f, 0.0f, 0.0f }, 0.5f));
	SWIM_CHECK(!RP::FaceSees(0, { 1.0f, 5.0f, 0.0f }, 0.5f));

	RP::Scheduler scheduler;
	ReflectionProbeSettings settings;
	settings.MaxProbes = 1;
	settings.FacesPerFrame = 1;
	std::vector<ReflectionProbeDesc> probes{ Probe(10, { 0, 1, 0 }) };
	const RP::Float3 camera{ 0, 2, 5 };
	std::uint64_t frame = 1;

	for (; frame <= 12; ++frame)
	{
		(void)scheduler.Update(probes, camera, frame, double(frame) / 60.0, settings);
	}

	// A ball rolling past the -Z side: the -Z face (5) goes next, every frame it moves,
	// ahead of the round-robin refresh of staler faces.
	const std::vector<ReflectionProbeMover> movers{ { { 0.3f, 1.0f, -3.0f }, 0.5f } };

	for (int i = 0; i < 3; ++i)
	{
		const auto plan = scheduler.Update(probes, camera, frame++, double(frame) / 60.0, settings, movers);
		SWIM_REQUIRE_EQUAL(plan.Captures.size(), 1u);
		SWIM_CHECK_EQUAL(plan.Captures[0].Face, 5u);
	}

	// The mover leaves the face's view (here: out of range): the face is captured once more,
	// so the probe stops showing it where it was (no ghost); then it waits its turn again.
	settings.MoverRange = 1.0f;
	auto plan = scheduler.Update(probes, camera, frame++, 1.0, settings, movers);
	SWIM_REQUIRE_EQUAL(plan.Captures.size(), 1u);
	SWIM_CHECK_EQUAL(plan.Captures[0].Face, 5u);
	plan = scheduler.Update(probes, camera, frame++, 1.05, settings, movers);
	SWIM_REQUIRE_EQUAL(plan.Captures.size(), 1u);
	SWIM_CHECK(plan.Captures[0].Face != 5u);
	// The probe's own object (at its centre) changes nothing.
	settings.MoverRange = 15.0f;
	const std::vector<ReflectionProbeMover> owner{ { { 0.0f, 1.0f, 0.0f }, 0.5f } };
	plan = scheduler.Update(probes, camera, frame++, 1.1, settings, owner);
	SWIM_REQUIRE_EQUAL(plan.Captures.size(), 1u);
	SWIM_CHECK(plan.Captures[0].Face != 5u);
}

SWIM_TEST("Render.ReflectionProbes", "TheSchedulerTimeSlicesFacesWithinTheBudget")
{
	RP::Scheduler scheduler;
	ReflectionProbeSettings settings;
	settings.MaxProbes = 2;
	settings.FacesPerFrame = 2;
	std::vector<ReflectionProbeDesc> probes{ Probe(10, { 0, 1, 0 }), Probe(20, { 5, 1, 0 }, false) };
	const RP::Float3 camera{ 0, 2, 5 };

	// New probes fill first, two faces a frame; a probe is active only once all six faces exist.
	std::uint64_t frame = 1;
	std::uint32_t captured = 0;

	for (; frame <= 6; ++frame)
	{
		const auto plan = scheduler.Update(probes, camera, frame, double(frame) / 60.0, settings);
		SWIM_CHECK(plan.Captures.size() <= 2u);
		captured += static_cast<std::uint32_t>(plan.Captures.size());
		SWIM_CHECK(plan.Filter.size() <= 2u);

		if (frame < 6)
		{
			SWIM_CHECK(plan.Active.size() < 2u);
		}
	}

	SWIM_CHECK_EQUAL(captured, 12u);
	auto plan = scheduler.Update(probes, camera, frame++, 7.0 / 60.0, settings);
	SWIM_CHECK_EQUAL(plan.Active.size(), 2u);
	// Afterwards only the dynamic probe keeps cycling (oldest faces first).
	for (int i = 0; i < 6; ++i)
	{
		plan = scheduler.Update(probes, camera, frame++, double(frame) / 60.0, settings);
		SWIM_CHECK_EQUAL(plan.Captures.size(), 2u);

		for (const auto& capture : plan.Captures)
		{
			SWIM_CHECK_EQUAL(capture.Probe, 0u);
		}
	}

	// The static probe moving is re-captured with priority.
	probes[1].Position = { 5, 1, 1 };
	plan = scheduler.Update(probes, camera, frame++, 1.0, settings);
	SWIM_REQUIRE_EQUAL(plan.Captures.size(), 2u);
	SWIM_CHECK_EQUAL(plan.Captures[0].Probe, 1u);
	SWIM_CHECK_EQUAL(plan.Captures[1].Probe, 1u);
	// Slots follow keys; a removed probe frees its slot for a new one, which starts over.
	const auto slotOfFirst = plan.Active.empty() ? 0u : plan.Active[0].Slot;
	(void)slotOfFirst;
	probes = { Probe(20, { 5, 1, 1 }, false), Probe(30, { 9, 1, 0 }) };
	plan = scheduler.Update(probes, camera, frame++, 2.0, settings);
	SWIM_CHECK_EQUAL(scheduler.GetUsedSlots(), 2u);
	SWIM_REQUIRE_EQUAL(plan.Captures.size(), 2u);
	SWIM_CHECK_EQUAL(plan.Captures[0].Probe, 1u); // The new probe's missing faces come first.

	for (const auto& active : plan.Active)
	{
		SWIM_CHECK_EQUAL(active.Probe, 0u);
	}

	// More probes than slots: the nearest (by priority / distance) win.
	probes.push_back(Probe(40, { 0, 2, 4 }));
	plan = scheduler.Update(probes, camera, frame++, 2.1, settings);
	SWIM_CHECK_EQUAL(scheduler.GetUsedSlots(), 2u);
	bool nearestPlaced = false;

	for (const auto& capture : plan.Captures)
	{
		nearestPlaced = nearestPlaced || capture.Probe == 2u;
	}

	SWIM_CHECK(nearestPlaced);
	// Disabled: nothing is captured or active.
	settings.Enabled = false;
	plan = scheduler.Update(probes, camera, frame++, 3.0, settings);
	SWIM_CHECK(plan.Captures.empty() && plan.Active.empty());
	SWIM_CHECK_EQUAL(scheduler.GetUsedSlots(), 0u);
}

// Movers in view of every probe, all the time (an orbiting object in a room of chrome balls):
// their faces go round robin, and a quarter of the budget still refreshes the still faces,
// which would otherwise keep their first capture forever.
SWIM_TEST("Render.ReflectionProbes", "StillFacesAreNotStarvedByConstantMovers")
{
	RP::Scheduler scheduler;
	ReflectionProbeSettings settings;
	settings.MaxProbes = 4;
	settings.FacesPerFrame = 4;
	settings.IdleRefreshFrames = 8;
	std::vector<ReflectionProbeDesc> probes{ Probe(1, { 0, 1, 0 }), Probe(2, { 3, 1, 0 }), Probe(3, { 6, 1, 0 }), Probe(4, { 9, 1, 0 }) };
	const RP::Float3 camera{ 4, 2, 6 };
	// One mover beside each probe (+Z): every probe has a face that sees one every frame.
	const std::vector<ReflectionProbeMover> movers{ { { 0, 1, 2 }, 0.3f }, { { 3, 1, 2 }, 0.3f }, { { 6, 1, 2 }, 0.3f }, { { 9, 1, 2 }, 0.3f } };
	std::uint64_t frame = 1;

	for (; frame <= 6; ++frame) // 24 new faces, four a frame.
	{
		(void)scheduler.Update(probes, camera, frame, double(frame) / 60.0, settings, movers);
	}

	std::array<std::array<std::uint64_t, 6>, 4> last{};

	for (; frame <= 6 + 120; ++frame)
	{
		const auto plan = scheduler.Update(probes, camera, frame, double(frame) / 60.0, settings, movers);
		SWIM_CHECK(plan.Captures.size() <= 4u);

		for (const auto& capture : plan.Captures)
		{
			last[capture.Probe][capture.Face] = frame;
		}
	}

	for (std::uint32_t probe = 0; probe < 4; ++probe)
	{
		for (std::uint32_t face = 0; face < 6; ++face)
		{
			// Every face was captured again in the last 60 frames (still faces included).
			SWIM_CHECK_MESSAGE(last[probe][face] + 60 > frame, "probe " + std::to_string(probe) + " face " + std::to_string(face));
		}
	}
}

// A mover across a cube seam (seen by two faces of each probe): a probe's changed faces are
// captured in the same frame, never split across frames - faces captured at different times
// showed the mover twice, or cut in sections, in chrome balls.
SWIM_TEST("Render.ReflectionProbes", "AProbesChangedFacesAreCapturedTogether")
{
	RP::Scheduler scheduler;
	ReflectionProbeSettings settings;
	settings.MaxProbes = 3;
	settings.FacesPerFrame = 3; // Room for one probe's pair at a time (the reserve takes the rest).
	settings.IdleRefreshFrames = 1000;
	std::vector<ReflectionProbeDesc> probes{ Probe(1, { 0, 1, 0 }), Probe(2, { 20, 1, 0 }), Probe(3, { 40, 1, 0 }) };
	const RP::Float3 camera{ 20, 2, 6 };
	// Each mover sits on the diagonal between a probe's +X and +Z faces.
	const std::vector<ReflectionProbeMover> movers{ { { 2, 1, 2 }, 0.3f }, { { 22, 1, 2 }, 0.3f }, { { 42, 1, 2 }, 0.3f } };
	std::uint64_t frame = 1;

	for (; frame <= 6; ++frame) // 18 new faces.
	{
		(void)scheduler.Update(probes, camera, frame, double(frame) / 60.0, settings, movers);
	}

	std::array<std::uint32_t, 3> captured{};

	for (; frame <= 6 + 30; ++frame)
	{
		const auto plan = scheduler.Update(probes, camera, frame, double(frame) / 60.0, settings, movers);
		std::array<std::uint32_t, 3> faces{};

		for (const auto& capture : plan.Captures)
		{
			++faces[capture.Probe];
		}

		for (std::uint32_t probe = 0; probe < 3; ++probe)
		{
			// Both faces that see the mover, or neither.
			SWIM_CHECK_MESSAGE(faces[probe] == 0u || faces[probe] == 2u, "frame " + std::to_string(frame) + " probe " + std::to_string(probe) +
					" faces " + std::to_string(faces[probe]));
			captured[probe] += faces[probe] != 0u ? 1u : 0u;
		}
	}

	// One probe's pair fits a frame. The probe nearest the camera (largest on screen) goes
	// about every second frame, the far ones at no less than about half its rate.
	SWIM_CHECK(captured[1] >= 14u);
	SWIM_CHECK(captured[0] >= 7u && captured[2] >= 7u);
	SWIM_CHECK(captured[1] > captured[0] && captured[1] > captured[2]);
}

// The sandbox's reflection lab, as the scheduler sees it: six chrome balls, the mirror cube and
// the area probe, the playground's six toy balls and the gallery probe far away, the orange
// block and a chrome ball orbiting the lab, at the sandbox's budget (32 faces, 10 m mover
// range, 1 mm move threshold). Every lab ball's faces that see a mover are refreshed every
// frame: the reflected block must not step, or show where it was a frame or two ago.
SWIM_TEST("Render.ReflectionProbes", "TheLabsBallsRefreshWhatMovesEveryFrame")
{
	RP::Scheduler scheduler;
	ReflectionProbeSettings settings;
	settings.MaxProbes = 16;
	settings.FacesPerFrame = 32;
	settings.MoverRange = 10.0f;
	settings.IdleRefreshFrames = 30;
	settings.MoveThreshold = 0.001f;
	const RP::Float3 lab{ -22.0f, 0.0f, 16.0f };
	const auto at = [&](float x, float y, float z)
	{
		return RP::Float3{ lab[0] + x, y, lab[2] + z };
	};
	std::vector<ReflectionProbeDesc> probes{ Probe(1, at(-2.505f, 0.5f, 0.0f)), Probe(2, at(-1.495f, 0.5f, 0.0f)), Probe(3, at(0.4f, 0.505f, 1.6f)),
		Probe(4, at(2.6f, 0.75f, -1.2f)), Probe(5, at(-0.9f, 0.5f, -1.9f)), Probe(6, at(0.4f, 0.5f, -1.9f)), Probe(7, at(4.2f, 0.6f, 0.0f)),
		Probe(8, at(0.0f, 1.2f, 0.0f)) };

	for (std::uint64_t i = 0; i < 6; ++i)
	{
		probes.push_back(Probe(20 + i, { 30.0f + float(i), 0.45f, -20.0f }));
	}

	probes.push_back(Probe(40, { 0.0f, 2.0f, -40.0f }));

	for (auto& probe : probes)
	{
		probe.OwnerObjectId = probe.Key < 8 || (probe.Key >= 20 && probe.Key < 26) ? std::uint32_t(probe.Key) : 0u;
	}

	const RP::Float3 camera{ -20.0f, 0.9f, 17.0f };
	std::array<std::uint64_t, 7> lastMoverCapture{};
	std::array<std::uint64_t, 7> worstGap{};
	std::uint64_t frame = 1;

	for (; frame <= 600; ++frame)
	{
		const double time = double(frame) / 60.0;
		const float block = 1.3f - 0.5f * float(time);
		const float ball = 0.35f * float(time);
		probes[6].Position = at(4.2f * std::cos(ball), 0.6f, 4.2f * std::sin(ball));
		const std::vector<ReflectionProbeMover> movers{ { at(3.0f * std::cos(block), 1.0f, 3.0f * std::sin(block)), 0.52f },
			{ probes[6].Position, 0.5f } };
		const auto plan = scheduler.Update(probes, camera, frame, time, settings, movers);
		SWIM_CHECK(plan.Captures.size() <= 32u);

		if (frame < 30)
		{
			continue; // Every probe's first capture.
		}

		// Lab balls (and the cube) with a mover in range and in view of a face: captured this frame?
		for (std::uint32_t p = 0; p < 7; ++p)
		{
			bool sees = false;

			for (const auto& mover : movers)
			{
				const RP::Float3 offset{ mover.Center[0] - probes[p].Position[0], mover.Center[1] - probes[p].Position[1],
					mover.Center[2] - probes[p].Position[2] };
				const float distance = std::sqrt(Dot(offset, offset));
				sees = sees || (distance > mover.Radius && distance - mover.Radius < settings.MoverRange);
			}

			sees = sees || p == 6; // The orbiting ball's own probe moves.
			const bool captured = std::any_of(plan.Captures.begin(), plan.Captures.end(),
				[&](const RP::FaceCapture& c)
				{
					return c.Probe == p;
				});

			if (captured || lastMoverCapture[p] == 0)
			{
				lastMoverCapture[p] = frame;
			}
			else if (sees)
			{
				worstGap[p] = std::max(worstGap[p], frame - lastMoverCapture[p]);
			}
		}
	}

	for (std::uint32_t p = 0; p < 7; ++p)
	{
		SWIM_CHECK_MESSAGE(worstGap[p] == 0u, "lab probe " + std::to_string(p) + " skipped up to " + std::to_string(worstGap[p]) + " frames");
	}
}
