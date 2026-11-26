#include "Engine/Systems/Replay/ReplayPath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Swim::Replay
{

	using namespace Animation;

	namespace
	{

		constexpr std::uint32_t TableSamplesPerSegment = 64;

		Vec3 CubicBezier(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t)
		{
			const float u = 1.0f - t;
			return Add(Add(Scale(p0, u * u * u), Scale(p1, 3.0f * u * u * t)), Add(Scale(p2, 3.0f * u * t * t), Scale(p3, t * t * t)));
		}

		// Barry-Goldman pyramid for a non-uniform Catmull-Rom segment from p1 to p2.
		Vec3 CatmullRomSegment(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t, float alpha)
		{
			const auto knot = [alpha](const Vec3& a, const Vec3& b)
			{
				return std::max(std::pow(Animation::Length(Sub(b, a)), alpha), 1.0e-4f);
			};
			const float t0 = 0.0f;
			const float t1 = t0 + knot(p0, p1);
			const float t2 = t1 + knot(p1, p2);
			const float t3 = t2 + knot(p2, p3);
			const float u = t1 + (t2 - t1) * t;
			const auto mix = [](const Vec3& a, const Vec3& b, float ta, float tb, float x)
			{
				return Add(Scale(a, (tb - x) / (tb - ta)), Scale(b, (x - ta) / (tb - ta)));
			};
			const Vec3 a1 = mix(p0, p1, t0, t1, u);
			const Vec3 a2 = mix(p1, p2, t1, t2, u);
			const Vec3 a3 = mix(p2, p3, t2, t3, u);
			const Vec3 b1 = mix(a1, a2, t0, t2, u);
			const Vec3 b2 = mix(a2, a3, t1, t3, u);
			return mix(b1, b2, t1, t2, u);
		}

		bool Finite(const Vec3& v)
		{
			return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
		}

		Vec3 OrZero(const Vec3& v)
		{
			return Finite(v) ? v : Vec3{ 0.0f, 0.0f, 0.0f };
		}

		Tooling::Json VecJson(const Vec3& v)
		{
			Tooling::Json a = Tooling::Json::MakeArray();
			a.Push(v[0]);
			a.Push(v[1]);
			a.Push(v[2]);
			return a;
		}

		Vec3 VecFrom(const Tooling::Json& j)
		{
			if (!j.IsArray() || j.Size() != 3)
			{
				throw std::runtime_error("expected [x, y, z]");
			}

			return { j[std::size_t{ 0 }].AsFloat(), j[std::size_t{ 1 }].AsFloat(), j[std::size_t{ 2 }].AsFloat() };
		}

	} // namespace

	const char* ToString(PathKind kind)
	{
		switch (kind)
		{
		case PathKind::Linear:
			return "linear";
		case PathKind::CatmullRom:
			return "catmull_rom";
		case PathKind::Bezier:
			return "bezier";
		default:
			return "centripetal";
		}
	}

	bool ParsePathKind(std::string_view text, PathKind& kind)
	{
		for (PathKind k : { PathKind::Linear, PathKind::CatmullRom, PathKind::Centripetal, PathKind::Bezier })
		{
			if (text == ToString(k))
			{
				kind = k;
				return true;
			}
		}

		return false;
	}

	std::size_t CameraPath::SegmentCount() const
	{
		if (Points.size() < 2)
		{
			return 0;
		}

		return Closed ? Points.size() : Points.size() - 1;
	}

	bool CameraPath::IsFinite() const
	{
		return std::all_of(Points.begin(), Points.end(),
			[](const PathPoint& p)
			{
				return Finite(p.Position) && Finite(p.InTangent) && Finite(p.OutTangent);
			});
	}

	std::uint64_t CameraPath::Key() const
	{
		// FNV-1a over everything the curve depends on.
		std::uint64_t key = 1469598103934665603ull;
		const auto mix = [&key](const void* data, std::size_t size)
		{
			const auto* bytes = static_cast<const unsigned char*>(data);

			for (std::size_t i = 0; i < size; ++i)
			{
				key = (key ^ bytes[i]) * 1099511628211ull;
			}
		};
		mix(&Kind, sizeof(Kind));
		mix(&Closed, sizeof(Closed));
		mix(Points.data(), Points.size() * sizeof(PathPoint));
		return key;
	}

	const PathPoint& CameraPath::Point(std::ptrdiff_t index) const
	{
		static const PathPoint origin;
		const auto n = static_cast<std::ptrdiff_t>(Points.size());

		if (n == 0)
		{
			return origin;
		}

		if (Closed)
		{
			return Points[static_cast<std::size_t>(((index % n) + n) % n)];
		}

		return Points[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(index, 0, n - 1))];
	}

	void CameraPath::AutoTangents()
	{
		const auto n = static_cast<std::ptrdiff_t>(Points.size());

		for (std::ptrdiff_t i = 0; i < n; ++i)
		{
			const Vec3 previous = Point(i - 1).Position;
			const Vec3 next = Point(i + 1).Position;
			const Vec3 tangent = Scale(Sub(next, previous), 0.5f);
			const Vec3& here = Points[static_cast<std::size_t>(i)].Position;
			const float inLength = Animation::Length(Sub(here, previous)) / 3.0f;
			const float outLength = Animation::Length(Sub(next, here)) / 3.0f;
			const float size = std::max(Animation::Length(tangent), 1.0e-6f);
			Points[static_cast<std::size_t>(i)].InTangent = Scale(tangent, -inLength / size);
			Points[static_cast<std::size_t>(i)].OutTangent = Scale(tangent, outLength / size);
		}

		Invalidate();
	}

	Vec3 CameraPath::Evaluate(float parameter) const
	{
		if (Points.empty())
		{
			return { 0.0f, 0.0f, 0.0f };
		}

		const std::size_t segments = SegmentCount();

		if (segments == 0)
		{
			return OrZero(Points.front().Position);
		}

		parameter = std::isfinite(parameter) ? std::clamp(parameter, 0.0f, static_cast<float>(segments)) : 0.0f;
		const auto segment = std::min(static_cast<std::size_t>(parameter), segments - 1);
		const float t = parameter - static_cast<float>(segment);
		const auto i = static_cast<std::ptrdiff_t>(segment);
		PathPoint a = Point(i);
		PathPoint b = Point(i + 1);

		// A broken point (NaN from a bad edit) is never interpolated.
		if (!IsFinite())
		{
			a = { OrZero(a.Position), OrZero(a.InTangent), OrZero(a.OutTangent) };
			b = { Finite(b.Position) ? b.Position : a.Position, OrZero(b.InTangent), OrZero(b.OutTangent) };
		}

		switch (Kind)
		{
		case PathKind::Linear:
			return Animation::Lerp(a.Position, b.Position, t);
		case PathKind::Bezier:
			return CubicBezier(a.Position, Add(a.Position, a.OutTangent), Add(b.Position, b.InTangent), b.Position, t);
		default:
		{
			// Open ends mirror the neighbour so the curve starts and ends on its points.
			const Vec3 p0 = (!Closed && i == 0) ? Sub(Scale(a.Position, 2.0f), b.Position) : Point(i - 1).Position;
			const Vec3 p3 = (!Closed && segment + 1 == segments) ? Sub(Scale(b.Position, 2.0f), a.Position) : Point(i + 2).Position;

			if (!Finite(p0) || !Finite(p3))
			{
				return Animation::Lerp(a.Position, b.Position, t);
			}

			return CatmullRomSegment(p0, a.Position, b.Position, p3, t, Kind == PathKind::Centripetal ? 0.5f : 0.0f);
		}
		}
	}

	void CameraPath::Build() const
	{
		table.clear();
		tableKey = Key();
		const std::size_t segments = SegmentCount();

		if (segments == 0)
		{
			return;
		}

		const std::size_t samples = segments * TableSamplesPerSegment;
		table.reserve(samples + 1);
		Vec3 previous = Evaluate(0.0f);
		float length = 0.0f;
		table.emplace_back(0.0f, 0.0f);

		for (std::size_t s = 1; s <= samples; ++s)
		{
			const float parameter = static_cast<float>(s) / TableSamplesPerSegment;
			const Vec3 position = Evaluate(parameter);
			const float step = Animation::Length(Sub(position, previous));
			length += std::isfinite(step) ? step : 0.0f;
			previous = position;
			table.emplace_back(length, parameter);
		}
	}

	float CameraPath::Length() const
	{
		if (table.empty() || tableKey != Key())
		{
			Build();
		}

		return table.empty() ? 0.0f : table.back().first;
	}

	float CameraPath::ParameterAt(float fraction) const
	{
		const float length = Length();

		if (table.empty() || length <= 0.0f)
		{
			return 0.0f;
		}

		if (!std::isfinite(fraction))
		{
			fraction = 0.0f;
		}

		fraction = Closed ? fraction - std::floor(fraction) : std::clamp(fraction, 0.0f, 1.0f);
		const float target = fraction * length;
		const auto next = std::lower_bound(table.begin(), table.end(), target,
			[](const std::pair<float, float>& entry, float value)
			{
				return entry.first < value;
			});

		if (next == table.begin())
		{
			return 0.0f;
		}

		if (next == table.end())
		{
			return table.back().second;
		}

		const auto& [l1, p1] = *next;
		const auto& [l0, p0] = *(next - 1);
		const float span = l1 - l0;
		return span > 0.0f ? p0 + (p1 - p0) * (target - l0) / span : p1;
	}

	Vec3 CameraPath::Sample(float fraction) const
	{
		return Evaluate(ParameterAt(fraction));
	}

	Vec3 CameraPath::Direction(float fraction) const
	{
		const float length = Length();
		const float step = length > 0.0f ? std::min(0.01f, 0.05f / length) : 0.01f;
		const float a = Closed ? fraction - step : std::max(fraction - step, 0.0f);
		const float b = Closed ? fraction + step : std::min(fraction + step, 1.0f);
		const Vec3 d = Sub(Sample(b), Sample(a));
		const float size = Animation::Length(d);
		return size > 1.0e-6f ? Scale(d, 1.0f / size) : Vec3{ 0.0f, 0.0f, -1.0f };
	}

	std::vector<Vec3> CameraPath::Polyline(std::uint32_t samplesPerSegment) const
	{
		std::vector<Vec3> points;
		const std::size_t segments = SegmentCount();
		samplesPerSegment = std::clamp(samplesPerSegment, 1u, 1024u);

		if (segments == 0)
		{
			if (!Points.empty())
			{
				points.push_back(Points.front().Position);
			}

			return points;
		}

		for (std::size_t s = 0; s <= segments * samplesPerSegment; ++s)
		{
			points.push_back(Evaluate(static_cast<float>(s) / samplesPerSegment));
		}

		return points;
	}

	float CameraPath::Nearest(const Vec3& point) const
	{
		const float length = Length();

		if (table.empty() || length <= 0.0f)
		{
			return 0.0f;
		}

		float best = 0.0f;
		float bestDistance = std::numeric_limits<float>::max();

		for (const auto& [distance, parameter] : table)
		{
			const float d = Animation::Length(Sub(Evaluate(parameter), point));

			if (d < bestDistance)
			{
				bestDistance = d;
				best = distance / length;
			}
		}

		return best;
	}

	Tooling::Json CameraPath::ToJson() const
	{
		Tooling::Json j = Tooling::Json::MakeObject();
		j["kind"] = ToString(Kind);
		j["closed"] = Closed;
		Tooling::Json points = Tooling::Json::MakeArray();

		for (const PathPoint& p : Points)
		{
			Tooling::Json point = Tooling::Json::MakeObject();
			point["position"] = VecJson(p.Position);

			if (Kind == PathKind::Bezier)
			{
				point["in"] = VecJson(p.InTangent);
				point["out"] = VecJson(p.OutTangent);
			}

			points.Push(std::move(point));
		}

		j["points"] = std::move(points);
		return j;
	}

	CameraPath CameraPath::FromJson(const Tooling::Json& json)
	{
		CameraPath path;

		if (const Tooling::Json* kind = json.Find("kind"); kind && !ParsePathKind(kind->AsString(), path.Kind))
		{
			throw std::runtime_error("unknown path kind: " + kind->AsString());
		}

		path.Closed = json["closed"].AsBool(false);

		for (const Tooling::Json& p : json["points"].GetArray())
		{
			PathPoint point;
			point.Position = VecFrom(p.IsArray() ? p : p["position"]);

			if (p.IsObject())
			{
				if (const Tooling::Json* in = p.Find("in"))
				{
					point.InTangent = VecFrom(*in);
				}

				if (const Tooling::Json* out = p.Find("out"))
				{
					point.OutTangent = VecFrom(*out);
				}
			}

			path.Points.push_back(point);
		}

		// Bezier paths given without handles get smooth ones.
		if (path.Kind == PathKind::Bezier && !path.Points.empty() &&
			std::all_of(path.Points.begin(), path.Points.end(),
				[](const PathPoint& p)
				{
					return p.InTangent == Vec3{} && p.OutTangent == Vec3{};
				}))
		{
			path.AutoTangents();
		}

		return path;
	}

} // namespace Swim::Replay
