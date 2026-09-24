#include "Engine/Systems/Renderer/Particles/ParticleReference.h"
#include "Tests/Fixtures/ParticleFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <set>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;
namespace P = Swim::Render::Particles;
namespace Scene = Swim::Testing::ParticleScene;

namespace
{
	bool Near(float a, float b, float tolerance)
	{
		return std::abs(a - b) <= tolerance;
	}

	float Length(const P::Float3& v)
	{
		return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	}

	GpuParticleEmitter Pack(const ParticleEmitterDesc& desc, const std::array<float, 12>& transform = Scene::Translation(0, 0, 0),
		std::uint32_t spawn = 0, std::uint32_t firstId = 0)
	{
		return PackParticleEmitter(desc, transform, 0, spawn, firstId);
	}
} // namespace

SWIM_TEST("Render.Particles.Reference", "RandomStreamsAreDeterministicUniformAndIndependent")
{
	double sum = 0.0, product = 0.0;
	std::array<std::uint32_t, 10> buckets{};
	constexpr std::uint32_t Count = 100000;
	for (std::uint32_t id = 0; id < Count; ++id)
	{
		const float a = P::Random(id, 7, 0);
		const float b = P::Random(id, 7, 1);
		SWIM_REQUIRE(a >= 0.0f && a < 1.0f && b >= 0.0f && b < 1.0f);
		SWIM_CHECK(a == P::Random(id, 7, 0)); // Pure.
		sum += a;
		product += (a - 0.5) * (b - 0.5);
		++buckets[std::min(9u, std::uint32_t(a * 10.0f))];
	}
	SWIM_CHECK(std::abs(sum / Count - 0.5) < 0.005);
	SWIM_CHECK(std::abs(product / Count) < 0.002); // Streams are uncorrelated.
	for (const auto bucket : buckets)
	{
		SWIM_CHECK(bucket > Count / 10 * 95 / 100 && bucket < Count / 10 * 105 / 100);
	}
	SWIM_CHECK(P::Random(5, 1, 0) != P::Random(5, 2, 0)); // Seeds differ.
	SWIM_CHECK_EQUAL(P::Hash(0u), 129708002u);			  // The published PCG hash.
}

SWIM_TEST("Render.Particles.Reference", "EmissionScheduleCountsRateBurstsCyclesAndCapacity")
{
	ParticleEmitterDesc desc;
	desc.Rate = 10.0f;
	desc.Capacity = 1000;
	ParticleEmitterClock clock;
	std::uint32_t total = 0;
	for (int frame = 0; frame < 60; ++frame)
	{
		total += P::AdvanceEmission(desc, clock, 1.0f / 60.0f);
	}
	SWIM_CHECK(total == 9u || total == 10u); // 10 per second, fractions carried.
	SWIM_CHECK_EQUAL(clock.NextId, total);
	SWIM_CHECK(Near(clock.Time, 1.0f, 1.0e-4f));

	// Non-looping: the rate stops at Duration and each burst fires once.
	ParticleEmitterDesc once;
	once.Rate = 100.0f;
	once.Duration = 0.5f;
	once.Looping = false;
	once.Bursts = { { 0.0f, 7 }, { 0.3f, 5 } };
	ParticleEmitterClock onceClock;
	std::vector<std::uint32_t> perFrame;
	for (int frame = 0; frame < 10; ++frame)
	{
		perFrame.push_back(P::AdvanceEmission(once, onceClock, 0.1f));
	}
	SWIM_CHECK_EQUAL(perFrame[0], 17u); // 10 continuous + the burst at 0.
	SWIM_CHECK_EQUAL(perFrame[3], 15u); // 10 continuous + the burst at 0.3.
	std::uint32_t after = 0;
	for (int frame = 5; frame < 10; ++frame)
	{
		after += perFrame[std::size_t(frame)];
	}
	SWIM_CHECK(after <= 1u); // Only float rounding of the last fraction may remain.

	// Looping: bursts fire every cycle, also when one frame spans several cycles.
	ParticleEmitterDesc looping;
	looping.Rate = 0.0f;
	looping.Duration = 0.25f;
	looping.Bursts = { { 0.1f, 3 } };
	ParticleEmitterClock loopClock;
	SWIM_CHECK_EQUAL(P::AdvanceEmission(looping, loopClock, 0.05f), 0u); // [0, 0.05)
	SWIM_CHECK_EQUAL(P::AdvanceEmission(looping, loopClock, 0.10f), 3u); // [0.05, 0.15): 0.1
	SWIM_CHECK_EQUAL(P::AdvanceEmission(looping, loopClock, 0.60f), 6u); // [0.15, 0.75): 0.35, 0.6
	SWIM_CHECK_EQUAL(P::AdvanceEmission(looping, loopClock, 0.10f), 0u); // [0.75, 0.85): half open, 0.85 is next
	SWIM_CHECK_EQUAL(P::AdvanceEmission(looping, loopClock, 0.10f), 3u); // [0.85, 0.95)
	SWIM_CHECK_EQUAL(loopClock.NextId, 12u);

	// Capacity caps one frame's spawns.
	ParticleEmitterDesc small;
	small.Capacity = 8;
	small.Bursts = { { 0.0f, 100 } };
	ParticleEmitterClock smallClock;
	SWIM_CHECK_EQUAL(P::AdvanceEmission(small, smallClock, 0.01f), 8u);
}

