#pragma once

#include "Engine/Systems/Replay/ReplayTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

namespace Swim::Replay
{

	// How a key moves to the next one.
	enum class Ease : std::uint8_t
	{
		Step,	// Hold until the next key.
		Linear, // Constant speed.
		Smooth, // Ease in and out (smoothstep).
		In,		// Accelerate.
		Out,	// Decelerate.
		Spline, // Catmull-Rom through the neighbouring keys (smooth camera paths).
		Bezier, // A cubic Bezier shaped by this key's OutHandle and the next key's InHandle.
	};

	const char* ToString(Ease ease);

	bool ParseEase(std::string_view text, Ease& ease);

	float ApplyEase(Ease ease, float t);

	// The Bezier ease of one segment, in segment-normalized (time fraction, value fraction)
	// space: P0 = (0, 0), P1 = out, P2 = (1, 1) + in, P3 = (1, 1). Handle x is clamped into
	// [0, 1] so time stays monotonic; y is free (overshoot is allowed). Returns the value
	// fraction at time fraction t (clamped to [0, 1]).
	float BezierEase(const std::array<float, 2>& out, const std::array<float, 2>& in, float t);

	namespace TrackMath
	{

		inline float Lerp(float a, float b, float t)
		{
			return a + (b - a) * t;
		}

		template <std::size_t N> std::array<float, N> Lerp(const std::array<float, N>& a, const std::array<float, N>& b, float t)
		{
			std::array<float, N> out;

			for (std::size_t i = 0; i < N; ++i)
			{
				out[i] = a[i] + (b[i] - a[i]) * t;
			}

			return out;
		}

		inline float CatmullRom(float p0, float p1, float p2, float p3, float t)
		{
			const float t2 = t * t;
			const float t3 = t2 * t;
			return 0.5f *
				(2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3);
		}

		template <std::size_t N>
		std::array<float, N> CatmullRom(const std::array<float, N>& p0, const std::array<float, N>& p1, const std::array<float, N>& p2,
			const std::array<float, N>& p3, float t)
		{
			std::array<float, N> out;

			for (std::size_t i = 0; i < N; ++i)
			{
				out[i] = CatmullRom(p0[i], p1[i], p2[i], p3[i], t);
			}

			return out;
		}

		// Per value type: how keys blend. Rotations slerp (a spline eases the parameter).
		template <typename T> struct Interp
		{
			static T Blend(const T& a, const T& b, float t) { return Lerp(a, b, t); }

			static T Spline(const T& p0, const T& p1, const T& p2, const T& p3, float t) { return CatmullRom(p0, p1, p2, p3, t); }
		};

		template <> struct Interp<Quat>
		{
			static Quat Blend(const Quat& a, const Quat& b, float t) { return Animation::Slerp(a, b, t); }

			static Quat Spline(const Quat&, const Quat& p1, const Quat& p2, const Quat&, float t)
			{
				return Animation::Slerp(p1, p2, t * t * (3.0f - 2.0f * t));
			}
		};

		template <> struct Interp<bool>
		{
			static bool Blend(bool a, bool b, float t) { return t >= 1.0f ? b : a; }

			static bool Spline(bool, bool p1, bool p2, bool, float t) { return t >= 1.0f ? p2 : p1; }
		};

		template <> struct Interp<Value>
		{
			static Value Blend(const Value& a, const Value& b, float t) { return Replay::Blend(a, b, t); }

			static Value Spline(const Value&, const Value& p1, const Value& p2, const Value&, float t) { return Replay::Blend(p1, p2, t); }
		};

	} // namespace TrackMath

	template <typename T> struct TrackKey
	{
		double Time = 0.0;
		T Data{};
		Ease Interp = Ease::Linear;
		// Bezier handles (Ease::Bezier), relative to their key: x = time as a fraction of the
		// segment's duration, y = value as a fraction of the segment's value change. This
		// key's OutHandle and the next key's InHandle shape the segment between them. The
		// defaults give flat tangents (ease in and out); (1/3, 1/3) and (-1/3, -1/3) is linear.
		std::array<float, 2> OutHandle{ 1.0f / 3.0f, 0.0f };
		std::array<float, 2> InHandle{ -1.0f / 3.0f, 0.0f };

		bool HasDefaultHandles() const
		{
			return OutHandle[0] == 1.0f / 3.0f && OutHandle[1] == 0.0f && InHandle[0] == -1.0f / 3.0f && InHandle[1] == 0.0f;
		}
	};

