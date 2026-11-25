#include "Engine/Systems/Water/OceanHeightField.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace Engine
{

	namespace
	{

		float HalfToFloat(std::uint16_t h)
		{
			const std::uint32_t sign = std::uint32_t(h & 0x8000u) << 16;
			const std::uint32_t exponent = (h >> 10) & 0x1Fu;
			std::uint32_t mantissa = h & 0x3FFu;

			if (exponent == 0)
			{
				// Zero or subnormal: value = mantissa * 2^-24.
				const float value = std::ldexp(static_cast<float>(mantissa), -24);
				return sign ? -value : value;
			}

			if (exponent == 31)
			{
				return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
			}

			return std::bit_cast<float>(sign | ((exponent + 112u) << 23) | (mantissa << 13));
		}

		// Seconds from `from` to `to` on a clock that wraps every `repeat` seconds.
		double Elapsed(double from, double to, double repeat)
		{
			double d = to - from;

			if (repeat > 0.0)
			{
				d = std::fmod(d, repeat);
				d = d < -0.5 * repeat ? d + repeat : (d > 0.5 * repeat ? d - repeat : d);
			}

			return d;
		}

		glm::vec3 Bilinear(const OceanFieldCascade& c, glm::vec2 xz)
		{
			const float n = static_cast<float>(c.Resolution);
			const glm::vec2 t = xz / c.TileSize * n - 0.5f;
			const glm::vec2 base = glm::floor(t);
			const glm::vec2 f = t - base;
			const auto wrap = [&](float v)
			{
				const auto i = static_cast<std::int64_t>(v) % static_cast<std::int64_t>(c.Resolution);
				return static_cast<std::uint32_t>(i < 0 ? i + c.Resolution : i);
			};
			const std::uint32_t x0 = wrap(base.x), x1 = wrap(base.x + 1.0f), y0 = wrap(base.y), y1 = wrap(base.y + 1.0f);
			const auto at = [&](std::uint32_t x, std::uint32_t y)
			{
				return c.Displacement[std::size_t(y) * c.Resolution + x];
			};
			return glm::mix(glm::mix(at(x0, y0), at(x1, y0), f.x), glm::mix(at(x0, y1), at(x1, y1), f.x), f.y);
		}

	} // namespace

	OceanFieldCascade OceanFieldCascade::FromHalfFloats(
		std::span<const std::uint16_t> rgba, std::uint32_t resolution, std::uint32_t step, float tileSize)
	{
		if (resolution == 0 || step == 0 || resolution % step != 0 || rgba.size() < std::size_t(resolution) * resolution * 4)
		{
			throw std::invalid_argument("ocean field texels must cover resolution^2 RGBA values, decimated by a divisor");
		}

		OceanFieldCascade cascade;
		cascade.TileSize = tileSize;
		cascade.Resolution = resolution / step;
		cascade.Displacement.resize(std::size_t(cascade.Resolution) * cascade.Resolution);

		for (std::uint32_t y = 0; y < cascade.Resolution; ++y)
		{
			for (std::uint32_t x = 0; x < cascade.Resolution; ++x)
			{
				// The 2 x 2 texels around the block's centre, whose mean sits on the coarse texel's
				// centre (one texel for step 1).
				const std::uint32_t first = step > 1 ? step / 2 - 1 : 0;
				const std::uint32_t taps = step > 1 ? 2 : 1;
				glm::vec3 sum(0.0f);

				for (std::uint32_t dy = 0; dy < taps; ++dy)
				{
					for (std::uint32_t dx = 0; dx < taps; ++dx)
					{
						const std::size_t sx = x * step + first + dx;
						const std::size_t sy = y * step + first + dy;
						const std::uint16_t* texel = rgba.data() + (sy * resolution + sx) * 4;
						sum += glm::vec3(HalfToFloat(texel[0]), HalfToFloat(texel[1]), HalfToFloat(texel[2]));
					}
				}

				cascade.Displacement[std::size_t(y) * cascade.Resolution + x] = sum / float(taps * taps);
			}
		}

		return cascade;
	}

	void OceanHeightField::Publish(std::vector<OceanFieldCascade> cascades, double time, double repeatSeconds)
	{
		for (const auto& c : cascades)
		{
			if (c.Resolution == 0 || c.Displacement.size() != std::size_t(c.Resolution) * c.Resolution || !(c.TileSize > 0.0f))
			{
				throw std::invalid_argument("ocean field cascades need a positive tile size and resolution^2 texels");
			}
		}

		auto snapshot = std::make_shared<Snapshot>();
		snapshot->Cascades = std::move(cascades);
		snapshot->Time = time;
		snapshot->Repeat = repeatSeconds;
		std::scoped_lock lock(mutex);
		previous = std::move(current);
		current = std::move(snapshot);
	}

	bool OceanHeightField::HasData() const
	{
		std::scoped_lock lock(mutex);
		return current != nullptr;
	}

	void OceanHeightField::Clear()
	{
		std::scoped_lock lock(mutex);
		current.reset();
		previous.reset();
	}

	glm::vec3 OceanHeightField::Displacement(const Snapshot& snapshot, glm::vec2 rest)
	{
		glm::vec3 sum(0.0f);

		for (const auto& c : snapshot.Cascades)
		{
			// Bilinear in a tile-relative coordinate so large world positions keep their precision.
			const glm::vec2 local = rest - glm::floor(rest / c.TileSize) * c.TileSize;
			sum += Bilinear(c, local);
		}

		return sum;
	}

	FluidSample OceanHeightField::Sample(float x, float z, float seaLevel, double now) const
	{
		std::shared_ptr<const Snapshot> latest, before;
		{
			std::scoped_lock lock(mutex);
			latest = current;
			before = previous;
		}

		FluidSample sample;
		sample.SurfaceHeight = seaLevel;

		if (!latest || latest->Cascades.empty())
		{
			return sample;
		}

		// The rest point whose displaced position lands on (x, z): fixed-point iteration on the
		// horizontal offset (it converges while the sea does not fold over itself).
		const glm::vec2 target(x, z);
		glm::vec2 rest = target;
		glm::vec3 d(0.0f);

		for (int i = 0; i < 6; ++i)
		{
			d = Displacement(*latest, rest);
			rest = target - glm::vec2(d.x, d.z);
		}

		d = Displacement(*latest, rest);
		glm::vec3 velocity(0.0f);

		if (before && !before->Cascades.empty())
		{
			const double dt = Elapsed(before->Time, latest->Time, latest->Repeat);

			if (dt > 1.0e-4 && dt < 0.5)
			{
				velocity = (d - Displacement(*before, rest)) / static_cast<float>(dt);
			}
		}

		const double age = std::clamp(Elapsed(latest->Time, now, latest->Repeat), 0.0, 0.25);
		sample.SurfaceHeight = seaLevel + d.y + velocity.y * static_cast<float>(age);
		sample.FlowVelocity = velocity;
		return sample;
	}

} // namespace Engine
