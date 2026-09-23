#include "Engine/Assets/AssetSystem.h"
#include "Engine/Systems/Scene/RenderExtraction/RenderExtractor.h"
#include "Tests/Fixtures/GpuSceneFixture.h"

#include <chrono>
#include <cstdio>
#include <map>

using namespace Swim;
using Swim::Render::RenderAffine;
using Swim::Render::RenderObjectFlags;

namespace
{
	// Stand-in for Engine::Transform: the test writes world poses directly and
	// queues the entity in the TransformSystem exactly like Transform::MarkDirty.
	struct TestPose
	{
		RenderAffine World;
		std::uint64_t LastQueuedEpoch = 0;
		std::uint32_t Id = 0; // Durable test id reported as ObjectId.
	};

	struct ExtractionWorld
	{
		explicit ExtractionWorld(std::uint32_t maxObjects = 64) : scene(maxObjects)
		{
			SWIM_REQUIRE(assets.Initialize());
			Engine::RenderExtractorDesc desc;
			desc.Scene = 7;
			desc.ResolveMesh = [this](Assets::AssetHandle<Assets::MeshAsset> mesh) -> std::optional<Render::ResolvedRenderMesh>
			{
				++resolveCalls;
				const auto found = resident.find(mesh.GetId());
				if (found == resident.end())
				{
					return std::nullopt;
				}
				return found->second;
			};
			desc.WorldTransform = [](const entt::registry& registry, entt::entity entity)
			{
				const auto* pose = registry.try_get<TestPose>(entity);
				return pose ? pose->World : RenderAffine{};
			};
			desc.ObjectId = [](const entt::registry& registry, entt::entity entity)
			{
				const auto* pose = registry.try_get<TestPose>(entity);
				return pose ? pose->Id : 0u;
			};
			extractor = std::make_unique<Engine::RenderExtractor>(registry, transforms, *scene.scene, std::move(desc));
		}

		~ExtractionWorld()
		{
			extractor.reset(); // Before the registry.
			assets.Shutdown();
		}

		entt::entity Spawn(float x, std::vector<Engine::MeshRendererPart> parts)
		{
			const auto entity = registry.create();
			registry.emplace<TestPose>(entity, TestPose{ RenderAffine::Translation(x, 0, 0), 0, ++spawned });
			registry.emplace<Engine::MeshRenderer>(entity, Engine::MeshRenderer{ std::move(parts) });
			return entity;
		}

		void Move(entt::entity entity, float x)
		{
			auto& pose = registry.get<TestPose>(entity);
			pose.World = RenderAffine::Translation(x, 0, 0);
			transforms.QueueDirty(entity, pose.LastQueuedEpoch);
		}

		// One engine frame: extract, upload, begin the next transform epoch.
		Engine::RenderExtractionStats Frame(Rhi::TimelinePoint lastUse = {})
		{
			const auto stats = extractor->Extract(lastUse);
			lastUpload = scene.Upload();
			transforms.BeginFrame();
			return stats;
		}

		// A declared (identity-only) mesh asset per test id.
		Assets::AssetHandle<Assets::MeshAsset> Mesh(std::uint64_t id)
		{
			return assets.Declare<Assets::MeshAsset>("Models/Test" + std::to_string(id) + ".mesh");
		}

		void MakeResident(std::uint64_t id, Render::GpuMeshHandle mesh, float extent = 1.0f)
		{
			resident[Mesh(id).GetId()] = { mesh,
				Render::RenderBounds::FromMinMax({ -extent, -extent, -extent }, { extent, extent, extent }) };
		}

		const Render::GpuInstanceRecord* Instance(entt::entity entity, std::uint32_t part = 0) const
		{
			return scene.scene->GetInstance(extractor->Find(entity, part));
		}

		float WorldX(entt::entity entity, std::uint32_t part = 0) const
		{
			return scene.scene->GetTransform(extractor->Find(entity, part))->Current[3];
		}

		Assets::AssetSystem assets;
		Testing::GpuSceneFixture scene;
		entt::registry registry;
		Engine::TransformSystem transforms;
		std::unique_ptr<Engine::RenderExtractor> extractor;
		std::map<Assets::AssetId, Render::ResolvedRenderMesh> resident;
		Render::GpuSceneGraphResources lastUpload;
		std::uint32_t resolveCalls = 0;
		std::uint32_t spawned = 0;
	};
} // namespace

