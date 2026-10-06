#include "Engine/Systems/Renderer/Reflections/PlanarReflections.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace Swim::Render::PlanarReflections
{

	namespace
	{

		Float3 Add(const Float3& a, const Float3& b)
		{
			return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
		}

		Float3 Sub(const Float3& a, const Float3& b)
		{
			return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
		}

		Float3 Mul(const Float3& a, float s)
		{
			return { a[0] * s, a[1] * s, a[2] * s };
		}

		float Dot(const Float3& a, const Float3& b)
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		}

		Float3 Cross(const Float3& a, const Float3& b)
		{
			return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
		}

		float Length(const Float3& a)
		{
			return std::sqrt(Dot(a, a));
		}

		Float3 Normalize(const Float3& a, const Float3& fallback = { 0.0f, 1.0f, 0.0f })
		{
			const float length = Length(a);
			return length > 1.0e-12f ? Mul(a, 1.0f / length) : fallback;
		}

		Matrix Multiply(const Matrix& a, const Matrix& b)
		{
			Matrix m{};

			for (int r = 0; r < 4; ++r)
			{
				for (int c = 0; c < 4; ++c)
				{
					float sum = 0.0f;

					for (int k = 0; k < 4; ++k)
					{
						sum += a[r * 4 + k] * b[k * 4 + c];
					}

					m[r * 4 + c] = sum;
				}
			}

			return m;
		}

		std::array<float, 4> Transform(const float* m, const Float3& p)
		{
			std::array<float, 4> out{};

			for (int r = 0; r < 4; ++r)
			{
				out[r] = m[r * 4 + 0] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3];
			}

			return out;
		}

		Float4 NormalizedPlane(float a, float b, float c, float d)
		{
			const float length = std::sqrt(a * a + b * b + c * c);
			return length > 1.0e-12f ? Float4{ a / length, b / length, c / length, d / length } : Float4{ 0.0f, 0.0f, 0.0f, 1.0f };
		}

		// Left, right, bottom, top and near (z <= w; reverse-Z) planes of a row-major view
		// projection; the sixth is the far plane (z >= 0: degenerate, never culls, for infinite
		// projections).
		std::array<Float4, 6> FrustumPlanes(const Matrix& m)
		{
			const auto row = [&](int r, int c)
			{
				return m[r * 4 + c];
			};
			std::array<Float4, 6> planes{};

			for (int axis = 0; axis < 2; ++axis)
			{
				for (int sign = 0; sign < 2; ++sign)
				{
					const float s = sign == 0 ? 1.0f : -1.0f;
					planes[axis * 2 + sign] = NormalizedPlane(row(3, 0) + s * row(axis, 0), row(3, 1) + s * row(axis, 1),
						row(3, 2) + s * row(axis, 2), row(3, 3) + s * row(axis, 3));
				}
			}

			planes[4] = NormalizedPlane(row(3, 0) - row(2, 0), row(3, 1) - row(2, 1), row(3, 2) - row(2, 2), row(3, 3) - row(2, 3));
			planes[5] = NormalizedPlane(row(2, 0), row(2, 1), row(2, 2), row(2, 3));
			return planes;
		}

		float Distance(const Float4& plane, const Float3& p)
		{
			return plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2] + plane[3];
		}

		bool SphereInside(const std::array<Float4, 6>& planes, const Float3& center, float radius)
		{
			for (const auto& plane : planes)
			{
				if (Distance(plane, center) < -radius)
				{
					return false;
				}
			}

			return true;
		}

		// Sutherland-Hodgman: keeps the part of a convex polygon in front of the plane.
		void ClipPolygon(const std::vector<Float3>& in, std::vector<Float3>& out, const Float4& plane)
		{
			out.clear();
			const std::size_t count = in.size();

			for (std::size_t i = 0; i < count; ++i)
			{
				const Float3& a = in[i];
				const Float3& b = in[(i + 1) % count];
				const float da = Distance(plane, a);
				const float db = Distance(plane, b);

				if (da >= 0.0f)
				{
					out.push_back(a);
				}

				if ((da >= 0.0f) != (db >= 0.0f))
				{
					const float t = da / (da - db);
					out.push_back(Add(a, Mul(Sub(b, a), t)));
				}
			}
		}

		std::uint32_t Quantize(float texels, std::uint32_t minimum, std::uint32_t maximum)
		{
			const float clamped = std::clamp(std::isfinite(texels) ? texels : 0.0f, float(minimum), float(maximum));
			const auto rounded = static_cast<std::uint32_t>(std::lround(clamped / 8.0f)) * 8u;
			return std::clamp(rounded, minimum, maximum);
		}

		float SmoothStep(float edge0, float edge1, float x)
		{
			const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1.0e-6f), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		Float3 Column(const std::array<float, 12>& m, int c)
		{
			return { m[c], m[4 + c], m[8 + c] };
		}

	} // namespace

	Float3 ReflectPoint(const Float3& p, const Float3& n, float d)
	{
		return Sub(p, Mul(n, 2.0f * (Dot(n, p) - d)));
	}

	Matrix ViewMatrix(const Float3& position, const Float3& right, const Float3& up, const Float3& forward)
	{
		// Right-handed view looking down -Z: rows are right, up and -forward.
		return { right[0], right[1], right[2], -Dot(right, position), up[0], up[1], up[2], -Dot(up, position), -forward[0], -forward[1],
			-forward[2], Dot(forward, position), 0.0f, 0.0f, 0.0f, 1.0f };
	}

	Matrix OffAxisReverseZ(float left, float right, float bottom, float top, float focal, float nearClip)
	{
		Matrix m{};
		m[0] = 2.0f * focal / (right - left);
		m[2] = (right + left) / (right - left);
		m[5] = 2.0f * focal / (top - bottom);
		m[6] = (top + bottom) / (top - bottom);
		m[11] = nearClip; // clip z = near, clip w = -z_view: depth = near / distance.
		m[14] = -1.0f;
		return m;
	}

	Float3 ProjectToCapture(const GpuPlanarReflectionRecord& record, const Float3& world)
	{
		const auto clip = Transform(record.ViewProjection, world);

		if (!(clip[3] > 1.0e-6f))
		{
			return { -1.0f, -1.0f, clip[3] };
		}

		return { clip[0] / clip[3] * 0.5f + 0.5f, 0.5f - clip[1] / clip[3] * 0.5f, clip[3] };
	}

	std::array<float, 2> LookupUv(const GpuPlanarReflectionRecord& record, const Float3& position, const Float3& direction,
		const std::function<float(const std::array<float, 2>&)>& distance, std::uint32_t iterations)
	{
		const Float3 camera{ record.Camera[0], record.Camera[1], record.Camera[2] };
		const auto start = ProjectToCapture(record, position);
		std::array<float, 2> uv{ start[0], start[1] };

		if (!(start[2] > 0.0f))
		{
			return uv;
		}

		// What the capture saw through the surface point, then along the ray: t is the ray
		// length to the captured hit nearest the ray.
		const auto rayLength = [&](const Float3& through, const std::array<float, 2>& at)
		{
			const Float3 hit = Add(camera, Mul(Normalize(Sub(through, camera)), distance(at)));
			return std::max(Dot(Sub(hit, position), direction), 0.0f);
		};
		float t = rayLength(position, uv);

		for (std::uint32_t i = 0; i < iterations; ++i)
		{
			const Float3 q = Add(position, Mul(direction, t));
			const auto projected = ProjectToCapture(record, q);

			if (!(projected[2] > 0.0f))
			{
				break;
			}

			uv = { projected[0], projected[1] };
			t = rayLength(q, uv);
		}

		const auto end = ProjectToCapture(record, Add(position, Mul(direction, t)));

		if (end[2] > 0.0f)
		{
			uv = { end[0], end[1] };
		}

		return uv;
	}

	void Planner::Reset()
	{
		slots.clear();
	}

	std::uint32_t Planner::GetUsedSlots() const
	{
		return static_cast<std::uint32_t>(std::count_if(slots.begin(), slots.end(),
			[](const Slot& slot)
			{
				return slot.Used;
			}));
	}

	bool Planner::InCameraFrustum(const Float3& center, float radius) const
	{
		return SphereInside(cameraFrustum, center, radius);
	}

	void Planner::AddPlane(const PlanarReflectorDesc& reflector, std::uint32_t face, const Float3& center, const Float3& halfU,
		const Float3& halfV, const ViewCamera& camera, const PlanarReflectionSettings& settings)
	{
		++plan.Candidates;
		const Float3 normal = Normalize(Cross(halfU, halfV));
		const Float3 toCamera = Sub(camera.Position, center);
		const float radius = Length(halfU) + Length(halfV);
		const float distance = std::max(Length(toCamera), 1.0e-3f);
		const float tanHalf = std::tan(std::max(camera.VerticalFov, 1.0e-3f) * 0.5f);
		const float pixels = radius * camera.ViewportHeight / (distance * tanHalf);

		// Back-facing (or edge-on), off screen, or too small to be worth a capture.
		if (Dot(normal, toCamera) <= 0.02f || !InCameraFrustum(center, radius) || pixels < settings.MinScreenFraction * camera.ViewportHeight)
		{
			++plan.Culled;
			return;
		}

		// Where the mirror ray at the centre goes: staying in front of the camera (on screen),
		// screen-space reflections show it at full resolution.
		const Float3 view = Mul(toCamera, -1.0f / distance);
		const Float3 mirror = Sub(view, Mul(normal, 2.0f * Dot(view, normal)));
		const float onScreen = Dot(mirror, Normalize(camera.Forward, { 0.0f, 0.0f, -1.0f }));

		if (onScreen > settings.SsrHandoff)
		{
			++plan.Culled;
			return;
		}

		Candidate candidate;
		candidate.Key = reflector.Key * 8u + face;
		candidate.Owner = reflector.OwnerObjectId;
		candidate.Center = center;
		candidate.HalfU = halfU;
		candidate.HalfV = halfV;
		candidate.Normal = normal;
		candidate.Radius = radius;
		candidate.Pixels = pixels;
		candidate.Quality = std::clamp(reflector.Quality, 0.1f, 4.0f);
		candidate.Priority = std::max(reflector.Priority, 0.0f);
		candidate.Score = pixels * candidate.Priority * (1.0f - 0.5f * SmoothStep(0.0f, settings.SsrHandoff, onScreen));
		candidates.push_back(candidate);
	}

	void Planner::AddSphere(const PlanarReflectorDesc& reflector, const ViewCamera& camera, const PlanarReflectionSettings& settings)
	{
		++plan.Candidates;
		const auto& m = reflector.World;
		const Float3 center = Column(m, 3);
		const float scale = std::max({ Length(Column(m, 0)), Length(Column(m, 1)), Length(Column(m, 2)) });
		const float radius = std::max(reflector.Radius, 0.0f) * scale;
		const Float3 toCamera = Sub(camera.Position, center);
		const float distance = Length(toCamera);
		const float tanHalf = std::tan(std::max(camera.VerticalFov, 1.0e-3f) * 0.5f);
		const float pixels = radius * camera.ViewportHeight / (std::max(distance, 1.0e-3f) * tanHalf);

		if (radius <= 0.0f || distance <= radius * 1.05f || !InCameraFrustum(center, radius) ||
			pixels < settings.MinScreenFraction * camera.ViewportHeight)
		{
			++plan.Culled;
			return;
		}

		Candidate candidate;
		candidate.Key = reflector.Key * 8u + 7u;
		candidate.Owner = reflector.OwnerObjectId;
		candidate.Sphere = true;
		candidate.Center = center;
		candidate.Normal = Mul(toCamera, 1.0f / distance);
		candidate.Radius = radius;
		candidate.Pixels = pixels;
		candidate.Quality = std::clamp(reflector.Quality, 0.1f, 4.0f);
		candidate.Priority = std::max(reflector.Priority, 0.0f);
		// Only the central cap reflects through the capture: weigh by its share.
		candidate.Score = pixels * candidate.Priority * (1.0f - std::clamp(settings.SphereMinCosine, 0.0f, 1.0f) * 0.5f);
		candidates.push_back(candidate);
	}

	Planner::Wanted Planner::Describe(const Group& group, const ViewCamera& camera, const PlanarReflectionSettings& settings)
	{
		Wanted wanted;
		const auto& first = candidates[group.First];
		const std::uint32_t atlas = std::max(settings.AtlasResolution, 64u);
		const std::uint32_t minimum = std::clamp(settings.MinResolution, 8u, atlas);
		const float tanHalf = std::tan(std::max(camera.VerticalFov, 1.0e-3f) * 0.5f);
		const float pixelsPerUnit = camera.ViewportHeight / (2.0f * tanHalf); // At distance 1.
		const float cull = std::max(settings.CullDistance, 1.0f);
		auto& view = wanted.View;
		wanted.Key = first.Key;
		wanted.Owner = first.Owner;

		if (first.Sphere)
		{
			const Float3 c = first.Center;
			const Float3 toCamera = Sub(camera.Position, c);
			const float distance = Length(toCamera);
			const Float3 f = Mul(toCamera, 1.0f / distance);
			const float cosine = std::clamp(settings.SphereMinCosine, 0.5f, 0.999f);
			// Mirror rays of the cap deviate up to twice its normal angle, plus the spread of
			// view directions across the sphere.
			const float alpha = std::min(2.0f * std::acos(cosine) + std::asin(std::min(first.Radius / distance, 0.99f)) + 0.08f, 1.2f);
			const float ta = std::tan(alpha);
			const Float3 u = Normalize(Cross(f, { 0.0f, 1.0f, 0.0f }), Normalize(Cross(f, { 1.0f, 0.0f, 0.0f })));
			const Float3 v = Cross(u, f);
			const auto size = Quantize(first.Pixels * settings.ResolutionScale * first.Quality, minimum, atlas);
			view.Width = size;
			view.Height = size;
			view.Position = c;
			view.Right = u;
			view.Up = v;
			view.Forward = f;
			view.NearClip = first.Radius * 1.002f + 0.001f;
			view.View = ViewMatrix(c, u, v, f);
			view.Projection = OffAxisReverseZ(-ta, ta, -ta, ta, 1.0f, view.NearClip);
			view.ViewProjection = Multiply(view.Projection, view.View);
			view.FrustumScale = { view.Projection[0], view.Projection[5], view.Projection[2], view.Projection[6] };
			view.CullPlane = { -f[0], -f[1], -f[2], Dot(f, c) + cull };
			view.ExcludedObjectId = first.Owner;
			view.LodScale = float(size) * 0.5f / ta;
			wanted.MatchPlane = { f[0], f[1], f[2], Dot(f, c) };
			wanted.Tolerance = 1.0e9f; // Matched by object, not by plane.
			wanted.MinCosine = cosine;
			wanted.TexelAngle = 2.0f * ta / float(size);
			wanted.Density = float(size) / std::max(first.Pixels, 1.0f);
			wanted.Distance = distance;
			wanted.Valid = true;
			return wanted;
		}

		const Float3 n = group.Normal;
		const float d = group.NearD;
		const float h = Dot(n, camera.Position) - d;

		if (h < 0.02f)
		{
			return wanted; // The camera is (almost) on the plane: nothing to see in it.
		}

		// The visible part of the portal: the members' rectangle clipped to the camera frustum.
		const auto corner = [&](float u, float v)
		{
			return Add(Add(Mul(n, d), Mul(group.U, u)), Mul(group.V, v));
		};
		clipA.assign({ corner(group.UMin, group.VMin), corner(group.UMax, group.VMin), corner(group.UMax, group.VMax),
			corner(group.UMin, group.VMax) });

		for (std::size_t p = 0; p < 5 && !clipA.empty(); ++p)
		{
			ClipPolygon(clipA, clipB, cameraFrustum[p]);
			clipA.swap(clipB);
		}

		if (clipA.size() < 3)
		{
			return wanted;
		}

		float u0 = 1.0e30f, u1 = -1.0e30f, v0 = 1.0e30f, v1 = -1.0e30f;

		for (const auto& p : clipA)
		{
			const float u = Dot(p, group.U);
			const float v = Dot(p, group.V);
			u0 = std::min(u0, u);
			u1 = std::max(u1, u);
			v0 = std::min(v0, v);
			v1 = std::max(v1, v);
		}

		// A margin keeps the capture usable while the camera moves (temporal reuse).
		const float margin = std::max(settings.PortalMargin, 0.0f);
		const float du = (u1 - u0) * margin;
		const float dv = (v1 - v0) * margin;
		u0 = std::max(group.UMin, u0 - du);
		u1 = std::min(group.UMax, u1 + du);
		v0 = std::max(group.VMin, v0 - dv);
		v1 = std::min(group.VMax, v1 + dv);

		if (u1 - u0 < 1.0e-4f || v1 - v0 < 1.0e-4f)
		{
			return wanted;
		}

		// The mirrored camera looks along +n through the portal; relative to the foot of the
		// perpendicular from the camera, the portal is the window of an off-axis frustum whose
		// near plane is the mirror itself.
		const Float3 foot = Sub(camera.Position, Mul(n, h));
		const float fu = Dot(foot, group.U);
		const float fv = Dot(foot, group.V);
		const float left = u0 - fu;
		const float right = u1 - fu;
		const float bottom = v0 - fv;
		const float top = v1 - fv;
		const Float3 mirrored = ReflectPoint(camera.Position, n, d);
		// LOD: the window's screen extent along each axis (foreshortened), in capture texels.
		const Float3 center = corner(0.5f * (u0 + u1), 0.5f * (v0 + v1));
		const Float3 toCenter = Sub(center, camera.Position);
		const float centerDistance = std::max(Length(toCenter), 0.05f);
		const Float3 viewDirection = Mul(toCenter, 1.0f / centerDistance);
		const float alongU = std::sqrt(std::max(1.0f - Dot(group.U, viewDirection) * Dot(group.U, viewDirection), 0.05f));
		const float alongV = std::sqrt(std::max(1.0f - Dot(group.V, viewDirection) * Dot(group.V, viewDirection), 0.05f));
		const float screenU = (u1 - u0) * pixelsPerUnit / centerDistance * alongU;
		const float screenV = (v1 - v0) * pixelsPerUnit / centerDistance * alongV;
		const float texelsPerPixel = settings.ResolutionScale * first.Quality;
		view.Width = Quantize(screenU * texelsPerPixel, minimum, atlas);
		view.Height = Quantize(screenV * texelsPerPixel, minimum, atlas);
		view.Position = mirrored;
		view.Right = group.U;
		view.Up = group.V;
		view.Forward = n;
		view.NearClip = h + 0.002f; // Just past the mirror: coplanar geometry (its own face) stays out.
		view.View = ViewMatrix(mirrored, group.U, group.V, n);
		view.Projection = OffAxisReverseZ(left, right, bottom, top, h, view.NearClip);
		view.ViewProjection = Multiply(view.Projection, view.View);
		view.FrustumScale = { view.Projection[0], view.Projection[5], view.Projection[2], view.Projection[6] };
		view.CullPlane = { -n[0], -n[1], -n[2], Dot(n, mirrored) + cull };
		view.ExcludedObjectId = first.Owner;
		view.LodScale = view.Projection[5] * float(view.Height) * 0.5f;
		wanted.MatchPlane = { n[0], n[1], n[2], 0.5f * (group.MinD + group.MaxD) };
		wanted.Tolerance = 0.5f * (group.MaxD - group.MinD) + 0.01f;
		wanted.MinCosine = 0.95f; // Planes match exactly; normal maps may tilt a little.
		wanted.TexelAngle = ((right - left) / h) / float(view.Width);
		wanted.Density = std::min(float(view.Width) / std::max(screenU, 1.0f), float(view.Height) / std::max(screenV, 1.0f));
		wanted.Distance = h;
		wanted.Portal = { corner(u0, v0), corner(u1, v0), corner(u1, v1), corner(u0, v1) };
		wanted.Valid = true;
		return wanted;
	}

	float Planner::Urgency(const Slot& slot, const Wanted& w, const ViewCamera& camera, double time,
		const PlanarReflectionSettings& settings, std::span<const ReflectionProbeMover> movers) const
	{
		if (!slot.Captured)
		{
			return 1.0e6f;
		}

		const auto& s = slot.State;
		const bool sphere = s.Tolerance > 1.0e8f;
		float urgency = 0.0f;
		const Float3 sn{ s.MatchPlane[0], s.MatchPlane[1], s.MatchPlane[2] };
		const Float3 wn{ w.MatchPlane[0], w.MatchPlane[1], w.MatchPlane[2] };

		// The reflector moved: the capture no longer lines up with it.
		if (sphere ? Length(Sub(s.View.Position, w.View.Position)) > 0.005f
				   : (Dot(sn, wn) < std::cos(settings.PlaneAngleTolerance * 0.5f) || std::abs(s.MatchPlane[3] - w.MatchPlane[3]) > 0.005f))
		{
			urgency += 1.0e5f;
		}

		// The wanted window reaches beyond the captured one (planes; the margin absorbs small motion).
		if (!sphere)
		{
			GpuPlanarReflectionRecord captured;
			std::copy(s.View.ViewProjection.begin(), s.View.ViewProjection.end(), captured.ViewProjection);

			for (const auto& corner : w.Portal)
			{
				const auto uv = ProjectToCapture(captured, corner);

				if (!(uv[2] > 0.0f) || uv[0] < -0.01f || uv[0] > 1.01f || uv[1] < -0.01f || uv[1] > 1.01f)
				{
					urgency += 1.0e4f;
					break;
				}
			}
		}

		// LOD changed by more than a fifth (hysteresis: no flicker between two sizes).
		const auto changed = [](std::uint32_t a, std::uint32_t b)
		{
			return std::abs(float(a) - float(b)) > 0.2f * float(b);
		};

		if (changed(w.View.Width, s.View.Width) || changed(w.View.Height, s.View.Height))
		{
			urgency += 50.0f;
		}

		// The camera moved: the lookup reprojects, but disocclusions grow with the angle.
		const float moved = Length(Sub(camera.Position, slot.CaptureCamera)) / std::max(w.Distance, 0.05f);
		float angle = moved;

		if (sphere)
		{
			const Float3 sf = s.View.Forward;
			const Float3 wf = w.View.Forward;
			angle += std::acos(std::clamp(Dot(sf, wf), -1.0f, 1.0f));
		}

		const float texels = angle / std::max(s.TexelAngle, 1.0e-6f);

		if (texels > settings.MotionTolerance)
		{
			urgency += 10.0f + texels;
		}

		// Something moved inside the capture's view.
		for (const auto& mover : movers)
		{
			if (SphereInside(slot.Frustum, mover.Center, mover.Radius))
			{
				urgency += 300.0f;
				break;
			}
		}

		const float age = float(time - slot.CaptureTime);

		if (age > settings.MaxAgeSeconds)
		{
			urgency += 1.0f + age;
		}

		return urgency;
	}

	const Plan& Planner::Update(std::span<const PlanarReflectorDesc> reflectors, const ViewCamera& camera, std::uint64_t frame, double time,
		const PlanarReflectionSettings& settings, std::span<const ReflectionProbeMover> movers)
	{
		plan.Captures.clear();
		plan.Records.clear();
		plan.Candidates = 0;
		plan.Culled = 0;
		plan.Groups = 0;

		if (!settings.Enabled || reflectors.empty())
		{
			return plan;
		}

		const auto maxPlanes = std::clamp(settings.MaxPlanes, 1u, MaxPlanarReflections);

		if (slots.size() != maxPlanes)
		{
			slots.assign(maxPlanes, Slot{});
		}

		cameraFrustum = FrustumPlanes(camera.ViewProjection);
		candidates.clear();

		for (const auto& reflector : reflectors)
		{
			const auto& m = reflector.World;
			const Float3 origin = Column(m, 3);
			const std::array<Float3, 3> axes{ Column(m, 0), Column(m, 1), Column(m, 2) };

			switch (reflector.Shape)
			{
			case PlanarReflectorShape::Box:

				for (std::uint32_t axis = 0; axis < 3; ++axis)
				{
					const Float3& a = axes[axis];
					const Float3& b = axes[(axis + 1) % 3];
					const Float3& c = axes[(axis + 2) % 3];

					for (std::uint32_t side = 0; side < 2; ++side)
					{
						const float s = side == 0 ? 0.5f : -0.5f;
						// (b, c) for +a and (c, b) for -a keep cross(halfU, halfV) pointing outwards.
						const Float3 halfU = Mul(side == 0 ? b : c, 0.5f);
						const Float3 halfV = Mul(side == 0 ? c : b, 0.5f);
						AddPlane(reflector, axis * 2 + side, Add(origin, Mul(a, s)), halfU, halfV, camera, settings);
					}
				}

				break;
			case PlanarReflectorShape::Plane:
				// Local +Y: cross(Z, X) = +Y.
				AddPlane(reflector, 6, origin, Mul(axes[2], std::max(reflector.PlaneHalfExtents[1], 0.0f)),
					Mul(axes[0], std::max(reflector.PlaneHalfExtents[0], 0.0f)), camera, settings);
				break;
			case PlanarReflectorShape::Sphere:
				AddSphere(reflector, camera, settings);
				break;
			}
		}

		// Largest first; ties by key keep the order (and slots) stable.
		order.resize(candidates.size());

		for (std::uint32_t i = 0; i < order.size(); ++i)
		{
			order[i] = i;
		}

		std::sort(order.begin(), order.end(),
			[&](std::uint32_t a, std::uint32_t b)
			{
				return candidates[a].Score != candidates[b].Score ? candidates[a].Score > candidates[b].Score
																  : candidates[a].Key < candidates[b].Key;
			});

		// Coplanar faces (within the tolerances) share one capture.
		groups.clear();
		const float cosTolerance = std::cos(std::max(settings.PlaneAngleTolerance, 0.0f));
		const float distanceTolerance = std::max(settings.PlaneDistanceTolerance, 0.0f);

		for (const auto index : order)
		{
			const auto& c = candidates[index];
			Group* target = nullptr;

			if (!c.Sphere)
			{
				for (auto& g : groups)
				{
					if (candidates[g.First].Sphere || Dot(g.Normal, c.Normal) < cosTolerance)
					{
						continue;
					}

					const float d = Dot(g.Normal, c.Center);

					if (std::max(g.MaxD, d) - std::min(g.MinD, d) <= distanceTolerance)
					{
						target = &g;
						break;
					}
				}
			}

			if (!target)
			{
				if (groups.size() >= maxPlanes)
				{
					continue;
				}

				Group g;
				g.First = index;
				g.Normal = c.Normal;
				g.MinD = g.MaxD = Dot(c.Normal, c.Center);
				g.UMin = g.VMin = 1.0e30f;
				g.UMax = g.VMax = -1.0e30f;

				if (!c.Sphere)
				{
					g.U = Normalize(Sub(c.HalfU, Mul(c.Normal, Dot(c.HalfU, c.Normal))), { 1.0f, 0.0f, 0.0f });
					g.V = Cross(g.U, g.Normal); // (U, V, -n) is right-handed: the capture's view axes.
				}

				groups.push_back(g);
				target = &groups.back();
			}
			else
			{
				const float d = Dot(target->Normal, c.Center);
				target->MinD = std::min(target->MinD, d);
				target->MaxD = std::max(target->MaxD, d);
			}

			target->Score += c.Score;

			if (!c.Sphere)
			{
				for (int corner = 0; corner < 4; ++corner)
				{
					const float su = corner & 1 ? 1.0f : -1.0f;
					const float sv = corner & 2 ? 1.0f : -1.0f;
					const Float3 p = Add(c.Center, Add(Mul(c.HalfU, su), Mul(c.HalfV, sv)));
					const float u = Dot(p, target->U);
					const float v = Dot(p, target->V);
					target->UMin = std::min(target->UMin, u);
					target->UMax = std::max(target->UMax, u);
					target->VMin = std::min(target->VMin, v);
					target->VMax = std::max(target->VMax, v);
				}
			}

			target->NearD = target->MinD; // The member plane nearest the reflected camera.
		}

		plan.Groups = static_cast<std::uint32_t>(groups.size());
		wanted.clear();
		urgent.clear();

		for (std::uint32_t gi = 0; gi < groups.size(); ++gi)
		{
			wanted.push_back(Describe(groups[gi], camera, settings));
			auto& w = wanted.back();

			if (!w.Valid)
			{
				continue;
			}

			// The group's slot: the one holding its key, else a free one, else the least
			// recently seen one not used this frame.
			std::uint32_t chosen = UINT32_MAX;

			for (std::uint32_t s = 0; s < slots.size() && chosen == UINT32_MAX; ++s)
			{
				chosen = slots[s].Used && slots[s].Key == w.Key ? s : chosen;
			}

			if (chosen == UINT32_MAX)
			{
				std::uint64_t oldest = UINT64_MAX;

				for (std::uint32_t s = 0; s < slots.size(); ++s)
				{
					const auto& slot = slots[s];
					const std::uint64_t seen = slot.Used ? slot.LastSeen : 0;

					if ((!slot.Used || slot.LastSeen != frame) && seen < oldest)
					{
						oldest = seen;
						chosen = s;
					}
				}

				if (chosen == UINT32_MAX)
				{
					w.Valid = false;
					continue;
				}

				slots[chosen] = Slot{};
				slots[chosen].Used = true;
				slots[chosen].Key = w.Key;
			}

			auto& slot = slots[chosen];
			slot.LastSeen = frame;
			w.View.Slot = chosen;
			const float urgency = Urgency(slot, w, camera, time, settings, movers);

			if (urgency > 0.0f)
			{
				// Bigger reflectors first among equally urgent ones.
				urgent.emplace_back(urgency * (1.0f + groups[gi].Score / std::max(camera.ViewportHeight, 1.0f)), gi);
			}
		}

		std::sort(urgent.begin(), urgent.end(),
			[](const auto& a, const auto& b)
			{
				return a.first > b.first;
			});
		const std::size_t budget = std::min<std::size_t>(urgent.size(), settings.CapturesPerFrame);

		for (std::size_t i = 0; i < budget; ++i)
		{
			const auto& w = wanted[urgent[i].second];
			auto& slot = slots[w.View.Slot];
			slot.Captured = true;
			slot.CaptureTime = time;
			slot.CaptureCamera = camera.Position;
			slot.State = w;
			slot.Frustum = FrustumPlanes(w.View.ViewProjection);
			slot.Frustum[5] = w.View.CullPlane;
			plan.Captures.push_back(w.View);
		}

		// Records: every group whose slot holds a capture (what the layer holds, which may be
		// a few frames old: the lookup reprojects it).
		const float atlas = float(std::max(settings.AtlasResolution, 64u));

		for (std::uint32_t gi = 0; gi < groups.size(); ++gi)
		{
			const auto& w = wanted[gi];

			if (!w.Valid || !slots[w.View.Slot].Captured)
			{
				continue;
			}

			const auto& s = slots[w.View.Slot].State;
			const bool sphere = s.Tolerance > 1.0e8f;
			GpuPlanarReflectionRecord record;
			std::copy(s.View.ViewProjection.begin(), s.View.ViewProjection.end(), record.ViewProjection);

			for (int c = 0; c < 4; ++c)
			{
				record.Plane[c] = s.MatchPlane[c];
			}

			for (int c = 0; c < 3; ++c)
			{
				record.Camera[c] = s.View.Position[c];
			}

			record.Camera[3] = sphere ? float(s.Owner) : 0.0f;
			record.Atlas[0] = float(w.View.Slot);
			record.Atlas[1] = float(s.View.Width) / atlas;
			record.Atlas[2] = float(s.View.Height) / atlas;
			// Where its texels are coarser than the screen's, a confident screen-space hit wins.
			record.Atlas[3] = std::clamp(1.0f - s.Density, 0.0f, 1.0f);
			record.Params[0] = std::min(s.Tolerance, 1.0e9f);
			record.Params[1] = s.MinCosine;
			record.Params[2] = settings.RoughnessFadeStart;
			record.Params[3] = std::max(settings.RoughnessFadeEnd, settings.RoughnessFadeStart + 1.0e-3f);
			plan.Records.push_back(record);
		}

		return plan;
	}

} // namespace Swim::Render::PlanarReflections