SWIM_TEST("Render.Particles.Reference", "SpawnsFollowTheirRangesConeAndShapes")
{
	auto desc = Scene::Fountain();
	desc.Collision = false;
	desc.Direction = { 1.0f, 1.0f, 0.0f };
	desc.ConeAngle = 0.4f;
	desc.RotationMin = -1.0f;
	desc.RotationMax = 2.0f;
	desc.AngularVelocityMin = 0.5f;
	desc.AngularVelocityMax = 0.75f;
	const auto local = Pack(desc);
	const P::Float3 axis{ std::sqrt(0.5f), std::sqrt(0.5f), 0.0f };
	float widest = 0.0f;
	for (std::uint32_t id = 0; id < 2000; ++id)
	{
		const auto p = P::SpawnParticle(local, id);
		SWIM_CHECK_EQUAL(p.Id, id);
		SWIM_CHECK(p.Age == 0.0f);
		SWIM_CHECK(p.Lifetime >= desc.LifetimeMin && p.Lifetime <= desc.LifetimeMax);
		SWIM_CHECK(p.Size >= desc.SizeMin && p.Size <= desc.SizeMax);
		SWIM_CHECK(p.Rotation >= -1.0f && p.Rotation <= 2.0f);
		SWIM_CHECK(p.AngularVelocity >= 0.5f && p.AngularVelocity <= 0.75f);
		const P::Float3 v{ p.Velocity[0], p.Velocity[1], p.Velocity[2] };
		const float speed = Length(v);
		SWIM_CHECK(speed >= desc.SpeedMin - 1.0e-4f && speed <= desc.SpeedMax + 1.0e-4f);
		const float cosine = (v[0] * axis[0] + v[1] * axis[1] + v[2] * axis[2]) / speed;
		widest = std::max(widest, std::acos(std::min(1.0f, cosine)));
		SWIM_CHECK(p.Position[0] == 0.0f && p.Position[1] == 0.0f && p.Position[2] == 0.0f); // Point shape.
		SWIM_CHECK(p.Size == P::SpawnParticle(local, id).Size);								 // Deterministic.
	}
	SWIM_CHECK(widest <= 0.4f + 1.0e-3f && widest > 0.35f); // Fills the cone, never leaves it.

	// Sphere: inside the ball, uniform in volume (mean r^3 = R^3 / 2).
	auto sphere = desc;
	sphere.Shape = ParticleShape::Sphere;
	sphere.ShapeExtent = { 2.0f, 0.0f, 0.0f };
	const auto sphereRecord = Pack(sphere);
	double cubes = 0.0;
	for (std::uint32_t id = 0; id < 4000; ++id)
	{
		const auto p = P::SpawnParticle(sphereRecord, id);
		const float r = Length({ p.Position[0], p.Position[1], p.Position[2] });
		SWIM_CHECK(r <= 2.0f + 1.0e-4f);
		cubes += double(r) * r * r;
	}
	SWIM_CHECK(std::abs(cubes / 4000.0 / 8.0 - 0.5) < 0.03);

	// Box: inside the half extents.
	auto box = desc;
	box.Shape = ParticleShape::Box;
	box.ShapeExtent = { 1.0f, 0.5f, 0.25f };
	const auto boxRecord = Pack(box);
	for (std::uint32_t id = 0; id < 1000; ++id)
	{
		const auto p = P::SpawnParticle(boxRecord, id);
		SWIM_CHECK(std::abs(p.Position[0]) <= 1.0f && std::abs(p.Position[1]) <= 0.5f && std::abs(p.Position[2]) <= 0.25f);
	}

	// World space: position and velocity go through the emitter transform; local space keeps them.
	const std::array<float, 12> turned{ 0, -1, 0, 5, 1, 0, 0, 6, 0, 0, 1, 7 }; // 90 degrees about z, then a translation.
	const auto world = PackParticleEmitter(box, turned, 0, 0, 0);
	auto localBox = box;
	localBox.Space = ParticleSpace::Local;
	const auto localRecord = PackParticleEmitter(localBox, turned, 0, 0, 0);
	for (std::uint32_t id = 0; id < 50; ++id)
	{
		const auto a = P::SpawnParticle(world, id);
		const auto b = P::SpawnParticle(localRecord, id);
		SWIM_CHECK(Near(a.Position[0], 5.0f - b.Position[1], 1.0e-5f) && Near(a.Position[1], 6.0f + b.Position[0], 1.0e-5f));
		SWIM_CHECK(Near(a.Position[2], 7.0f + b.Position[2], 1.0e-5f));
		SWIM_CHECK(Near(a.Velocity[0], -b.Velocity[1], 1.0e-5f) && Near(a.Velocity[1], b.Velocity[0], 1.0e-5f));
		const auto worldPosition = P::WorldPosition(localRecord, b);
		SWIM_CHECK(Near(worldPosition[0], a.Position[0], 1.0e-5f) && Near(worldPosition[1], a.Position[1], 1.0e-5f));
	}
}

