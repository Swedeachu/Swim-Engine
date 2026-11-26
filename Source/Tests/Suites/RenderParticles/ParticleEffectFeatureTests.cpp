#include "Engine/Systems/Renderer/Particles/ParticleReference.h"
#include "Tests/Fixtures/ParticleFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <stdexcept>
#include <vector>

using namespace Swim::Render;
namespace P = Swim::Render::Particles;
namespace Scene = Swim::Testing::ParticleScene;

namespace
{

	bool Near(float a, float b, float tolerance = 1.0e-4f)
	{
		return std::abs(a - b) <= tolerance;
	}

	float Length(const P::Float3& v)
	{
		return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	}

	float Dot(const P::Float3& a, const P::Float3& b)
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	}

	GpuParticleEmitter Pack(const ParticleEmitterDesc& desc)
	{
		return PackParticleEmitter(desc, Scene::Translation(0, 0, 0), 0, 0, 0);
	}

} // namespace

SWIM_TEST("Render.Particles.Effects", "ShapesSurfacesAndRadialDirections")
{
	ParticleEmitterDesc desc;
	desc.SpeedMin = desc.SpeedMax = 2.0f;
	desc.Shape = ParticleShape::Disc;
	desc.ShapeExtent = { 1.5f, 0.0f, 0.0f };
	desc.DirectionMode = ParticleDirection::Outward;
	auto rim = desc;
	rim.SurfaceOnly = true;
	const auto disc = Pack(desc);
	const auto ring = Pack(rim);

	for (std::uint32_t id = 0; id < 500; ++id)
	{
		const auto p = P::SpawnParticle(disc, id);
		const P::Float3 position{ p.Position[0], p.Position[1], p.Position[2] };
		const P::Float3 velocity{ p.Velocity[0], p.Velocity[1], p.Velocity[2] };
		SWIM_CHECK(p.Position[1] == 0.0f && Length(position) <= 1.5f + 1.0e-4f);
		SWIM_CHECK(Near(Length(velocity), 2.0f, 1.0e-3f));
		SWIM_CHECK(Length(position) < 1.0e-5f || Dot(position, velocity) > 0.0f); // Outward.
		const auto r = P::SpawnParticle(ring, id);
		SWIM_CHECK(Near(Length({ r.Position[0], r.Position[1], r.Position[2] }), 1.5f, 1.0e-4f));
	}

	auto shell = desc;
	shell.Shape = ParticleShape::Box;
	shell.ShapeExtent = { 1.0f, 2.0f, 3.0f };
	shell.SurfaceOnly = true;
	shell.DirectionMode = ParticleDirection::Inward;
	const auto box = Pack(shell);

	for (std::uint32_t id = 0; id < 500; ++id)
	{
		const auto p = P::SpawnParticle(box, id);
		const bool onFace =
			Near(std::abs(p.Position[0]), 1.0f) || Near(std::abs(p.Position[1]), 2.0f) || Near(std::abs(p.Position[2]), 3.0f);
		SWIM_CHECK(onFace);
		SWIM_CHECK(Dot({ p.Position[0], p.Position[1], p.Position[2] }, { p.Velocity[0], p.Velocity[1], p.Velocity[2] }) < 0.0f);
	}
}

SWIM_TEST("Render.Particles.Effects", "ExpireOnContactAndHeldFlipbooks")
{
	ParticleEmitterDesc desc;
	desc.Collision = true;
	desc.GroundHeight = 0.0f;
	desc.ExpireOnContact = true;
	desc.FlipbookColumns = 4;
	desc.FlipbookFrames = 4;
	desc.FlipbookFrameRate = 10.0f;
	desc.FlipbookLoop = false;
	desc.LifetimeMin = desc.LifetimeMax = 10.0f;
	const auto e = Pack(desc);
	GpuParticle p;
	p.Lifetime = 10.0f;
	p.Position[1] = 0.05f;
	p.Velocity[1] = -1.0f;
	SWIM_CHECK(!P::SimulateParticle(e, 0.1f, p)); // Touched the ground: gone, no bounce.

	GpuParticle held;
	held.Lifetime = 10.0f;
	held.Age = 2.0f; // 20 frames in at 10 fps.
	SWIM_CHECK(P::FlipbookFrame(e, held) == 3u);
	desc.FlipbookLoop = true;
	SWIM_CHECK(P::FlipbookFrame(Pack(desc), held) == 0u);
}

