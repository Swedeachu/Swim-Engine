#include "Engine/Systems/Renderer/GpuMaterials/GpuMaterialTable.h"
#include "Engine/Systems/Renderer/Materials/StandardMaterial.h"
#include "Tests/Fixtures/GpuSceneFixture.h"

#include <cstring>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	struct MaterialWorld
	{
		explicit MaterialWorld(std::uint32_t capacity = 8)
			: materialTemplate(CreateStandardMaterialTemplate()), table(fixture.device, { materialTemplate, capacity, "Test materials" })
		{
		}

		// Imports, executes and commits; returns the rows the import uploaded.
		std::uint32_t Frame()
		{
			RenderGraph graph;
			const auto resources = table.Import(graph);
			SWIM_CHECK_EQUAL(resources.RecordSize, StandardMaterialRecordSize);
			graph.AddPass(
				"Reader", Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(resources.Materials, Rhi::ResourceState::ShaderRead);
				},
				[](RenderCommandContext&)
				{
				});
			fixture.executor->Execute(graph.Compile());
			fixture.executor->Wait();
			table.CommitUploads();
			return table.GetStats().LastUploadRows;
		}

		float GpuFloat(std::uint32_t row, std::uint32_t offset) const
		{
			float value = 0;
			const auto& bytes = Testing::GpuSceneFixture::Bytes(table.GetBuffer());
			std::memcpy(&value, bytes.data() + std::size_t(row) * StandardMaterialRecordSize + offset, sizeof(value));
			return value;
		}

		std::uint32_t GpuUint(std::uint32_t row, std::uint32_t offset) const
		{
			std::uint32_t value = 0;
			const auto& bytes = Testing::GpuSceneFixture::Bytes(table.GetBuffer());
			std::memcpy(&value, bytes.data() + std::size_t(row) * StandardMaterialRecordSize + offset, sizeof(value));
			return value;
		}

		std::shared_ptr<MaterialInstance> Material(float roughness)
		{
			auto instance = std::make_shared<MaterialInstance>(materialTemplate);
			instance->SetFloat("RoughnessFactor", roughness);
			return instance;
		}

		Testing::GpuSceneFixture fixture;
		std::shared_ptr<const MaterialTemplate> materialTemplate;
		GpuMaterialTable table;
	};

	constexpr std::uint32_t RoughnessOffset = 32;
	constexpr std::uint32_t NormalTextureOffset = 56;
} // namespace

