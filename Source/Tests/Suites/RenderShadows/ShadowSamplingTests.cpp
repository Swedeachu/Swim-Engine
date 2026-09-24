// Shadow sampling against exact CPU shadow maps (Tests/Fixtures/ShadowFixture.h):
// a cube on a ground plane lit by a sun (cascades), a spot and a point light. The
// ground truth for every receiver is a ray cast toward the light.
#include "Engine/Systems/Renderer/Lights/LightDesc.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Engine/Systems/Renderer/Shadows/ShadowPlanner.h"
#include "Tests/Fixtures/ClusterFixture.h"
#include "Tests/Fixtures/ShadowFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <vector>

using namespace Swim;
using namespace Swim::Render;
namespace Sh = Swim::Render::Shadows;
namespace Fs = Swim::Testing::ForwardScene;
namespace Ss = Swim::Testing::ShadowScene;

namespace
{
	constexpr float Pi = 3.14159265f;

	struct World
	{
		std::vector<Fs::Object> Objects;
		std::vector<bool> Casts;
		Sh::ShadowCamera Camera;
		std::vector<ShadowCasterDesc> Casters;
		ShadowPlan Plan;
		Sh::ShadowAtlasImage Atlas;
	};

	ShadowCasterDesc Caster(LightType type, std::uint32_t slot, const Sh::Float3& position, const Sh::Float3& direction)
	{
		LightDesc desc;
		desc.Type = type;
		desc.Position = position;
		desc.Direction = direction;
		desc.Range = 30.0f;
		desc.OuterConeAngle = 0.7f;
		desc.ShadowIndex = slot;
		desc.Flags = LightFlags::CastsShadows;
		ShadowCasterDesc caster;
		caster.Slot = slot;
		caster.Light = Lights::EncodeLight(desc);
		caster.Priority = float(10 - slot);
		return caster;
	}

	// Ground [-12, 12]^2 at y = 0, a 2 m cube on it, and a floating non-caster.
	World MakeWorld(ShadowSettings settings)
	{
		World world;
		world.Objects.push_back({ Fs::Shape::Quad, Fs::MakeTransform({ 0, 0, 0 }, 0.0f, -Pi * 0.5f, { 12, 12, 1 }) });
		world.Objects.push_back({ Fs::Shape::Cube, Fs::MakeTransform({ 0, 1, 0 }, 0.4f, 0.0f, { 1, 1, 1 }) });
		world.Objects.push_back({ Fs::Shape::Cube, Fs::MakeTransform({ -5, 1.5f, 4 }, 0.0f, 0.0f, { 0.5f, 0.5f, 0.5f }) });
		world.Casts = { true, true, false };
		world.Camera.View = Testing::ClusterScene::LookAt({ 2, 4, 7 }, { 0, 0, 0 }, { 0, 1, 0 });
		world.Camera.VerticalFov = 0.9f;
		world.Camera.Aspect = 16.0f / 9.0f;
		world.Camera.Near = 0.1f;
		world.Casters = {
			Caster(LightType::Directional, 0, { 0, 0, 0 }, { 0.5f, -1.0f, 0.3f }),
			Caster(LightType::Spot, 1, { -1.5f, 6, -1.0f }, { 0.25f, -1.0f, 0.15f }),
			Caster(LightType::Point, 2, { 3.0f, 3.0f, -2.5f }, { 0, -1, 0 }),
		};
		ShadowAtlasAllocator allocator(settings.AtlasSize, settings.MinTile);
		world.Plan = PlanShadows(settings, world.Camera, world.Casters, allocator);
		world.Atlas.Size = settings.AtlasSize;
		world.Atlas.Depth.assign(std::size_t(settings.AtlasSize) * settings.AtlasSize, 0.0f);
		for (const auto& view : world.Plan.Views)
		{
			Ss::RenderView(world.Objects, world.Casts, view, world.Atlas);
		}
		return world;
	}

	ShadowSettings SmallSettings()
	{
		ShadowSettings settings;
		settings.AtlasSize = 2048;
		settings.MinTile = 64;
		settings.CascadeResolution = 512;
		settings.SpotResolution = 256;
		settings.PointResolution = 256;
		settings.Cascades.MaxDistance = 30.0f;
		return settings;
	}

