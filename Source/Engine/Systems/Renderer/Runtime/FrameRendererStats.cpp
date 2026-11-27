#include "Engine/Systems/Renderer/Runtime/FrameRendererImpl.h"

namespace Engine
{

	using namespace FrameRendererDetail;

	void FrameRenderer::GatherTimings()
	{
		auto& executor = device.GetExecutor();
		executor.Wait();

		// Timings of the frame that just completed (none before the first submission).
		std::vector<R::GraphPassTiming> timings;

		if (lastCompletion.Semaphore)
		{
			try
			{
				timings = executor.ReadTimings();
			}
			catch (const std::exception&)
			{
				timings.clear(); // The last graph failed or was never executed.
			}
		}

		// Each pass's exclusive GPU time: the step of its end timestamp (begin timestamps are
		// written before the pass's barriers and can overlap earlier work, so summing the
		// begin-to-end spans counts overlap twice). The last end is the frame's GPU time.
		std::vector<RenderStats::PassTiming> passes;
		double total = 0.0;
		double previousEnd = 0.0;
		bool haveEnds = true;

		for (const auto& timing : timings)
		{
			haveEnds = haveEnds && timing.EndOffsetNanoseconds.has_value();
		}

		for (const auto& timing : timings)
		{
			if (haveEnds)
			{
				const double end = *timing.EndOffsetNanoseconds * 1.0e-6;
				const double ms = std::max(end - previousEnd, 0.0);
				previousEnd = std::max(previousEnd, end);
				passes.push_back({ timing.Name, ms });
				total = previousEnd;
			}
			else if (timing.Nanoseconds)
			{
				const double ms = *timing.Nanoseconds * 1.0e-6;
				total += ms;
				passes.push_back({ timing.Name, ms });
			}
		}

		stats.GpuPasses = passes;
		stats.GpuTimingsAvailable = !passes.empty();
		stats.GpuMilliseconds = total;
		stats.Passes = static_cast<std::uint32_t>(timings.size());
		std::sort(passes.begin(), passes.end(),
			[](const auto& a, const auto& b)
			{
				return a.Milliseconds > b.Milliseconds;
			});
		stats.TopPassCount = static_cast<std::uint32_t>(std::min(passes.size(), stats.TopPasses.size()));

		for (std::uint32_t i = 0; i < stats.TopPassCount; ++i)
		{
			stats.TopPasses[i] = passes[i];
		}
	}

	bool FrameRenderer::WriteCapture(const std::filesystem::path& path) const
	{
		if (capture.empty() || !captureWidth || !captureHeight)
		{
			return false;
		}

		if (path.has_parent_path())
		{
			std::error_code ignored;
			std::filesystem::create_directories(path.parent_path(), ignored);
		}

		std::ofstream file(path, std::ios::binary | std::ios::trunc);

		if (!file)
		{
			return false;
		}

		file << "P6\n" << captureWidth << ' ' << captureHeight << "\n255\n";
		std::vector<char> row(std::size_t(captureWidth) * 3);

		for (std::uint32_t y = 0; y < captureHeight; ++y)
		{
			for (std::uint32_t x = 0; x < captureWidth; ++x)
			{
				const std::size_t source = (std::size_t(y) * captureWidth + x) * 4;
				row[x * 3] = static_cast<char>(capture[source]);
				row[x * 3 + 1] = static_cast<char>(capture[source + 1]);
				row[x * 3 + 2] = static_cast<char>(capture[source + 2]);
			}

			file.write(row.data(), static_cast<std::streamsize>(row.size()));
		}

		return static_cast<bool>(file);
	}

	void FrameRenderer::Impl::AbortFrame(const FramePending& pending)
	{
		if (pending.Residency)
		{
			residency->AbortUploads();
		}

		if (pending.Materials)
		{
			materialTable->AbortUploads();
		}

		if (pending.Scene)
		{
			scene->AbortUploads();
		}

		if (pending.Lights)
		{
			lights->AbortUploads();
		}

		if (pending.Particles)
		{
			particles->AbortFrame();
		}

		if (pending.Skinning)
		{
			skinning->AbortFrame();
		}

		if (pending.Atlas)
		{
			atlasTextures->AbortFrame();
		}

		if (pending.Viewports)
		{
			viewports->AbortFrame();
		}
	}

	void FrameRenderer::Impl::CommitFrame(const FramePending& pending, const S::TimelinePoint& completion)
	{
		residency->CommitUploads(completion);
		materialTable->CommitUploads();
		scene->CommitUploads();
		lights->CommitUploads();

		if (pending.Particles)
		{
			particles->CommitFrame();
		}

		if (pending.Skinning)
		{
			skinning->CommitFrame();
		}

		if (pending.Atlas)
		{
			atlasTextures->CommitFrame();
		}

		if (pending.Viewports)
		{
			viewports->CommitFrame();
		}

		viewports->RetireAfter(completion);
	}

	void FrameRenderer::Impl::FillFrameStats(RenderStats& stats, std::uint32_t width, std::uint32_t height)
	{
		const auto sceneStats = scene->GetStats();
		const auto lightStats = lights->GetStats();
		stats.Frame = frameIndex;
		stats.Width = width;
		stats.Height = height;
		stats.RenderObjects = sceneStats.LiveObjects;
		stats.Materials = materials->GetCount();
		stats.DirectionalLights = lightStats.DirectionalLights;
		stats.LocalLights = lightStats.LocalLights;
		stats.SkinnedInstances = skinning->GetStats().Instances;
		if (frameIndex % 32 == 0)
		{
			residentMeshes = meshes->GetResidentMeshCount();
			residentTextures = meshes->GetResidentTextureCount();
		}

		stats.ResidentMeshes = residentMeshes;
		stats.ResidentTextures = residentTextures;
		stats.PendingAssets =
			(meshes->GetRequestedMeshCount() - stats.ResidentMeshes) + (meshes->GetRequestedTextureCount() - stats.ResidentTextures);
	}

} // namespace Engine
