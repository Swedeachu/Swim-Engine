#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceReference.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Tests/Fixtures/ScreenSpaceFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <cstdio>

using namespace Swim;
using namespace Swim::Render;
namespace Ss = Swim::Render::ScreenSpace;
namespace Scene = Swim::Testing::ScreenSpaceScene;

// Screen-space reflection hit validation against ground truth. Every mirror pixel's
// reflected ray is also cast analytically through the scene: a screen-space hit is right
// when the surface it reports is where the real ray lands; wrong otherwise (the sliced,
// black and "inner sphere" artifacts of the sandbox screenshots were wrong hits: rays that
// passed behind a sphere near its silhouette, accepted because the depth buffer has no
// thickness). With the back-face depth the march knows each surface's thickness.
namespace
{

	using Float3 = Scene::Float3;

	float Dot(const Float3& a, const Float3& b)
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	}

	float Distance(const Float3& a, const Float3& b)
	{
		const Float3 d{ a[0] - b[0], a[1] - b[1], a[2] - b[2] };
		return std::sqrt(Dot(d, d));
	}

	struct Tally
	{
		std::uint32_t Traced = 0;	// Mirror pixels whose reflected ray was marched.
		std::uint32_t Visible = 0;	// ...whose real hit is visible to the camera (SSR can find it).
		std::uint32_t Found = 0;	// ...and SSR reported that surface.
		std::uint32_t Wrong = 0;	// SSR reported a surface the real ray does not hit.
		std::uint32_t Rejected = 0; // SSR reached a surface from its hidden side (falls back).
	};

	// `mirror`: which surface pixels to trace (by their world position and normal).
	template <typename Mirror>
	Tally Measure(const Scene::Scene& scene, const ScreenSpaceView& view, std::uint32_t width, std::uint32_t height, bool backFaces,
		Mirror mirror)
	{
		const auto inputs = Scene::Render(scene, view, width, height, 0.0f);
		ScreenSpaceSettings settings;
		settings.AmbientOcclusion.Enabled = false;
		settings.Reflections.Enabled = true;
		settings.Reflections.MaxDistance = 70.0f; // The sandbox's settings.
		settings.Reflections.MaxSteps = 128;
		settings.Reflections.Thickness = 0.1f;
		auto params = BuildScreenSpaceParams(settings, view, width, height, 0);
		params.SsrBackDepth = backFaces ? 1u : 0u;
		Tally tally;

		for (std::uint32_t y = 0; y < height; ++y)
		{
			for (std::uint32_t x = 0; x < width; ++x)
			{
				const auto& hit = inputs.Hits[std::size_t(y) * width + x];

				if (hit.T <= 0.0f || !mirror(hit.Position, hit.Normal))
				{
					continue;
				}

				const auto traced = Ss::TraceReflection(params, inputs.Depth, inputs.Normal, x, y, backFaces ? &inputs.BackDepth : nullptr);
				++tally.Traced;
				// The real reflected ray.
				const Float3 v{ hit.Position[0] - inputs.Camera[0], hit.Position[1] - inputs.Camera[1], hit.Position[2] - inputs.Camera[2] };
				const float vLength = std::sqrt(Dot(v, v));
				const Float3 vn{ v[0] / vLength, v[1] / vLength, v[2] / vLength };
				const float k = 2.0f * Dot(vn, hit.Normal);
				const Float3 r{ vn[0] - k * hit.Normal[0], vn[1] - k * hit.Normal[1], vn[2] - k * hit.Normal[2] };
				const Float3 start{ hit.Position[0] + hit.Normal[0] * 1.0e-3f, hit.Position[1] + hit.Normal[1] * 1.0e-3f,
					hit.Position[2] + hit.Normal[2] * 1.0e-3f };
				const auto truth = Scene::Cast(scene, start, r);
				bool visible = false;

				if (truth)
				{
					const Float3 toTruth{ truth->Position[0] - inputs.Camera[0], truth->Position[1] - inputs.Camera[1],
						truth->Position[2] - inputs.Camera[2] };
					const float length = std::sqrt(Dot(toTruth, toTruth));
					const auto seen = Scene::Cast(scene, inputs.Camera, { toTruth[0] / length, toTruth[1] / length, toTruth[2] / length });
					// Within the march's reach (before its distance fade) and seen by the camera.
					// On screen (away from the edge fade) too.
					const auto viewProjection = MultiplyRowMajor(view.Projection, view.View);
					float clip[4];

					for (int row = 0; row < 4; ++row)
					{
						clip[row] = viewProjection[row * 4] * truth->Position[0] + viewProjection[row * 4 + 1] * truth->Position[1] +
							viewProjection[row * 4 + 2] * truth->Position[2] + viewProjection[row * 4 + 3];
					}

					const bool onScreen = clip[3] > 0.0f && std::abs(clip[0] / clip[3]) < 0.8f && std::abs(clip[1] / clip[3]) < 0.8f;
					visible = onScreen && truth->T <= settings.Reflections.MaxDistance * (1.0f - settings.Reflections.DistanceFade) && seen &&
						std::abs(seen->T - length) < 0.01f && Dot(truth->Normal, toTruth) < 0.0f;
				}

				tally.Visible += visible ? 1u : 0u;

				if (!traced)
				{
					continue;
				}

				if (traced->Rejected)
				{
					++tally.Rejected;
					continue;
				}

				const auto& reported = inputs.Hits[std::size_t(traced->Y) * width + traced->X];
				const float tolerance = 0.12f + 0.02f * (truth ? truth->T : 0.0f);
				const bool right = truth && reported.T > 0.0f && Distance(reported.Position, truth->Position) <= tolerance;
				tally.Found += right && visible ? 1u : 0u;
				tally.Wrong += right ? 0u : 1u;
			}
		}

		return tally;
	}

	void Print(const char* name, const Tally& old, const Tally& now)
	{
		std::printf("             [ssr %s] traced %u, visible %u | thickness: found %u wrong %u | back faces: found %u wrong %u "
					"rejected %u\n",
			name, now.Traced, now.Visible, old.Found, old.Wrong, now.Found, now.Wrong, now.Rejected);
	}

} // namespace

