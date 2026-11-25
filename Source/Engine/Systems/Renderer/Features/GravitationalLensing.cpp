#include "Engine/Systems/Renderer/Features/GravitationalLensing.h"

#include "Engine/Math/Hash.h"
#include "Engine/Math/VectorMath.h"

#include <algorithm>
#include <cmath>
#include <span>

#include <glm/glm.hpp>

namespace Engine
{

	namespace
	{

		using ::Engine::NormalizeOr;
		using ::Engine::ToGlm;

		// glm::normalize, keeping a zero vector zero.
		glm::vec3 Normalize(const glm::vec3& v)
		{
			return NormalizeOr(v, v);
		}

		glm::vec3 Acceleration(const glm::vec3& x, const glm::vec3& v, float rs)
		{
			const glm::vec3 h = glm::cross(x, v);
			const float r2 = glm::dot(x, x);
			return x * (-1.5f * rs * glm::dot(h, h) / (r2 * r2 * std::sqrt(r2)));
		}

		glm::vec3 RotateAbout(const glm::vec3& p, const glm::vec3& axis, float angle)
		{
			const float c = std::cos(angle);
			const float s = std::sin(angle);
			return p * c + glm::cross(axis, p) * s + axis * (glm::dot(axis, p) * (1.0f - c));
		}

		float Lattice(int x, int y, int z)
		{
			const std::uint32_t key = std::uint32_t(x) * 73856093u ^ std::uint32_t(y) * 19349663u ^ std::uint32_t(z) * 83492791u;
			return float(::Engine::PcgHash(key)) * (1.0f / 4294967296.0f);
		}

		float ValueNoise(const glm::vec3& p)
		{
			const float cx = std::floor(p.x), cy = std::floor(p.y), cz = std::floor(p.z);
			const float fx = p.x - cx, fy = p.y - cy, fz = p.z - cz;
			const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy), sz = fz * fz * (3 - 2 * fz);
			const int x = int(cx), y = int(cy), z = int(cz);
			const auto lerp = [](float a, float b, float t)
			{
				return a + (b - a) * t;
			};
			const float x00 = lerp(Lattice(x, y, z), Lattice(x + 1, y, z), sx);
			const float x10 = lerp(Lattice(x, y + 1, z), Lattice(x + 1, y + 1, z), sx);
			const float x01 = lerp(Lattice(x, y, z + 1), Lattice(x + 1, y, z + 1), sx);
			const float x11 = lerp(Lattice(x, y + 1, z + 1), Lattice(x + 1, y + 1, z + 1), sx);
			return lerp(lerp(x00, x10, sy), lerp(x01, x11, sy), sz);
		}

