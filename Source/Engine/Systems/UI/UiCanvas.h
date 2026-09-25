#pragma once

#include "Engine/Systems/UI/UiDocument.h"

#include <array>
#include <optional>

// Where a UiDocument is shown (critical-path item 79). A document is always a 2D canvas
// in framebuffer pixels (its Layout size); a canvas placement maps those pixels to the
// screen or to the world. Pure math: no renderer, scene or platform dependency. The
// renderer draws with ClipFromCanvas; input maps pointers or rays back to canvas pixels
// (UiCanvasRouter).
//
// Conventions (as the renderer's): matrices are row-major and multiply column vectors
// (clip = M * v); views are rigid world -> view transforms looking down -Z; clip space
// is +Y up; canvas pixels are +Y down with the origin at the top left.
namespace Swim::UI
{
	using UiMatrix4 = std::array<float, 16>;
	using UiMatrix3x4 = std::array<float, 12>; // Affine: the first three rows of a 4x4.

	inline constexpr UiMatrix4 UiIdentity4{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	inline constexpr UiMatrix3x4 UiIdentity3x4{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };

	struct UiVec3
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
	};

	struct UiRay
	{
		UiVec3 Origin;
		UiVec3 Direction; // Need not be normalized; hit distances are in its units.
	};

	enum class UiCanvasMode : std::uint8_t
	{
		Screen,		   // An overlay on a color target (swapchain, post output).
		RenderSurface, // Rendered into its own texture, sampled by any material or mesh.
		WorldPanel,	   // A quad in the world with a transform.
		Billboard,	   // A world panel facing the camera.
	};

	enum class UiBillboardMode : std::uint8_t
	{
		Spherical,	  // Faces the camera position.
		Cylindrical,  // Turns only around UpAxis towards the camera.
		ScreenAligned // Parallel to the image plane (camera right/up axes).
	};

	struct UiCameraView
	{
		UiMatrix4 View = UiIdentity4; // World -> view (rigid, -Z forward).
		UiMatrix4 Projection{};		  // View -> clip (perspective or orthographic, any Z convention).
		float ViewportWidth = 0.0f;	  // Pixels.
		float ViewportHeight = 0.0f;
	};

	struct UiWorldPlacement
	{
		// Panel -> world. Billboards use only its translation (the anchor).
		UiMatrix3x4 Transform = UiIdentity3x4;
		UiPoint Pivot{ 0.5f, 0.5f };  // Fraction of the canvas placed at the transform's origin.
		float UnitsPerPixel = 0.001f; // World units per canvas pixel (1 m per 1000 px).
		UiBillboardMode Billboard = UiBillboardMode::Spherical;
		UiVec3 UpAxis{ 0.0f, 1.0f, 0.0f }; // Cylindrical billboards, and the up reference of spherical ones.
		// Billboards: keep ScreenPixelsPerCanvasPixel screen pixels per canvas pixel at any
		// distance (name tags, markers) instead of a world size.
		bool ConstantScreenSize = false;
		float ScreenPixelsPerCanvasPixel = 1.0f;
		// Distance fade (world units from the camera): 1 before FadeStart, 0 after FadeEnd.
		// FadeEnd <= FadeStart disables fading.
		float FadeStart = 0.0f;
		float FadeEnd = 0.0f;
		bool TwoSided = true; // Back faces are hit (mirrored) too.
	};

	struct UiCanvasHit
	{
		UiPoint Point;		   // Canvas pixels.
		float Distance = 0.0f; // Along the ray, in the ray direction's units.
		bool FrontFacing = true;
	};

	// Throws std::invalid_argument for a non-finite or degenerate placement or view.
	void ValidatePlacement(const UiWorldPlacement& placement);

	UiMatrix4 Multiply(const UiMatrix4& a, const UiMatrix4& b);
	UiMatrix4 ToMatrix4(const UiMatrix3x4& affine);
	UiVec3 TransformPoint(const UiMatrix3x4& m, UiVec3 p);
	// The world position of the camera (inverse of a rigid view).
	UiVec3 CameraPosition(const UiMatrix4& view);

	// Canvas pixels -> world for a world panel or billboard of canvasSize pixels. Billboards
	// need the camera; panels ignore it. Columns 0 and 1 are the world vectors of one canvas
	// pixel right and down; column 2 is the panel's unit front normal (towards viewers).
	UiMatrix3x4 CanvasToWorld(
		UiCanvasMode mode, const UiWorldPlacement& placement, UiPoint canvasSize, const UiCameraView* camera = nullptr);
	// Canvas pixels -> clip for the renderer (UiRenderFrame::ClipFromCanvas).
	UiMatrix4 ClipFromCanvas(const UiMatrix3x4& canvasToWorld, const UiCameraView& camera);
	// A screen overlay of a width x height target, canvas origin at (offsetX, offsetY) pixels.
	UiMatrix4 ScreenClipFromCanvas(float width, float height, float offsetX = 0.0f, float offsetY = 0.0f);

	// The world ray through a viewport pixel (top-left origin), for mouse picking.
	UiRay ScreenRay(const UiCameraView& camera, UiPoint pixel);
	// The ray hit inside a canvas rectangle [0, size) (back faces only when twoSided).
	std::optional<UiCanvasHit> IntersectCanvas(
		const UiRay& ray, const UiMatrix3x4& canvasToWorld, UiPoint canvasSize, bool twoSided = true);
	// The hit on the canvas's unbounded plane (pointer capture continues off the panel);
	// empty for rays parallel to it or pointing away.
	std::optional<UiCanvasHit> IntersectCanvasPlane(const UiRay& ray, const UiMatrix3x4& canvasToWorld);

	// Canvas pixel -> viewport pixel; empty behind the camera.
	std::optional<UiPoint> ProjectCanvasPoint(const UiMatrix4& clipFromCanvas, UiPoint viewport, UiPoint canvasPoint);
	// The viewport bounding box of a canvas rectangle (IME candidate windows); empty when
	// any corner is behind the camera.
	std::optional<UiRect> ProjectCanvasRect(const UiMatrix4& clipFromCanvas, UiPoint viewport, const UiRect& rect);

	// The placement's fade (1 opaque .. 0 invisible) at a camera distance.
	float CanvasFade(const UiWorldPlacement& placement, float distance);
	// Distance from the camera to the canvas's origin (the pivot point).
	float CanvasDistance(const UiMatrix3x4& canvasToWorld, UiPoint canvasSize, UiPoint pivot, const UiMatrix4& view);
} // namespace Swim::UI
