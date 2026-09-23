#include "Engine/Systems/Renderer/Lights/GpuLightBuffer.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Tests/Fixtures/GpuSceneFixture.h"

#include <cstring>
#include <random>
#include <set>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	struct LightWorld
	{
		explicit LightWorld(std::uint32_t directional = 2, std::uint32_t local = 8)
			: lights(fixture.device, { directional, local, "Test lights" })
		{
		}

		// Imports, executes and commits.
		GpuLightGraphResources Frame()
		{
			RenderGraph graph;
			const auto resources = lights.Import(graph);
			graph.AddPass(
				"Reader", Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(resources.Lights, Rhi::ResourceState::ShaderRead);
					b.Read(resources.Header, Rhi::ResourceState::ShaderRead);
				},
				[](RenderCommandContext&)
				{
				});
			fixture.executor->Execute(graph.Compile());
			fixture.executor->Wait();
			lights.CommitUploads();
			return resources;
		}

		GpuLightRecord GpuRow(std::uint32_t row) const
		{
			GpuLightRecord record;
			std::memcpy(
				&record, Testing::GpuSceneFixture::Bytes(lights.GetBuffer()).data() + std::size_t(row) * sizeof(record), sizeof(record));
			return record;
		}

		GpuLightHeader GpuHeader() const
		{
			GpuLightHeader header;
			std::memcpy(&header, Testing::GpuSceneFixture::Bytes(lights.GetHeaderBuffer()).data(), sizeof(header));
			return header;
		}

		// Every live row on the GPU equals the mirror, and the header matches.
		void CheckGpuMatchesMirror() const
		{
			const auto header = GpuHeader();
			SWIM_CHECK_EQUAL(header.DirectionalCount, lights.GetHeader().DirectionalCount);
			SWIM_CHECK_EQUAL(header.LocalCount, lights.GetHeader().LocalCount);
			SWIM_CHECK_EQUAL(header.FirstLocalRow, lights.GetHeader().FirstLocalRow);
			const auto rows = lights.GetRecords();
			const auto same = [&](std::uint32_t row)
			{
				const auto gpu = GpuRow(row);
				SWIM_CHECK(std::memcmp(&gpu, &rows[row], sizeof(gpu)) == 0);
			};
			for (std::uint32_t i = 0; i < header.DirectionalCount; ++i)
			{
				same(i);
			}
			for (std::uint32_t i = 0; i < header.LocalCount; ++i)
			{
				same(header.FirstLocalRow + i);
			}
		}

		Testing::GpuSceneFixture fixture;
		GpuLightBuffer lights;
	};

	LightDesc Point(float x, float intensity = 1.0f)
	{
		LightDesc desc;
		desc.Position = { x, 0, 0 };
		desc.Intensity = intensity;
		return desc;
	}

	LightDesc Sun(float intensity = 1.0f)
	{
		LightDesc desc;
		desc.Type = LightType::Directional;
		desc.Intensity = intensity;
		return desc;
	}
} // namespace

