#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"
#include "Tests/Fixtures/BindlessTableFixture.h"
#include "Tests/Framework/Test.h"

#include <vector>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	std::vector<std::unique_ptr<Testing::MockTextureView>> MakeViews(Testing::MockTexture& texture, std::size_t count)
	{
		std::vector<std::unique_ptr<Testing::MockTextureView>> views;
		for (std::size_t i = 0; i < count; ++i)
		{
			views.push_back(std::make_unique<Testing::MockTextureView>(texture, Rhi::TextureViewDesc{}));
		}
		return views;
	}
} // namespace

SWIM_TEST("Render.Bindless", "FallbacksOccupyElementZeroAndRegistrationWritesImmediately")
{
	Testing::BindlessTableFixture fixture;
	BindlessResourceTable bindless(fixture.device, fixture.Desc());
	auto& table = fixture.Table();
	SWIM_CHECK(&bindless.GetTable() == &table);
	SWIM_CHECK_EQUAL(bindless.GetSpace(), 1u);
	SWIM_CHECK(table.Element(1, 0) == fixture.fallbackView.get());
	SWIM_CHECK(table.Element(0, 0) == fixture.fallbackSampler.get());
	SWIM_CHECK_EQUAL(table.ElementWrites, 2u); // Partially bound: nothing else is pre-filled.

	auto views = MakeViews(*fixture.fallbackTexture, 3);
	const auto first = bindless.RegisterTexture(*views[0]);
	const auto second = bindless.RegisterTexture(*views[1]);
	SWIM_CHECK_EQUAL(bindless.GetIndex(first), 1u);
	SWIM_CHECK_EQUAL(bindless.GetIndex(second), 2u);
	SWIM_CHECK(table.Element(1, 2) == views[1].get());
	Testing::MockSampler sampler(Rhi::SamplerDesc{});
	const auto samplerHandle = bindless.RegisterSampler(sampler);
	SWIM_CHECK_EQUAL(bindless.GetIndex(samplerHandle), 1u);
	SWIM_CHECK(table.Element(0, 1) == &sampler);
	// Image and sampler identities are independent index spaces.
	SWIM_CHECK_EQUAL(bindless.GetIndex(BindlessTextureHandle{}), BindlessResourceTable::FallbackIndex);
	SWIM_CHECK_EQUAL(bindless.GetIndex(BindlessSamplerHandle{ 1, 7 }), BindlessResourceTable::FallbackIndex);
	// The fallback elements can never be released.
	SWIM_CHECK(!bindless.Release(BindlessTextureHandle{ 0, 1 }));
	SWIM_CHECK(!bindless.Release(BindlessSamplerHandle{ 0, 1 }));

	const auto stats = bindless.GetStats();
	SWIM_CHECK_EQUAL(stats.LiveTextures, 3u);
	SWIM_CHECK_EQUAL(stats.TextureCapacity, 4u);
	SWIM_CHECK_EQUAL(stats.LiveSamplers, 2u);
	SWIM_CHECK_EQUAL(stats.SamplerCapacity, 3u);
	SWIM_CHECK_EQUAL(stats.DescriptorWrites, 5u);
}

SWIM_TEST("Render.Bindless", "ReleasedElementsAreRewrittenToTheFallbackAndReusedOnlyAfterTheirTimelinePoint")
{
	Testing::BindlessTableFixture fixture;
	BindlessResourceTable bindless(fixture.device, fixture.Desc());
	auto& table = fixture.Table();
	Testing::MockTimeline timeline;
	auto views = MakeViews(*fixture.fallbackTexture, 5);
	const auto a = bindless.RegisterTexture(*views[0]);
	const auto b = bindless.RegisterTexture(*views[1]);
	const auto c = bindless.RegisterTexture(*views[2]);
	// Capacity 4 including the fallback: the table is now full.
	SWIM_CHECK(!bindless.TryRegisterTexture(*views[3]));
	SWIM_CHECK_THROWS(bindless.RegisterTexture(*views[3]), std::length_error);

	SWIM_CHECK(bindless.Release(b, { &timeline, 5 }));
	SWIM_CHECK(!bindless.Release(b, { &timeline, 5 })); // Handles are invalid immediately.
	SWIM_CHECK(!bindless.IsValid(b));
	SWIM_CHECK_EQUAL(bindless.GetIndex(b), BindlessResourceTable::FallbackIndex);
	// In-flight work may still read element 2: it keeps its view and is not reused.
	SWIM_CHECK_EQUAL(bindless.Collect(), 0u);
	SWIM_CHECK(table.Element(1, 2) == views[1].get());
	SWIM_CHECK(!bindless.TryRegisterTexture(*views[3]));
	SWIM_CHECK_EQUAL(bindless.GetStats().RetiringTextures, 1u);

	timeline.Complete(5);
	SWIM_CHECK_EQUAL(bindless.Collect(), 1u);
	SWIM_CHECK(table.Element(1, 2) == fixture.fallbackView.get()); // Stale indices sample the fallback.
	const auto d = bindless.RegisterTexture(*views[3]);
	SWIM_CHECK_EQUAL(d.Index, 2u);
	SWIM_CHECK(d.Generation != b.Generation);
	SWIM_CHECK(table.Element(1, 2) == views[3].get());
	SWIM_CHECK_EQUAL(bindless.GetIndex(a), 1u);
	SWIM_CHECK_EQUAL(bindless.GetIndex(c), 3u);

	// Freed elements are reused in release order: element 3 was released first.
	bindless.Release(c, { &timeline, 9 });
	bindless.Release(a, { &timeline, 8 });
	timeline.Complete(9);
	SWIM_CHECK_EQUAL(bindless.Collect(), 2u);
	SWIM_CHECK_EQUAL(bindless.RegisterTexture(*views[4]).Index, 3u);

	// Drain waits for pending releases.
	Testing::MockTimeline late;
	bindless.Release(d, { &late, 3 });
	SWIM_CHECK_EQUAL(bindless.Drain(), 1u);
	SWIM_CHECK_EQUAL(late.WaitCount, 1u);
	SWIM_CHECK(table.Element(1, 2) == fixture.fallbackView.get());
}

