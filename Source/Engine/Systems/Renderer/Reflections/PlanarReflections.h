#pragma once
#include "Engine/Systems/Renderer/Reflections/PlanarReflectionTypes.h"
#include "Engine/Systems/Renderer/Reflections/ReflectionProbeTypes.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

// Planar reflections: the sharp, current layer of the reflection hierarchy for flat mirrors
// (and the camera-facing cap of chrome spheres), ahead of screen-space reflections, local
// probes and the environment. The CPU half: which reflectors deserve a capture, how
// captures are shared, sized and scheduled, and the lookup math the composite mirrors
// (Shaders/Slang/ScreenSpace/ScreenSpaceComposite.slang, SamplePlanar).
//
// A capture renders the scene from the camera mirrored in the plane, through the reflector
// as a window: the view looks along the plane normal and the off-axis frustum spans the
// reflector's visible portal, so the near plane IS the mirror (nothing behind it renders)
// and a point of the plane maps to the capture linearly (uniform texel density on the
// mirror, no texel spent outside it). Everything outside that small frustum, beyond
// CullDistance, or the reflector itself is culled by GPU visibility, at coarse LODs.
namespace Swim::Render::PlanarReflections
{

	using Float3 = std::array<float, 3>;
	using Float4 = std::array<float, 4>;
	using Matrix = std::array<float, 16>; // Row-major: clip = M * v.

	// The camera reflectors are seen from.
	struct ViewCamera
	{
		Matrix ViewProjection{};
		Float3 Position{};
		Float3 Forward{ 0.0f, 0.0f, -1.0f };
		float VerticalFov = 1.0f; // Radians.
		float ViewportWidth = 1.0f;
		float ViewportHeight = 1.0f;
	};

	// One capture to render this frame.
	struct Capture
	{
		std::uint32_t Slot = 0;	 // Atlas layer.
		std::uint32_t Width = 0; // Stored size (the atlas region).
		std::uint32_t Height = 0;
		// Rendered size (>= the stored size: supersampled, averaged down by the resolve).
		std::uint32_t RenderWidth = 0;
		std::uint32_t RenderHeight = 0;
		Matrix View{};
		Matrix Projection{};	 // Off-axis, reverse-Z, infinite far.
		Matrix ViewProjection{};
		Float3 Position{}; // The reflected camera (planes) or the sphere centre.
		Float3 Right{};
		Float3 Up{};
		Float3 Forward{};
		float NearClip = 0.0f; // Distance to the mirror plane (or the sphere surface): nothing nearer renders.
		// View rays for the resolve: ndc -> view x = depth * (ndc + Scale[2]) / Scale[0], y likewise.
		std::array<float, 4> FrustumScale{}; // P00, P11, P02, P12.
		Float4 CullPlane{};					 // Far cull plane (world; inside when n . p + d >= 0).
		std::uint32_t ExcludedObjectId = 0;
		float LodScale = 1.0f; // Capture pixels per world unit at distance 1.
	};

	struct Plan
	{
		std::vector<Capture> Captures;					 // This frame's re-renders, most urgent first.
		std::vector<GpuPlanarReflectionRecord> Records; // What shading uses (captured slots), largest first.
		std::uint32_t Candidates = 0;					 // Reflector faces considered.
		std::uint32_t Culled = 0;						 // Rejected: back-facing, off screen or too small.
		std::uint32_t Groups = 0;						 // Shared planes after merging.
		std::uint32_t DynamicCaptures = 0;				 // Captures re-rendered because their content changed.
	};

	// The mirror image of p in the plane n . x = d.
	Float3 ReflectPoint(const Float3& p, const Float3& n, float d);

	// A row-major view matrix from a position and an orthonormal right/up/forward basis.
	Matrix ViewMatrix(const Float3& position, const Float3& right, const Float3& up, const Float3& forward);

	// Off-axis perspective: the window [left, right] x [bottom, top] at distance `focal`
	// fills the image; reverse-Z with an infinite far plane and depth = nearClip / distance.
	Matrix OffAxisReverseZ(float left, float right, float bottom, float top, float focal, float nearClip);

	// Where a world point lands in a record's capture: (u, v) in [0, 1] inside, w > 0 in front.
	Float3 ProjectToCapture(const GpuPlanarReflectionRecord& record, const Float3& world);

	// The composite's lookup (SamplePlanar): the capture coordinates of what the ray
	// position + t * direction hits. It starts where the surface point itself projects -
	// exact for a mirror whose capture is current - and refines t `iterations` times with the
	// captured distances (`distance(uv)`: from the capture position to what that texel sees),
	// which corrects a camera that moved since the capture and the curvature of spheres.
	std::array<float, 2> LookupUv(const GpuPlanarReflectionRecord& record, const Float3& position, const Float3& direction,
		const std::function<float(const std::array<float, 2>&)>& distance, std::uint32_t iterations = 2);

	inline constexpr std::uint32_t LookupIterations = 2;
	inline constexpr float DistanceSky = 10000.0f;
	// The widest half angle of a sphere cap's capture (radians; perspective stays usable).
	inline constexpr float SphereMaxHalfAngle = 1.3f;

