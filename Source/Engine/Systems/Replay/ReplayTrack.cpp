#include "Engine/Systems/Replay/ReplayTrack.h"

#include <algorithm>
#include <cmath>

namespace Swim::Replay
{

	const char* ToString(Ease ease)
	{
		switch (ease)
		{
		case Ease::Step:
			return "step";
		case Ease::Smooth:
			return "smooth";
		case Ease::In:
			return "in";
		case Ease::Out:
			return "out";
		case Ease::Spline:
			return "spline";
		case Ease::Bezier:
			return "bezier";
		default:
			return "linear";
		}
	}

	bool ParseEase(std::string_view text, Ease& ease)
	{
		for (int i = 0; i <= static_cast<int>(Ease::Bezier); ++i)
		{
			if (text == ToString(static_cast<Ease>(i)))
			{
				ease = static_cast<Ease>(i);
				return true;
			}
		}

		return false;
	}

	float ApplyEase(Ease ease, float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);

		switch (ease)
		{
		case Ease::Step:
			return t >= 1.0f ? 1.0f : 0.0f;
		case Ease::Smooth:
			return t * t * (3.0f - 2.0f * t);
		case Ease::In:
			return t * t;
		case Ease::Out:
			return 1.0f - (1.0f - t) * (1.0f - t);
		case Ease::Bezier:
			return BezierEase({ 1.0f / 3.0f, 0.0f }, { -1.0f / 3.0f, 0.0f }, t);
		default:
			return t;
		}
	}

	float BezierEase(const std::array<float, 2>& out, const std::array<float, 2>& in, float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		const float x1 = std::isfinite(out[0]) ? std::clamp(out[0], 0.0f, 1.0f) : 1.0f / 3.0f;
		const float x2 = std::isfinite(in[0]) ? std::clamp(1.0f + in[0], 0.0f, 1.0f) : 2.0f / 3.0f;
		const float y1 = std::isfinite(out[1]) ? out[1] : 0.0f;
		const float y2 = std::isfinite(in[1]) ? 1.0f + in[1] : 1.0f;
		const auto cubic = [](float p1, float p2, float s)
		{
			const float u = 1.0f - s;
			return 3.0f * u * u * s * p1 + 3.0f * u * s * s * p2 + s * s * s;
		};

		// x(s) is non-decreasing with both x controls in [0, 1]: bisect for x(s) = t.
		float low = 0.0f;
		float high = 1.0f;

		for (int i = 0; i < 30; ++i)
		{
			const float mid = 0.5f * (low + high);
			(cubic(x1, x2, mid) < t ? low : high) = mid;
		}

		return cubic(y1, y2, 0.5f * (low + high));
	}

} // namespace Swim::Replay