SWIM_TEST("Render.Particles.Effects", "FacingModesOrientTheQuad")
{
	const auto frame = BuildParticleFrame(Scene::View(1.0f, { 0.0f, 2.0f, 8.0f }, { 0.0f, 2.0f, 0.0f }), 0.0f, 1, 0);
	ParticleEmitterDesc desc;
	desc.SizeMin = desc.SizeMax = 2.0f;
	desc.AspectMin = desc.AspectMax = 3.0f;
	GpuParticle p;
	p.Lifetime = 1.0f;
	p.Size = 2.0f;
	p.Position[1] = 2.0f;
	p.Velocity[0] = 4.0f;

	const auto camera = P::ParticleBillboardAxes(frame, Pack(desc), p);
	SWIM_CHECK(Near(camera.HalfWidth, 1.0f) && Near(camera.HalfHeight, 3.0f));

	desc.Facing = ParticleFacing::Velocity;
	desc.Stretch = 0.5f;
	const auto streak = P::ParticleBillboardAxes(frame, Pack(desc), p);
	SWIM_CHECK(Near(streak.Up[0], 1.0f) && Near(streak.HalfHeight, 3.0f * (1.0f + 0.5f * 4.0f)));
	SWIM_CHECK(Near(Dot(streak.Right, streak.Up), 0.0f));

	desc.Facing = ParticleFacing::UprightY;
	const auto upright = P::ParticleBillboardAxes(frame, Pack(desc), p);
	SWIM_CHECK(Near(upright.Up[1], 1.0f) && Near(upright.Right[1], 0.0f));

	desc.Facing = ParticleFacing::Horizontal;
	const auto flat = P::ParticleBillboardAxes(frame, Pack(desc), p);
	SWIM_CHECK(Near(flat.Right[1], 0.0f) && Near(flat.Up[1], 0.0f));

	// Corners follow the axes: the quad is three times as tall as it is wide.
	desc.Facing = ParticleFacing::Camera;
	desc.Stretch = 0.0f;
	const auto e = Pack(desc);
	const auto bottom = P::BillboardCorner(frame, e, p, 0);
	const auto top = P::BillboardCorner(frame, e, p, 2);
	const auto right = P::BillboardCorner(frame, e, p, 1);
	SWIM_CHECK(Near(top.Position[1] - bottom.Position[1], 6.0f, 1.0e-3f) && Near(right.Position[0] - bottom.Position[0], 2.0f, 1.0e-3f));

	desc.AspectMin = -1.0f;
	SWIM_CHECK_THROWS(ValidateParticleEmitterDesc(desc), std::invalid_argument);
}

SWIM_TEST("Render.Particles.Effects", "CurvesFitAnyNumberOfKeys")
{
	// A tent with a notch: five keys fitted to four keep the ends and the sharpest corners.
	const std::vector<float> times{ 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
	const std::vector<float> values{ 0.0f, 1.0f, 0.9f, 1.0f, 0.0f };
	const auto curve = ParticleCurve::FromKeys(times, values);
	SWIM_CHECK(curve.KeyCount == 4u);
	SWIM_CHECK(curve.Times[0] == 0.0f && curve.Times[3] == 1.0f && curve.Values[0] == 0.0f && curve.Values[3] == 0.0f);
	SWIM_CHECK(curve.Values[1] == 1.0f && curve.Values[2] == 1.0f);

	const std::vector<float> two{ 0.2f, 0.8f };
	const std::vector<std::array<float, 4>> colors{ { 1, 0, 0, 1 }, { 0, 0, 1, 0 } };
	const auto gradient = ParticleGradient::FromKeys(two, colors);
	SWIM_CHECK(gradient.KeyCount == 2u && gradient.Colors[1][2] == 1.0f && gradient.Times[0] == 0.2f);

	ParticleEmitterDesc desc;
	desc.SizeOverLife = curve;
	desc.ColorOverLife = gradient;
	ValidateParticleEmitterDesc(desc); // Fitted keys are always valid.
	SWIM_CHECK(ParticleCurve::FromKeys({}, {}).KeyCount == 1u);
}