SWIM_TEST("Render.ScreenSpace.HitValidation", "NearlyTouchingChromeSpheresReflectOnlyWhatIsReallyThere")
{
	// Two mirror spheres 2 cm apart on a floor, a box beside them (screenshot 3's "black
	// inner sphere" case): most of each sphere's view of the other is its far side, which
	// the depth buffer does not have.
	Scene::Scene scene;
	scene.Spheres.push_back({ { -0.51f, 0.5f, 0.0f }, 0.5f });
	scene.Spheres.push_back({ { 0.51f, 0.5f, 0.0f }, 0.5f });
	scene.Boxes.push_back({ { 1.4f, 0.0f, -1.2f }, { 2.4f, 1.0f, -0.2f } });
	constexpr std::uint32_t w = 320;
	constexpr std::uint32_t h = 180;
	const auto view = Scene::View({ 0.2f, 1.1f, 2.6f }, { 0.0f, 0.45f, 0.0f }, float(w) / float(h));
	const auto onSpheres = [](const Float3& p, const Float3&)
	{
		return p[1] > 0.01f && p[0] > -1.2f && p[0] < 1.2f && p[2] > -0.6f && p[2] < 0.6f;
	};
	const auto old = Measure(scene, view, w, h, false, onSpheres);
	const auto now = Measure(scene, view, w, h, true, onSpheres);
	Print("touching spheres", old, now);
	SWIM_REQUIRE(now.Traced > 5000u);
	// Wrong surfaces (the artifacts) are essentially gone, and far fewer than before...
	SWIM_CHECK(now.Wrong * 200u <= now.Traced);
	SWIM_CHECK(now.Wrong <= old.Wrong);
	// ...while surfaces the camera sees are still found (the floor, the other sphere's near side).
	SWIM_CHECK(now.Found * 100u >= now.Visible * 80u);
	// Rays that reach the other sphere's hidden side are reported as rejected (they fall back
	// to the probes instead of showing whatever the depth buffer has there).
	SWIM_CHECK(now.Rejected > 0u);
}

