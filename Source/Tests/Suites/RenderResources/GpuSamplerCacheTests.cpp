#include "Engine/Systems/Renderer/Resources/GpuSamplerCache.h"
#include "Tests/Fixtures/BindlessTableFixture.h"
#include "Tests/Framework/Test.h"

using namespace Swim;
using namespace Swim::Render;

SWIM_TEST("Render.SamplerCache", "EqualDescriptionsShareOneSamplerAndBindlessElement")
{
	Testing::BindlessTableFixture fixture;
	BindlessResourceTable bindless(fixture.device, fixture.Desc());
	GpuSamplerCache cache(fixture.device, &bindless);
	Rhi::SamplerDesc linear{};
	linear.DebugName = "first";
	Rhi::SamplerDesc renamed = linear;
	renamed.DebugName = "second"; // Names are not part of sampler identity.
	Rhi::SamplerDesc clamp = linear;
	clamp.AddressU = Rhi::SamplerAddressMode::ClampToEdge;

	const auto a = cache.Acquire(linear);
	const auto b = cache.Acquire(renamed);
	const auto c = cache.Acquire(clamp);
	SWIM_CHECK(a == b);
	SWIM_CHECK(a != c);
	SWIM_CHECK_EQUAL(fixture.device.SamplerCreateCount, 2u);
	SWIM_CHECK_EQUAL(cache.GetBindlessIndex(a), 1u);
	SWIM_CHECK_EQUAL(cache.GetBindlessIndex(c), 2u);
	SWIM_CHECK(fixture.Table().Element(0, 1) == cache.Get(a));
	auto stats = cache.GetStats();
	SWIM_CHECK_EQUAL(stats.Live, 2u);
	SWIM_CHECK_EQUAL(stats.References, 3u);
	SWIM_CHECK_EQUAL(stats.Created, 2u);
	SWIM_CHECK_EQUAL(stats.Reused, 1u);
	// Both bindless sampler elements beyond the fallback are used.
	Rhi::SamplerDesc nearest = linear;
	nearest.MinFilter = Rhi::Filter::Nearest;
	SWIM_CHECK_THROWS(cache.Acquire(nearest), std::length_error);
	SWIM_CHECK_EQUAL(cache.GetStats().Live, 2u);
	SWIM_CHECK_EQUAL(cache.Collect(), 1u); // The never-submitted sampler retires at once.
	SWIM_CHECK_EQUAL(bindless.GetStats().LiveSamplers, 3u);
}

SWIM_TEST("Render.SamplerCache", "TheLastReleaseRetiresAfterTheLatestReportedUse")
{
	Testing::BindlessTableFixture fixture;
	BindlessResourceTable bindless(fixture.device, fixture.Desc());
	GpuSamplerCache cache(fixture.device, &bindless);
	Testing::MockTimeline timeline;
	const auto handle = cache.Acquire({});
	cache.Acquire({});
	auto* sampler = cache.Get(handle);
	SWIM_CHECK(cache.Release(handle, { &timeline, 7 }));
	SWIM_CHECK(cache.Get(handle) == sampler); // Still referenced.
	SWIM_CHECK(cache.Release(handle, { &timeline, 4 }));
	SWIM_CHECK(!cache.Get(handle));
	SWIM_CHECK(!cache.Release(handle));
	SWIM_CHECK_EQUAL(cache.GetBindlessIndex(handle), BindlessResourceTable::FallbackIndex);

	timeline.Complete(4);
	SWIM_CHECK_EQUAL(cache.Collect(), 0u); // Retires after value 7, the latest use.
	SWIM_CHECK_EQUAL(bindless.Collect(), 0u);
	timeline.Complete(7);
	SWIM_CHECK_EQUAL(cache.Collect(), 1u);
	SWIM_CHECK_EQUAL(bindless.Collect(), 1u);
	SWIM_CHECK(fixture.Table().Element(0, 1) == fixture.fallbackSampler.get());

	// A new acquisition after retirement creates a fresh sampler.
	const auto again = cache.Acquire({});
	SWIM_CHECK(again != handle);
	SWIM_CHECK_EQUAL(fixture.device.SamplerCreateCount, 2u);
	Testing::MockTimeline other;
	cache.Acquire({});
	cache.Release(again, { &timeline, 9 });
	SWIM_CHECK_THROWS(cache.Release(again, { &other, 1 }), std::invalid_argument);
}

SWIM_TEST("Render.SamplerCache", "WorksWithoutABindlessTable")
{
	Testing::MockDevice device;
	GpuSamplerCache cache(device, nullptr, { 1, "Solo" });
	const auto handle = cache.Acquire({});
	SWIM_CHECK(cache.Get(handle) != nullptr);
	SWIM_CHECK_EQUAL(cache.GetBindlessIndex(handle), BindlessResourceTable::FallbackIndex);
	Rhi::SamplerDesc other{};
	other.EnableComparison = true;
	SWIM_CHECK_THROWS(cache.Acquire(other), std::length_error);
	SWIM_CHECK(cache.Release(handle));
	SWIM_CHECK_EQUAL(cache.Drain(), 1u);
	SWIM_CHECK(cache.Get(cache.Acquire(other)) != nullptr);
}