	// Rendering the atlas by ray casting is the slow part: build it once.
	const World& SharedWorld()
	{
		static const World world = MakeWorld(SmallSettings());
		return world;
	}

	float CameraDepth(const Sh::Matrix& view, const Sh::Float3& p)
	{
		return -(view[8] * p[0] + view[9] * p[1] + view[10] * p[2] + view[11]);
	}

	Sh::Float3 ToLight(const GpuLightRecord& light, const Sh::Float3& p)
	{
		if (light.Type == std::uint32_t(LightType::Directional))
		{
			return { -light.Direction[0], -light.Direction[1], -light.Direction[2] };
		}
		return StandardPbr::Normalize({ light.Position[0] - p[0], light.Position[1] - p[1], light.Position[2] - p[2] });
	}

	// Inside a casting cube (the ray caster cannot see a cube from inside).
	bool InsideCaster(const World& world, const Sh::Float3& p)
	{
		for (std::size_t index = 0; index < world.Objects.size(); ++index)
		{
			const auto& object = world.Objects[index];
			if (!world.Casts[index] || object.Kind != Fs::Shape::Cube)
			{
				continue;
			}
			const auto& rows = object.Transform.Current;
			const auto inverse = Fs::InverseLinear(rows);
			const Sh::Float3 relative{ p[0] - rows[3], p[1] - rows[7], p[2] - rows[11] };
			bool inside = true;
			for (int r = 0; r < 3; ++r)
			{
				const float local = inverse[r * 3] * relative[0] + inverse[r * 3 + 1] * relative[1] + inverse[r * 3 + 2] * relative[2];
				inside = inside && std::abs(local) < 1.0f;
			}
			if (inside)
			{
				return true;
			}
		}
		return false;
	}

	// Exact visibility of the light from p (a caster between p and the light).
	bool Occluded(const World& world, const GpuLightRecord& light, const Sh::Float3& p, const Sh::Float3& normal)
	{
		if (InsideCaster(world, { p[0] + normal[0] * 1.0e-3f, p[1] + normal[1] * 1.0e-3f, p[2] + normal[2] * 1.0e-3f }))
		{
			return true;
		}
		const auto toLight = ToLight(light, p);
		const Sh::Float3 origin{ p[0] + normal[0] * 1.0e-3f, p[1] + normal[1] * 1.0e-3f, p[2] + normal[2] * 1.0e-3f };
		float limit = 1.0e30f;
		if (light.Type != std::uint32_t(LightType::Directional))
		{
			const Sh::Float3 d{ light.Position[0] - p[0], light.Position[1] - p[1], light.Position[2] - p[2] };
			limit = std::sqrt(Fs::Dot(d, d));
		}
		for (const auto& hit : Fs::CastRay(world.Objects, origin, toLight))
		{
			if (world.Casts[hit.Object] && hit.T < limit)
			{
				return true;
			}
		}
		return false;
	}

	struct Tally
	{
		std::uint32_t Lit = 0;
		std::uint32_t Shadowed = 0;
		std::uint32_t Partial = 0;
		std::uint32_t Wrong = 0;
	};

	// Ground points on a grid: where the exact answer is the same `margin` shadow
	// texels around the point, the sampled factor must match it exactly.
	Tally CheckGround(const World& world, std::uint32_t slot, float extent, float step)
	{
		Tally tally;
		const Sh::ShadowSampleInputs inputs{ &world.Atlas, world.Plan.Records, world.Plan.Views };
		const auto& light = world.Casters[slot].Light;
		const Sh::Float3 up{ 0, 1, 0 };
		for (float z = -extent; z <= extent; z += step)
		{
			for (float x = -extent; x <= extent; x += step)
			{
				const Sh::Float3 p{ x, 0, z };
				const float depth = CameraDepth(world.Camera.View, p);
				const auto viewIndex = Sh::SelectShadowView(world.Plan.Records[slot], p, depth);
				if (!viewIndex || !Sh::ProjectToShadowView(world.Plan.Views[*viewIndex], p))
				{
					continue;
				}
				const auto toLight = ToLight(light, p);
				if (Fs::Dot(toLight, up) <= 0.05f)
				{
					continue;
				}
				const float factor = Sh::ShadowFactor(inputs, slot, p, up, toLight, depth);
				const auto& view = world.Plan.Views[*viewIndex];
				const Sh::Float3 d{ p[0] - light.Position[0], p[1] - light.Position[1], p[2] - light.Position[2] };
				const float texel = view.TexelWorldSize * (view.Perspective ? std::sqrt(Fs::Dot(d, d)) : 1.0f);
				// The PCF footprint plus the normal offset, in world units on the ground.
				const auto& record = world.Plan.Records[slot];
				const float margin = 2.0f * float(record.PcfRadius + 1) * texel / std::max(Fs::Dot(toLight, up), 0.2f);
				const bool center = Occluded(world, light, p, up);
				bool uniform = true;
				for (int oz = -1; oz <= 1 && uniform; ++oz)
				{
					for (int ox = -1; ox <= 1 && uniform; ++ox)
					{
						uniform = Occluded(world, light, { x + ox * margin, 0, z + oz * margin }, up) == center;
					}
				}
				if (!uniform)
				{
					tally.Partial += factor > 0.0f && factor < 1.0f ? 1u : 0u;
					continue;
				}
				if (factor != (center ? 0.0f : 1.0f))
				{
					++tally.Wrong;
				}
				(center ? tally.Shadowed : tally.Lit) += 1;
			}
		}
		return tally;
	}
} // namespace

