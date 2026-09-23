#include "Tests/Fixtures/GpuSceneFixture.h"

#include <cstring>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	RenderObjectDesc MakeObject(std::uint32_t id, float x = 0.0f)
	{
		RenderObjectDesc desc;
		desc.Transform = RenderAffine::Translation(x, 0.0f, 0.0f);
		desc.Mesh = { 3, 7 };
		desc.LocalBounds = { { 0.0f, 1.0f, 0.0f }, { 1.0f, 2.0f, 3.0f } };
		desc.MaterialSet = 40 + id;
		desc.ObjectId = id;
		return desc;
	}

	bool PreviousEqualsCurrent(const GpuTransformRecord& record)
	{
		return std::memcmp(record.Previous, record.Current, sizeof(record.Current)) == 0;
	}

	float TranslationX(const float* rows)
	{
		return rows[3];
	}
} // namespace

SWIM_TEST("Render.GpuScene", "CreatedObjectsGetStableRowsAndLandOnTheGpuThroughOneImport")
{
	Testing::GpuSceneFixture fixture;
	auto& scene = *fixture.scene;
	const auto a = scene.Create(MakeObject(1, 1.0f));
	const auto b = scene.Create(MakeObject(2, 2.0f));
	RenderObjectDesc bare;
	bare.Flags = RenderObjectFlags::Visible | RenderObjectFlags::Static;
	const auto c = scene.Create(bare); // No mesh yet.
	SWIM_CHECK_EQUAL(a.Index, 0u);
	SWIM_CHECK_EQUAL(c.Index, 2u);

	const auto* instance = scene.GetInstance(b);
	SWIM_REQUIRE(instance != nullptr);
	SWIM_CHECK_EQUAL(instance->MeshIndex, 3u);
	SWIM_CHECK_EQUAL(instance->MeshGeneration, 7u);
	SWIM_CHECK_EQUAL(instance->TransformIndex, b.Index);
	SWIM_CHECK_EQUAL(instance->MaterialSet, 42u);
	SWIM_CHECK_EQUAL(instance->ObjectId, 2u);
	SWIM_CHECK_EQUAL(instance->Generation, b.Generation);
	SWIM_CHECK_EQUAL(instance->LocalExtents[2], 3.0f);
	SWIM_CHECK(HasAny(static_cast<RenderObjectFlags>(instance->Flags), RenderObjectFlags::Live));
	SWIM_CHECK(HasAny(static_cast<RenderObjectFlags>(instance->Flags), RenderObjectFlags::HasMesh));
	SWIM_CHECK(!HasAny(static_cast<RenderObjectFlags>(scene.GetInstance(c)->Flags), RenderObjectFlags::HasMesh));
	SWIM_CHECK_EQUAL(scene.GetInstance(c)->MeshIndex, GpuInstanceRecord::InvalidIndex);
	SWIM_CHECK(PreviousEqualsCurrent(*scene.GetTransform(a))); // A new object has no motion.

	const auto stats = scene.GetStats();
	SWIM_CHECK_EQUAL(stats.DirtyInstanceRows, 3u);
	SWIM_CHECK_EQUAL(stats.DirtyTransformRows, 3u);

	const auto uploaded = fixture.Upload();
	SWIM_CHECK_EQUAL(uploaded.UploadPasses.size(), 2u);
	SWIM_CHECK_EQUAL(uploaded.RowCount, 3u);
	SWIM_CHECK_EQUAL(uploaded.InstanceRows, 3u);
	SWIM_CHECK_EQUAL(uploaded.UploadRuns, 2u); // Rows 0..2 are one run per buffer.
	SWIM_CHECK_EQUAL(uploaded.UploadBytes, 3u * 64u + 3u * 96u);
	SWIM_CHECK_EQUAL(fixture.CopyCount(), 2u);
	SWIM_CHECK(fixture.GpuMatchesMirror());
	SWIM_CHECK_EQUAL(scene.GetStats().DirtyInstanceRows, 0u);

	// Nothing changed: no pass, no bytes.
	const auto idle = fixture.Upload();
	SWIM_CHECK(idle.UploadPasses.empty());
	SWIM_CHECK_EQUAL(idle.UploadBytes, 0u);
	SWIM_CHECK_EQUAL(scene.GetStats().TotalUploadBytes, 3u * 64u + 3u * 96u);
}

