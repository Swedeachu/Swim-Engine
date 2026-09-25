#include "Engine/Systems/UI/UiCanvas.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>

namespace Swim::UI
{
	namespace
	{
		UiVec3 Add(UiVec3 a, UiVec3 b)
		{
			return { a.X + b.X, a.Y + b.Y, a.Z + b.Z };
		}

		UiVec3 Sub(UiVec3 a, UiVec3 b)
		{
			return { a.X - b.X, a.Y - b.Y, a.Z - b.Z };
		}

		UiVec3 Scale(UiVec3 a, float s)
		{
			return { a.X * s, a.Y * s, a.Z * s };
		}

		float Dot(UiVec3 a, UiVec3 b)
		{
			return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
		}

		UiVec3 Cross(UiVec3 a, UiVec3 b)
		{
			return { a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X };
		}

		float Length(UiVec3 a)
		{
			return std::sqrt(Dot(a, a));
		}

		std::optional<UiVec3> Normalize(UiVec3 a)
		{
			const float length = Length(a);
			if (!(length > 1e-12f) || !std::isfinite(length))
			{
				return std::nullopt;
			}
			return Scale(a, 1.0f / length);
		}

		UiVec3 Column(const UiMatrix3x4& m, int c)
		{
			return { m[c], m[4 + c], m[8 + c] };
		}

		void SetColumn(UiMatrix3x4& m, int c, UiVec3 v)
		{
			m[c] = v.X;
			m[4 + c] = v.Y;
			m[8 + c] = v.Z;
		}

		bool Finite(std::span<const float> values)
		{
			return std::all_of(values.begin(), values.end(),
				[](float v)
				{
					return std::isfinite(v);
				});
		}

		void ValidateCamera(const UiCameraView& camera)
		{
			if (!Finite(camera.View) || !Finite(camera.Projection) || !std::isfinite(camera.ViewportWidth) ||
				!std::isfinite(camera.ViewportHeight) || camera.ViewportWidth <= 0.0f || camera.ViewportHeight <= 0.0f)
			{
				throw std::invalid_argument("UI canvas camera needs finite matrices and a positive viewport");
			}
		}

		// Camera basis rows of a rigid view: right, up, back (towards the viewer).
		UiVec3 ViewRow(const UiMatrix4& view, int row)
		{
			return { view[row * 4 + 0], view[row * 4 + 1], view[row * 4 + 2] };
		}

		std::array<float, 4> Apply(const UiMatrix4& m, const std::array<float, 4>& v)
		{
			std::array<float, 4> r{};
			for (int row = 0; row < 4; ++row)
			{
				r[row] = m[row * 4] * v[0] + m[row * 4 + 1] * v[1] + m[row * 4 + 2] * v[2] + m[row * 4 + 3] * v[3];
			}
			return r;
		}

		// View-space point of an NDC position at view depth z (z < 0), solving the
		// projection's x/y rows; empty when singular.
		std::optional<UiVec3> Unproject(const UiMatrix4& p, float nx, float ny, float z)
		{
			// (P0 - nx P3) . (x, y, z, 1) = 0 and (P1 - ny P3) . (x, y, z, 1) = 0.
			const float a0 = p[0] - nx * p[12], b0 = p[1] - nx * p[13], c0 = (p[2] - nx * p[14]) * z + (p[3] - nx * p[15]);
			const float a1 = p[4] - ny * p[12], b1 = p[5] - ny * p[13], c1 = (p[6] - ny * p[14]) * z + (p[7] - ny * p[15]);
			const float det = a0 * b1 - a1 * b0;
			if (std::abs(det) < 1e-20f)
			{
				return std::nullopt;
			}
			return UiVec3{ (-c0 * b1 + c1 * b0) / det, (-a0 * c1 + a1 * c0) / det, z };
		}
	} // namespace