SWIM_TEST("Render.Particles.Reference", "SimulationIntegratesDiesAndBounces")
{
	auto desc = Scene::Fountain();
	desc.Collision = false;
	desc.Drag = 0.0f;
	desc.Gravity = { 0.0f, -10.0f, 0.0f };
	const auto emitter = Pack(desc);
	GpuParticle p;
	p.Lifetime = 1.0f;
	p.Velocity[0] = 2.0f;
	p.Velocity[1] = 5.0f;
	p.AngularVelocity = 3.0f;
	const float dt = 0.01f;
	float x = 0.0f, y = 0.0f, vy = 5.0f;
	for (int step = 0; step < 50; ++step)
	{
		SWIM_REQUIRE(P::SimulateParticle(emitter, dt, p));
		vy += -10.0f * dt; // Semi-implicit Euler.
		y += vy * dt;
		x += 2.0f * dt;
	}
	SWIM_CHECK(Near(p.Position[0], x, 1.0e-4f) && Near(p.Position[1], y, 1.0e-4f) && Near(p.Velocity[1], vy, 1.0e-4f));
	SWIM_CHECK(Near(p.Rotation, 1.5f, 1.0e-4f) && Near(p.Age, 0.5f, 1.0e-5f));
	// Death: a step reaching the lifetime leaves the particle untouched.
	GpuParticle old = p;
	old.Age = 0.995f;
	const auto before = old;
	SWIM_CHECK(!P::SimulateParticle(emitter, dt, old));
	SWIM_CHECK(old.Position[1] == before.Position[1] && old.Age == before.Age);

	// Drag scales velocity by (1 - drag dt) each step.
	auto dragged = desc;
	dragged.Gravity = { 0, 0, 0 };
	dragged.Drag = 2.0f;
	const auto dragEmitter = Pack(dragged);
	GpuParticle d;
	d.Lifetime = 10.0f;
	d.Velocity[2] = 1.0f;
	for (int step = 0; step < 10; ++step)
	{
		P::SimulateParticle(dragEmitter, 0.1f, d);
	}
	SWIM_CHECK(Near(d.Velocity[2], std::pow(0.8f, 10.0f), 1.0e-5f));

	// Collision: never below the ground; bounces lose restitution and friction.
	auto bouncing = desc;
	bouncing.Collision = true;
	bouncing.GroundHeight = 0.5f;
	bouncing.Restitution = 0.5f;
	bouncing.Friction = 0.25f;
	const auto bounceEmitter = Pack(bouncing);
	GpuParticle b;
	b.Lifetime = 100.0f;
	b.Position[1] = 1.0f;
	b.Velocity[0] = 1.0f;
	// The first three bounces: each leaves the ground at restitution x the impact speed.
	std::vector<float> rebounds;
	for (int iteration = 0; iteration < 2000 && rebounds.size() < 3; ++iteration)
	{
		const float vxBefore = b.Velocity[0];
		const float step = 0.005f;
		const float impact = (b.Velocity[1] - 10.0f * step); // Vertical speed after gravity, before the contact.
		SWIM_REQUIRE(P::SimulateParticle(bounceEmitter, step, b));
		SWIM_CHECK(b.Position[1] >= 0.5f);
		if (b.Velocity[1] > 0.0f && b.Position[1] == 0.5f)
		{
			SWIM_CHECK(Near(b.Velocity[1], -impact * 0.5f, 1.0e-5f));
			SWIM_CHECK(Near(b.Velocity[0], vxBefore * 0.75f, 1.0e-6f));
			rebounds.push_back(b.Velocity[1]);
		}
	}
	SWIM_REQUIRE_EQUAL(rebounds.size(), std::size_t(3));
	SWIM_CHECK(rebounds[1] < rebounds[0] && rebounds[2] < rebounds[1]); // Each bounce is lower.
}