		float Fbm(const glm::vec3& p)
		{
			return 0.67f * ValueNoise(p) + 0.33f * ValueNoise(p * 2.03f + glm::vec3(11.7f));
		}

	} // namespace

	GravitationalLensing::Lens& GravitationalLensing::Upsert(std::uint64_t owner)
	{
		for (auto& lens : Lenses)
		{
			if (lens.Owner == owner)
			{
				return lens;
			}
		}

		Lenses.push_back({});
		Lenses.back().Owner = owner;
		return Lenses.back();
	}

	void GravitationalLensing::Remove(std::uint64_t owner)
	{
		std::erase_if(Lenses,
			[owner](const Lens& lens)
			{
				return lens.Owner == owner;
			});
	}

	float GravitationalLensing::ShadowRadius(float schwarzschildRadius)
	{
		return 1.5f * std::sqrt(3.0f) * schwarzschildRadius;
	}

	float GravitationalLensing::Deflection(float schwarzschildRadius, float impact)
	{
		return 2.0f * schwarzschildRadius / impact;
	}

	float GravitationalLensing::EinsteinAngle(float schwarzschildRadius, float lensDistance)
	{
		return std::sqrt(2.0f * schwarzschildRadius / lensDistance);
	}

	GravitationalLensing::Trace GravitationalLensing::TraceRay(const Float3& origin, const Float3& direction, float rs, float region)
	{
		Trace trace;
		glm::vec3 x = ToGlm(origin);
		glm::vec3 v = Normalize(ToGlm(direction));

		for (; trace.Steps < MaxSteps; ++trace.Steps)
		{
			const float r = glm::length(x);

			if (r < rs)
			{
				trace.Captured = true;
				break;
			}

			if (r > region * 1.0001f && glm::dot(x, v) > 0.0f)
			{
				trace.Escaped = true;
				break;
			}

			const float ds = std::max(0.14f * (r - 0.5f * rs), 0.02f * rs);
			const glm::vec3 a1 = Acceleration(x, v, rs);
			const glm::vec3 xm = x + v * (0.5f * ds);
			const glm::vec3 vm = Normalize(v + a1 * (0.5f * ds));
			const glm::vec3 a2 = Acceleration(xm, vm, rs);
			x = x + vm * ds;
			v = Normalize(v + a2 * ds);
		}

		trace.Direction = ::Engine::ToArray(v);
		return trace;
	}

	float GravitationalLensing::GasDensityAt(const Float3& position, float time, const Lens& lens)
	{
		const glm::vec3 p = ToGlm(position);
		const float outer = lens.GasRadius;
		const float r = glm::length(p);

		if (r < 1.2f || r > outer)
		{
			return 0.0f;
		}

		const glm::vec3 axis = Normalize(ToGlm(lens.DiskNormal));
		const glm::vec3 e1 =
			Normalize(std::abs(axis.y) < 0.9f ? glm::cross(axis, glm::vec3(0, 1, 0)) : glm::cross(axis, glm::vec3(1, 0, 0)));
		const glm::vec3 e2 = glm::cross(axis, e1);
		float density = 0.0f;
		{
			const float z = glm::dot(p, axis);
			const float rho = glm::length(p + axis * -z);
			const float omega = lens.GasSpeed * std::pow(std::max(rho, 1.5f) / 3.0f, -1.5f);
			const glm::vec3 q = RotateAbout(p, axis, -omega * time);
			const float height = 0.3f + 0.12f * rho;
			const float radial = glm::smoothstep(2.2f, 3.4f, rho) * (1.0f - glm::smoothstep(outer * 0.55f, outer * 0.95f, rho));
			const float envelope = radial * std::exp(-z * z / (height * height));
			const float turbulence = envelope > 1.0e-3f ? std::clamp(Fbm(q * 0.85f + glm::vec3(7.1f)) * 1.7f - 0.35f, 0.0f, 1.0f) : 0.0f;
			density += envelope * turbulence;
		}
		const std::uint32_t rings = std::min(lens.Orbits, 3u);

		for (std::uint32_t k = 0; k < rings; ++k)
		{
			const float azimuth = float(k) * 2.0943951f;
			const glm::vec3 ringAxis = Normalize(axis * 0.42f + (e1 * std::cos(azimuth) + e2 * std::sin(azimuth)) * 0.91f);
			const float radius = 4.2f + 1.4f * float(k);
			const float z = glm::dot(p, ringAxis);
			const float rho = glm::length(p + ringAxis * -z);
			const float tube = 0.35f + 0.1f * float(k);
			const float falloff = std::exp(-((rho - radius) * (rho - radius) + z * z) / (tube * tube));

			if (falloff < 1.0e-3f)
			{
				continue;
			}

			const float omega = 2.2f * lens.GasSpeed * std::pow(radius / 3.0f, -1.5f) * (k == 1u ? -1.0f : 1.0f);
			const glm::vec3 q = RotateAbout(p, ringAxis, -omega * time);
			const glm::vec3 f1 = Normalize(
				std::abs(ringAxis.y) < 0.9f ? glm::cross(ringAxis, glm::vec3(0, 1, 0)) : glm::cross(ringAxis, glm::vec3(1, 0, 0)));
			const float angle = std::atan2(glm::dot(q, glm::cross(ringAxis, f1)), glm::dot(q, f1));
			const float clumps = std::pow(0.5f + 0.5f * std::cos(3.0f * angle), 10.0f);
			const float stream = std::clamp(Fbm(q * 1.5f + glm::vec3(float(k) * 13.0f)) * 1.5f - 0.3f, 0.0f, 1.0f);
			density += falloff * (0.25f + 0.6f * stream + 1.6f * clumps);
		}

		return density * lens.GasDensity;
	}

	void GravitationalLensing::Record(RenderFeatureContext& context)
	{
		const auto& view = context.View();

		if (Lenses.empty() || view.Width == 0 || view.Height == 0)
		{
			return;
		}

		struct LensRecord
		{
			float PositionRadius[4];
			float Params[4];
			float Gas[4];
			float Normal[4];
		};

		struct Params
		{
			float Forward[4];
			float Right[4];
			float Up[4];
			float Camera[4];
			float Misc[4];
			std::uint32_t Size[4];
			LensRecord Lenses[MaxLenses];
		} params{};

		static_assert(sizeof(Params) == 96 + 64 * MaxLenses);
		std::uint32_t count = 0;

		for (const auto& lens : Lenses)
		{
			if (count == MaxLenses)
			{
				break;
			}

			if (!(lens.SchwarzschildRadius > 0.0f) || !std::isfinite(lens.SchwarzschildRadius))
			{
				continue;
			}

			auto& record = params.Lenses[count++];

			for (int c = 0; c < 3; ++c)
			{
				record.PositionRadius[c] = lens.Position[c];
			}

			record.PositionRadius[3] = lens.SchwarzschildRadius;
			record.Params[0] = std::clamp(lens.Strength, 0.0f, 10.0f);
			record.Params[1] = std::clamp(lens.Reach, 4.0f, 200.0f);
			record.Params[2] = std::clamp(lens.GasRadius, 3.0f, record.Params[1]);
			record.Params[3] = std::clamp(lens.GasDensity, 0.0f, 20.0f);
			record.Gas[0] = std::clamp(lens.GasBrightness, 0.0f, 1000.0f);
			record.Gas[1] = std::clamp(lens.GasSpeed, -50.0f, 50.0f);
			record.Gas[2] = float(std::min(lens.Orbits, 3u));
			const glm::vec3 normal = Normalize(ToGlm(lens.DiskNormal));
			const bool valid = std::isfinite(normal.x) && glm::dot(normal, normal) > 0.5f;

			for (int c = 0; c < 3; ++c)
			{
				record.Normal[c] = valid ? normal[c] : (c == 1 ? 1.0f : 0.0f);
			}
		}

		if (count == 0)
		{
			return;
		}

		for (int c = 0; c < 3; ++c)
		{
			params.Forward[c] = view.Forward[c];
			params.Right[c] = view.Right[c];
			params.Up[c] = view.Up[c];
			params.Camera[c] = view.Position[c];
		}

		params.Forward[3] = view.TanHalfFovX;
		params.Right[3] = view.TanHalfFovY;
		params.Up[3] = std::max(view.Projection[11], 1.0e-4f);
		params.Camera[3] = float(count);
		params.Misc[0] = std::fmod(view.Time, 3600.0f);
		params.Size[0] = view.Width;
		params.Size[1] = view.Height;

		auto& graph = context.Graph();
		const auto buffer =
			graph.CreateUpload(std::as_bytes(std::span(&params, 1)), "Lensing params", Swim::Rhi::BufferUsage::Storage, 16);
		const auto output = context.CreateColorTarget("Gravitational lensing");
		context.Compute("GravitationalLensing")
			.Buffer("Params", buffer)
			.Texture("Color", context.Color())
			.Texture("Depth", context.Depth())
			.Sampler("LinearClamp")
			.Storage("Output", output)
			.Dispatch(view.Width, view.Height);
		context.SetColor(output);
	}

} // namespace Engine