SWIM_TEST("Scene.RenderExtraction", "MeshRenderersBecomeStableRenderObjectsKeyedByEntityAndPart")
{
	ExtractionWorld world;
	world.MakeResident(10, { 4, 2 }, 2.0f);
	const auto before = world.Spawn(1.0f, { { world.Mesh(10), 3 } }); // Exists before the first Extract.
	const auto multi = world.Spawn(2.0f, { { world.Mesh(10), 5 }, { world.Mesh(11), 6 } });
	const auto stats = world.Frame();
	SWIM_CHECK_EQUAL(stats.EntitiesChanged, 2u);
	SWIM_CHECK_EQUAL(stats.ObjectsCreated, 3u);
	SWIM_CHECK_EQUAL(stats.MeshesResolved, 2u);
	SWIM_CHECK_EQUAL(stats.PendingMeshes, 1u); // Mesh 11 is not resident yet.
	SWIM_CHECK_EQUAL(stats.LiveObjects, 3u);
	SWIM_CHECK_EQUAL(world.extractor->GetScene(), 7u);

	const auto* instance = world.Instance(before);
	SWIM_REQUIRE(instance != nullptr);
	SWIM_CHECK_EQUAL(instance->MeshIndex, 4u);
	SWIM_CHECK_EQUAL(instance->MeshGeneration, 2u);
	SWIM_CHECK_EQUAL(instance->LocalExtents[0], 2.0f);
	SWIM_CHECK_EQUAL(instance->MaterialSet, 3u);
	SWIM_CHECK_EQUAL(instance->ObjectId, 1u);
	SWIM_CHECK_EQUAL(world.Instance(multi, 1)->ObjectId, 2u);
	SWIM_CHECK_EQUAL(world.WorldX(multi, 1), 2.0f);
	SWIM_CHECK_EQUAL(world.Instance(multi, 1)->MaterialSet, 6u);
	SWIM_CHECK(!HasAny(static_cast<RenderObjectFlags>(world.Instance(multi, 1)->Flags), RenderObjectFlags::HasMesh));
	SWIM_CHECK(!world.extractor->Find(multi, 2));
	SWIM_CHECK(world.scene.GpuMatchesMirror());

	// The mesh becomes resident later: only that part is touched.
	world.MakeResident(11, { 9, 1 });
	const auto later = world.Frame();
	SWIM_CHECK_EQUAL(later.MeshesResolved, 1u);
	SWIM_CHECK_EQUAL(later.PendingMeshes, 0u);
	SWIM_CHECK_EQUAL(later.ObjectsCreated, 0u);
	SWIM_CHECK_EQUAL(world.lastUpload.InstanceRows, 1u);
	SWIM_CHECK_EQUAL(world.lastUpload.TransformRows, 0u);
	SWIM_CHECK_EQUAL(world.Instance(multi, 1)->MeshIndex, 9u);

	// A static scene extracts and uploads nothing and resolves nothing.
	const auto calls = world.resolveCalls;
	const auto idle = world.Frame();
	SWIM_CHECK_EQUAL(idle.EntitiesChanged + idle.ObjectsCreated + idle.TransformsWritten + idle.MeshesResolved, 0u);
	SWIM_CHECK(world.lastUpload.UploadPasses.empty());
	SWIM_CHECK_EQUAL(world.resolveCalls, calls);
}

SWIM_TEST("Scene.RenderExtraction", "TransformDirtyListDrivesTransformOnlyUpdates")
{
	ExtractionWorld world;
	world.MakeResident(1, { 0, 1 });
	std::vector<entt::entity> entities;
	for (int i = 0; i < 8; ++i)
	{
		entities.push_back(world.Spawn(float(i), { { world.Mesh(1) } }));
	}
	world.Frame();

	world.Move(entities[3], 30.0f);
	world.Move(entities[3], 31.0f); // Deduplicated in the transform epoch.
	world.Move(entities[6], 60.0f);
	const auto stats = world.Frame();
	SWIM_CHECK_EQUAL(stats.TransformsWritten, 2u);
	SWIM_CHECK_EQUAL(stats.EntitiesChanged, 0u);
	SWIM_CHECK_EQUAL(world.lastUpload.TransformRows, 2u);
	SWIM_CHECK_EQUAL(world.lastUpload.InstanceRows, 0u);
	SWIM_CHECK_EQUAL(world.WorldX(entities[3]), 31.0f);
	SWIM_CHECK_EQUAL(world.scene.scene->GetTransform(world.extractor->Find(entities[3]))->Previous[3], 3.0f);

	// Dirty entities without a MeshRenderer are ignored.
	const auto bare = world.registry.create();
	world.registry.emplace<TestPose>(bare);
	world.Move(bare, 5.0f);
	SWIM_CHECK_EQUAL(world.Frame().TransformsWritten, 0u);
	SWIM_CHECK(world.scene.GpuMatchesMirror());
}