SWIM_TEST("Render.Particles.Reference", "CurvesGradientsFlipbooksAndBillboards")
{
	const float times[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
	const float values[4] = { 1.0f, 3.0f, 2.0f, 2.0f };
	SWIM_CHECK(P::EvaluateCurve(times, values, 3, 0.25f) == 2.0f);
	SWIM_CHECK(P::EvaluateCurve(times, values, 3, 0.75f) == 2.5f);
	SWIM_CHECK(P::EvaluateCurve(times, values, 3, -1.0f) == 1.0f && P::EvaluateCurve(times, values, 3, 2.0f) == 2.0f);
	SWIM_CHECK(P::EvaluateCurve(times, values, 1, 0.75f) == 1.0f);

	auto desc = Scene::Sparks();
	desc.TextureIndex = 3;
	desc.SamplerIndex = 1;
	desc.ColorOverLife = { 2, { 0.2f, 0.8f, 1.0f, 1.0f }, { { { 1, 0, 0, 1 }, { 0, 0, 1, 0.5f }, {}, {} } } };
	const auto emitter = Pack(desc);
	SWIM_CHECK((emitter.Flags & ParticleFlagTextured) != 0u);
	SWIM_CHECK(emitter.ColorTimes[2] == 0.8f && emitter.ColorValues[8] == 0.0f); // Unused keys repeat the last.
	GpuParticle p;
	p.Lifetime = 2.0f;
	p.Age = 1.0f;
	p.Size = 0.4f;
	const auto color = P::EvaluateGradient(emitter, 0.5f);
	SWIM_CHECK(Near(color[0], 0.5f, 1.0e-6f) && Near(color[2], 0.5f, 1.0e-6f) && Near(color[3], 0.75f, 1.0e-6f));
	// Over-life flipbook: 7 frames over the lifetime; frame-rate flipbooks wrap.
	SWIM_CHECK_EQUAL(P::FlipbookFrame(emitter, p), 3u);
	p.Age = 1.999f;
	SWIM_CHECK_EQUAL(P::FlipbookFrame(emitter, p), 6u);
	auto rate = emitter;
	rate.FlipbookFrameRate = 10.0f;
	p.Age = 0.95f;
	SWIM_CHECK_EQUAL(P::FlipbookFrame(rate, p), 2u); // floor(9.5) mod 7

	// Billboards face the camera, have the right size, rotate, and map the frame's cell.
	const auto view = Scene::View(16.0f / 9.0f);
	const auto frame = BuildParticleFrame(view, 0.0f, 1, 0);
	p.Age = 0.0f;
	p.Position[0] = 1.0f;
	p.Position[1] = 2.0f;
	p.Position[2] = -1.0f;
	p.Rotation = 0.3f;
	const float size = P::ParticleSize(emitter, p);
	std::array<P::BillboardVertex, 4> corners{};
	for (std::uint32_t c = 0; c < 4; ++c)
	{
		corners[c] = P::BillboardCorner(frame, emitter, p, c);
		const P::Float3 offset{ corners[c].Position[0] - 1.0f, corners[c].Position[1] - 2.0f, corners[c].Position[2] + 1.0f };
		SWIM_CHECK(Near(Length(offset), size * std::sqrt(0.5f), 1.0e-5f));
		const float along = offset[0] * frame.CameraForward[0] + offset[1] * frame.CameraForward[1] + offset[2] * frame.CameraForward[2];
		SWIM_CHECK(Near(along, 0.0f, 1.0e-5f)); // In the plane facing the camera.
	}
	// Corner 3 (top right) minus corner 2 (top left) is the rotated camera right.
	const float edge[3] = { corners[3].Position[0] - corners[2].Position[0], corners[3].Position[1] - corners[2].Position[1],
		corners[3].Position[2] - corners[2].Position[2] };
	const float onRight = (edge[0] * frame.CameraRight[0] + edge[1] * frame.CameraRight[1] + edge[2] * frame.CameraRight[2]) / size;
	SWIM_CHECK(Near(onRight, std::cos(0.3f), 1.0e-5f));
	// Frame 0 is the atlas' top-left cell of 4 x 2.
	SWIM_CHECK(Near(corners[2].Uv[0], 0.0f, 1.0e-6f) && Near(corners[2].Uv[1], 0.0f, 1.0e-6f));
	SWIM_CHECK(Near(corners[1].Uv[0], 0.25f, 1.0e-6f) && Near(corners[1].Uv[1], 0.5f, 1.0e-6f));

	// Fragments: premultiplied; the untextured disc fades to its rim; textures multiply.
	auto plain = desc;
	plain.TextureIndex = 0;
	plain.SamplerIndex = 0;
	const auto disc = Pack(plain);
	p.Age = 0.0f;
	const auto centre = P::ShadeFragment(disc, p, { 0.5f, 0.5f }, {});
	SWIM_CHECK(Near(centre[3], 1.0f, 1.0e-6f) && Near(centre[0], 1.0f, 1.0e-6f));
	const auto rim = P::ShadeFragment(disc, p, { 1.0f, 0.5f }, {});
	SWIM_CHECK(rim[3] == 0.0f && rim[0] == 0.0f);
	const auto textured = P::ShadeFragment(emitter, p, { 1.0f, 1.0f }, { 0.5f, 1.0f, 1.0f, 0.5f });
	SWIM_CHECK(Near(textured[3], 0.5f, 1.0e-6f) && Near(textured[0], 0.25f, 1.0e-6f));
}

SWIM_TEST("Render.Particles.Reference", "ReferenceEmitterReachesSteadyStateAndDropsBeyondCapacity")
{
	auto desc = Scene::Fountain();
	desc.Capacity = 400;
	ParticleEmitterClock clock;
	P::ReferenceEmitter pool(desc.Capacity);
	std::uint32_t emitted = 0;
	for (int frame = 0; frame < 180; ++frame)
	{
		const auto spawn = P::AdvanceEmission(desc, clock, 1.0f / 60.0f);
		emitted += spawn;
		pool.Step(PackParticleEmitter(desc, Scene::Translation(0, 0, 0), 0, spawn, clock.NextId - spawn), 1.0f / 60.0f);
		for (const auto& p : pool.Particles())
		{
			SWIM_CHECK(p.Age < p.Lifetime && p.Position[1] >= 0.0f);
		}
	}
	// Rate x mean lifetime = 120 x 1.1 = 132 live on average.
	const auto live = pool.Particles().size();
	std::printf("             [particles] fountain steady state: %zu live of %u emitted\n", live, emitted);
	SWIM_CHECK(live > 100u && live < 170u);
	SWIM_CHECK_EQUAL(pool.Dropped(), 0u);
	std::set<std::uint32_t> ids;
	for (const auto& p : pool.Particles())
	{
		ids.insert(p.Id);
	}
	SWIM_CHECK_EQUAL(ids.size(), live);

	// A tiny pool drops what does not fit.
	P::ReferenceEmitter tiny(10);
	auto burst = Pack(desc, Scene::Translation(0, 0, 0), 25, 0);
	tiny.Step(burst, 0.01f);
	SWIM_CHECK_EQUAL(tiny.Particles().size(), std::size_t(10));
	SWIM_CHECK_EQUAL(tiny.Dropped(), 15u);
}

SWIM_TEST("Render.Particles.Reference", "PackingAndFramesValidate")
{
	const auto view = Scene::View(2.0f, { 3.0f, 4.0f, 5.0f });
	auto jittered = view;
	jittered.Jitter = { 0.01f, -0.02f };
	const auto frame = BuildParticleFrame(jittered, 1.0f / 30.0f, 3, 12);
	SWIM_CHECK(Near(frame.CameraPosition[0], 3.0f, 1.0e-5f) && Near(frame.CameraPosition[1], 4.0f, 1.0e-5f) &&
		Near(frame.CameraPosition[2], 5.0f, 1.0e-5f));
	SWIM_CHECK(frame.EmitterCount == 3u && frame.MaxSpawn == 12u && frame.DeltaTime == 1.0f / 30.0f);
	// Jitter shifts NDC by exactly the jitter.
	const auto plain = BuildParticleFrame(view, 0.0f, 0, 0);
	const float point[4] = { 0.5f, 1.0f, -2.0f, 1.0f };
	std::array<float, 4> a{}, b{};
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			a[r] += frame.ViewProjection[r * 4 + c] * point[c];
			b[r] += plain.ViewProjection[r * 4 + c] * point[c];
		}
	}
	SWIM_CHECK(Near(a[0] / a[3] - b[0] / b[3], 0.01f, 1.0e-5f) && Near(a[1] / a[3] - b[1] / b[3], -0.02f, 1.0e-5f));
	const float rightDotForward = frame.CameraRight[0] * frame.CameraForward[0] + frame.CameraRight[1] * frame.CameraForward[1] +
		frame.CameraRight[2] * frame.CameraForward[2];
	SWIM_CHECK(Near(rightDotForward, 0.0f, 1.0e-6f));

	auto desc = Scene::Smoke();
	const auto record = PackParticleEmitter(desc, Scene::Translation(1, 2, 3), 64, 500, 9);
	SWIM_CHECK(record.FirstSlot == 64u && record.FirstId == 9u && record.SpawnCount == desc.Capacity); // Capped.
	SWIM_CHECK(record.Flags == (ParticleFlagLocalSpace | ParticleFlagSorted));
	SWIM_CHECK(record.Shape == ParticleShapeSphere && Near(record.CosConeAngle, std::cos(0.8f), 1.0e-6f));

	const auto rejects = [](const std::function<void(ParticleEmitterDesc&)>& edit)
	{
		auto d = Scene::Fountain();
		edit(d);
		bool threw = false;
		try
		{
			ValidateParticleEmitterDesc(d);
		}
		catch (const std::invalid_argument&)
		{
			threw = true;
		}
		return threw;
	};
	SWIM_CHECK(!rejects(
		[](auto&)
		{
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.Capacity = 0;
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.Blend = ParticleBlendMode::AlphaBlend;
			d.Capacity = MaxSortedParticles + 1;
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.Rate = -1.0f;
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.Duration = 1.0f;
			d.Bursts = { { 1.0f, 3 } };
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.Direction = { 0, 0, 0 };
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.ConeAngle = 4.0f;
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.SpeedMin = 5.0f;
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.LifetimeMin = 0.0f;
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.SizeMin = 0.0f;
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.Drag = -1.0f;
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.Space = ParticleSpace::Local;
		})); // Collision needs world space.
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.Restitution = 1.5f;
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.SizeOverLife.Times = { 0.0f, 0.0f, 1.0f, 1.0f };
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.ColorOverLife.Colors[0][3] = 2.0f;
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.FlipbookFrames = 2;
		}));
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.TextureIndex = 4;
		})); // Without a sampler.
	SWIM_CHECK(rejects(
		[](auto& d)
		{
			d.ShapeExtent[1] = -1.0f;
		}));
	SWIM_CHECK_THROWS(PackParticleEmitter(Scene::Fountain(), { 0, 0, 0, NAN, 0, 0, 0, 0, 0, 0, 0, 0 }, 0, 0, 0), std::invalid_argument);
	SWIM_CHECK_THROWS(BuildParticleFrame(view, -1.0f, 0, 0), std::invalid_argument);
}

