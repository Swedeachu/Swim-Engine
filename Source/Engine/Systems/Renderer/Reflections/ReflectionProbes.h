#pragma once
#include "Engine/Systems/Renderer/Reflections/ReflectionProbeTypes.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

// Reflection probes (the local layer of the reflection hierarchy: screen-space
// reflections -> local probes -> the global environment). The CPU definition of the probe
// math that Shaders/Slang/ScreenSpace/ScreenSpaceComposite.slang and
// ReflectionProbeResolve.slang mirror, and the scheduler that decides which cube faces
// are captured each frame.
namespace Swim::Render::ReflectionProbes
{
	using Float3 = std::array<float, 3>;

	// The axes of a cube face in the renderer's cube convention (Environment::
	// CubeFaceDirection): texel (s, t) in [-1, 1] (t down) looks along Forward + s * Right -
	// t * Up.
	struct FaceBasis
	{
		Float3 Forward;
		Float3 Right;
		Float3 Up;
	};

	FaceBasis CubeFace(std::uint32_t face);

	// Whether a sphere (centre relative to the probe) is inside face `face`'s 90-degree frustum.
	bool FaceSees(std::uint32_t face, const Float3& offset, float radius);

	// A right-handed world -> view matrix (row-major) looking along the face from `position`.
	// Its image is the face mirrored left-right (cube faces are left-handed): capture pixel
	// column CaptureColumn(x) holds cube texel column x.
	std::array<float, 16> CubeFaceView(std::uint32_t face, const Float3& position);
	inline std::uint32_t CaptureColumn(std::uint32_t x, std::uint32_t size)
	{
		return size - 1u - x;
	}

	// The 90-degree square reverse-Z projection of the capture views.
	std::array<float, 16> CubeFaceProjection(float nearPlane);

	// Distance from the probe to what a capture pixel sees: reverse-Z depth -> view depth
	// near / depth, times the pixel's ray length; DistanceSky where nothing was drawn.
	inline constexpr float DistanceSky = 10000.0f;
	float CaptureDistance(float depth, float nearPlane, float ndcX, float ndcY);

	// Which probes a surface point uses. Object probes (OwnerObjectId != 0) serve only
	// their owner's pixels, exclusively (a probe inside a sphere sees the scene around the
	// sphere but not the sphere: other objects must not use it). Area probes serve every
	// other pixel within their influence radius, weighted by
	// saturate((radius - d) / blend); the two strongest blend in proportion to weight /
	// (d / radius + 0.05) (the nearer probe's view dominates), and their coverage (how much
	// of the fallback the probes replace, the rest being the global environment) is the
	// strongest weight.
	struct Selection
	{
		std::int32_t First = -1; // Index into the records.
		std::int32_t Second = -1;
		float FirstShare = 0.0f; // Blend shares (sum to 1 when both are used).
		float SecondShare = 0.0f;
		float Coverage = 0.0f;
	};

	Selection Select(std::span<const GpuReflectionProbeRecord> records, const Float3& position, float pixelObjectId);

	// Parallax correction with the stored distances: the direction from the probe to where
	// the ray position + t * direction first meets the captured surface. The ray is marched
	// in the probe's space with texel-adaptive steps - each turns the sample's direction from
	// the probe by about ParallaxTexels texels of the probe (`texelAngle` radians each), so
	// steps are short where the ray passes close to the probe and long where it heads away -
	// comparing each sample's distance from the probe with the captured distance D in its
	// direction. The first step from in front of to beyond D brackets a crossing, and
	// ParallaxRefineSteps bisections place it. It counts only when the refined point lies on
	// the captured surface (within ParallaxThickness): a ray passing behind an occluder, as
	// the probe sees it, also goes "beyond" - at the occluder's silhouette, far behind its
	// surface - and marches on instead of stopping there. A ray that crosses nothing within
	// ParallaxReach keeps its own direction (the sky). Fixed or jittered sample spacings left
	// stair steps and noise on flat mirrors, which show the probe 1:1.
	inline float ParallaxThickness(float length, float interval)
	{
		return 0.08f * length + 2.0f * interval + 0.02f;
	}

	inline constexpr std::uint32_t ParallaxSteps = 96;
	inline constexpr std::uint32_t ParallaxRefineSteps = 5;
	inline constexpr float ParallaxReach = 40.0f;
	inline constexpr float ParallaxTexels = 3.0f;
	inline constexpr float ParallaxMinStep = 0.01f;
	inline constexpr float ParallaxMinAngle = 0.02f; // Radians per step at the least (high-resolution probes).
	// The texel angle of a probe face of `resolution` texels (90 degrees across).
	inline float ProbeTexelAngle(std::uint32_t resolution)
	{
		return 1.5707963f / float(resolution > 0 ? resolution : 1u);
	}
	Float3 ParallaxDirection(const Float3& position, const Float3& direction, const Float3& probe,
		const std::function<float(const Float3&)>& distance, float texelAngle = ProbeTexelAngle(128));

	// The time-sliced update plan. Slots are the probe atlas's cube indices; each keeps the
	// key of the probe it holds, the frame each face was last captured and where from.
	struct FaceCapture
	{
		std::uint32_t Slot = 0;
		std::uint32_t Face = 0;
		std::uint32_t Probe = 0; // Index into the probes passed to Update.
	};

	struct ActiveProbe
	{
		std::uint32_t Slot = 0;
		std::uint32_t Probe = 0;
		float Age = 0.0f; // Seconds since its oldest face was captured.
	};

	struct Plan
	{
		std::vector<FaceCapture> Captures; // This frame's faces, most urgent first.
		std::vector<std::uint32_t> Filter; // Slots to prefilter after the captures (sorted).
		std::vector<ActiveProbe> Active;   // Probes shading may use (every face captured).
	};

	class Scheduler
	{
	  public:
		// Urgency of a face: a probe with faces never captured first (x 1000), then faces of
		// a dynamic probe that see something that moved this frame (`movers` within
		// MoverRange inside the face's frustum: 300 + frames since capture), then faces of a
		// probe that moved beyond MoveThreshold since their capture (x 100), then the oldest
		// faces of dynamic probes (frames since capture), each x Priority / (1 + the camera's
		// distance to the probe). Static probes are captured only when new or moved. Probes
		// beyond MaxProbes (by that distance-weighted priority) get no slot.
		Plan Update(std::span<const ReflectionProbeDesc> probes, const Float3& camera, std::uint64_t frame, double time,
			const ReflectionProbeSettings& settings, std::span<const ReflectionProbeMover> movers = {});

		void Reset();

		std::uint32_t GetUsedSlots() const;

	  private:
		struct Slot
		{
			bool Used = false;
			std::uint64_t Key = 0;
			std::array<std::uint64_t, 6> FaceFrame{};	   // 0: never captured.
			std::array<double, 6> FaceTime{};
			std::array<Float3, 6> FacePosition{};
			std::uint64_t LastSeen = 0;
		};

		std::vector<Slot> slots;
	};
} // namespace Swim::Render::ReflectionProbes
