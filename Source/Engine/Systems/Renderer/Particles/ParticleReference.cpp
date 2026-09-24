#include "Engine/Systems/Renderer/Particles/ParticleReference.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		bool Finite(float value)
		{
			return std::isfinite(value);
		}

		template <std::size_t N> bool AllFinite(const std::array<float, N>& values)
		{
			return std::all_of(values.begin(), values.end(), Finite);
		}

		bool Range(float low, float high)
		{
			return Finite(low) && Finite(high) && low <= high;
		}

		void ValidateKeys(std::uint32_t keyCount, const std::array<float, MaxParticleCurveKeys>& times, const char* what)
		{
			if (keyCount < 1 || keyCount > MaxParticleCurveKeys)
			{
				throw std::invalid_argument(std::string(what) + " needs 1 .. MaxParticleCurveKeys keys");
			}
			for (std::uint32_t k = 0; k < keyCount; ++k)
			{
				if (!Finite(times[k]) || times[k] < 0.0f || times[k] > 1.0f || (k > 0 && !(times[k] > times[k - 1])))
				{
					throw std::invalid_argument(std::string(what) + " key times must increase strictly within [0, 1]");
				}
			}
		}
	} // namespace

	void ValidateParticleEmitterDesc(const ParticleEmitterDesc& desc)
	{
		if (desc.Capacity < 1 || (desc.Blend == ParticleBlendMode::AlphaBlend && desc.Capacity > MaxSortedParticles))
		{
			throw std::invalid_argument("particle emitter capacity must be >= 1, and <= MaxSortedParticles when alpha blended");
		}
		if (!Finite(desc.Rate) || desc.Rate < 0.0f || !Finite(desc.Duration) || desc.Duration < 0.0f)
		{
			throw std::invalid_argument("particle emission rate and duration must be finite and non-negative");
		}
		for (const auto& burst : desc.Bursts)
		{
			if (!Finite(burst.Time) || burst.Time < 0.0f || (desc.Duration > 0.0f && burst.Time >= desc.Duration))
			{
				throw std::invalid_argument("particle bursts must lie in [0, Duration)");
			}
		}
		if (desc.Shape != ParticleShape::Point && desc.Shape != ParticleShape::Sphere && desc.Shape != ParticleShape::Box)
		{
			throw std::invalid_argument("unknown particle spawn shape");
		}
		if (!AllFinite(desc.ShapeExtent) || desc.ShapeExtent[0] < 0.0f || desc.ShapeExtent[1] < 0.0f || desc.ShapeExtent[2] < 0.0f)
		{
			throw std::invalid_argument("particle shape extents must be finite and non-negative");
		}
		const float directionLength = std::sqrt(
			desc.Direction[0] * desc.Direction[0] + desc.Direction[1] * desc.Direction[1] + desc.Direction[2] * desc.Direction[2]);
		if (!AllFinite(desc.Direction) || !(directionLength > 1.0e-6f) || !Finite(desc.ConeAngle) || desc.ConeAngle < 0.0f ||
			desc.ConeAngle > std::numbers::pi_v<float>)
		{
			throw std::invalid_argument("particle direction must be non-zero and the cone angle in [0, pi]");
		}
		if (!Range(desc.SpeedMin, desc.SpeedMax) || desc.SpeedMin < 0.0f || !Range(desc.LifetimeMin, desc.LifetimeMax) ||
			desc.LifetimeMin <= 0.0f || !Range(desc.SizeMin, desc.SizeMax) || desc.SizeMin <= 0.0f ||
			!Range(desc.RotationMin, desc.RotationMax) || !Range(desc.AngularVelocityMin, desc.AngularVelocityMax))
		{
			throw std::invalid_argument("particle spawn ranges must be finite with min <= max (speed >= 0, lifetime and size > 0)");
		}
		if (!AllFinite(desc.Gravity) || !Finite(desc.Drag) || desc.Drag < 0.0f)
		{
			throw std::invalid_argument("particle gravity must be finite and drag non-negative");
		}
		if (desc.Collision &&
			(desc.Space != ParticleSpace::World || !Finite(desc.GroundHeight) || !Finite(desc.Restitution) || desc.Restitution < 0.0f ||
				desc.Restitution > 1.0f || !Finite(desc.Friction) || desc.Friction < 0.0f || desc.Friction > 1.0f))
		{
			throw std::invalid_argument("particle collision needs world space, a finite ground and restitution/friction in [0, 1]");
		}
		ValidateKeys(desc.SizeOverLife.KeyCount, desc.SizeOverLife.Times, "particle size curve");
		ValidateKeys(desc.ColorOverLife.KeyCount, desc.ColorOverLife.Times, "particle color gradient");
		for (std::uint32_t k = 0; k < desc.SizeOverLife.KeyCount; ++k)
		{
			if (!Finite(desc.SizeOverLife.Values[k]) || desc.SizeOverLife.Values[k] < 0.0f)
			{
				throw std::invalid_argument("particle size curve values must be finite and non-negative");
			}
		}
		for (std::uint32_t k = 0; k < desc.ColorOverLife.KeyCount; ++k)
		{
			const auto& color = desc.ColorOverLife.Colors[k];
			if (!AllFinite(color) || color[0] < 0.0f || color[1] < 0.0f || color[2] < 0.0f || color[3] < 0.0f || color[3] > 1.0f)
			{
				throw std::invalid_argument("particle colors must be finite and non-negative with alpha in [0, 1]");
			}
		}
		if (desc.FlipbookColumns < 1 || desc.FlipbookRows < 1 || desc.FlipbookFrames < 1 ||
			std::uint64_t(desc.FlipbookFrames) > std::uint64_t(desc.FlipbookColumns) * desc.FlipbookRows ||
			!Finite(desc.FlipbookFrameRate) || desc.FlipbookFrameRate < 0.0f)
		{
			throw std::invalid_argument("particle flipbooks need 1 .. Columns * Rows frames and a non-negative frame rate");
		}
		if ((desc.TextureIndex == 0) != (desc.SamplerIndex == 0))
		{
			throw std::invalid_argument("a particle sprite needs both a texture and a sampler index (or neither)");
		}
	}

	GpuParticleEmitter PackParticleEmitter(const ParticleEmitterDesc& desc, const std::array<float, 12>& transform, std::uint32_t firstSlot,
		std::uint32_t spawnCount, std::uint32_t firstId)
	{
		ValidateParticleEmitterDesc(desc);
		if (!AllFinite(transform))
		{
			throw std::invalid_argument("particle emitter transform must be finite");
		}
		GpuParticleEmitter e;
		std::copy(transform.begin(), transform.end(), e.Transform);
		const float directionLength = std::sqrt(
			desc.Direction[0] * desc.Direction[0] + desc.Direction[1] * desc.Direction[1] + desc.Direction[2] * desc.Direction[2]);
		for (int c = 0; c < 3; ++c)
		{
			e.Gravity[c] = desc.Gravity[c];
			e.ShapeExtent[c] = desc.ShapeExtent[c];
			e.Direction[c] = desc.Direction[c] / directionLength;
		}
		e.Drag = desc.Drag;
		e.Shape = desc.Shape == ParticleShape::Sphere ? ParticleShapeSphere
			: desc.Shape == ParticleShape::Box		  ? ParticleShapeBox
													  : ParticleShapePoint;
		e.CosConeAngle = std::cos(desc.ConeAngle);
		e.SpeedMin = desc.SpeedMin;
		e.SpeedMax = desc.SpeedMax;
		e.LifetimeMin = desc.LifetimeMin;
		e.LifetimeMax = desc.LifetimeMax;
		e.SizeMin = desc.SizeMin;
		e.SizeMax = desc.SizeMax;
		e.RotationMin = desc.RotationMin;
		e.RotationMax = desc.RotationMax;
		e.AngularVelocityMin = desc.AngularVelocityMin;
		e.AngularVelocityMax = desc.AngularVelocityMax;
		e.GroundHeight = desc.GroundHeight;
		e.Restitution = desc.Restitution;
		e.Friction = desc.Friction;
		e.Flags = (desc.Space == ParticleSpace::Local ? ParticleFlagLocalSpace : 0u) | (desc.Collision ? ParticleFlagCollision : 0u) |
			(desc.Blend == ParticleBlendMode::AlphaBlend ? ParticleFlagSorted : 0u) | (desc.TextureIndex != 0 ? ParticleFlagTextured : 0u);
		e.FirstSlot = firstSlot;
		e.Capacity = desc.Capacity;
		e.SpawnCount = std::min(spawnCount, desc.Capacity);
		e.FirstId = firstId;
		e.Seed = desc.Seed;
		e.TextureIndex = desc.TextureIndex;
		e.SamplerIndex = desc.SamplerIndex;
		e.FlipbookColumns = desc.FlipbookColumns;
		e.FlipbookRows = desc.FlipbookRows;
		e.FlipbookFrames = desc.FlipbookFrames;
		e.FlipbookFrameRate = desc.FlipbookFrameRate;
		e.SizeKeyCount = desc.SizeOverLife.KeyCount;
		e.ColorKeyCount = desc.ColorOverLife.KeyCount;
		for (std::uint32_t k = 0; k < MaxParticleCurveKeys; ++k)
		{
			// Unused keys repeat the last one, so the shader never reads garbage.
			const std::uint32_t s = std::min(k, desc.SizeOverLife.KeyCount - 1);
			const std::uint32_t c = std::min(k, desc.ColorOverLife.KeyCount - 1);
			e.SizeTimes[k] = desc.SizeOverLife.Times[s];
			e.SizeValues[k] = desc.SizeOverLife.Values[s];
			e.ColorTimes[k] = desc.ColorOverLife.Times[c];
			for (int channel = 0; channel < 4; ++channel)
			{
				e.ColorValues[k * 4 + channel] = desc.ColorOverLife.Colors[c][channel];
			}
		}
		return e;
	}

	GpuParticleFrame BuildParticleFrame(const ParticleView& view, float deltaTime, std::uint32_t emitterCount, std::uint32_t maxSpawn)
	{
		if (!AllFinite(view.View) || !AllFinite(view.Projection) || !AllFinite(view.Jitter) || !Finite(deltaTime) || deltaTime < 0.0f)
		{
			throw std::invalid_argument("particle view, projection, jitter and delta time must be finite (dt >= 0)");
		}
		const auto& v = view.View;
		std::array<std::array<float, 3>, 3> rows{};
		for (int r = 0; r < 3; ++r)
		{
			const float length = std::sqrt(v[r * 4] * v[r * 4] + v[r * 4 + 1] * v[r * 4 + 1] + v[r * 4 + 2] * v[r * 4 + 2]);
			if (!(length > 1.0e-6f))
			{
				throw std::invalid_argument("particle view must be invertible");
			}
			rows[r] = { v[r * 4] / length, v[r * 4 + 1] / length, v[r * 4 + 2] / length };
		}
		GpuParticleFrame frame;
		// Jittered projection: clip.xy += jitter * clip.w.
		auto projection = view.Projection;
		for (int c = 0; c < 4; ++c)
		{
			projection[c] += view.Jitter[0] * projection[12 + c];
			projection[4 + c] += view.Jitter[1] * projection[12 + c];
		}
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				float sum = 0.0f;
				for (int k = 0; k < 4; ++k)
				{
					sum += projection[r * 4 + k] * v[k * 4 + c];
				}
				frame.ViewProjection[r * 4 + c] = sum;
			}
		}
		// Camera position of a rigid view: -R^T t.
		for (int c = 0; c < 3; ++c)
		{
			frame.CameraPosition[c] = -(v[c] * v[3] + v[4 + c] * v[7] + v[8 + c] * v[11]);
			frame.CameraRight[c] = rows[0][c];
			frame.CameraUp[c] = rows[1][c];
			frame.CameraForward[c] = -rows[2][c];
		}
		frame.DeltaTime = deltaTime;
		frame.EmitterCount = emitterCount;
		frame.MaxSpawn = maxSpawn;
		return frame;
	}
} // namespace Swim::Render