	// How much a confident screen-space hit may replace a reflector's planar reflection: 0
	// while it covers at least SsrFallbackScreenFraction of the screen height, rising smoothly
	// to 1 at MinScreenFraction (where it is culled). The far LOD of the hierarchy.
	float SsrPreference(float screenFraction, const PlanarReflectionSettings& settings);

	// The rendered size of a capture: its stored size times Supersample (1 .. 4), lowered so
	// the longer side stays within MaxRenderResolution (never below the stored size).
	void Finish(Capture& capture, const PlanarReflectionSettings& settings);

	class Planner
	{

	  public:

		// Builds this frame's plan (a reference valid until the next Update). Reflectors are
		// expanded into faces, culled (facing, frustum, size), coplanar faces merged into shared
		// captures, sized by their screen footprint (LOD) and given slots. Up to
		// CapturesPerFrame re-render, most urgent first: changed content (new, the reflector
		// moved, the portal outgrew the capture, a mover inside the old or the new view, or a
		// mover that was inside at the last capture: it left, and the capture must lose it),
		// then the camera moved more than MotionTolerance texels, the LOD changed, or older than
		// MaxAgeSeconds (0: every frame). Allocation-free once warm.
		const Plan& Update(std::span<const PlanarReflectorDesc> reflectors, const ViewCamera& camera, std::uint64_t frame, double time,
			const PlanarReflectionSettings& settings, std::span<const ReflectionProbeMover> movers = {});

		void Reset();

		std::uint32_t GetUsedSlots() const;

		const Plan& GetPlan() const { return plan; }

	  private:

		struct Candidate
		{
			std::uint64_t Key = 0;
			std::uint32_t Owner = 0;
			bool Sphere = false;
			Float3 Center{};
			Float3 HalfU{};
			Float3 HalfV{};
			Float3 Normal{};
			float Radius = 0.0f;
			float Score = 0.0f;
			float Pixels = 0.0f;
			float Quality = 1.0f;
			float Priority = 1.0f;
		};

		struct Group
		{
			std::uint32_t First = 0; // Candidate index of its highest-scoring member.
			Float3 Normal{};
			Float3 U{};
			Float3 V{};
			float NearD = 0.0f; // The plane of the member nearest the reflected camera.
			float MinD = 0.0f;
			float MaxD = 0.0f;
			float UMin = 0.0f, UMax = 0.0f, VMin = 0.0f, VMax = 0.0f; // Portal, in-plane coordinates.
			float Score = 0.0f;
		};

		struct Wanted
		{
			bool Valid = false;
			Capture View;
			Float4 MatchPlane{}; // Normal and offset for the geometric pixel match.
			float Tolerance = 0.0f;
			float MinCosine = 0.0f;
			float TexelAngle = 0.0f;
			float Density = 1.0f;		 // Capture texels per screen pixel.
			float ScreenFraction = 0.0f; // The reflector's screen extent / viewport height (LOD).
			float Distance = 0.0f;		 // Camera to the mirror (planes) or the sphere centre.
			std::uint32_t Owner = 0;
			std::uint64_t Key = 0;
			std::array<Float3, 4> Portal{}; // Plane groups: the captured window's corners (world).
			std::array<Float4, 6> Frustum{}; // The capture's view (far: CullPlane).
		};

		struct Slot
		{
			bool Used = false;
			bool Captured = false;
			std::uint64_t Key = 0;
			std::uint64_t LastSeen = 0;
			double CaptureTime = 0.0;
			Float3 CaptureCamera{}; // The real camera position at capture.
			bool SawMover = false;	 // Something moving was inside at the last capture.
			Wanted State;			 // What the atlas layer holds.
		};

		void AddPlane(const PlanarReflectorDesc& reflector, std::uint32_t face, const Float3& center, const Float3& halfU,
			const Float3& halfV, const ViewCamera& camera, const PlanarReflectionSettings& settings);

		void AddSphere(const PlanarReflectorDesc& reflector, const ViewCamera& camera, const PlanarReflectionSettings& settings);

		bool InCameraFrustum(const Float3& center, float radius) const;

		Wanted Describe(const Group& group, const ViewCamera& camera, const PlanarReflectionSettings& settings);

		// How urgently a slot needs `wanted` re-rendered (0: its capture is still good);
		// `dynamic`: because its content changed (the dynamic budget), not just aged.
		float Urgency(const Slot& slot, const Wanted& wanted, const ViewCamera& camera, double time,
			const PlanarReflectionSettings& settings, bool moverInside, bool& dynamic) const;

		// A mover inside the view (its frustum, far: CullPlane), within `range` of the reflector.
		static bool SeesMover(const Wanted& view, std::span<const ReflectionProbeMover> movers, float range);

		std::array<Float4, 6> cameraFrustum{};
		std::vector<Candidate> candidates;
		std::vector<std::uint32_t> order;
		std::vector<Group> groups;
		std::vector<Wanted> wanted;
		struct Urgent
		{
			float Urgency = 0.0f;
			std::uint32_t Group = 0;
			bool Dynamic = false;
			bool MoverInside = false;
		};
		std::vector<Urgent> urgent;
		std::vector<Float3> clipA;
		std::vector<Float3> clipB;
		std::vector<Slot> slots;
		Plan plan;

	};

} // namespace Swim::Render::PlanarReflections