SWIM_TEST("Scene.RenderExtraction", "ComponentChangesReconcilePartsAndRemovalsRetireObjects")
{
	ExtractionWorld world;
	world.MakeResident(1, { 0, 1 });
	world.MakeResident(2, { 1, 1 });
	Testing::MockTimeline timeline;
	const auto entity = world.Spawn(0.0f, { { world.Mesh(1), 1 }, { world.Mesh(1), 2 } });
	world.Frame();
	const auto first = world.extractor->Find(entity, 0);
	const auto second = world.extractor->Find(entity, 1);

	// patch: new material on part 0, new mesh on part 1, hidden, LOD bias, third part.
	world.registry.patch<Engine::MeshRenderer>(entity,
		[&](Engine::MeshRenderer& renderer)
		{
			renderer.Parts[0].MaterialSet = 10;
			renderer.Parts[1].Mesh = world.Mesh(2);
			renderer.Parts.push_back({ world.Mesh(1), 3 });
			renderer.Flags = RenderObjectFlags::CastShadows;
			renderer.LodBias = 0.5f;
		});
	auto stats = world.Frame();
	SWIM_CHECK_EQUAL(stats.ObjectsUpdated, 2u);
	SWIM_CHECK_EQUAL(stats.ObjectsCreated, 1u);
	SWIM_CHECK(world.extractor->Find(entity, 0) == first); // Rows are stable across updates.
	SWIM_CHECK(world.extractor->Find(entity, 1) == second);
	SWIM_CHECK_EQUAL(world.Instance(entity, 0)->MaterialSet, 10u);
	SWIM_CHECK_EQUAL(world.Instance(entity, 1)->MeshIndex, 1u);
	SWIM_CHECK_EQUAL(world.Instance(entity, 2)->MaterialSet, 3u);
	SWIM_CHECK(!HasAny(static_cast<RenderObjectFlags>(world.Instance(entity, 0)->Flags), RenderObjectFlags::Visible));
	SWIM_CHECK_EQUAL(world.Instance(entity, 1)->LodBias, 0.5f);
	SWIM_CHECK_EQUAL(world.lastUpload.TransformRows, 1u); // Only the new part; unchanged transforms stay put.

	// Dropping a part retires exactly that object.
	world.registry.patch<Engine::MeshRenderer>(entity,
		[](Engine::MeshRenderer& renderer)
		{
			renderer.Parts.resize(1);
		});
	stats = world.Frame({ &timeline, 3 });
	SWIM_CHECK_EQUAL(stats.ObjectsDestroyed, 2u);
	SWIM_CHECK(!world.scene.scene->IsValid(second));
	SWIM_CHECK_EQUAL(world.scene.scene->GetStats().RetiringObjects, 2u);

	// Removing the component (or destroying the entity) retires the rest.
	world.registry.remove<Engine::MeshRenderer>(entity);
	stats = world.Frame({ &timeline, 4 });
	SWIM_CHECK_EQUAL(stats.EntitiesDestroyed, 1u);
	SWIM_CHECK_EQUAL(stats.ObjectsDestroyed, 1u);
	SWIM_CHECK(!world.extractor->Find(entity));
	SWIM_CHECK_EQUAL(world.extractor->GetTrackedEntities(), 0u);
	const auto doomed = world.Spawn(1.0f, { { world.Mesh(1) } });
	world.Frame();
	world.registry.destroy(doomed);
	SWIM_CHECK_EQUAL(world.Frame({ &timeline, 5 }).ObjectsDestroyed, 1u);
	SWIM_CHECK_EQUAL(world.extractor->GetLiveObjects(), 0u);

	// Remove and re-add within one frame rebuilds the entity.
	const auto flicker = world.Spawn(2.0f, { { world.Mesh(1) } });
	world.Frame();
	world.registry.remove<Engine::MeshRenderer>(flicker);
	world.registry.emplace<Engine::MeshRenderer>(flicker, Engine::MeshRenderer{ { { world.Mesh(2), 8 } } });
	stats = world.Frame({ &timeline, 6 });
	SWIM_CHECK_EQUAL(stats.ObjectsDestroyed, 1u);
	SWIM_CHECK_EQUAL(stats.ObjectsCreated, 1u);
	SWIM_CHECK_EQUAL(world.Instance(flicker)->MaterialSet, 8u);

	timeline.Complete(6);
	SWIM_CHECK_EQUAL(world.scene.scene->Collect(), 5u);
	SWIM_CHECK(world.scene.GpuMatchesMirror());
}