SWIM_TEST("Render.Bindless", "SamplerElementsRetireIndependentlyAndRejectedWritesReleaseTheirElement")
{
	Testing::BindlessTableFixture fixture;
	BindlessResourceTable bindless(fixture.device, fixture.Desc());
	auto& table = fixture.Table();
	Testing::MockSampler first(Rhi::SamplerDesc{});
	Testing::MockSampler second(Rhi::SamplerDesc{});
	const auto handle = bindless.RegisterSampler(first);
	bindless.RegisterSampler(second);
	SWIM_CHECK(!bindless.TryRegisterSampler(first));
	bindless.Release(handle);
	SWIM_CHECK_EQUAL(bindless.Collect(), 1u); // No timeline: nothing submitted referenced it.
	SWIM_CHECK(table.Element(0, 1) == fixture.fallbackSampler.get());

	// An RHI-rejected view throws and leaves no live element behind.
	auto views = MakeViews(*fixture.fallbackTexture, 1);
	table.Reject = views[0].get();
	SWIM_CHECK_THROWS(bindless.RegisterTexture(*views[0]), std::invalid_argument);
	SWIM_CHECK_EQUAL(bindless.GetStats().LiveTextures, 1u);
	table.Reject = nullptr;
	SWIM_CHECK_EQUAL(bindless.Collect(), 1u);
	SWIM_CHECK_EQUAL(bindless.RegisterTexture(*views[0]).Index, 1u);
}

SWIM_TEST("Render.Bindless", "ConstructionValidatesTheBindlessSpace")
{
	Testing::BindlessTableFixture fixture;
	auto desc = fixture.Desc();
	desc.FallbackTexture = nullptr;
	SWIM_CHECK_THROWS(BindlessResourceTable(fixture.device, desc), std::invalid_argument);
	desc = fixture.Desc();
	desc.Space = 0;
	SWIM_CHECK_THROWS(BindlessResourceTable(fixture.device, desc), std::invalid_argument);
	desc = fixture.Desc();
	desc.TextureBinding = 0;
	SWIM_CHECK_THROWS(BindlessResourceTable(fixture.device, desc), std::invalid_argument);

	auto& bindings = fixture.layout.program.Interface.DescriptorSchemas[0].Bindings;
	const auto original = bindings;
	for (int invalid = 0; invalid < 5; ++invalid)
	{
		bindings = original;
		switch (invalid)
		{
		case 0:
			bindings[1].UpdateAfterBind = false;
			break;
		case 1:
			bindings[0].PartiallyBound = false;
			break;
		case 2:
			bindings[1].SampledClass = Rhi::SampledTextureClass::Uint;
			break;
		case 3:
			bindings[1].SampledDimension = Rhi::TextureViewDimension::TextureCube;
			break;
		case 4:
			bindings.push_back({ 2, Rhi::DescriptorType::UniformBuffer, 1, Rhi::ShaderStageMask::Fragment });
			break;
		}
		SWIM_CHECK_THROWS(BindlessResourceTable(fixture.device, fixture.Desc()), std::invalid_argument);
	}
	bindings = original;
	fixture.device.FailDescriptorTable = true;
	SWIM_CHECK_THROWS(BindlessResourceTable(fixture.device, fixture.Desc()), std::runtime_error);
	fixture.device.FailDescriptorTable = false;
	// Custom binding numbers are honored.
	std::swap(bindings[0].Binding, bindings[1].Binding);
	desc = fixture.Desc();
	desc.SamplerBinding = 1;
	desc.TextureBinding = 0;
	BindlessResourceTable swapped(fixture.device, desc);
	SWIM_CHECK(fixture.Table().Element(0, 0) == fixture.fallbackView.get());
}