namespace Swim::Render::Particles
{
	namespace
	{
		constexpr float Pi = std::numbers::pi_v<float>;

		float Lerp(float a, float b, float t)
		{
			return a + (b - a) * t;
		}

		Float3 TransformPoint(const float (&rows)[12], const Float3& p)
		{
			Float3 result{};
			for (int r = 0; r < 3; ++r)
			{
				result[r] = rows[r * 4] * p[0] + rows[r * 4 + 1] * p[1] + rows[r * 4 + 2] * p[2] + rows[r * 4 + 3];
			}
			return result;
		}

		Float3 TransformVector(const float (&rows)[12], const Float3& v)
		{
			Float3 result{};
			for (int r = 0; r < 3; ++r)
			{
				result[r] = rows[r * 4] * v[0] + rows[r * 4 + 1] * v[1] + rows[r * 4 + 2] * v[2];
			}
			return result;
		}

		float NormalizedAge(const GpuParticle& particle)
		{
			return std::clamp(particle.Age / particle.Lifetime, 0.0f, 1.0f);
		}
	} // namespace

	std::uint32_t Hash(std::uint32_t value)
	{
		const std::uint32_t state = value * 747796405u + 2891336453u;
		const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
		return (word >> 22u) ^ word;
	}

	float Random(std::uint32_t id, std::uint32_t seed, std::uint32_t stream)
	{
		const std::uint32_t h = Hash(Hash(seed + stream * 0x9E3779B9u) ^ id);
		return float(h >> 8) * (1.0f / 16777216.0f);
	}