SWIM_TEST("Render.GpuMaterials", "RowsUploadOnlyWhenInstancesChangeAndRowZeroIsTheFallback")
{
	MaterialWorld world;
	// First import: every row (defaults) in one run.
	SWIM_CHECK_EQUAL(world.Frame(), 8u);
	SWIM_CHECK_EQUAL(world.table.GetStats().LastUploadRuns, 1u);
	SWIM_CHECK_EQUAL(world.table.GetStats().LastUploadBytes, 8u * StandardMaterialRecordSize);
	SWIM_CHECK_EQUAL(world.GpuFloat(0, RoughnessOffset), 1.0f); // Fallback = template defaults.
	SWIM_CHECK_EQUAL(world.GpuFloat(5, RoughnessOffset), 1.0f); // Unused rows too.
	SWIM_CHECK_EQUAL(world.Frame(), 0u);

	auto rough = world.Material(0.75f);
	auto smooth = world.Material(0.125f);
	const auto first = world.table.Create(rough);
	const auto second = world.table.Create(smooth);
	SWIM_CHECK_EQUAL(world.table.GetIndex(first), 1u);
	SWIM_CHECK_EQUAL(world.table.GetIndex(second), 2u);
	SWIM_CHECK_EQUAL(world.table.GetStats().LiveMaterials, 2u);
	SWIM_CHECK_EQUAL(world.Frame(), 2u);
	SWIM_CHECK_EQUAL(world.table.GetStats().LastUploadRuns, 1u); // Rows 1-2 are adjacent.
	SWIM_CHECK_EQUAL(world.GpuFloat(1, RoughnessOffset), 0.75f);
	SWIM_CHECK_EQUAL(world.GpuFloat(2, RoughnessOffset), 0.125f);

	// Edits are picked up by version; no-op writes are free.
	smooth->SetTexture("NormalTexture", 9);
	rough->SetFloat("RoughnessFactor", 0.75f);
	SWIM_CHECK_EQUAL(world.Frame(), 1u);
	SWIM_CHECK_EQUAL(world.GpuUint(2, NormalTextureOffset), 9u);
	SWIM_CHECK_EQUAL(world.Frame(), 0u);

	// Aborted imports upload again.
	rough->SetFloat("RoughnessFactor", 0.5f);
	{
		RenderGraph graph;
		world.table.Import(graph);
		SWIM_CHECK_THROWS(world.table.Import(graph), std::logic_error); // One import in flight.
		world.table.AbortUploads();
	}
	SWIM_CHECK_EQUAL(world.table.GetStats().DirtyRows, 1u);
	SWIM_CHECK_EQUAL(world.Frame(), 1u);
	SWIM_CHECK_EQUAL(world.GpuFloat(1, RoughnessOffset), 0.5f);

	// Release: the handle dies now, the row returns to the defaults once retired.
	SWIM_CHECK(world.table.Release(first));
	SWIM_CHECK(!world.table.IsValid(first));
	SWIM_CHECK_EQUAL(world.table.GetIndex(first), GpuMaterialTable::FallbackIndex);
	SWIM_CHECK(!world.table.Release(first));
	SWIM_CHECK_EQUAL(world.table.GetStats().RetiringMaterials, 1u);
	SWIM_CHECK_EQUAL(world.table.Collect(), 1u);
	SWIM_CHECK_EQUAL(world.Frame(), 1u);
	SWIM_CHECK_EQUAL(world.GpuFloat(1, RoughnessOffset), 1.0f);
	rough->SetFloat("RoughnessFactor", 0.9f); // Edits to released instances are ignored.
	SWIM_CHECK_EQUAL(world.Frame(), 0u);

	// The fallback row cannot be released; invalid handles map to it.
	SWIM_CHECK(!world.table.Release(GpuMaterialHandle{ 0, 1 }));
	SWIM_CHECK(!world.table.IsValid(GpuMaterialHandle{}));
	SWIM_CHECK_EQUAL(world.table.GetIndex(GpuMaterialHandle{}), 0u);
}

SWIM_TEST("Render.GpuMaterials", "RejectsForeignTemplatesAndReportsExhaustion")
{
	MaterialWorld world(3); // Fallback + two materials.
	SWIM_CHECK_THROWS(GpuMaterialTable(world.fixture.device, { nullptr, 4, "Bad" }), std::invalid_argument);
	SWIM_CHECK_THROWS(GpuMaterialTable(world.fixture.device, { world.materialTemplate, 1, "Bad" }), std::invalid_argument);
	SWIM_CHECK_THROWS(world.table.Create(nullptr), std::invalid_argument);
	// Same layout, different template object: rejected (layouts are shared by pointer).
	auto lookalike = std::make_shared<const MaterialInstance>(CreateStandardMaterialTemplate());
	SWIM_CHECK_THROWS(world.table.Create(lookalike), std::invalid_argument);

	const auto a = world.table.Create(world.Material(0.1f));
	world.table.Create(world.Material(0.2f));
	SWIM_CHECK(!world.table.TryCreate(world.Material(0.3f)).has_value());
	SWIM_CHECK_THROWS(world.table.Create(world.Material(0.3f)), std::length_error);
	// A released row is reusable only after it retires.
	world.table.Release(a);
	SWIM_CHECK(!world.table.TryCreate(world.Material(0.3f)).has_value());
	world.table.Drain();
	const auto reused = world.table.TryCreate(world.Material(0.3f));
	SWIM_REQUIRE(reused.has_value());
	SWIM_CHECK_EQUAL(world.table.GetIndex(*reused), 1u);
	SWIM_CHECK(reused->Generation != a.Generation);
	world.Frame();
	SWIM_CHECK_EQUAL(world.GpuFloat(1, RoughnessOffset), 0.3f);
}