SWIM_TEST("Render.Shadows.Sampling", "CascadedSunShadowsMatchRayCastVisibility")
{
	const auto& world = SharedWorld();
	const auto tally = CheckGround(world, 0, 10.0f, 0.1f);
	SWIM_CHECK_EQUAL(tally.Wrong, 0u);
	SWIM_CHECK(tally.Shadowed > 200u);
	SWIM_CHECK(tally.Lit > 5000u);
	SWIM_CHECK(tally.Partial > 0u);
}

SWIM_TEST("Render.Shadows.Sampling", "SpotShadowsMatchRayCastVisibility")
{
	const auto& world = SharedWorld();
	const auto tally = CheckGround(world, 1, 10.0f, 0.1f);
	SWIM_CHECK_EQUAL(tally.Wrong, 0u);
	SWIM_CHECK(tally.Shadowed > 200u);
	SWIM_CHECK(tally.Lit > 1000u);
	SWIM_CHECK(tally.Partial > 0u);
}

SWIM_TEST("Render.Shadows.Sampling", "PointShadowsMatchRayCastVisibilityOnEveryFace")
{
	const auto& world = SharedWorld();
	const auto tally = CheckGround(world, 2, 10.0f, 0.1f);
	SWIM_CHECK_EQUAL(tally.Wrong, 0u);
	SWIM_CHECK(tally.Shadowed > 200u);
	SWIM_CHECK(tally.Lit > 1000u);
	// The ground around the light is seen through the -Y face and four side faces.
	std::uint32_t faces = 0;
	for (std::uint32_t face = 0; face < 6; ++face)
	{
		bool used = false;
		for (float z = -10.0f; z <= 10.0f && !used; z += 0.5f)
		{
			for (float x = -10.0f; x <= 10.0f && !used; x += 0.5f)
			{
				used = Sh::PointShadowFace({ x - 3.0f, -3.0f, z + 2.5f }) == face;
			}
		}
		faces += used ? 1u : 0u;
	}
	SWIM_CHECK_EQUAL(faces, 5u);
}

SWIM_TEST("Render.Shadows.Sampling", "LitCasterSurfacesHaveNoAcne")
{
	const auto& world = SharedWorld();
	const Sh::ShadowSampleInputs inputs{ &world.Atlas, world.Plan.Records, world.Plan.Views };
	// Every face of the cube that faces a light, sampled away from its edges, is lit
	// by that light: normal-offset and slope bias keep self-shadowing out.
	const auto& transform = world.Objects[1].Transform.Current;
	std::uint32_t samples = 0, acne = 0;
	for (std::uint32_t slot = 0; slot < 3; ++slot)
	{
		const auto& light = world.Casters[slot].Light;
		for (const auto& face : Fs::CubeFaces)
		{
			const auto normal = StandardPbr::Normalize(ForwardPlus::TransformNormal(transform, face[0]));
			const auto bitangent = Fs::Cross(face[0], face[1]);
			for (float u = -0.8f; u <= 0.8f; u += 0.1f)
			{
				for (float v = -0.8f; v <= 0.8f; v += 0.1f)
				{
					Sh::Float3 local{};
					for (int c = 0; c < 3; ++c)
					{
						local[c] = face[0][c] + face[1][c] * u + bitangent[c] * v;
					}
					const Sh::Float3 p{ transform[0] * local[0] + transform[1] * local[1] + transform[2] * local[2] + transform[3],
						transform[4] * local[0] + transform[5] * local[1] + transform[6] * local[2] + transform[7],
						transform[8] * local[0] + transform[9] * local[1] + transform[10] * local[2] + transform[11] };
					const auto toLight = ToLight(light, p);
					if (Fs::Dot(normal, toLight) < 0.2f || Occluded(world, light, p, normal))
					{
						continue;
					}
					++samples;
					const float factor = Sh::ShadowFactor(inputs, slot, p, normal, toLight, CameraDepth(world.Camera.View, p));
					acne += factor < 1.0f ? 1u : 0u;
				}
			}
		}
	}
	SWIM_CHECK(samples > 400u);
	SWIM_CHECK_EQUAL(acne, 0u);
}