	std::uint32_t AdvanceEmission(const ParticleEmitterDesc& desc, ParticleEmitterClock& clock, float deltaTime)
	{
		const float t0 = clock.Time;
		const float t1 = t0 + deltaTime;
		const bool cycles = desc.Duration > 0.0f && desc.Looping;
		const float activeEnd = desc.Duration > 0.0f && !desc.Looping ? std::min(t1, desc.Duration) : t1;
		if (activeEnd > t0)
		{
			clock.Accumulator += desc.Rate * (activeEnd - t0);
		}
		const float whole = std::floor(clock.Accumulator);
		clock.Accumulator -= whole;
		double count = whole;
		for (const auto& burst : desc.Bursts)
		{
			if (cycles)
			{
				const double lo = std::max(0.0, std::ceil(double(t0 - burst.Time) / desc.Duration));
				const double hi = std::ceil(double(t1 - burst.Time) / desc.Duration) - 1.0;
				count += hi >= lo ? (hi - lo + 1.0) * burst.Count : 0.0;
			}
			else if (t0 <= burst.Time && burst.Time < t1)
			{
				count += burst.Count;
			}
		}
		clock.Time = t1;
		const auto spawned = static_cast<std::uint32_t>(std::min(count, double(desc.Capacity)));
		clock.NextId += spawned;
		return spawned;
	}

