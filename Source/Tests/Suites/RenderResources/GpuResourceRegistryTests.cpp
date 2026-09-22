#include "Engine/Systems/Renderer/Resources/GpuResourceRegistry.h"
#include "Tests/Framework/Test.h"
#include "Tests/Fixtures/RhiFrameCapture.h"

using namespace Swim;
using namespace Swim::Render;

namespace
{
	// Counts destruction so tests can prove deferred GPU-object lifetime.
	struct TrackedRecord
	{
		explicit TrackedRecord(int* destroyed, int value = 0) : Destroyed(destroyed), Value(value) {}

		TrackedRecord(TrackedRecord&& other) noexcept : Destroyed(std::exchange(other.Destroyed, nullptr)), Value(other.Value) {}

		TrackedRecord& operator=(TrackedRecord&& other) noexcept
		{
			Reset();
			Destroyed = std::exchange(other.Destroyed, nullptr);
			Value = other.Value;
			return *this;
		}

		~TrackedRecord() { Reset(); }

		void Reset()
		{
			if (Destroyed)
			{
				++*Destroyed;
				Destroyed = nullptr;
			}
		}

		int* Destroyed = nullptr;
		int Value = 0;
	};

	using Registry = GpuResourceRegistry<GpuTextureTag, TrackedRecord>;
} // namespace

SWIM_TEST("Render.GpuResourceRegistry", "HandlesAreGenerationalAndPackRoundTrip")
{
	static_assert(!std::is_same_v<GpuMeshHandle, GpuTextureHandle>);
	GpuMeshHandle invalid;
	SWIM_CHECK(!invalid.IsValid());
	const GpuMeshHandle handle{ 7, 3 };
	SWIM_CHECK(GpuMeshHandle::Unpack(handle.Pack()) == handle);
	SWIM_CHECK_EQUAL(handle.Pack(), (std::uint64_t(3) << 32) | 7u);

	int destroyed = 0;
	Registry registry;
	auto a = registry.Create(TrackedRecord(&destroyed, 11));
	auto b = registry.Create(TrackedRecord(&destroyed, 22));
	SWIM_CHECK_EQUAL(a.Index, 0u);
	SWIM_CHECK_EQUAL(b.Index, 1u);
	SWIM_REQUIRE(registry.Get(a) != nullptr);
	SWIM_CHECK_EQUAL(registry.Get(a)->Value, 11);
	SWIM_CHECK(registry.Get(GpuTextureHandle{ 0, 2 }) == nullptr);
	SWIM_CHECK(registry.Get(GpuTextureHandle{ 9, 1 }) == nullptr);
	SWIM_CHECK_THROWS(registry.GetChecked(GpuTextureHandle{}), std::invalid_argument);
	SWIM_CHECK_EQUAL(destroyed, 0);
}

SWIM_TEST("Render.GpuResourceRegistry", "ReleaseInvalidatesNowAndDestroysAfterTimelineCompletion")
{
	Testing::MockTimeline timeline;
	int destroyed = 0;
	Registry registry;
	auto handle = registry.Create(TrackedRecord(&destroyed, 1));
	SWIM_CHECK(registry.Release(handle, { &timeline, 5 }));
	SWIM_CHECK(!registry.IsValid(handle));
	SWIM_CHECK(!registry.Release(handle, { &timeline, 6 })); // Stale handles are rejected.
	SWIM_CHECK_EQUAL(registry.GetStats().Retiring, 1u);

	timeline.Complete(4);
	SWIM_CHECK_EQUAL(registry.CollectRetired(), 0u);
	SWIM_CHECK_EQUAL(destroyed, 0);
	// The retiring slot is not handed out while work may still read it.
	auto other = registry.Create(TrackedRecord(&destroyed, 2));
	SWIM_CHECK(other.Index != handle.Index);

	timeline.Complete(5);
	std::vector<GpuTextureHandle> retired;
	SWIM_CHECK_EQUAL(registry.CollectRetired(
						 [&](GpuTextureHandle released, TrackedRecord& record)
						 {
							 SWIM_CHECK_EQUAL(record.Value, 1);
							 retired.push_back(released);
						 }),
		1u);
	SWIM_REQUIRE_EQUAL(retired.size(), 1u);
	SWIM_CHECK(retired[0] == handle);
	SWIM_CHECK_EQUAL(destroyed, 1);

	auto reused = registry.Create(TrackedRecord(&destroyed, 3));
	SWIM_CHECK_EQUAL(reused.Index, handle.Index);
	SWIM_CHECK_EQUAL(reused.Generation, handle.Generation + 1);
	SWIM_CHECK(!registry.IsValid(handle));
	SWIM_CHECK(registry.IsValid(reused));
	SWIM_CHECK_THROWS(registry.Release(reused, { nullptr, 3 }), std::invalid_argument);
}