SWIM_TEST("Render.Particles.Reference", "RasterizerBlendsAdditiveThenSortedAlpha")
{
	// The CPU draw used by the native smoke: additive sums are order independent;
	// alpha blending in back-to-front order puts the nearest particle on top.
	const std::uint32_t width = 64, height = 36;
	const auto view = Scene::View(float(width) / float(height), { 0, 0, 5 }, { 0, 0, 0 });
	const auto frame = BuildParticleFrame(view, 0.0f, 2, 0);
	auto additiveDesc = Scene::Fountain();
	additiveDesc.ColorOverLife = { 1, { 0, 1, 1, 1 }, { { { 0.25f, 0.5f, 1.0f, 1.0f }, {}, {}, {} } } };
	additiveDesc.SizeOverLife = {};
	additiveDesc.TextureIndex = additiveDesc.SamplerIndex = 1; // An opaque white sprite: alpha is the color's alone.
	const auto additive = Pack(additiveDesc);
	auto alphaDesc = Scene::Smoke();
	alphaDesc.Space = ParticleSpace::World;
	alphaDesc.ColorOverLife = { 1, { 0, 1, 1, 1 }, { { { 1.0f, 0.0f, 0.0f, 0.5f }, {}, {}, {} } } };
	alphaDesc.SizeOverLife = {};
	alphaDesc.TextureIndex = alphaDesc.SamplerIndex = 1;
	const auto alpha = Pack(alphaDesc);
	const auto particle = [](float z, std::uint32_t id)
	{
		GpuParticle p;
		p.Position[2] = z;
		p.Lifetime = 1.0f;
		p.Size = 1.0f;
		p.Id = id;
		return p;
	};
	Scene::Image image{ width, height, std::vector<Scene::Float4>(std::size_t(width) * height, { 0, 0, 0, 1 }), {} };
	std::vector<float> depth(image.Texels.size(), 0.0f);
	std::vector<Scene::DrawBatch> batches(2);
	batches[0] = { &additive, ParticleBlendMode::Additive, { particle(0.0f, 0), particle(0.5f, 1) } };
	batches[1] = { &alpha, ParticleBlendMode::AlphaBlend, { particle(-1.0f, 0), particle(1.0f, 1) } };
	Scene::SortBackToFront(batches[1].Particles, frame, alpha);
	SWIM_CHECK_EQUAL(batches[1].Particles[0].Id, 0u); // The farther one first.
	Scene::Rasterize(image, depth, frame, batches,
		[](const GpuParticleEmitter&, const Scene::Float2&)
		{
			return Scene::Float4{ 1, 1, 1, 1 };
		});
	const auto& centre = image.At(width / 2, height / 2);
	// Two additive sprites: (0.5, 1, 2); then two red layers of alpha 0.5 over it.
	SWIM_CHECK(!image.Ambiguous[(height / 2) * width + width / 2]);
	SWIM_CHECK(Near(centre[0], 0.875f, 1.0e-5f) && Near(centre[1], 0.25f, 1.0e-5f) && Near(centre[2], 0.5f, 1.0e-5f));
	SWIM_CHECK(Near(centre[3], 1.0f, 1.0e-6f));
	// A scene depth nearer than every particle hides them.
	std::vector<float> near(image.Texels.size(), 1.0f);
	Scene::Image hidden{ width, height, std::vector<Scene::Float4>(std::size_t(width) * height, { 0, 0, 0, 1 }), {} };
	Scene::Rasterize(hidden, near, frame, batches,
		[](const GpuParticleEmitter&, const Scene::Float2&)
		{
			return Scene::Float4{ 1, 1, 1, 1 };
		});
	SWIM_CHECK(hidden.At(width / 2, height / 2)[0] == 0.0f);
}