SWIM_TEST("Render.ScreenSpace.HitValidation", "AMirrorFaceReflectsASphereWithoutSlicing")
{
	// A sphere in front of a mirror box face, seen at an angle (screenshots 1 and 2: the
	// reflected ball came out sliced into bands, one per march step, near its silhouette).
	Scene::Scene scene;
	scene.Boxes.push_back({ { -3.0f, 0.0f, -2.0f }, { 3.0f, 3.0f, 0.0f } });
	scene.Spheres.push_back({ { 0.3f, 0.6f, 1.0f }, 0.45f });
	constexpr std::uint32_t w = 320;
	constexpr std::uint32_t h = 180;
	const auto view = Scene::View({ 2.6f, 1.3f, 4.0f }, { -0.2f, 0.9f, 0.0f }, float(w) / float(h));
	const auto onMirror = [](const Float3& p, const Float3& n)
	{
		return n[2] > 0.9f && std::abs(p[2]) < 1.0e-3f && p[1] > 0.02f;
	};
	const auto old = Measure(scene, view, w, h, false, onMirror);
	const auto now = Measure(scene, view, w, h, true, onMirror);
	Print("mirror face", old, now);
	SWIM_REQUIRE(now.Traced > 5000u);
	SWIM_CHECK(now.Wrong * 200u <= now.Traced);
	SWIM_CHECK(now.Wrong * 3u <= old.Wrong);
	SWIM_CHECK(now.Found * 100u >= now.Visible * 85u);
	// The sphere's far side, which faces the mirror, is what most rays toward it really hit:
	// those fall back instead of showing the sphere's top.
	SWIM_CHECK(now.Rejected > 200u);
}

SWIM_TEST("Render.ScreenSpace.HitValidation", "SurfaceThicknessComesFromTheBackFaces")
{
	// A sphere 1 m across in front of the camera: its thickness at the centre pixel is the
	// diameter; the ground (no back face) is solid; without the back depth the constant.
	Scene::Scene scene;
	scene.Spheres.push_back({ { 0.0f, 1.0f, 0.0f }, 0.5f });
	constexpr std::uint32_t w = 64;
	constexpr std::uint32_t h = 64;
	const auto view = Scene::View({ 0.0f, 1.0f, 4.0f }, { 0.0f, 1.0f, 0.0f }, 1.0f);
	const auto inputs = Scene::Render(scene, view, w, h, 0.0f);
	ScreenSpaceSettings settings;
	settings.Reflections.Enabled = true;
	auto params = BuildScreenSpaceParams(settings, view, w, h, 0);
	const auto front = Ss::ViewPosition(params, 32.5f, 32.5f, inputs.Depth.At(32, 32));
	SWIM_REQUIRE(front.has_value());
	SWIM_CHECK_EQUAL(Ss::SurfaceThickness(params, &inputs.BackDepth, 32, 32, -(*front)[2]), settings.Reflections.Thickness);
	params.SsrBackDepth = 1;
	SWIM_CHECK(std::abs(Ss::SurfaceThickness(params, &inputs.BackDepth, 32, 32, -(*front)[2]) - 1.0f) < 0.02f);
	const auto ground = Ss::ViewPosition(params, 32.5f, 63.5f, inputs.Depth.At(32, 63));
	SWIM_REQUIRE(ground.has_value());
	SWIM_CHECK_EQUAL(Ss::SurfaceThickness(params, &inputs.BackDepth, 32, 63, -(*ground)[2]), Ss::OpenThickness);
}
