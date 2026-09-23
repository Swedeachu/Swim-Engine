#include "Tests/Fixtures/GpuSceneFixture.h"

#include <chrono>
#include <cstdio>

using namespace Swim;
using namespace Swim::Render;

// Critical-path item 48 (CPU side): 100k persistent objects, then frames where a
// small fraction changes. Upload volume must follow the changed rows only.
SWIM_TEST("Render.GpuSceneStress", "HundredThousandObjectsUploadOnlyDirtyRows")
{
	constexpr std::uint32_t count = 100000;
	Testing::GpuSceneFixture fixture(count);
	auto& scene = *fixture.scene;
	std::vector<RenderObjectHandle> objects;
	objects.reserve(count);
	const auto createStart = std::chrono::steady_clock::now();
	for (std::uint32_t i = 0; i < count; ++i)
	{
		RenderObjectDesc desc;
		desc.Transform = RenderAffine::Translation(float(i % 1000), 0.0f, float(i / 1000));
		desc.Mesh = { i % 64, 1 };
		desc.LocalBounds = RenderBounds::FromMinMax({ -1, -1, -1 }, { 1, 1, 1 });
		desc.MaterialSet = i % 16;
		desc.ObjectId = i;
		objects.push_back(scene.Create(desc));
	}
	const auto initial = fixture.Upload();
	const auto createEnd = std::chrono::steady_clock::now();
	SWIM_CHECK_EQUAL(initial.RowCount, count);
	SWIM_CHECK_EQUAL(initial.UploadRuns, 2u); // Every row, one run per buffer.
	SWIM_CHECK_EQUAL(initial.UploadBytes, std::uint64_t(count) * (64 + 96));
	SWIM_CHECK(fixture.GpuMatchesMirror());

	// Frames with 1% of objects moving (strided, so no two are adjacent) and a few
	// material changes: bytes and runs scale with the changes, not the scene.
	std::chrono::steady_clock::duration steady{};
	for (std::uint32_t frame = 1; frame <= 3; ++frame)
	{
		for (std::uint32_t i = frame; i < count; i += 100)
		{
			scene.SetTransform(objects[i], RenderAffine::Translation(float(i), float(frame), 0.0f));
		}
		for (std::uint32_t i = 0; i < 10; ++i)
		{
			scene.SetMaterialSet(objects[i * 7919], 1000 + frame);
		}
		const auto start = std::chrono::steady_clock::now();
		const auto uploaded = fixture.Upload();
		steady += std::chrono::steady_clock::now() - start;
		// 1000 moved this frame plus (from frame 2) the 1000 that moved last frame settling.
		const std::uint32_t transformRows = frame == 1 ? 1000 : 2000;
		SWIM_CHECK_EQUAL(uploaded.TransformRows, transformRows);
		SWIM_CHECK_EQUAL(uploaded.InstanceRows, 10u);
		SWIM_CHECK_EQUAL(uploaded.UploadBytes, std::uint64_t(transformRows) * 96 + 10 * 64);
		SWIM_CHECK(uploaded.UploadBytes * 50 < initial.UploadBytes);
	}
	SWIM_CHECK(fixture.GpuMatchesMirror());

	// A static frame after everything settles costs nothing.
	fixture.Upload();
	const auto idle = fixture.Upload();
	SWIM_CHECK(idle.UploadPasses.empty());
	SWIM_CHECK_EQUAL(scene.GetStats().LiveObjects, count);
	std::printf("             [GpuScene stress] 100k create+upload %.1f ms, dirty frame avg %.2f ms (mock device)\n",
		std::chrono::duration<double, std::milli>(createEnd - createStart).count(),
		std::chrono::duration<double, std::milli>(steady).count() / 3.0);
}