SWIM_TEST("Render.GpuScene", "OnlyTouchedRowsUploadAndTransformChangesNeverTouchInstances")
{
	Testing::GpuSceneFixture fixture;
	auto& scene = *fixture.scene;
	std::vector<RenderObjectHandle> objects;
	for (std::uint32_t i = 0; i < 10; ++i)
	{
		objects.push_back(scene.Create(MakeObject(i, float(i))));
	}
	fixture.Upload();

	// Rows 2, 3 and 7 move: two runs, transforms only.
	scene.SetTransform(objects[2], RenderAffine::Translation(20, 0, 0));
	scene.SetTransform(objects[3], RenderAffine::Translation(30, 0, 0));
	scene.SetTransform(objects[7], RenderAffine::Translation(70, 0, 0));
	scene.SetTransform(objects[5], RenderAffine::Translation(5, 0, 0)); // Unchanged value: no upload.
	auto uploaded = fixture.Upload();
	SWIM_CHECK_EQUAL(uploaded.InstanceRows, 0u);
	SWIM_CHECK_EQUAL(uploaded.TransformRows, 3u);
	SWIM_CHECK_EQUAL(uploaded.UploadRuns, 2u);
	SWIM_CHECK_EQUAL(uploaded.UploadBytes, 3u * 96u);
	SWIM_CHECK_EQUAL(uploaded.UploadPasses.size(), 1u);
	SWIM_CHECK(fixture.GpuMatchesMirror());

	// Instance-only edits: material, flags, skin, LOD bias, bounds, object id.
	scene.SetMaterialSet(objects[1], 99);
	scene.SetFlags(objects[4], RenderObjectFlags::CastShadows); // Hidden, still casting.
	scene.SetSkin(objects[4], 5);
	scene.SetLodBias(objects[9], 1.5f);
	scene.SetBounds(objects[9], RenderBounds::FromMinMax({ -1, -1, -1 }, { 1, 1, 1 }));
	scene.SetObjectId(objects[0], 1234);
	scene.SetMaterialSet(objects[6], scene.GetInstance(objects[6])->MaterialSet); // Same value: not dirty.
	uploaded = fixture.Upload();
	SWIM_CHECK_EQUAL(uploaded.InstanceRows, 4u);  // Rows 0, 1, 4 and 9.
	SWIM_CHECK_EQUAL(uploaded.TransformRows, 3u); // Rows 2, 3 and 7 settle (they stopped moving).
	SWIM_CHECK(fixture.GpuMatchesMirror());
	const auto flags = static_cast<RenderObjectFlags>(scene.GetInstance(objects[4])->Flags);
	SWIM_CHECK(!HasAny(flags, RenderObjectFlags::Visible));
	SWIM_CHECK(HasAny(flags, RenderObjectFlags::CastShadows | RenderObjectFlags::Live | RenderObjectFlags::HasMesh));
	// Producers cannot clear GpuScene-owned bits.
	scene.SetFlags(objects[4], RenderObjectFlags::None);
	SWIM_CHECK(HasAny(static_cast<RenderObjectFlags>(scene.GetInstance(objects[4])->Flags), RenderObjectFlags::Live));
	scene.SetMesh(objects[8], {}, RenderBounds::Infinite());
	SWIM_CHECK(!HasAny(static_cast<RenderObjectFlags>(scene.GetInstance(objects[8])->Flags), RenderObjectFlags::HasMesh));
	SWIM_CHECK_EQUAL(scene.GetInstance(objects[8])->MeshIndex, GpuInstanceRecord::InvalidIndex);
}