	GpuParticle SpawnParticle(const GpuParticleEmitter& e, std::uint32_t id)
	{
		const auto random = [&](std::uint32_t stream)
		{
			return Random(id, e.Seed, stream);
		};
		GpuParticle p;
		p.Id = id;
		p.Age = 0.0f;
		p.Lifetime = Lerp(e.LifetimeMin, e.LifetimeMax, random(0));
		const float speed = Lerp(e.SpeedMin, e.SpeedMax, random(1));
		// Direction in the cone around the axis (Duff et al. 2017 orthonormal basis).
		const float cosTheta = 1.0f - random(2) * (1.0f - e.CosConeAngle);
		const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
		const float phi = 2.0f * Pi * random(3);
		const Float3 d{ e.Direction[0], e.Direction[1], e.Direction[2] };
		const float sign = d[2] >= 0.0f ? 1.0f : -1.0f;
		const float a = -1.0f / (sign + d[2]);
		const float b = d[0] * d[1] * a;
		const Float3 tangent{ 1.0f + sign * d[0] * d[0] * a, sign * b, -sign * d[0] };
		const Float3 bitangent{ b, sign + d[1] * d[1] * a, -d[1] };
		const float x = std::cos(phi) * sinTheta;
		const float y = std::sin(phi) * sinTheta;
		Float3 velocity{};
		for (int c = 0; c < 3; ++c)
		{
			velocity[c] = (tangent[c] * x + bitangent[c] * y + d[c] * cosTheta) * speed;
		}
		Float3 position{ 0, 0, 0 };
		if (e.Shape == ParticleShapeSphere)
		{
			const float radius = e.ShapeExtent[0] * std::pow(random(4), 1.0f / 3.0f);
			const float z = 2.0f * random(5) - 1.0f;
			const float ring = std::sqrt(std::max(0.0f, 1.0f - z * z));
			const float angle = 2.0f * Pi * random(6);
			position = { radius * ring * std::cos(angle), radius * ring * std::sin(angle), radius * z };
		}
		else if (e.Shape == ParticleShapeBox)
		{
			for (int c = 0; c < 3; ++c)
			{
				position[c] = e.ShapeExtent[c] * (2.0f * random(4 + std::uint32_t(c)) - 1.0f);
			}
		}
		p.Size = Lerp(e.SizeMin, e.SizeMax, random(7));
		p.Rotation = Lerp(e.RotationMin, e.RotationMax, random(8));
		p.AngularVelocity = Lerp(e.AngularVelocityMin, e.AngularVelocityMax, random(9));
		if ((e.Flags & ParticleFlagLocalSpace) == 0)
		{
			position = TransformPoint(e.Transform, position);
			velocity = TransformVector(e.Transform, velocity);
		}
		for (int c = 0; c < 3; ++c)
		{
			p.Position[c] = position[c];
			p.Velocity[c] = velocity[c];
		}
		return p;
	}