SWIM_TEST("Scene.RenderExtraction", "SceneUnloadRetiresEverythingAndFullScenesRetry")
{
	ExtractionWorld world(3);
	world.MakeResident(1, { 0, 1 });
	Testing::MockTimeline timeline;
	std::vector<entt::entity> entities;
	for (int i = 0; i < 4; ++i)
	{
		entities.push_back(world.Spawn(float(i), { { world.Mesh(1) } }));
	}
	auto stats = world.Frame();
	SWIM_CHECK_EQUAL(stats.ObjectsCreated, 3u);
	SWIM_CHECK_EQUAL(stats.CapacityFailures, 2u); // Reconcile and the pending retry in the same call.
	world.registry.destroy(entities[0]);
	stats = world.Frame({ &timeline, 1 });
	SWIM_CHECK_EQUAL(stats.ObjectsCreated, 0u); // The freed row is still retiring.
	timeline.Complete(1);
	world.scene.scene->Collect();
	stats = world.Frame();
	SWIM_CHECK_EQUAL(stats.ObjectsCreated, 1u);
	SWIM_CHECK(world.extractor->Find(entities[3]));
	SWIM_CHECK_EQUAL(world.WorldX(entities[3]), 3.0f);

	// Scene unload.
	world.extractor->ReleaseAll({ &timeline, 2 });
	SWIM_CHECK_EQUAL(world.scene.scene->GetStats().LiveObjects, 0u);
	SWIM_CHECK_EQUAL(world.scene.scene->GetStats().RetiringObjects, 3u);
	SWIM_CHECK_EQUAL(world.extractor->GetTrackedEntities(), 0u);
	SWIM_CHECK_EQUAL(world.Frame().ObjectsCreated, 0u);
	timeline.Complete(2);
	world.scene.scene->Collect();
	world.extractor->Rescan();
	SWIM_CHECK_EQUAL(world.Frame().ObjectsCreated, 3u);

	// RefreshMeshes re-resolves (for example after residency replaced a mesh).
	world.MakeResident(1, { 5, 2 });
	SWIM_CHECK_EQUAL(world.Instance(entities[1])->MeshIndex, 0u);
	world.extractor->RefreshMeshes();
	world.Frame();
	SWIM_CHECK_EQUAL(world.Instance(entities[1])->MeshIndex, 5u);
	SWIM_CHECK_EQUAL(world.Instance(entities[1])->MeshGeneration, 2u);
}

// Critical-path item 48 (CPU side): 100k entities extracted once, then frames
// where 1% move. Per-frame work follows the dirty list, not the scene size.
SWIM_TEST("Scene.RenderExtraction", "HundredThousandEntitiesExtractDirtyOnly")
{
	constexpr std::uint32_t count = 100000;
	ExtractionWorld world(count);
	world.MakeResident(1, { 0, 1 });
	std::vector<entt::entity> entities;
	entities.reserve(count);
	for (std::uint32_t i = 0; i < count; ++i)
	{
		entities.push_back(world.Spawn(float(i), { { world.Mesh(1), i % 32 } }));
	}
	const auto start = std::chrono::steady_clock::now();
	const auto initial = world.Frame();
	const auto initialTime = std::chrono::steady_clock::now() - start;
	SWIM_CHECK_EQUAL(initial.ObjectsCreated, count);
	SWIM_CHECK_EQUAL(world.lastUpload.UploadBytes, std::uint64_t(count) * (64 + 96));

	std::chrono::steady_clock::duration dirtyTime{};
	for (std::uint32_t frame = 1; frame <= 3; ++frame)
	{
		for (std::uint32_t i = frame; i < count; i += 100)
		{
			world.Move(entities[i], float(i) + float(frame) * 0.5f);
		}
		const auto frameStart = std::chrono::steady_clock::now();
		const auto stats = world.Frame();
		dirtyTime += std::chrono::steady_clock::now() - frameStart;
		SWIM_CHECK_EQUAL(stats.TransformsWritten, 1000u);
		SWIM_CHECK_EQUAL(stats.EntitiesChanged, 0u);
		SWIM_CHECK_EQUAL(world.lastUpload.InstanceRows, 0u);
		SWIM_CHECK_EQUAL(world.lastUpload.TransformRows, frame == 1 ? 1000u : 2000u);
	}
	SWIM_CHECK(world.scene.GpuMatchesMirror());
	std::printf("             [Extraction stress] 100k initial extract+upload %.1f ms, 1%% dirty frame avg %.2f ms (mock device)\n",
		std::chrono::duration<double, std::milli>(initialTime).count(), std::chrono::duration<double, std::milli>(dirtyTime).count() / 3.0);
}