SWIM_TEST("Render.GpuResourceRegistry", "RetirementIsPerTimelineAndSlotsRecycleOldestFirst")
{
	Testing::MockTimeline graphics;
	Testing::MockTimeline transfer;
	int destroyed = 0;
	Registry registry;
	auto a = registry.Create(TrackedRecord(&destroyed));
	auto b = registry.Create(TrackedRecord(&destroyed));
	auto c = registry.Create(TrackedRecord(&destroyed));
	registry.Release(b, { &graphics, 2 });
	registry.Release(a, { &transfer, 1 });
	registry.Release(c); // No GPU use: retires at the next collection.
	transfer.Complete(1);
	SWIM_CHECK_EQUAL(registry.CollectRetired(), 2u);
	SWIM_CHECK_EQUAL(registry.GetStats().FreeSlots, 2u);
	// Freed order: a, then c (b still retiring).
	SWIM_CHECK_EQUAL(registry.Create(TrackedRecord(&destroyed)).Index, a.Index);
	SWIM_CHECK_EQUAL(registry.Create(TrackedRecord(&destroyed)).Index, c.Index);
	SWIM_CHECK_EQUAL(registry.Create(TrackedRecord(&destroyed)).Index, 3u);
	const auto stats = registry.GetStats();
	SWIM_CHECK_EQUAL(stats.Live, 3u);
	SWIM_CHECK_EQUAL(stats.Retiring, 1u);
	SWIM_CHECK_EQUAL(stats.SlotHighWater, 4u);
}

SWIM_TEST("Render.GpuResourceRegistry", "CapacityCountsRetiringSlotsAndDrainWaits")
{
	Testing::MockTimeline timeline;
	int destroyed = 0;
	SWIM_CHECK_THROWS(Registry({ 0, "empty" }), std::invalid_argument);
	Registry registry({ 2, "Bindless textures" });
	auto a = registry.Create(TrackedRecord(&destroyed));
	registry.Create(TrackedRecord(&destroyed));
	SWIM_CHECK(!registry.TryCreate(TrackedRecord(&destroyed)));
	SWIM_CHECK_THROWS(registry.Create(TrackedRecord(&destroyed)), std::length_error);
	registry.Release(a, { &timeline, 9 });
	SWIM_CHECK(!registry.TryCreate(TrackedRecord(&destroyed))); // Retiring still occupies its slot.

	timeline.FailWait = true;
	SWIM_CHECK_THROWS(registry.Drain(), std::runtime_error);
	SWIM_CHECK_EQUAL(registry.GetStats().Retiring, 1u);
	timeline.FailWait = false;
	SWIM_CHECK_EQUAL(registry.Drain(), 1u);
	SWIM_CHECK_EQUAL(timeline.WaitCount, 2u);
	SWIM_CHECK(registry.TryCreate(TrackedRecord(&destroyed)).has_value());
	// 3 rejected temporaries + the retired record.
	SWIM_CHECK_EQUAL(destroyed, 4);
}

SWIM_TEST("Render.GpuResourceRegistry", "ThrowingRetirementCallbackKeepsEntryPending")
{
	Testing::MockTimeline timeline;
	int destroyed = 0;
	Registry registry;
	auto a = registry.Create(TrackedRecord(&destroyed, 1));
	auto b = registry.Create(TrackedRecord(&destroyed, 2));
	registry.Release(a, { &timeline, 1 });
	registry.Release(b, { &timeline, 1 });
	timeline.Complete(1);
	SWIM_CHECK_THROWS(registry.CollectRetired(
						  [](GpuTextureHandle, TrackedRecord& record)
						  {
							  if (record.Value == 2)
							  {
								  throw std::runtime_error("callback failure");
							  }
						  }),
		std::runtime_error);
	SWIM_CHECK_EQUAL(destroyed, 1);
	SWIM_CHECK_EQUAL(registry.GetStats().Retiring, 1u);
	SWIM_CHECK_EQUAL(registry.CollectRetired(), 1u);
	SWIM_CHECK_EQUAL(destroyed, 2);

	int visited = 0;
	registry.Create(TrackedRecord(&destroyed, 5));
	registry.ForEach(
		[&](GpuTextureHandle, TrackedRecord& record)
		{
			visited += record.Value;
		});
	SWIM_CHECK_EQUAL(visited, 5);
}

SWIM_TEST("Render.GpuResourceRegistry", "DestructionWaitsPendingRetirements")
{
	Testing::MockTimeline timeline;
	int destroyed = 0;
	{
		Registry registry;
		registry.Release(registry.Create(TrackedRecord(&destroyed)), { &timeline, 3 });
		registry.Create(TrackedRecord(&destroyed));
	}
	SWIM_CHECK_EQUAL(timeline.WaitCount, 1u);
	SWIM_CHECK_EQUAL(timeline.GetCompletedValue(), 3u);
	SWIM_CHECK_EQUAL(destroyed, 2);
}