	bool SimulateParticle(const GpuParticleEmitter& e, float dt, GpuParticle& p)
	{
		const float age = p.Age + dt;
		if (age >= p.Lifetime)
		{
			return false;
		}
		const float damping = std::max(0.0f, 1.0f - e.Drag * dt);
		for (int c = 0; c < 3; ++c)
		{
			p.Velocity[c] = (p.Velocity[c] + e.Gravity[c] * dt) * damping;
			p.Position[c] = p.Position[c] + p.Velocity[c] * dt;
		}
		p.Rotation = p.Rotation + p.AngularVelocity * dt;
		p.Age = age;
		if ((e.Flags & ParticleFlagCollision) != 0 && p.Position[1] < e.GroundHeight)
		{
			p.Position[1] = e.GroundHeight;
			if (p.Velocity[1] < 0.0f)
			{
				p.Velocity[1] = -p.Velocity[1] * e.Restitution;
				p.Velocity[0] *= 1.0f - e.Friction;
				p.Velocity[2] *= 1.0f - e.Friction;
			}
		}
		return true;
	}

	float EvaluateCurve(const float (&times)[4], const float (&values)[4], std::uint32_t keyCount, float t)
	{
		if (keyCount <= 1 || t <= times[0])
		{
			return values[0];
		}
		for (std::uint32_t k = 1; k < keyCount; ++k)
		{
			if (t <= times[k])
			{
				const float f = (t - times[k - 1]) / (times[k] - times[k - 1]);
				return Lerp(values[k - 1], values[k], f);
			}
		}
		return values[keyCount - 1];
	}

	Float4 EvaluateGradient(const GpuParticleEmitter& e, float t)
	{
		Float4 color{};
		for (int c = 0; c < 4; ++c)
		{
			const float channel[4] = { e.ColorValues[c], e.ColorValues[4 + c], e.ColorValues[8 + c], e.ColorValues[12 + c] };
			color[c] = EvaluateCurve(e.ColorTimes, channel, e.ColorKeyCount, t);
		}
		return color;
	}

	float ParticleSize(const GpuParticleEmitter& e, const GpuParticle& p)
	{
		return p.Size * EvaluateCurve(e.SizeTimes, e.SizeValues, e.SizeKeyCount, NormalizedAge(p));
	}