	// The eased fraction (0 at a, 1 at b) of the segment a -> b at time fraction t, exactly as
	// Track::Evaluate blends it, for drawing curves. Spline segments (which depend on the
	// neighbouring keys) report t.
	template <typename T> float EvaluateSegmentFraction(const TrackKey<T>& a, const TrackKey<T>& b, float t)
	{
		if (a.Interp == Ease::Bezier)
		{
			return BezierEase(a.OutHandle, b.InHandle, t);
		}

		return a.Interp == Ease::Spline ? std::clamp(t, 0.0f, 1.0f) : ApplyEase(a.Interp, t);
	}

	// Keyframed values over time. Before the first key the first value holds, after the
	// last key the last value holds; an empty track has no value (no override).
	template <typename T> class Track
	{

	  public:
		std::vector<TrackKey<T>> Keys; // Sorted by time.

		bool Empty() const { return Keys.empty(); }

		// Adds a key or replaces the one at (nearly) the same time. Returns its index.
		// A non-finite time is ignored (returns Keys.size()).
		std::size_t Set(double time, T value, Ease ease = Ease::Linear)
		{
			if (!std::isfinite(time))
			{
				return Keys.size();
			}

			const auto at = std::lower_bound(Keys.begin(), Keys.end(), time - Epsilon,
				[](const TrackKey<T>& k, double t)
				{
					return k.Time < t;
				});

			if (at != Keys.end() && std::abs(at->Time - time) <= Epsilon)
			{
				at->Data = std::move(value);
				at->Interp = ease;
				return static_cast<std::size_t>(at - Keys.begin());
			}

			return static_cast<std::size_t>(Keys.insert(at, TrackKey<T>{ time, std::move(value), ease }) - Keys.begin());
		}

		// Removes the key nearest to time within tolerance.
		bool Remove(double time, double tolerance = Epsilon)
		{
			auto best = Keys.end();
			double bestDistance = tolerance;

			for (auto it = Keys.begin(); it != Keys.end(); ++it)
			{
				const double distance = std::abs(it->Time - time);

				if (distance <= bestDistance)
				{
					best = it;
					bestDistance = distance;
				}
			}

			if (best == Keys.end())
			{
				return false;
			}

			Keys.erase(best);
			return true;
		}

		std::optional<T> Evaluate(double time) const
		{
			if (Keys.empty())
			{
				return std::nullopt;
			}

			if (time <= Keys.front().Time)
			{
				return Keys.front().Data;
			}

			if (time >= Keys.back().Time)
			{
				return Keys.back().Data;
			}

			const auto next = std::upper_bound(Keys.begin(), Keys.end(), time,
				[](double t, const TrackKey<T>& k)
				{
					return t < k.Time;
				});
			const std::size_t i = static_cast<std::size_t>(next - Keys.begin()) - 1;
			const TrackKey<T>& a = Keys[i];
			const TrackKey<T>& b = Keys[i + 1];
			const double span = b.Time - a.Time;
			const float t = span > 0.0 ? static_cast<float>((time - a.Time) / span) : 1.0f;

			if (a.Interp == Ease::Step)
			{
				return a.Data;
			}

			if (a.Interp == Ease::Spline)
			{
				const T& p0 = i > 0 ? Keys[i - 1].Data : a.Data;
				const T& p3 = i + 2 < Keys.size() ? Keys[i + 2].Data : b.Data;
				return TrackMath::Interp<T>::Spline(p0, a.Data, b.Data, p3, t);
			}

			return TrackMath::Interp<T>::Blend(a.Data, b.Data, EvaluateSegmentFraction(a, b, t));
		}

		static constexpr double Epsilon = 1.0e-6;
	};

} // namespace Swim::Replay
