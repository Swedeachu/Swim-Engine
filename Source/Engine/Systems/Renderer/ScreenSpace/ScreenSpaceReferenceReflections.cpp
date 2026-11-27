#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceReference.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceReferenceMath.h"

#include <algorithm>
#include <cmath>

// The reference's reflections: the screen-space trace, the probe and planar fallbacks, the
// temporal resolve and the composite.
namespace Swim::Render::ScreenSpace
{

	using namespace ReferenceMath;

	ScreenPoint ProjectToScreen(const GpuScreenSpaceParams& params, const Float3& view)
	{
		std::array<float, 4> clip{};

		for (int r = 0; r < 4; ++r)
		{
			clip[r] = params.Projection[r * 4] * view[0] + params.Projection[r * 4 + 1] * view[1] + params.Projection[r * 4 + 2] * view[2] +
				params.Projection[r * 4 + 3];
		}

		const float inverseW = 1.0f / clip[3];
		ScreenPoint point;
		point.X = (clip[0] * inverseW + 1.0f + params.Jitter[0]) * 0.5f * float(params.Width);
		point.Y = (1.0f - clip[1] * inverseW - params.Jitter[1]) * 0.5f * float(params.Height);
		point.W = clip[3];
		return point;
	}

	float ScreenExit(float x0, float y0, float dx, float dy, float width, float height)
	{
		constexpr float inset = 0.01f;
		float t = 1.0f;

		if (dx > 0.0f)
		{
			t = std::min(t, (width - inset - x0) / dx);
		}
		else if (dx < 0.0f)
		{
			t = std::min(t, (inset - x0) / dx);
		}

		if (dy > 0.0f)
		{
			t = std::min(t, (height - inset - y0) / dy);
		}
		else if (dy < 0.0f)
		{
			t = std::min(t, (inset - y0) / dy);
		}

		return std::max(t, 0.0f);
	}

	float SurfaceThickness(
		const GpuScreenSpaceParams& params, const ScalarImage* backDepth, std::uint32_t x, std::uint32_t y, float frontDepth)
	{
		if (params.SsrBackDepth == 0u || !backDepth)
		{
			return params.SsrThickness;
		}

		const auto back = ViewPosition(params, float(x) + 0.5f, float(y) + 0.5f, backDepth->At(x, y));

		if (!back)
		{
			return OpenThickness;
		}

		return std::max(-(*back)[2] - frontDepth, BackFaceMinThickness);
	}