SWIM_TEST("Render.GpuScene", "PreviousTransformFollowsTheFrameBeforeAndSettlesOnce")
{
	Testing::GpuSceneFixture fixture;
	auto& scene = *fixture.scene;
	const auto object = scene.Create(MakeObject(1, 0.0f));
	fixture.Upload(); // Frame 1.

	// Frame 2: two moves in one frame keep Previous at the frame-1 value.
	scene.SetTransform(object, RenderAffine::Translation(1, 0, 0));
	scene.SetTransform(object, RenderAffine::Translation(2, 0, 0));
	auto* transform = scene.GetTransform(object);
	SWIM_CHECK_EQUAL(TranslationX(transform->Previous), 0.0f);
	SWIM_CHECK_EQUAL(TranslationX(transform->Current), 2.0f);
	fixture.Upload();
	SWIM_CHECK_EQUAL(scene.GetStats().SettlingTransforms, 1u);

	// Frame 3 moves again: Previous is frame 2's Current.
	scene.SetTransform(object, RenderAffine::Translation(5, 0, 0));
	SWIM_CHECK_EQUAL(TranslationX(transform->Previous), 2.0f);
	fixture.Upload();

	// Frame 4 does not move: the import settles Previous = Current once (motion stops).
	auto uploaded = fixture.Upload();
	SWIM_CHECK_EQUAL(uploaded.TransformRows, 1u);
	SWIM_CHECK(PreviousEqualsCurrent(*scene.GetTransform(object)));
	SWIM_CHECK(PreviousEqualsCurrent(fixture.GpuTransform(object.Index)));
	// Frame 5: fully static, nothing uploads.
	uploaded = fixture.Upload();
	SWIM_CHECK_EQUAL(uploaded.TransformRows, 0u);
	SWIM_CHECK_EQUAL(scene.GetStats().SettlingTransforms, 0u);
}

SWIM_TEST("Render.GpuScene", "DestroyedRowsGoDeadAtOnceAndAreReusedOnlyAfterTheirTimelinePoint")
{
	Testing::GpuSceneFixture fixture(4);
	auto& scene = *fixture.scene;
	Testing::MockTimeline timeline;
	std::vector<RenderObjectHandle> objects;
	for (std::uint32_t i = 0; i < 4; ++i)
	{
		objects.push_back(scene.Create(MakeObject(i)));
	}
	SWIM_CHECK(!scene.TryCreate(MakeObject(9)));
	SWIM_CHECK_THROWS(scene.Create(MakeObject(9)), std::length_error);
	fixture.Upload();

	scene.SetTransform(objects[1], RenderAffine::Translation(1, 2, 3)); // Moved, then destroyed: no stale settle.
	SWIM_CHECK(scene.Destroy(objects[1], { &timeline, 6 }));
	SWIM_CHECK(!scene.Destroy(objects[1]));
	SWIM_CHECK(!scene.IsValid(objects[1]));
	SWIM_CHECK(!scene.SetTransform(objects[1], {}));
	SWIM_CHECK(!scene.SetMaterialSet(objects[1], 1));
	SWIM_CHECK(scene.GetInstance(objects[1]) == nullptr);
	auto uploaded = fixture.Upload();
	SWIM_CHECK_EQUAL(uploaded.InstanceRows, 1u);
	const auto dead = fixture.GpuInstance(1);
	SWIM_CHECK_EQUAL(dead.Flags, 0u);
	SWIM_CHECK_EQUAL(dead.Generation, 0u);
	SWIM_CHECK_EQUAL(scene.GetStats().RetiringObjects, 1u);

	// The row may still be read by in-flight work: not reusable before value 6.
	SWIM_CHECK_EQUAL(scene.Collect(), 0u);
	SWIM_CHECK(!scene.TryCreate(MakeObject(9)));
	timeline.Complete(6);
	SWIM_CHECK_EQUAL(scene.Collect(), 1u);
	const auto reused = scene.Create(MakeObject(9, 9.0f));
	SWIM_CHECK_EQUAL(reused.Index, 1u);
	SWIM_CHECK(reused.Generation != objects[1].Generation);
	SWIM_CHECK(PreviousEqualsCurrent(*scene.GetTransform(reused)));
	fixture.Upload();
	SWIM_CHECK_EQUAL(fixture.GpuInstance(1).ObjectId, 9u);
	SWIM_CHECK_EQUAL(fixture.GpuInstance(1).Generation, reused.Generation);
	SWIM_CHECK(fixture.GpuMatchesMirror());

	Testing::MockTimeline late;
	scene.Destroy(reused, { &late, 2 });
	SWIM_CHECK_EQUAL(scene.Drain(), 1u);
	SWIM_CHECK_EQUAL(late.WaitCount, 1u);
	SWIM_CHECK_EQUAL(scene.GetStats().RowCount, 4u); // The high-water mark stays.
}

