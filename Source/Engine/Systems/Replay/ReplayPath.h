#pragma once

#include "Engine/Systems/Replay/ReplayTypes.h"
#include "Engine/Systems/Tooling/Json.h"

#include <string_view>
#include <vector>

namespace Swim::Replay
{

	enum class PathKind : std::uint8_t
	{
		Linear,		 // Straight segments.
		CatmullRom,	 // Uniform Catmull-Rom through the points.
		Centripetal, // Catmull-Rom with alpha 0.5: no cusps or loops on uneven spacing.
		Bezier,		 // Cubic segments through the points with explicit tangent handles.
	};

	const char* ToString(PathKind kind);

	bool ParsePathKind(std::string_view text, PathKind& kind);

	struct PathPoint
	{
		Vec3 Position{ 0.0f, 0.0f, 0.0f };
		Vec3 InTangent{ 0.0f, 0.0f, 0.0f };	 // Bezier handle before the point, relative to it.
		Vec3 OutTangent{ 0.0f, 0.0f, 0.0f }; // Bezier handle after the point, relative to it.
	};

	// A spline through points, sampled by arc length so a camera moving along it keeps a
	// constant speed for a linear progress (eases on the progress track shape the speed).
	class CameraPath
	{

	  public:
		PathKind Kind = PathKind::Centripetal;
		bool Closed = false;
		std::vector<PathPoint> Points;

		// The arc-length table is cached and rebuilt when Kind, Closed or Points change
		// (checked on use); Invalidate forces it.
		void Invalidate() { table.clear(); }

		// True when every point and handle is finite (non-finite ones are treated as the
		// previous finite point, so a broken edit never produces a NaN camera).
		bool IsFinite() const;

		// Bezier handles for a smooth curve through the points (a third of the neighbour
		// spacing along the Catmull-Rom tangent).
		void AutoTangents();

		float Length() const;

		// Position at a fraction of the arc length (0 start, 1 end; wraps when Closed).
		Vec3 Sample(float fraction) const;

		// Unit direction of travel at a fraction of the arc length.
		Vec3 Direction(float fraction) const;

		// Position by the raw segment parameter (segment index + local t).
		Vec3 Evaluate(float parameter) const;

		// Points along the curve for drawing (samplesPerSegment per segment, ends included).
		std::vector<Vec3> Polyline(std::uint32_t samplesPerSegment = 16) const;

		// The fraction nearest to a point (for snapping a cursor to the path).
		float Nearest(const Vec3& point) const;

		Tooling::Json ToJson() const;

		static CameraPath FromJson(const Tooling::Json& json);

	  private:
		std::size_t SegmentCount() const;

		const PathPoint& Point(std::ptrdiff_t index) const;

		float ParameterAt(float fraction) const;

		void Build() const;

		std::uint64_t Key() const;

		// (cumulative length, parameter) pairs, densely sampled.
		mutable std::vector<std::pair<float, float>> table;
		mutable std::uint64_t tableKey = 0;
	};

} // namespace Swim::Replay