SWIM_TEST("Render.GpuLights", "LightsPackDenselyByTypeAndUploadOnlyChangedRows")
{
	LightWorld world;
	SWIM_CHECK_THROWS(GpuLightBuffer(world.fixture.device, { 0, 8, "Bad" }), std::invalid_argument);
	SWIM_CHECK_THROWS(GpuLightBuffer(world.fixture.device, { 2, 0, "Bad" }), std::invalid_argument);

	// The first frame uploads the header even without lights.
	auto frame = world.Frame();
	SWIM_CHECK(!frame.UploadPass && frame.HeaderUploadPass);
	SWIM_CHECK_EQUAL(frame.RowCount, 10u);
	SWIM_CHECK_EQUAL(frame.FirstLocalRow, 2u);
	SWIM_CHECK((world.GpuHeader().LocalCapacity) == 8u);
	frame = world.Frame();
	SWIM_CHECK(!frame.UploadPass && !frame.HeaderUploadPass); // Nothing changed.

	const auto a = world.lights.Create(Point(1));
	const auto b = world.lights.Create(Point(2));
	const auto sun = world.lights.Create(Sun());
	const auto c = world.lights.Create(Point(3));
	SWIM_CHECK_EQUAL(world.lights.GetRow(sun).value_or(UINT32_MAX), 0u);
	SWIM_CHECK_EQUAL(world.lights.GetRow(a).value_or(UINT32_MAX), 2u);
	SWIM_CHECK_EQUAL(world.lights.GetRow(c).value_or(UINT32_MAX), 4u);
	frame = world.Frame();
	SWIM_CHECK(frame.UploadPass && frame.HeaderUploadPass);
	SWIM_CHECK_EQUAL(frame.DirectionalCount, 1u);
	SWIM_CHECK_EQUAL(frame.LocalCount, 3u);
	auto stats = world.lights.GetStats();
	SWIM_CHECK_EQUAL(stats.LastUploadRows, 4u);
	SWIM_CHECK_EQUAL(stats.LastUploadRuns, 2u); // Row 0 and rows 2-4.
	SWIM_CHECK_EQUAL(stats.LastUploadBytes, 4u * 64u + 32u);
	SWIM_CHECK(stats.LastUploadHeader);
	world.CheckGpuMatchesMirror();
	SWIM_CHECK((world.GpuRow(3).Position[0]) == 2.0f);

	// An edit uploads one row and no header.
	auto edited = Point(2, 5.0f);
	SWIM_CHECK(world.lights.Update(b, edited));
	world.Frame();
	stats = world.lights.GetStats();
	SWIM_CHECK_EQUAL(stats.LastUploadRows, 1u);
	SWIM_CHECK(!stats.LastUploadHeader);
	SWIM_CHECK((world.GpuRow(3).Color[0]) == 5.0f);
	SWIM_CHECK_EQUAL(world.lights.Find(b)->Intensity, 5.0f);

	// Releasing the first local light swaps the last one into its row.
	SWIM_CHECK(world.lights.Release(a));
	SWIM_CHECK(!world.lights.IsValid(a));
	SWIM_CHECK(!world.lights.Release(a));
	SWIM_CHECK(!world.lights.Update(a, Point(9)));
	SWIM_CHECK(world.lights.Find(a) == nullptr);
	SWIM_CHECK(!world.lights.GetRow(a));
	SWIM_CHECK_EQUAL(world.lights.GetRow(c).value_or(UINT32_MAX), 2u);
	world.Frame();
	stats = world.lights.GetStats();
	SWIM_CHECK_EQUAL(stats.LastUploadRows, 1u);
	SWIM_CHECK(stats.LastUploadHeader);
	SWIM_CHECK((world.GpuHeader().LocalCount) == 2u);
	SWIM_CHECK((world.GpuRow(2).Position[0]) == 3.0f);
	world.CheckGpuMatchesMirror();

	// A reused slot gets a new generation; the stale handle stays invalid.
	const auto d = world.lights.Create(Point(4));
	SWIM_CHECK_EQUAL(d.Index, a.Index);
	SWIM_CHECK(d.Generation != a.Generation);
	SWIM_CHECK(!world.lights.IsValid(a));
	SWIM_CHECK(world.lights.IsValid(d));
	world.Frame();
	world.CheckGpuMatchesMirror();
}

SWIM_TEST("Render.GpuLights", "TypeChangesMoveBetweenRangesAndFullRangesAreReported")
{
	LightWorld world(1, 2);
	const auto sun = world.lights.Create(Sun());
	SWIM_CHECK(!world.lights.TryCreate(Sun()));
	SWIM_CHECK_THROWS(world.lights.Create(Sun()), std::length_error);
	const auto p = world.lights.Create(Point(1));
	const auto q = world.lights.Create(Point(2));
	SWIM_CHECK(!world.lights.TryCreate(Point(3)));
	SWIM_CHECK_THROWS(world.lights.Update(p, Sun()), std::length_error); // Directional range full.
	SWIM_CHECK_EQUAL(world.lights.Find(p)->Type, LightType::Point);		 // Unchanged.
	SWIM_CHECK_THROWS(world.lights.Create(LightDesc{ LightType::Point, {}, {}, {}, 1.0f, -1.0f }), std::invalid_argument);
	SWIM_CHECK_THROWS(world.lights.Update(p, LightDesc{ LightType::Point, {}, {}, {}, 1.0f, -1.0f }), std::invalid_argument);

	// Sun -> spot: it leaves the directional range (now empty) and needs a local row.
	SWIM_CHECK(world.lights.Release(q));
	LightDesc spot;
	spot.Type = LightType::Spot;
	spot.Position = { 0, 5, 0 };
	SWIM_CHECK(world.lights.Update(sun, spot));
	SWIM_CHECK_EQUAL(world.lights.GetHeader().DirectionalCount, 0u);
	SWIM_CHECK_EQUAL(world.lights.GetHeader().LocalCount, 2u);
	SWIM_CHECK_EQUAL(world.lights.GetRow(sun).value_or(UINT32_MAX), 2u);
	SWIM_CHECK_EQUAL(world.lights.GetRecords()[2].Type, 2u);
	world.Frame();
	world.CheckGpuMatchesMirror();
	// And back.
	SWIM_CHECK(world.lights.Update(sun, Sun(3.0f)));
	SWIM_CHECK_EQUAL(world.lights.GetRow(sun).value_or(UINT32_MAX), 0u);
	world.Frame();
	world.CheckGpuMatchesMirror();
	SWIM_CHECK((world.GpuRow(0).Color[0]) == 3.0f);
}