SWIM_TEST("Render.Shadows.Sampling", "NonCastersAndUnshadowedRecordsStayLit")
{
	const auto& world = SharedWorld();
	const Sh::ShadowSampleInputs inputs{ &world.Atlas, world.Plan.Records, world.Plan.Views };
	// The floating cube does not cast: the ground below it is lit by the sun.
	const auto& sun = world.Casters[0].Light;
	const Sh::Float3 up{ 0, 1, 0 };
	const Sh::Float3 toSun = ToLight(sun, { 0, 0, 0 });
	// Where the floating cube's shadow would land (it is at y = 1.5 above (-5, 4)).
	const float t = 1.5f / toSun[1];
	const Sh::Float3 below{ -5.0f - toSun[0] * t, 0.0f, 4.0f - toSun[2] * t };
	SWIM_CHECK(!Occluded(world, sun, below, up));
	SWIM_CHECK_EQUAL(Sh::ShadowFactor(inputs, 0, below, up, toSun, CameraDepth(world.Camera.View, below)), 1.0f);

	// A point clearly in the big cube's sun shadow, then the same point through a
	// None record, an out-of-range index and a missing atlas.
	const float tc = 2.0f / toSun[1];
	const Sh::Float3 shaded{ -toSun[0] * tc * 1.3f, 0.0f, -toSun[2] * tc * 1.3f };
	SWIM_REQUIRE(!InsideCaster(world, shaded));
	const float depth = CameraDepth(world.Camera.View, shaded);
	SWIM_REQUIRE(Occluded(world, sun, shaded, up));
	SWIM_CHECK_EQUAL(Sh::ShadowFactor(inputs, 0, shaded, up, toSun, depth), 0.0f);
	SWIM_CHECK_EQUAL(Sh::ShadowFactor(inputs, 5, shaded, up, toSun, depth), 1.0f);
	SWIM_CHECK_EQUAL(Sh::ShadowFactor(inputs, 500, shaded, up, toSun, depth), 1.0f);
	const Sh::ShadowSampleInputs noAtlas{ nullptr, world.Plan.Records, world.Plan.Views };
	SWIM_CHECK_EQUAL(Sh::ShadowFactor(noAtlas, 0, shaded, up, toSun, depth), 1.0f);
	// Beyond the last cascade: unshadowed.
	SWIM_CHECK_EQUAL(Sh::ShadowFactor(inputs, 0, shaded, up, toSun, 1000.0f), 1.0f);
}

SWIM_TEST("Render.Shadows.Sampling", "WiderPcfSoftensTheSameEdge")
{
	// PCF only changes the records: the same atlas with radius 0 and radius 2.
	auto hard = SharedWorld();
	auto soft = SharedWorld();
	hard.Plan.Records[0].PcfRadius = 0;
	soft.Plan.Records[0].PcfRadius = 2;
	const auto hardTally = CheckGround(hard, 0, 10.0f, 0.08f);
	const auto softTally = CheckGround(soft, 0, 10.0f, 0.08f);
	SWIM_CHECK_EQUAL(hardTally.Wrong, 0u);
	SWIM_CHECK_EQUAL(softTally.Wrong, 0u);
	SWIM_CHECK_EQUAL(hardTally.Partial, 0u); // One tap: 0 or 1 only.
	SWIM_CHECK(softTally.Partial > 50u);
}