SWIM_TEST("Render.GpuScene", "AbortedImportsUploadAgainAndPendingImportsAreExclusive")
{
	Testing::GpuSceneFixture fixture;
	auto& scene = *fixture.scene;
	const auto object = scene.Create(MakeObject(1));
	{
		RenderGraph graph;
		const auto resources = scene.Import(graph);
		SWIM_CHECK_EQUAL(resources.UploadPasses.size(), 2u);
		RenderGraph second;
		SWIM_CHECK_THROWS(scene.Import(second), std::logic_error);
		scene.AbortUploads(); // The graph never executed.
	}
	SWIM_CHECK_EQUAL(scene.GetStats().DirtyInstanceRows, 1u);
	SWIM_CHECK_EQUAL(scene.GetStats().DirtyTransformRows, 1u);
	scene.SetMaterialSet(object, 77);
	const auto uploaded = fixture.Upload();
	SWIM_CHECK_EQUAL(uploaded.InstanceRows, 1u);
	SWIM_CHECK_EQUAL(fixture.GpuInstance(object.Index).MaterialSet, 77u);
	SWIM_CHECK(fixture.GpuMatchesMirror());
}

SWIM_TEST("Render.GpuScene", "RecordRunsAndValueTypes")
{
	const std::vector<std::uint32_t> rows{ 0, 1, 2, 5, 7, 8 };
	const auto runs = BuildRecordRuns(rows);
	SWIM_REQUIRE_EQUAL(runs.size(), 3u);
	SWIM_CHECK_EQUAL(runs[0].RowCount, 3u);
	SWIM_CHECK_EQUAL(runs[1].FirstRow, 5u);
	SWIM_CHECK_EQUAL(runs[2].RowCount, 2u);
	SWIM_CHECK(BuildRecordRuns({}).empty());

	// Column-major 4x4 (glm order) to row-major 3x4.
	const float columns[16] = { 1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 3, 0, 4, 5, 6, 1 };
	const auto affine = RenderAffine::FromColumnMajor(columns);
	SWIM_CHECK_EQUAL(affine.Rows[0], 1.0f);
	SWIM_CHECK_EQUAL(affine.Rows[3], 4.0f);
	SWIM_CHECK_EQUAL(affine.Rows[7], 5.0f);
	SWIM_CHECK_EQUAL(affine.Rows[10], 3.0f);
	SWIM_CHECK_EQUAL(affine.Rows[11], 6.0f);
	SWIM_CHECK_EQUAL(affine.MaxScale(), 3.0f);
	const auto point = affine.TransformPoint({ 1, 1, 1 });
	SWIM_CHECK_EQUAL(point[0], 5.0f);
	SWIM_CHECK_EQUAL(point[2], 9.0f);

	const auto bounds = RenderBounds::FromMinMax({ -2, 0, 2 }, { 2, 4, 2 });
	SWIM_CHECK_EQUAL(bounds.Center[1], 2.0f);
	SWIM_CHECK_EQUAL(bounds.Extents[0], 2.0f);
	SWIM_CHECK_EQUAL(bounds.Extents[2], 0.0f);
	SWIM_CHECK(RenderBounds::FromMinMax({ 1, 0, 0 }, { 0, 0, 0 }) == RenderBounds::Infinite());
	SWIM_CHECK_THROWS(GpuScene(*std::make_unique<Testing::MockDevice>(), { 0, "Empty" }), std::invalid_argument);
}