	void ValidatePlacement(const UiWorldPlacement& p)
	{
		const bool valid = Finite(p.Transform) && std::isfinite(p.Pivot.X) && std::isfinite(p.Pivot.Y) && std::isfinite(p.UnitsPerPixel) &&
			p.UnitsPerPixel > 0.0f && p.UnitsPerPixel <= 1e6f &&
			static_cast<std::uint8_t>(p.Billboard) <= static_cast<std::uint8_t>(UiBillboardMode::ScreenAligned) &&
			Finite(std::array{ p.UpAxis.X, p.UpAxis.Y, p.UpAxis.Z }) && Length(p.UpAxis) > 1e-6f &&
			std::isfinite(p.ScreenPixelsPerCanvasPixel) && p.ScreenPixelsPerCanvasPixel > 0.0f && std::isfinite(p.FadeStart) &&
			std::isfinite(p.FadeEnd) && p.FadeStart >= 0.0f && p.FadeEnd >= 0.0f;
		if (!valid)
		{
			throw std::invalid_argument("Invalid UI world placement");
		}
	}

	UiMatrix4 Multiply(const UiMatrix4& a, const UiMatrix4& b)
	{
		UiMatrix4 r{};
		for (int row = 0; row < 4; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				float sum = 0.0f;
				for (int k = 0; k < 4; ++k)
				{
					sum += a[row * 4 + k] * b[k * 4 + column];
				}
				r[row * 4 + column] = sum;
			}
		}
		return r;
	}

	UiMatrix4 ToMatrix4(const UiMatrix3x4& m)
	{
		return { m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], 0, 0, 0, 1 };
	}

	UiVec3 TransformPoint(const UiMatrix3x4& m, UiVec3 p)
	{
		return { m[0] * p.X + m[1] * p.Y + m[2] * p.Z + m[3], m[4] * p.X + m[5] * p.Y + m[6] * p.Z + m[7],
			m[8] * p.X + m[9] * p.Y + m[10] * p.Z + m[11] };
	}

	UiVec3 CameraPosition(const UiMatrix4& view)
	{
		// view = [R t; 0 1] -> camera = -R^T t.
		const UiVec3 t{ view[3], view[7], view[11] };
		return { -(view[0] * t.X + view[4] * t.Y + view[8] * t.Z), -(view[1] * t.X + view[5] * t.Y + view[9] * t.Z),
			-(view[2] * t.X + view[6] * t.Y + view[10] * t.Z) };
	}

	UiMatrix3x4 CanvasToWorld(UiCanvasMode mode, const UiWorldPlacement& p, UiPoint size, const UiCameraView* camera)
	{
		ValidatePlacement(p);
		if (!std::isfinite(size.X) || !std::isfinite(size.Y) || size.X < 0.0f || size.Y < 0.0f)
		{
			throw std::invalid_argument("UI canvas size must be finite and non-negative");
		}
		if (mode != UiCanvasMode::WorldPanel && mode != UiCanvasMode::Billboard)
		{
			throw std::invalid_argument("CanvasToWorld is for world panels and billboards");
		}
		UiVec3 right;
		UiVec3 up;
		UiVec3 front;
		float scale = p.UnitsPerPixel;
		const UiVec3 anchor = Column(p.Transform, 3);
		if (mode == UiCanvasMode::WorldPanel)
		{
			right = Column(p.Transform, 0);
			up = Column(p.Transform, 1);
			const auto normal = Normalize(Cross(right, up));
			if (!normal)
			{
				throw std::invalid_argument("UI world panel transform is degenerate");
			}
			front = *normal;
		}
		else
		{
			if (!camera)
			{
				throw std::invalid_argument("UI billboards need the camera");
			}
			ValidateCamera(*camera);
			const auto& view = camera->View;
			const UiVec3 cameraRight = ViewRow(view, 0);
			const UiVec3 cameraUp = ViewRow(view, 1);
			const UiVec3 cameraBack = ViewRow(view, 2);
			const UiVec3 toCamera = Sub(CameraPosition(view), anchor);
			const UiVec3 axis = *Normalize(p.UpAxis);
			switch (p.Billboard)
			{
			case UiBillboardMode::ScreenAligned:
				right = cameraRight;
				up = cameraUp;
				front = cameraBack;
				break;
			case UiBillboardMode::Cylindrical:
			{
				auto flat = Normalize(Sub(toCamera, Scale(axis, Dot(toCamera, axis))));
				if (!flat)
				{
					flat = Normalize(Sub(cameraBack, Scale(axis, Dot(cameraBack, axis)))); // Camera on the axis.
				}
				front = flat.value_or(cameraBack);
				up = axis;
				right = Normalize(Cross(up, front)).value_or(cameraRight);
				break;
			}
			default:
			{
				front = Normalize(toCamera).value_or(cameraBack);
				auto side = Normalize(Cross(axis, front));
				right = side ? *side : cameraRight; // Looking straight along the up axis.
				up = Cross(front, right);
				break;
			}
			}
			if (p.ConstantScreenSize)
			{
				const auto& proj = camera->Projection;
				const auto viewPosition = Apply(view, { anchor.X, anchor.Y, anchor.Z, 1.0f });
				const float w = proj[12] * viewPosition[0] + proj[13] * viewPosition[1] + proj[14] * viewPosition[2] + proj[15];
				if (!(std::abs(proj[5]) > 1e-12f) || !(w > 0.0f))
				{
					throw std::invalid_argument("Constant-size UI billboards need a valid projection and an anchor in front of the camera");
				}
				scale = p.ScreenPixelsPerCanvasPixel * 2.0f * w / (std::abs(proj[5]) * camera->ViewportHeight);
			}
		}
		const float scaleRight = mode == UiCanvasMode::WorldPanel ? scale : scale / std::max(Length(right), 1e-30f);
		const float scaleUp = mode == UiCanvasMode::WorldPanel ? scale : scale / std::max(Length(up), 1e-30f);
		const UiVec3 pixelRight = Scale(right, scaleRight);
		const UiVec3 pixelDown = Scale(up, -scaleUp);
		UiMatrix3x4 m{};
		SetColumn(m, 0, pixelRight);
		SetColumn(m, 1, pixelDown);
		SetColumn(m, 2, front);
		SetColumn(m, 3, Sub(anchor, Add(Scale(pixelRight, p.Pivot.X * size.X), Scale(pixelDown, p.Pivot.Y * size.Y))));
		return m;
	}

	UiMatrix4 ClipFromCanvas(const UiMatrix3x4& canvasToWorld, const UiCameraView& camera)
	{
		ValidateCamera(camera);
		return Multiply(Multiply(camera.Projection, camera.View), ToMatrix4(canvasToWorld));
	}

	UiMatrix4 ScreenClipFromCanvas(float width, float height, float offsetX, float offsetY)
	{
		if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f || !std::isfinite(offsetX) ||
			!std::isfinite(offsetY))
		{
			throw std::invalid_argument("UI screen canvas needs a positive target size and a finite offset");
		}
		// Clip space is +Y up; canvas pixels are +Y down.
		return { 2.0f / width, 0, 0, 2.0f * offsetX / width - 1.0f, 0, -2.0f / height, 0, 1.0f - 2.0f * offsetY / height, 0, 0, 0, 0, 0, 0,
			0, 1 };
	}

	UiRay ScreenRay(const UiCameraView& camera, UiPoint pixel)
	{
		ValidateCamera(camera);
		const float nx = 2.0f * pixel.X / camera.ViewportWidth - 1.0f;
		const float ny = 1.0f - 2.0f * pixel.Y / camera.ViewportHeight;
		const auto nearPoint = Unproject(camera.Projection, nx, ny, -1.0f);
		const auto farPoint = Unproject(camera.Projection, nx, ny, -2.0f);
		if (!nearPoint || !farPoint)
		{
			throw std::invalid_argument("UI screen ray needs an invertible projection");
		}
		// Extrapolated to view depth 0: the eye for perspective, the view plane for
		// orthographic projections.
		const UiVec3 direction = Sub(*farPoint, *nearPoint);
		const UiVec3 origin = Sub(*nearPoint, direction);
		const auto& v = camera.View;
		const UiVec3 t{ v[3], v[7], v[11] };
		const auto toWorld = [&](UiVec3 p, bool point)
		{
			const UiVec3 q = point ? Sub(p, t) : p;
			return UiVec3{ v[0] * q.X + v[4] * q.Y + v[8] * q.Z, v[1] * q.X + v[5] * q.Y + v[9] * q.Z,
				v[2] * q.X + v[6] * q.Y + v[10] * q.Z };
		};
		return { toWorld(origin, true), Normalize(toWorld(direction, false)).value_or(UiVec3{ 0, 0, -1 }) };
	}

	std::optional<UiCanvasHit> IntersectCanvasPlane(const UiRay& ray, const UiMatrix3x4& m)
	{
		if (!Finite(m) ||
			!Finite(std::array{ ray.Origin.X, ray.Origin.Y, ray.Origin.Z, ray.Direction.X, ray.Direction.Y, ray.Direction.Z }))
		{
			return std::nullopt;
		}
		// origin + x * right + y * down = ray.Origin + t * ray.Direction.
		const UiVec3 a = Column(m, 0);
		const UiVec3 b = Column(m, 1);
		const UiVec3 d = Scale(ray.Direction, -1.0f);
		const UiVec3 rhs = Sub(ray.Origin, Column(m, 3));
		const float det = Dot(a, Cross(b, d));
		const float scale = Length(a) * Length(b) * Length(d);
		if (!(std::abs(det) > 1e-7f * scale) || scale <= 0.0f)
		{
			return std::nullopt; // Parallel (or degenerate).
		}
		const float x = Dot(rhs, Cross(b, d)) / det;
		const float y = Dot(a, Cross(rhs, d)) / det;
		const float t = Dot(a, Cross(b, rhs)) / det;
		if (!(t >= 0.0f))
		{
			return std::nullopt;
		}
		const UiVec3 normal = Cross(a, Scale(b, -1.0f)); // Towards viewers of the front face.
		return UiCanvasHit{ { x, y }, t, Dot(ray.Direction, normal) < 0.0f };
	}

	std::optional<UiCanvasHit> IntersectCanvas(const UiRay& ray, const UiMatrix3x4& m, UiPoint size, bool twoSided)
	{
		const auto hit = IntersectCanvasPlane(ray, m);
		if (!hit || hit->Point.X < 0.0f || hit->Point.Y < 0.0f || hit->Point.X >= size.X || hit->Point.Y >= size.Y ||
			(!twoSided && !hit->FrontFacing))
		{
			return std::nullopt;
		}
		return hit;
	}

	std::optional<UiPoint> ProjectCanvasPoint(const UiMatrix4& m, UiPoint viewport, UiPoint point)
	{
		const auto clip = Apply(m, { point.X, point.Y, 0.0f, 1.0f });
		if (!(clip[3] > 1e-12f))
		{
			return std::nullopt;
		}
		const float nx = clip[0] / clip[3];
		const float ny = clip[1] / clip[3];
		return UiPoint{ (nx + 1.0f) * 0.5f * viewport.X, (1.0f - ny) * 0.5f * viewport.Y };
	}

	std::optional<UiRect> ProjectCanvasRect(const UiMatrix4& m, UiPoint viewport, const UiRect& rect)
	{
		float x0 = std::numeric_limits<float>::infinity();
		float y0 = x0;
		float x1 = -x0;
		float y1 = -x0;
		for (const UiPoint corner : { UiPoint{ rect.X, rect.Y }, UiPoint{ rect.X + rect.Width, rect.Y },
				 UiPoint{ rect.X, rect.Y + rect.Height }, UiPoint{ rect.X + rect.Width, rect.Y + rect.Height } })
		{
			const auto projected = ProjectCanvasPoint(m, viewport, corner);
			if (!projected)
			{
				return std::nullopt;
			}
			x0 = std::min(x0, projected->X);
			y0 = std::min(y0, projected->Y);
			x1 = std::max(x1, projected->X);
			y1 = std::max(y1, projected->Y);
		}
		return UiRect{ x0, y0, x1 - x0, y1 - y0 };
	}

	float CanvasFade(const UiWorldPlacement& placement, float distance)
	{
		if (placement.FadeEnd <= placement.FadeStart || !std::isfinite(distance))
		{
			return 1.0f;
		}
		return std::clamp((placement.FadeEnd - distance) / (placement.FadeEnd - placement.FadeStart), 0.0f, 1.0f);
	}

	float CanvasDistance(const UiMatrix3x4& m, UiPoint size, UiPoint pivot, const UiMatrix4& view)
	{
		const UiVec3 point = TransformPoint(m, { pivot.X * size.X, pivot.Y * size.Y, 0.0f });
		return Length(Sub(point, CameraPosition(view)));
	}
} // namespace Swim::UI