	std::uint32_t FlipbookFrame(const GpuParticleEmitter& e, const GpuParticle& p)
	{
		const std::uint32_t frames = e.FlipbookFrames;
		if (e.FlipbookFrameRate > 0.0f)
		{
			return static_cast<std::uint32_t>(std::floor(p.Age * e.FlipbookFrameRate)) % frames;
		}
		return std::min(static_cast<std::uint32_t>(std::floor(NormalizedAge(p) * float(frames))), frames - 1);
	}

	Float3 WorldPosition(const GpuParticleEmitter& e, const GpuParticle& p)
	{
		const Float3 position{ p.Position[0], p.Position[1], p.Position[2] };
		return (e.Flags & ParticleFlagLocalSpace) != 0 ? TransformPoint(e.Transform, position) : position;
	}

	float ViewDepth(const GpuParticleFrame& frame, const GpuParticleEmitter& e, const GpuParticle& p)
	{
		const auto world = WorldPosition(e, p);
		return (world[0] - frame.CameraPosition[0]) * frame.CameraForward[0] +
			(world[1] - frame.CameraPosition[1]) * frame.CameraForward[1] + (world[2] - frame.CameraPosition[2]) * frame.CameraForward[2];
	}

	bool DrawsBefore(float depthA, std::uint32_t idA, float depthB, std::uint32_t idB)
	{
		return depthA > depthB || (depthA == depthB && idA < idB);
	}

	BillboardVertex BillboardCorner(const GpuParticleFrame& frame, const GpuParticleEmitter& e, const GpuParticle& p, std::uint32_t corner)
	{
		const float ox = (corner & 1u) != 0 ? 1.0f : -1.0f;
		const float oy = (corner & 2u) != 0 ? 1.0f : -1.0f;
		const float half = 0.5f * ParticleSize(e, p);
		const float c = std::cos(p.Rotation);
		const float s = std::sin(p.Rotation);
		const auto center = WorldPosition(e, p);
		BillboardVertex vertex;
		for (int k = 0; k < 3; ++k)
		{
			const float right = frame.CameraRight[k] * c + frame.CameraUp[k] * s;
			const float up = frame.CameraUp[k] * c - frame.CameraRight[k] * s;
			vertex.Position[k] = center[k] + (right * ox + up * oy) * half;
		}
		const std::uint32_t index = FlipbookFrame(e, p);
		const float column = float(index % e.FlipbookColumns);
		const float row = float(index / e.FlipbookColumns);
		vertex.Uv = { (column + (ox + 1.0f) * 0.5f) / float(e.FlipbookColumns), (row + (1.0f - oy) * 0.5f) / float(e.FlipbookRows) };
		return vertex;
	}

	Float4 ShadeFragment(const GpuParticleEmitter& e, const GpuParticle& p, const Float2& local, const Float4& texel)
	{
		auto color = EvaluateGradient(e, NormalizedAge(p));
		if ((e.Flags & ParticleFlagTextured) != 0)
		{
			for (int c = 0; c < 4; ++c)
			{
				color[c] *= texel[c];
			}
		}
		else
		{
			const float u = 2.0f * local[0] - 1.0f;
			const float v = 2.0f * local[1] - 1.0f;
			color[3] *= std::clamp(1.0f - (u * u + v * v), 0.0f, 1.0f);
		}
		return { color[0] * color[3], color[1] * color[3], color[2] * color[3], color[3] };
	}

	ReferenceEmitter::ReferenceEmitter(std::uint32_t capacityInput) : capacity(capacityInput)
	{
	}

	void ReferenceEmitter::Step(const GpuParticleEmitter& emitter, float deltaTime)
	{
		std::erase_if(particles,
			[&](GpuParticle& particle)
			{
				return !SimulateParticle(emitter, deltaTime, particle);
			});
		const std::uint32_t free = capacity - static_cast<std::uint32_t>(particles.size());
		const std::uint32_t spawned = std::min(emitter.SpawnCount, free);
		dropped += emitter.SpawnCount - spawned;
		for (std::uint32_t i = 0; i < spawned; ++i)
		{
			particles.push_back(SpawnParticle(emitter, emitter.FirstId + i));
		}
	}
} // namespace Swim::Render::Particles
