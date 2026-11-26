#include "Engine/Systems/Replay/ReplayPreview.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace Swim::Replay
{

	using Animation::Add;
	using Animation::Scale;

	void RenderPreviews(const PreviewRenderer& renderer, std::vector<PreviewRequest> requests,
		std::function<void(std::vector<Tooling::Image> images, std::string error)> done)
	{
		if (!renderer)
		{
			done({}, "this host cannot render previews");
			return;
		}

		struct State
		{
			PreviewRenderer Renderer;
			std::vector<PreviewRequest> Requests;
			std::vector<Tooling::Image> Images;
			std::function<void(std::vector<Tooling::Image>, std::string)> Done;
			std::function<void()> Next;
		};

		auto state = std::make_shared<State>();
		state->Renderer = renderer;
		state->Requests = std::move(requests);
		state->Done = std::move(done);
		std::weak_ptr<State> weak = state;
		state->Next = [weak]()
		{
			const auto s = weak.lock();

			if (!s)
			{
				return;
			}

			if (s->Images.size() == s->Requests.size())
			{
				auto finish = std::move(s->Done);
				auto images = std::move(s->Images);
				s->Next = nullptr; // Breaks the cycle held by the replies.
				finish(std::move(images), {});
				return;
			}

			// The reply keeps the state alive until the host answers.
			s->Renderer(s->Requests[s->Images.size()],
				[s](Tooling::Image image, std::string error)
				{
					if (!s->Next)
					{
						return; // Answered twice, or after a failure.
					}

					if (!error.empty() || !image.IsValid())
					{
						auto finish = std::move(s->Done);
						s->Next = nullptr;
						finish({}, error.empty() ? "the host returned an invalid image" : error);
						return;
					}

					s->Images.push_back(std::move(image));
					s->Next();
				});
		};
		state->Next();
	}

	std::vector<double> SampleTimes(double from, double to, std::uint32_t count)
	{
		std::vector<double> times;

		if (count == 0 || !std::isfinite(from) || !std::isfinite(to))
		{
			return times;
		}

		times.reserve(count);

		for (std::uint32_t i = 0; i < count; ++i)
		{
			times.push_back(from + (to - from) * (i + 0.5) / count);
		}

		return times;
	}

	CameraFrustum MakeFrustum(const CameraPose& pose, float aspect, float nearDistance, float farDistance)
	{
		CameraFrustum f;
		const Quat rotation = Animation::Normalize(pose.Rotation);
		f.Apex = pose.Position;
		f.Forward = Animation::Rotate(rotation, { 0.0f, 0.0f, -1.0f });
		f.Up = Animation::Rotate(rotation, { 0.0f, 1.0f, 0.0f });
		f.Right = Animation::Rotate(rotation, { 1.0f, 0.0f, 0.0f });
		aspect = std::isfinite(aspect) && aspect > 0.0f ? aspect : 16.0f / 9.0f;
		const float tanHalf = std::tan(std::clamp(pose.Fov, 1.0f, 170.0f) * 0.5f * 3.14159265f / 180.0f);

		const auto corners = [&](float distance)
		{
			const Vec3 centre = Add(f.Apex, Scale(f.Forward, distance));
			const Vec3 up = Scale(f.Up, tanHalf * distance);
			const Vec3 right = Scale(f.Right, tanHalf * aspect * distance);
			return std::array<Vec3, 4>{ Add(Add(centre, Scale(right, -1.0f)), Scale(up, -1.0f)), Add(Add(centre, right), Scale(up, -1.0f)),
				Add(Add(centre, right), up), Add(Add(centre, Scale(right, -1.0f)), up) };
		};

		f.Near = corners(std::max(nearDistance, 0.0f));
		f.Far = corners(std::max(farDistance, nearDistance));
		return f;
	}

	PathViz SamplePathViz(const CameraPath& path, std::uint32_t samplesPerSegment, std::uint32_t ticks)
	{
		PathViz viz;
		viz.Curve = path.Polyline(samplesPerSegment);
		viz.Length = path.Length();

		for (const PathPoint& p : path.Points)
		{
			viz.Keys.push_back(p.Position);

			if (path.Kind == PathKind::Bezier)
			{
				viz.Handles.push_back({ Add(p.Position, p.InTangent), Add(p.Position, p.OutTangent) });
			}
		}

		if (!path.Points.empty())
		{
			ticks = std::min(ticks, 1024u);

			for (std::uint32_t i = 0; i <= ticks && ticks > 0; ++i)
			{
				const float fraction = static_cast<float>(i) / static_cast<float>(ticks);
				viz.Ticks.push_back(path.Sample(fraction));
				viz.TickDirections.push_back(path.Direction(fraction));
			}
		}

		return viz;
	}

	std::vector<CameraSample> SampleCamera(ReplayPlayer& player, double from, double to, std::uint32_t count)
	{
		std::vector<CameraSample> samples;

		for (const double t : SampleTimes(from, to, std::min(count, 4096u)))
		{
			samples.push_back({ t, player.Peek(t).Camera });
		}

		return samples;
	}

} // namespace Swim::Replay
