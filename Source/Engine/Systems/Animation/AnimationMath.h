#pragma once
#include <array>
#include <cmath>

namespace Swim::Animation
{
	// Small value types for joint poses. Quaternions are (x, y, z, w); matrices are
	// column-major 4x4 (glTF order), skinning matrices row-major 3x4 (the GPU layout
	// of Render::RenderAffine: three float4 rows).
	using Vec3 = std::array<float, 3>;
	using Quat = std::array<float, 4>;
	using Matrix4 = std::array<float, 16>;
	using Matrix3x4 = std::array<float, 12>;

	inline constexpr Quat IdentityQuat{ 0.0f, 0.0f, 0.0f, 1.0f };
	inline constexpr Matrix4 IdentityMatrix{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	inline constexpr Matrix3x4 IdentityAffine{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };

	// A joint's local transform: scale, then rotation, then translation.
	struct JointPose
	{
		Vec3 Translation{ 0.0f, 0.0f, 0.0f };
		Quat Rotation = IdentityQuat;
		Vec3 Scale{ 1.0f, 1.0f, 1.0f };

		bool operator==(const JointPose&) const = default;
	};

	inline Vec3 Add(const Vec3& a, const Vec3& b)
	{
		return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
	}

	inline Vec3 Sub(const Vec3& a, const Vec3& b)
	{
		return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
	}

	inline Vec3 Scale(const Vec3& a, float s)
	{
		return { a[0] * s, a[1] * s, a[2] * s };
	}

	inline Vec3 Mul(const Vec3& a, const Vec3& b)
	{
		return { a[0] * b[0], a[1] * b[1], a[2] * b[2] };
	}

	inline Vec3 Lerp(const Vec3& a, const Vec3& b, float t)
	{
		return Add(a, Scale(Sub(b, a), t));
	}

	inline float Dot(const Quat& a, const Quat& b)
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	}

	inline float Length(const Vec3& a)
	{
		return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
	}

	Quat Normalize(const Quat& q);
	Quat Multiply(const Quat& a, const Quat& b); // Rotation by b, then by a.
	Quat Conjugate(const Quat& q);
	Quat Inverse(const Quat& q);
	Vec3 Rotate(const Quat& q, const Vec3& v);
	// Shortest-path spherical interpolation; falls back to nlerp for nearly equal inputs.
	Quat Slerp(const Quat& a, const Quat& b, float t);
	// Shortest-path normalized linear interpolation.
	Quat Nlerp(const Quat& a, const Quat& b, float t);
	Quat FromAxisAngle(const Vec3& axis, float radians);

	// parent * child: the child expressed in the parent's space (as matrices do).
	JointPose Compose(const JointPose& parent, const JointPose& child);

	Matrix4 ToMatrix(const JointPose& pose);
	Matrix4 Multiply(const Matrix4& a, const Matrix4& b);
	Vec3 TransformPoint(const Matrix4& m, const Vec3& p);
	Matrix3x4 ToAffineRows(const Matrix4& m); // Drops the last row, transposes to rows.
	Vec3 TransformPoint(const Matrix3x4& rows, const Vec3& p);
	Vec3 TransformDirection(const Matrix3x4& rows, const Vec3& v);
	float MaxColumnScale(const Matrix3x4& rows);
} // namespace Swim::Animation
