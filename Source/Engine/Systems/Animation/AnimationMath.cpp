#include "Engine/Systems/Animation/AnimationMath.h"

#include <algorithm>

namespace Swim::Animation
{
	Quat Normalize(const Quat& q)
	{
		const float length = std::sqrt(Dot(q, q));
		if (!(length > 1e-20f))
		{
			return IdentityQuat;
		}
		const float inverse = 1.0f / length;
		return { q[0] * inverse, q[1] * inverse, q[2] * inverse, q[3] * inverse };
	}

	Quat Multiply(const Quat& a, const Quat& b)
	{
		return {
			a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
			a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
			a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
			a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
		};
	}

	Quat Conjugate(const Quat& q)
	{
		return { -q[0], -q[1], -q[2], q[3] };
	}

	Quat Inverse(const Quat& q)
	{
		const float lengthSquared = Dot(q, q);
		if (!(lengthSquared > 1e-30f))
		{
			return IdentityQuat;
		}
		const Quat c = Conjugate(q);
		return { c[0] / lengthSquared, c[1] / lengthSquared, c[2] / lengthSquared, c[3] / lengthSquared };
	}

	Vec3 Rotate(const Quat& q, const Vec3& v)
	{
		// v + 2w(u x v) + 2u x (u x v), u = q.xyz.
		const Vec3 u{ q[0], q[1], q[2] };
		const Vec3 uv{ u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
		const Vec3 uuv{ u[1] * uv[2] - u[2] * uv[1], u[2] * uv[0] - u[0] * uv[2], u[0] * uv[1] - u[1] * uv[0] };
		return Add(v, Add(Scale(uv, 2.0f * q[3]), Scale(uuv, 2.0f)));
	}

	Quat Nlerp(const Quat& a, const Quat& b, float t)
	{
		const float sign = Dot(a, b) < 0.0f ? -1.0f : 1.0f;
		return Normalize({ a[0] + (sign * b[0] - a[0]) * t, a[1] + (sign * b[1] - a[1]) * t, a[2] + (sign * b[2] - a[2]) * t,
			a[3] + (sign * b[3] - a[3]) * t });
	}

	Quat Slerp(const Quat& a, const Quat& b, float t)
	{
		float cosine = Dot(a, b);
		Quat target = b;
		if (cosine < 0.0f)
		{
			cosine = -cosine;
			target = { -b[0], -b[1], -b[2], -b[3] };
		}
		if (cosine > 0.9995f)
		{
			return Nlerp(a, target, t);
		}
		const float angle = std::acos(std::clamp(cosine, -1.0f, 1.0f));
		const float sine = std::sin(angle);
		const float wa = std::sin((1.0f - t) * angle) / sine;
		const float wb = std::sin(t * angle) / sine;
		return Normalize(
			{ a[0] * wa + target[0] * wb, a[1] * wa + target[1] * wb, a[2] * wa + target[2] * wb, a[3] * wa + target[3] * wb });
	}

	Quat FromAxisAngle(const Vec3& axis, float radians)
	{
		const float length = Length(axis);
		if (!(length > 1e-20f))
		{
			return IdentityQuat;
		}
		const float s = std::sin(radians * 0.5f) / length;
		return { axis[0] * s, axis[1] * s, axis[2] * s, std::cos(radians * 0.5f) };
	}

	JointPose Compose(const JointPose& parent, const JointPose& child)
	{
		JointPose result;
		result.Translation = Add(parent.Translation, Rotate(parent.Rotation, Mul(parent.Scale, child.Translation)));
		result.Rotation = Normalize(Multiply(parent.Rotation, child.Rotation));
		result.Scale = Mul(parent.Scale, child.Scale);
		return result;
	}

	Matrix4 ToMatrix(const JointPose& pose)
	{
		const Quat q = Normalize(pose.Rotation);
		const float x = q[0], y = q[1], z = q[2], w = q[3];
		const Vec3& s = pose.Scale;
		Matrix4 m = IdentityMatrix;
		m[0] = (1 - 2 * (y * y + z * z)) * s[0];
		m[1] = (2 * (x * y + z * w)) * s[0];
		m[2] = (2 * (x * z - y * w)) * s[0];
		m[4] = (2 * (x * y - z * w)) * s[1];
		m[5] = (1 - 2 * (x * x + z * z)) * s[1];
		m[6] = (2 * (y * z + x * w)) * s[1];
		m[8] = (2 * (x * z + y * w)) * s[2];
		m[9] = (2 * (y * z - x * w)) * s[2];
		m[10] = (1 - 2 * (x * x + y * y)) * s[2];
		m[12] = pose.Translation[0];
		m[13] = pose.Translation[1];
		m[14] = pose.Translation[2];
		return m;
	}

	Matrix4 Multiply(const Matrix4& a, const Matrix4& b)
	{
		Matrix4 result{};
		for (int column = 0; column < 4; ++column)
		{
			for (int row = 0; row < 4; ++row)
			{
				float sum = 0.0f;
				for (int k = 0; k < 4; ++k)
				{
					sum += a[k * 4 + row] * b[column * 4 + k];
				}
				result[column * 4 + row] = sum;
			}
		}
		return result;
	}

	Vec3 TransformPoint(const Matrix4& m, const Vec3& p)
	{
		return { m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
			m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14] };
	}

	Matrix3x4 ToAffineRows(const Matrix4& m)
	{
		Matrix3x4 rows{};
		for (int row = 0; row < 3; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				rows[row * 4 + column] = m[column * 4 + row];
			}
		}
		return rows;
	}

	Vec3 TransformPoint(const Matrix3x4& r, const Vec3& p)
	{
		return { r[0] * p[0] + r[1] * p[1] + r[2] * p[2] + r[3], r[4] * p[0] + r[5] * p[1] + r[6] * p[2] + r[7],
			r[8] * p[0] + r[9] * p[1] + r[10] * p[2] + r[11] };
	}

	Vec3 TransformDirection(const Matrix3x4& r, const Vec3& v)
	{
		return { r[0] * v[0] + r[1] * v[1] + r[2] * v[2], r[4] * v[0] + r[5] * v[1] + r[6] * v[2],
			r[8] * v[0] + r[9] * v[1] + r[10] * v[2] };
	}

	float MaxColumnScale(const Matrix3x4& r)
	{
		float largest = 0.0f;
		for (int column = 0; column < 3; ++column)
		{
			largest = std::max(largest, Length({ r[column], r[4 + column], r[8 + column] }));
		}
		return largest;
	}
} // namespace Swim::Animation