	float NormalTurnPerPixel(
		const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal, std::uint32_t x, std::uint32_t y)
	{
		const auto& centre = normal.At(x, y);
		const auto here = ViewPositionAt(params, depth, x, y);

		if (!here)
		{
			return 0.0f;
		}

		const float centreDepth = -(*here)[2];
		float turn = 0.0f;
		constexpr int Offsets[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

		for (const auto& offset : Offsets)
		{
			const int nx = int(x) + offset[0];
			const int ny = int(y) + offset[1];

			if (nx < 0 || ny < 0 || nx >= int(params.Width) || ny >= int(params.Height))
			{
				continue;
			}

			const auto there = ViewPositionAt(params, depth, std::uint32_t(nx), std::uint32_t(ny));

			if (!there || std::abs(-(*there)[2] - centreDepth) > 0.05f * centreDepth)
			{
				continue; // Another surface: not this one's curvature.
			}

			const auto& n = normal.At(std::uint32_t(nx), std::uint32_t(ny));
			const float dx = n[0] - centre[0];
			const float dy = n[1] - centre[1];
			const float dz = n[2] - centre[2];
			turn = std::max(turn, std::sqrt(dx * dx + dy * dy + dz * dz));
		}

		return turn;
	}

	float ReflectionFootprint(const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal, std::uint32_t x,
		std::uint32_t y, float surfaceDepth, float hitDepth, float travelled)
	{
		// A pixel's angle (at the view centre) and how fast its reflected ray turns from one
		// pixel to the next: the view ray's own step plus twice the normal's.
		const float pixelAngle = 2.0f / std::max(params.Projection[5] * float(params.Height), 1.0e-6f);
		const float spread = pixelAngle + 2.0f * NormalTurnPerPixel(params, depth, normal, x, y);
		return (surfaceDepth * pixelAngle + travelled * spread) / std::max(hitDepth * pixelAngle, 1.0e-9f);
	}

	float FootprintFade(const GpuScreenSpaceParams& params, float footprint)
	{
		return params.SsrMaxFootprint > 0.0f ? std::clamp(2.0f - footprint / params.SsrMaxFootprint, 0.0f, 1.0f) : 1.0f;
	}

	namespace
	{

		struct RaySample
		{
			bool Inside = false;
			std::uint32_t X = 0;
			std::uint32_t Y = 0;
			float RayDepth = 0.0f;
		};

		// TraceReflection's ray: view-space start p, normal n and direction r, projected to
		// the screen over [0, 1] (perspective-correct depth through q / k).
		struct ReflectionRay
		{
			const GpuScreenSpaceParams& Params;
			const ScalarImage& Depth;
			const ColorImage& Normal;
			const ScalarImage* BackDepth;
			bool BackFaces;
			std::uint32_t X;
			std::uint32_t Y;
			Float3 P;
			Float3 N;
			Float3 R;
			float RoughnessFade;
			ScreenPoint S0;
			float Dx;
			float Dy;
			float K0;
			float K1;
			Float3 Q0;
			Float3 Q1;
			float Width;
			float Height;
			float PlaneTolerance;

			RaySample SampleAt(float t) const
			{
				RaySample sample;
				const float sx = S0.X + Dx * t;
				const float sy = S0.Y + Dy * t;

				if (!(sx >= 0.0f && sy >= 0.0f && sx < Width && sy < Height))
				{
					return sample;
				}

				sample.Inside = true;
				sample.X = static_cast<std::uint32_t>(std::floor(sx));
				sample.Y = static_cast<std::uint32_t>(std::floor(sy));
				sample.RayDepth = -(Q0[2] + (Q1[2] - Q0[2]) * t) / (K0 + (K1 - K0) * t);
				return sample;
			}

			// The depth buffer at a pixel, as seen by the march: nothing at the start pixel, the
			// sky, or a point on the start pixel's own tangent plane (a reflected ray leaves that
			// plane, so meeting it is depth quantization, not a hit: self-intersections showed up
			// as grain on flat glossy faces).
			std::optional<Float3> SceneAt(std::uint32_t sx, std::uint32_t sy) const
			{
				if (sx == X && sy == Y)
				{
					return std::nullopt;
				}

				const auto scene = ViewPositionAt(Params, Depth, sx, sy);

				if (!scene || std::abs(Dot(Subtract(*scene, P), N)) <= PlaneTolerance)
				{
					return std::nullopt;
				}

				return scene;
			}

			ReflectionHit Rejected(const RaySample& hit) const
			{
				ReflectionHit rejected;
				rejected.X = hit.X;
				rejected.Y = hit.Y;
				rejected.Rejected = true;
				return rejected;
			}

			// The trace's result once a sample at t (after `previous`) met the depth buffer;
			// Continue when the ray passed behind a closer surface and marches on.
			struct Outcome
			{
				bool Continue = false;
				std::optional<ReflectionHit> Hit;
			};

			Outcome Resolve(float previous, float t, const RaySample& sample, const std::optional<Float3>& scene) const;
		};

		ReflectionRay::Outcome ReflectionRay::Resolve(
			float previous, float t, const RaySample& sample, const std::optional<Float3>& scene) const
		{
			// Bisect (previous, t] toward the first sample behind the depth buffer.
			float lo = previous;
			float hi = t;

			for (std::uint32_t k = 0; k < Params.SsrRefineSteps; ++k)
			{
				const float mid = 0.5f * (lo + hi);
				const auto probe = SampleAt(mid);
				const auto probeScene = probe.Inside ? SceneAt(probe.X, probe.Y) : std::nullopt;

				if (probeScene && probe.RayDepth >= -(*probeScene)[2])
				{
					hi = mid;
				}
				else
				{
					lo = mid;
				}
			}

			auto hit = SampleAt(hi);
			auto hitScene = hit.Inside ? SceneAt(hit.X, hit.Y) : std::nullopt;

			if (!hitScene)
			{
				hit = sample;
				hitScene = scene;
				hi = t;
				lo = previous;
			}

			// On the surface: within the thickness behind it, or, for a grazing ray, within the
			// ray's depth span over the last interval of a surface that continues across it
			// (a depth jump there is a silhouette the ray passed behind, not a hit).
			const auto loSample = SampleAt(lo);
			const float span = std::abs(hit.RayDepth - loSample.RayDepth);
			const float behindBy = hit.RayDepth - -(*hitScene)[2];
			const auto loScene = loSample.Inside ? SceneAt(loSample.X, loSample.Y) : std::nullopt;
			const bool continuous = loScene && std::abs((*loScene)[2] - (*hitScene)[2]) <= 2.0f * span;
			// With back faces the surface's own thickness decides: a ray that passed behind a
			// sphere near its silhouette (where it is thin) is not a hit, however long the step
			// (the span rule accepted those, slicing reflected objects into bands).
			const bool accepted = BackFaces ? behindBy <= SurfaceThickness(Params, BackDepth, hit.X, hit.Y, -(*hitScene)[2]) + span
											: (behindBy <= Params.SsrThickness || (behindBy <= span && continuous));
			// Inside a surface, but reached across a depth discontinuity (the object's
			// silhouette): the ray entered it through a side the camera does not see.
			// (The ray was already behind that surface before the hit: it came from inside
			// or from behind the object, not through its visible face.)
			const bool enteredHidden = BackFaces && accepted &&
				!(loScene && loSample.RayDepth < -(*loScene)[2] &&
					std::abs((*loScene)[2] - (*hitScene)[2]) <= std::max(2.0f * span, SilhouetteTolerance * -(*hitScene)[2]));

			if (enteredHidden)
			{
				return { false, Rejected(hit) };
			}

			if (!accepted)
			{
				return { true, std::nullopt }; // Behind a closer surface, not on it.
			}

			const auto& hitEncoded = Normal.At(hit.X, hit.Y);
			const auto hitNormal = ViewNormal(Params, { hitEncoded[0], hitEncoded[1], hitEncoded[2] });

			if (!hitNormal || Dot(*hitNormal, R) > 0.0f)
			{
				// The ray entered the surface through its hidden side: what it hit is not on screen.
				return { false, BackFaces ? std::optional<ReflectionHit>(Rejected(hit)) : std::nullopt };
			}

			const float k = K0 + (K1 - K0) * hi;
			const Float3 point{ (Q0[0] + (Q1[0] - Q0[0]) * hi) / k, (Q0[1] + (Q1[1] - Q0[1]) * hi) / k,
				(Q0[2] + (Q1[2] - Q0[2]) * hi) / k };
			const float travelled = Length(Subtract(point, P));
			const float hitX = S0.X + Dx * hi;
			const float hitY = S0.Y + Dy * hi;
			const float u = hitX / Width;
			const float w = hitY / Height;
			const float edge = std::min(std::min(u, 1.0f - u), std::min(w, 1.0f - w));
			const float edgeFade = std::clamp(edge / Params.SsrEdgeFade, 0.0f, 1.0f);
			const float distanceFade =
				std::clamp((Params.SsrMaxDistance - travelled) / (Params.SsrMaxDistance * Params.SsrDistanceFade), 0.0f, 1.0f);
			// Surfaces seen edge-on by the ray fade out: near a silhouette the ray usually passed
			// behind the object (whose far side the depth buffer does not have).
			const float facingFade = std::clamp(-Dot(*hitNormal, R) / FacingFade, 0.0f, 1.0f);
			const float footprintFade =
				FootprintFade(Params, ReflectionFootprint(Params, Depth, Normal, X, Y, -P[2], -point[2], travelled));
			const float confidence = RoughnessFade * edgeFade * distanceFade * facingFade * footprintFade;

			if (!(confidence > 0.0f))
			{
				return { false, std::nullopt };
			}

			return { false, ReflectionHit{ hit.X, hit.Y, hitX, hitY, travelled, confidence } };
		}

	} // namespace

	std::optional<ReflectionHit> TraceReflection(const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal,
		std::uint32_t x, std::uint32_t y, const ScalarImage* backDepth)
	{
		const bool backFaces = params.SsrBackDepth != 0u && backDepth;
		const auto center = ViewPositionAt(params, depth, x, y);
		const auto& encoded = normal.At(x, y);
		const auto n3 = center ? ViewNormal(params, { encoded[0], encoded[1], encoded[2] }) : std::nullopt;

		if (!center || !n3 || !(-(*center)[2] > 0.0f))
		{
			return std::nullopt;
		}

		const float roughness = encoded[3];

		if (!(roughness < params.SsrMaxRoughness))
		{
			return std::nullopt;
		}

		const float roughnessFade = std::clamp((params.SsrMaxRoughness - roughness) / params.SsrRoughnessFade, 0.0f, 1.0f);
		const Float3 p = *center;
		const Float3 n = *n3;
		const Float3 v = Scale(p, 1.0f / Length(p));
		const Float3 reflectedRaw = Subtract(v, Scale(n, 2.0f * Dot(v, n)));
		const Float3 r = Scale(reflectedRaw, 1.0f / Length(reflectedRaw));
		float rayLength = params.SsrMaxDistance;

		if (p[2] + r[2] * rayLength > params.SsrNearZ)
		{
			rayLength = (params.SsrNearZ - p[2]) / r[2]; // Clipped to the near plane.
		}

		if (!(rayLength > 0.0f))
		{
			return std::nullopt;
		}

		const Float3 end{ p[0] + r[0] * rayLength, p[1] + r[1] * rayLength, p[2] + r[2] * rayLength };
		const auto s0 = ProjectToScreen(params, p);
		const auto s1 = ProjectToScreen(params, end);
		const float k0 = 1.0f / s0.W;
		const float k1 = 1.0f / s1.W;
		const float dx = s1.X - s0.X;
		const float dy = s1.Y - s0.Y;
		const float width = float(params.Width);
		const float height = float(params.Height);
		const ReflectionRay ray{ params, depth, normal, backDepth, backFaces, x, y, p, n, r, roughnessFade, s0, dx, dy, k0, k1,
			Scale(p, k0), Scale(end, k1), width, height, SelfPlaneTolerance * -p[2] + SelfPlaneOffset };
		// The samples cover only the on-screen part of the projected ray, [0, tExit]: a ray
		// toward the camera projects far off screen, and spreading MaxSteps over all of it
		// left one or two samples on screen (hits then came and went with the jitter).
		const float tExit = ScreenExit(s0.X, s0.Y, dx, dy, width, height);
		const float pixelLength = std::sqrt(dx * dx + dy * dy) * tExit;
		const auto steps = std::min(params.SsrMaxSteps, static_cast<std::uint32_t>(std::ceil(pixelLength / params.SsrStride)));
		const float jitter = InterleavedGradientNoise(float(x) + 23.0f, float(y) + 41.0f, 0u) /* Fixed per pixel: no temporal grain. */;
		// A sample hits when the ray is behind the depth buffer by at most the thickness, or
		// when it has just crossed from in front of the surface to behind it (a thin
		// intersection the stride stepped over: without this, silhouettes in the reflection
		// alias to the stride). Either way the hit refines by bisection and is kept only if
		// the ray is still within the thickness there (the span of the last bisection
		// interval, for grazing rays): a ray that passed behind a closer silhouette marches on.
		float previous = 0.0f;
		float previousDepth = -p[2]; // The ray's view depth at `previous`.
		bool previousBehind = false; // Whether the sample at `previous` was behind the depth buffer.

		for (std::uint32_t i = 1; i <= steps; ++i)
		{
			const float t = std::min((float(i) + jitter) / float(steps), 1.0f) * tExit;
			const auto sample = ray.SampleAt(t);

			if (!sample.Inside)
			{
				return std::nullopt; // Left the screen.
			}

			const auto scene = ray.SceneAt(sample.X, sample.Y);
			const bool behind = scene && sample.RayDepth >= -(*scene)[2];
			const bool within =
				behind && sample.RayDepth <= -(*scene)[2] + SurfaceThickness(params, backDepth, sample.X, sample.Y, -(*scene)[2]);
			// Crossed this surface's depth since the previous sample (it was in front of it
			// there), or went behind the depth buffer after being in front of it (the surface
			// hit in between may be too thin on screen for any sample to land on).
			const bool crossed = behind && (previousDepth < -(*scene)[2] || !previousBehind);

			if (within || crossed)
			{
				auto outcome = ray.Resolve(previous, t, sample, scene);

				if (!outcome.Continue)
				{
					return outcome.Hit;
				}
			}

			previous = t;
			previousDepth = sample.RayDepth;
			previousBehind = behind;
		}

		return std::nullopt;
	}

	Float3 HitRadiance(const GpuScreenSpaceParams& params, const ColorImage& color, const ColorImage& indirect, const ScalarImage* ao,
		std::uint32_t x, std::uint32_t y)
	{
		const auto& c = color.At(x, y);
		Float3 radiance{ c[0], c[1], c[2] };

		if (params.AoEnabled != 0u && ao)
		{
			const float occlusion = ao->At(x, y);
			const auto& i = indirect.At(x, y);

			for (int k = 0; k < 3; ++k)
			{
				radiance[k] = std::max(radiance[k] - (1.0f - occlusion) * i[k], 0.0f);
			}
		}

		return radiance;
	}

	namespace
	{

		// The four nearest texels of the current colour (HitRadiance) or of the previous frame's
		// (`previous`), bilinearly weighted and divided by 1 + luminance, renormalized.
		Float3 BilinearHitRadiance(const GpuScreenSpaceParams& params, const ColorImage& color, const ColorImage& indirect,
			const ScalarImage* ao, float px, float py, const ColorImage* previous)
		{
			const float ux = px - 0.5f;
			const float uy = py - 0.5f;
			const float bx = std::floor(ux);
			const float by = std::floor(uy);
			const float fx = ux - bx;
			const float fy = uy - by;
			const int maxX = int(color.Width) - 1;
			const int maxY = int(color.Height) - 1;
			Float3 sum{ 0, 0, 0 };
			float weights = 0.0f;

			for (int j = 0; j < 4; ++j)
			{
				const int ox = j & 1;
				const int oy = j >> 1;
				const auto tx = static_cast<std::uint32_t>(std::clamp(int(bx) + ox, 0, maxX));
				const auto ty = static_cast<std::uint32_t>(std::clamp(int(by) + oy, 0, maxY));
				Float3 radiance;

				if (previous)
				{
					const auto& texel = previous->At(tx, ty);
					radiance = { std::max(texel[0], 0.0f), std::max(texel[1], 0.0f), std::max(texel[2], 0.0f) };
				}
				else
				{
					radiance = HitRadiance(params, color, indirect, ao, tx, ty);
				}

				const float luma = 0.2126f * radiance[0] + 0.7152f * radiance[1] + 0.0722f * radiance[2];
				const float weight = (ox != 0 ? fx : 1.0f - fx) * (oy != 0 ? fy : 1.0f - fy) / (1.0f + luma);

				for (int k = 0; k < 3; ++k)
				{
					sum[k] += weight * radiance[k];
				}

				weights += weight;
			}

			const float inverse = weights > 0.0f ? 1.0f / weights : 0.0f;
			return { sum[0] * inverse, sum[1] * inverse, sum[2] * inverse };
		}

	} // namespace

	float HistoryShare(const Float3& reflectance)
	{
		const float luma = 0.2126f * reflectance[0] + 0.7152f * reflectance[1] + 0.0722f * reflectance[2];
		const float t = std::clamp((luma - 0.1f) / 0.4f, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	Float3 FilteredHitRadiance(const GpuScreenSpaceParams& params, const ColorImage& color, const ColorImage& indirect,
		const ScalarImage* ao, float px, float py, const ReflectionHistory& history)
	{
		const auto current = BilinearHitRadiance(params, color, indirect, ao, px, py, nullptr);

		if (params.SsrHistory == 0u || !history.Color || !history.Velocity)
		{
			return current;
		}

		const float width = float(color.Width);
		const float height = float(color.Height);
		const auto hitX = static_cast<std::uint32_t>(std::clamp(int(std::floor(px)), 0, int(color.Width) - 1));
		const auto hitY = static_cast<std::uint32_t>(std::clamp(int(std::floor(py)), 0, int(color.Height) - 1));
		float share = 1.0f;

		if (history.Reflectance)
		{
			const auto& r = history.Reflectance->At(hitX, hitY);
			share = HistoryShare({ r[0], r[1], r[2] });
		}

		if (!(share > 0.0f))
		{
			return current;
		}

		const auto& motion = history.Velocity->At(hitX, hitY);
		const float hx = px - motion[0] * width;
		const float hy = py - motion[1] * height;

		if (!(hx >= 0.0f && hy >= 0.0f && hx <= width && hy <= height))
		{
			return current;
		}

		const auto previous = BilinearHitRadiance(params, color, indirect, ao, hx, hy, history.Color);
		return { current[0] + (previous[0] - current[0]) * share, current[1] + (previous[1] - current[1]) * share,
			current[2] + (previous[2] - current[2]) * share };
	}

	Float4 ReflectionTexel(const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal, const ColorImage& color,
		const ColorImage& indirect, const ScalarImage* ao, std::uint32_t x, std::uint32_t y, const ReflectionHistory& history,
		const ScalarImage* backDepth)
	{
		const auto hit = TraceReflection(params, depth, normal, x, y, backDepth);

		if (!hit)
		{
			return { 0, 0, 0, 0 };
		}

		if (hit->Rejected)
		{
			return { 0, 0, 0, RejectedHit };
		}

		const auto radiance = FilteredHitRadiance(params, color, indirect, ao, hit->HitX, hit->HitY, history);
		return { radiance[0], radiance[1], radiance[2], hit->Confidence };
	}

	ColorImage Reflections(const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal, const ColorImage& color,
		const ColorImage& indirect, const ScalarImage* ao, const ReflectionHistory& history, const ScalarImage* backDepth)
	{
		ColorImage result(depth.Width, depth.Height);

		for (std::uint32_t y = 0; y < depth.Height; ++y)
		{
			for (std::uint32_t x = 0; x < depth.Width; ++x)
			{
				result.At(x, y) = ReflectionTexel(params, depth, normal, color, indirect, ao, x, y, history, backDepth);
			}
		}

		return result;
	}

	float ReactiveTexel(const ReflectionSample& reflection, float confidence)
	{
		const auto smoothstep = [](float edge0, float edge1, float v)
		{
			const float t = std::clamp((v - edge0) / (edge1 - edge0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		};
		const float luminance =
			0.2126f * reflection.Reflectance[0] + 0.7152f * reflection.Reflectance[1] + 0.0722f * reflection.Reflectance[2];

		if (!reflection.Surface || !(luminance > 0.02f))
		{
			return 0.0f;
		}

		const float mirror =
			smoothstep(0.25f, 0.6f, luminance) * (1.0f - smoothstep(0.05f, 0.2f, std::clamp(reflection.Roughness, 0.0f, 1.0f)));
		return mirror * (1.0f - confidence) * reflection.Interior;
	}

	float InteriorTexel(const GpuScreenSpaceParams& params, const ScalarImage& depth, std::uint32_t x, std::uint32_t y)
	{
		const auto centre = ViewPosition(params, float(x) + 0.5f, float(y) + 0.5f, depth.At(x, y));

		if (!centre)
		{
			return 0.0f;
		}

		const float centreDepth = -(*centre)[2];
		const int offsets[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

		for (const auto& o : offsets)
		{
			const int nx = int(x) + o[0];
			const int ny = int(y) + o[1];

			if (nx < 0 || ny < 0 || nx >= int(depth.Width) || ny >= int(depth.Height))
			{
				return 0.0f;
			}

			const auto there = ViewPosition(params, float(nx) + 0.5f, float(ny) + 0.5f, depth.At(std::uint32_t(nx), std::uint32_t(ny)));

			if (!there || std::abs(-(*there)[2] - centreDepth) > 0.05f * centreDepth)
			{
				return 0.0f;
			}
		}

		return 1.0f;
	}

	Float4 CompositeTexel(const GpuScreenSpaceParams& params, const Float4& color, const Float4& indirect, float ao,
		const ReflectionSample& reflection, float depth, std::uint32_t x, std::uint32_t y)
	{
		Float3 c{ color[0], color[1], color[2] };
		float reactive = 0.0f;

		if (params.AoEnabled != 0u)
		{
			for (int i = 0; i < 3; ++i)
			{
				c[i] = std::max(c[i] - (1.0f - ao) * indirect[i], 0.0f);
			}
		}

		if (params.SsrEnabled != 0u || params.ProbeCount > 0u)
		{
			const float aoWeight = params.AoEnabled != 0u ? ao : 1.0f;
			// The fallback: local probes (coverage Probe[3]) over the global environment (Specular).
			Float3 fallback{ reflection.Specular[0], reflection.Specular[1], reflection.Specular[2] };
			const float coverage = reflection.Probe[3];

			if (coverage > 0.0f)
			{
				for (int i = 0; i < 3; ++i)
				{
					fallback[i] =
						reflection.Specular[i] + coverage * (reflection.Reflectance[i] * reflection.Probe[i] - reflection.Specular[i]);
					c[i] = std::max(c[i] + aoWeight * (fallback[i] - reflection.Specular[i]), 0.0f);
				}
			}

			const float confidence = params.SsrEnabled != 0u ? std::max(reflection.Reflection[3], 0.0f) : 0.0f;
			reactive = depth > 0.0f ? ReactiveTexel(reflection, confidence) : 0.0f;

			if (confidence > 0.0f)
			{
				const float weight = confidence * aoWeight;

				for (int i = 0; i < 3; ++i)
				{
					c[i] = std::max(c[i] + weight * (reflection.Reflectance[i] * reflection.Reflection[i] - fallback[i]), 0.0f);
				}
			}
		}

		if (params.FogEnabled != 0u)
		{
			const Float3 camera{ params.InverseViewRows[3], params.InverseViewRows[7], params.InverseViewRows[11] };
			const float px = float(x) + 0.5f;
			const float py = float(y) + 0.5f;
			// The sky is fogged along its view ray (through the near plane) at the max distance.
			const auto view = ViewPosition(params, px, py, depth > 0.0f ? depth : 1.0f);

			if (view)
			{
				const Float3 delta = Subtract(TransformPoint(params.InverseViewRows, *view), camera);
				const float length = Length(delta);

				if (length > 1.0e-6f)
				{
					const Float3 direction = Scale(delta, 1.0f / length);
					const float distance = depth > 0.0f ? std::min(length, params.FogMaxDistance) : params.FogMaxDistance;
					const auto fog = EvaluateFog(params, camera, direction, distance);

					for (int i = 0; i < 3; ++i)
					{
						c[i] = c[i] * fog.Transmittance + fog.Inscatter[i] * (1.0f - fog.Transmittance);
					}
				}
			}
		}

		return { c[0], c[1], c[2], reactive > 0.0f && params.ReflectionDebug == 0u ? 2.0f + reactive : color[3] };
	}

	Float4 CompositeTexel(const GpuScreenSpaceParams& params, const Float4& color, const Float4& indirect, float ao, float depth,
		std::uint32_t x, std::uint32_t y)
	{
		return CompositeTexel(params, color, indirect, ao, ReflectionSample{}, depth, x, y);
	}

	float GlossyBlurRadius(const GpuScreenSpaceParams& params, float roughness)
	{
		const float r = std::clamp(roughness, 0.0f, 1.0f);
		return std::min(GlossyBlurScale * r * std::sqrt(r), GlossyBlurMax) * (float(params.Height) / 1080.0f);
	}

	Float4 ResolvedReflectionTexel(const GpuScreenSpaceParams& params, const ColorImage& reflection, const ColorImage& normal,
		const ScalarImage& depth, std::uint32_t x, std::uint32_t y)
	{
		const auto& centre = reflection.At(x, y);
		const auto& encoded = normal.At(x, y);
		const auto position = ViewPositionAt(params, depth, x, y);

		if (!position)
		{
			return centre;
		}

		const int step = std::max(1, int(std::floor(GlossyBlurRadius(params, encoded[3]) * 0.5f + 0.5f)));
		const float centreDepth = -(*position)[2];
		const Float3 n{ encoded[0], encoded[1], encoded[2] };
		Float3 radiance{ 0, 0, 0 };
		float radianceWeight = 0.0f;
		float confidence = 0.0f;
		float weights = 0.0f;

		for (int j = 0; j < 25; ++j)
		{
			const int ox = j % 5 - 2;
			const int oy = j / 5 - 2;
			const int tx = std::clamp(int(x) + ox * step, 0, int(reflection.Width) - 1);
			const int ty = std::clamp(int(y) + oy * step, 0, int(reflection.Height) - 1);
			const auto tapPosition = ViewPositionAt(params, depth, std::uint32_t(tx), std::uint32_t(ty));

			if (!tapPosition || std::abs(-(*tapPosition)[2] - centreDepth) > 0.05f * centreDepth)
			{
				continue;
			}

			const auto& tapNormal = normal.At(std::uint32_t(tx), std::uint32_t(ty));

			if (Dot(n, { tapNormal[0], tapNormal[1], tapNormal[2] }) < 0.9f)
			{
				continue;
			}

			const float w = GlossyTapWeights[std::abs(ox)] * GlossyTapWeights[std::abs(oy)];
			const auto& tap = reflection.At(std::uint32_t(tx), std::uint32_t(ty));
			const float tapConfidence = std::max(tap[3], 0.0f); // RejectedHit counts as a miss.
			confidence += w * tapConfidence;
			weights += w;

			for (int c = 0; c < 3; ++c)
			{
				radiance[c] += w * tapConfidence * tap[c];
			}

			radianceWeight += w * tapConfidence;
		}

		if (!(weights > 0.0f))
		{
			return centre;
		}

		const float inverse = radianceWeight > 0.0f ? 1.0f / radianceWeight : 0.0f;
		return { radiance[0] * inverse, radiance[1] * inverse, radiance[2] * inverse, confidence / weights };
	}

	ColorImage Composite(const GpuScreenSpaceParams& params, const ColorImage& color, const ColorImage& indirect, const ScalarImage* ao,
		const ScalarImage& depth, const ReflectionImages& reflections)
	{
		ColorImage result(color.Width, color.Height);

		for (std::uint32_t y = 0; y < color.Height; ++y)
		{
			for (std::uint32_t x = 0; x < color.Width; ++x)
			{
				ReflectionSample reflection;

				if (reflections.Reflection && reflections.Reflectance && reflections.Specular)
				{
					reflection = { reflections.Normal
							? ResolvedReflectionTexel(params, *reflections.Reflection, *reflections.Normal, depth, x, y)
							: reflections.Reflection->At(x, y),
						reflections.Reflectance->At(x, y), reflections.Specular->At(x, y) };
				}

				result.At(x, y) =
					CompositeTexel(params, color.At(x, y), indirect.At(x, y), ao ? ao->At(x, y) : 1.0f, reflection, depth.At(x, y), x, y);
			}
		}

		return result;
	}

} // namespace Swim::Render::ScreenSpace
