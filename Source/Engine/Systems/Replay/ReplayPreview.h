#pragma once

#include "Engine/Systems/Replay/ReplayPlayer.h"
#include "Engine/Systems/Tooling/ImageEncoding.h"

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Swim::Replay
{

	// One offscreen frame: the scene at a timeline time seen from a camera pose. Options
	// are host-defined flags passed through untouched (for example "hud": "off").
	struct PreviewRequest
	{
		double Time = 0.0;
		CameraPose Camera;
		std::uint32_t Width = 640;
		std::uint32_t Height = 360;
		std::map<std::string, std::string> Options;
	};

	// Called exactly once on the main thread with the image, or an empty image and an error.
	using PreviewReply = std::function<void(Tooling::Image image, std::string error)>;

	// The host renders previews (ReplayToolsDesc::RenderPreview). It may answer later
	// (frames from now); requests are issued one at a time.
	using PreviewRenderer = std::function<void(const PreviewRequest& request, PreviewReply reply)>;

	// Renders requests one after another through the renderer and collects the images in
	// order; done gets them all (or the first error).
	void RenderPreviews(const PreviewRenderer& renderer, std::vector<PreviewRequest> requests,
		std::function<void(std::vector<Tooling::Image> images, std::string error)> done);

	// count times spread over [from, to): the centre of each of count equal parts.
	std::vector<double> SampleTimes(double from, double to, std::uint32_t count);

	// The camera's view volume for drawing. Corners run bottom-left, bottom-right,
	// top-right, top-left (as seen through the camera).
	struct CameraFrustum
	{
		Vec3 Apex{ 0.0f, 0.0f, 0.0f };
		Vec3 Forward{ 0.0f, 0.0f, -1.0f };
		Vec3 Up{ 0.0f, 1.0f, 0.0f };
		Vec3 Right{ 1.0f, 0.0f, 0.0f };
		std::array<Vec3, 4> Near{};
		std::array<Vec3, 4> Far{};
	};

	CameraFrustum MakeFrustum(const CameraPose& pose, float aspect, float nearDistance = 0.1f, float farDistance = 2.0f);

	// A path ready to draw: the curve, its control points, Bezier handles (absolute) and
	// ticks at equal arc-length steps (direction of travel included).
	struct PathViz
	{
		std::vector<Vec3> Curve;
		std::vector<Vec3> Keys;
		std::vector<std::array<Vec3, 2>> Handles; // Bezier only: (in, out) per key.
		std::vector<Vec3> Ticks;
		std::vector<Vec3> TickDirections;
		float Length = 0.0f;
	};

	PathViz SamplePathViz(const CameraPath& path, std::uint32_t samplesPerSegment = 16, std::uint32_t ticks = 10);

	struct CameraSample
	{
		double Time = 0.0;
		CameraPose Pose;
	};

	// The evaluated camera at count times over [from, to) (Peek: the transport stays put).
	std::vector<CameraSample> SampleCamera(ReplayPlayer& player, double from, double to, std::uint32_t count);

} // namespace Swim::Replay