SWIM_TEST("Render.GpuLights", "AbortedUploadsRetryAndImportsMustBeCommitted")
{
	LightWorld world;
	world.lights.Create(Point(1));
	RenderGraph graph;
	world.lights.Import(graph);
	RenderGraph second;
	SWIM_CHECK_THROWS(world.lights.Import(second), std::logic_error);
	world.lights.AbortUploads();
	SWIM_CHECK((world.lights.GetStats().DirtyRows) == 1u);
	const auto frame = world.Frame();
	SWIM_CHECK(frame.UploadPass && frame.HeaderUploadPass); // The header retries too.
	world.CheckGpuMatchesMirror();
}

SWIM_TEST("Render.GpuLights", "RandomChurnKeepsRangesDenseAndTheGpuCopyExact")
{
	LightWorld world(4, 64);
	std::mt19937 random(163);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	std::vector<GpuLightHandle> live;
	for (int step = 0; step < 40; ++step)
	{
		for (int op = 0; op < 12; ++op)
		{
			const float choice = unit(random);
			if (choice < 0.45f || live.empty())
			{
				LightDesc desc = unit(random) < 0.1f ? Sun(unit(random)) : Point(unit(random) * 10, unit(random));
				if (desc.Type == LightType::Point && unit(random) < 0.5f)
				{
					desc.Type = LightType::Spot;
				}
				if (auto handle = world.lights.TryCreate(desc))
				{
					live.push_back(*handle);
				}
			}
			else if (choice < 0.75f)
			{
				const auto index = std::size_t(unit(random) * float(live.size())) % live.size();
				SWIM_CHECK(world.lights.Release(live[index]));
				live.erase(live.begin() + std::ptrdiff_t(index));
			}
			else
			{
				const auto index = std::size_t(unit(random) * float(live.size())) % live.size();
				try
				{
					SWIM_CHECK(world.lights.Update(live[index], unit(random) < 0.1f ? Sun(2.0f) : Point(unit(random) * 10, 3.0f)));
				}
				catch (const std::length_error&)
				{
				}
			}
		}
		// Rows are dense, distinct and each owned by exactly one live light.
		const auto& header = world.lights.GetHeader();
		SWIM_CHECK_EQUAL(header.DirectionalCount + header.LocalCount, std::uint32_t(live.size()));
		std::set<std::uint32_t> rows;
		for (const auto handle : live)
		{
			const auto row = world.lights.GetRow(handle);
			SWIM_REQUIRE(row.has_value());
			const bool directional = world.lights.Find(handle)->Type == LightType::Directional;
			SWIM_CHECK(directional ? *row < header.DirectionalCount
								   : (*row >= header.FirstLocalRow && *row < header.FirstLocalRow + header.LocalCount));
			const auto expected = Lights::EncodeLight(*world.lights.Find(handle));
			SWIM_CHECK(std::memcmp(&world.lights.GetRecords()[*row], &expected, sizeof(GpuLightRecord)) == 0);
			rows.insert(*row);
		}
		SWIM_CHECK_EQUAL(rows.size(), live.size());
		world.Frame();
		world.CheckGpuMatchesMirror();
	}
}
