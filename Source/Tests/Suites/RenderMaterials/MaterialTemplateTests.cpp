#include "Engine/Systems/Renderer/Materials/MaterialInstance.h"
#include "Tests/Framework/Test.h"

#include <array>
#include <cstring>
#include <memory>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	using T = MaterialParameterType;

	// A small hand-written layout: { float4 Color; float3 Emissive; float Roughness; float2 Tiling; uint BaseTexture; int Layer; uint
	// Flags; ... }.
	MaterialTemplateDesc SampleDesc()
	{
		MaterialTemplateDesc desc;
		desc.Name = "Sample";
		desc.RecordSize = 48;
		desc.Parameters = { { "Color", T::Float4, 0 }, { "Emissive", T::Float3, 16 }, { "Roughness", T::Float, 28 },
			{ "Tiling", T::Float2, 32 }, { "BaseTexture", T::TextureIndex, 40 }, { "Layer", T::Int, 44 } };
		return desc;
	}

	template <typename Value> Value At(std::span<const std::byte> record, std::uint32_t offset)
	{
		Value value{};
		std::memcpy(&value, record.data() + offset, sizeof(value));
		return value;
	}
} // namespace

SWIM_TEST("Render.Materials", "TemplateValidatesStd430LayoutsAndNames")
{
	const MaterialTemplate sample(SampleDesc());
	SWIM_CHECK_EQUAL(sample.GetName(), std::string("Sample"));
	SWIM_CHECK_EQUAL(sample.GetRecordSize(), 48u);
	SWIM_CHECK_EQUAL(sample.GetParameters().size(), 6u);
	SWIM_REQUIRE(sample.FindParameter("Tiling") != nullptr);
	SWIM_CHECK(sample.FindParameter("Tiling")->Type == T::Float2);
	SWIM_CHECK(sample.FindParameter("Missing") == nullptr);
	SWIM_CHECK_EQUAL(sample.GetDefaultRecord().size(), 48u);

	const auto rejects = [](auto edit)
	{
		auto desc = SampleDesc();
		edit(desc);
		bool threw = false;
		try
		{
			MaterialTemplate invalid(std::move(desc));
		}
		catch (const std::invalid_argument&)
		{
			threw = true;
		}
		return threw;
	};
	SWIM_CHECK(rejects(
		[](MaterialTemplateDesc& d)
		{
			d.Name.clear();
		}));
	SWIM_CHECK(rejects(
		[](MaterialTemplateDesc& d)
		{
			d.RecordSize = 44;
		})); // Not a multiple of 16.
	SWIM_CHECK(rejects(
		[](MaterialTemplateDesc& d)
		{
			d.Parameters[1].Offset = 20;
		})); // float3 needs 16-byte alignment.
	SWIM_CHECK(rejects(
		[](MaterialTemplateDesc& d)
		{
			d.Parameters[3].Offset = 36;
		})); // float2 needs 8-byte alignment.
	SWIM_CHECK(rejects(
		[](MaterialTemplateDesc& d)
		{
			d.Parameters[2].Offset = 24;
		})); // Overlaps Emissive.
	SWIM_CHECK(rejects(
		[](MaterialTemplateDesc& d)
		{
			d.Parameters[5].Offset = 48;
		})); // Outside the record.
	SWIM_CHECK(rejects(
		[](MaterialTemplateDesc& d)
		{
			d.Parameters[5].Name = "Color";
		})); // Duplicate name.
	SWIM_CHECK(rejects(
		[](MaterialTemplateDesc& d)
		{
			d.Parameters[5].Name.clear();
		})); // Unnamed.
	SWIM_CHECK(!rejects(
		[](MaterialTemplateDesc& d)
		{
			d.Parameters.clear();
		})); // An empty record is legal.
	SWIM_CHECK_EQUAL(MaterialParameterTypeName(T::TextureIndex), std::string_view("texture index"));
}

SWIM_TEST("Render.Materials", "InstancesStartFromDefaultsAndWriteTypedValues")
{
	auto shared = std::make_shared<MaterialTemplate>(SampleDesc());
	const std::array<float, 4> white{ 1, 1, 1, 1 };
	shared->SetDefault("Color", white);
	shared->SetDefault("Roughness", std::array<float, 1>{ 0.5f });
	shared->SetDefault("Layer", std::int32_t(-1));
	SWIM_CHECK_THROWS(shared->SetDefault("Color", std::array<float, 3>{ 1, 1, 1 }), std::invalid_argument);
	SWIM_CHECK_THROWS(shared->SetDefault("Layer", std::uint32_t(1)), std::invalid_argument);
	const std::shared_ptr<const MaterialTemplate> materialTemplate = shared;

	MaterialInstance instance(materialTemplate);
	SWIM_CHECK_EQUAL(instance.GetRecord().size(), 48u);
	SWIM_CHECK((instance.GetVector("Color") == white));
	SWIM_CHECK_EQUAL(instance.GetFloat("Roughness"), 0.5f);
	SWIM_CHECK_EQUAL(instance.GetInt("Layer"), -1);
	SWIM_CHECK_EQUAL(instance.GetUint("BaseTexture"), 0u); // The bindless fallback element.
	const auto first = instance.GetVersion();

	// Typed writes land at the reflected offsets and bump the version.
	instance.SetVector("Emissive", std::array<float, 3>{ 2, 3, 4 });
	instance.SetFloat("Roughness", 0.25f);
	instance.SetVector("Tiling", std::array<float, 2>{ 8, 9 });
	instance.SetTexture("BaseTexture", 17);
	instance.SetInt("Layer", 3);
	const auto record = instance.GetRecord();
	SWIM_CHECK_EQUAL(At<float>(record, 16), 2.0f);
	SWIM_CHECK_EQUAL(At<float>(record, 24), 4.0f);
	SWIM_CHECK_EQUAL(At<float>(record, 28), 0.25f);
	SWIM_CHECK_EQUAL(At<float>(record, 36), 9.0f);
	SWIM_CHECK_EQUAL(At<std::uint32_t>(record, 40), 17u);
	SWIM_CHECK_EQUAL(At<std::int32_t>(record, 44), 3);
	SWIM_CHECK_EQUAL(instance.GetVersion(), first + 5);
	SWIM_CHECK((instance.GetVector("Emissive") == std::array<float, 4>{ 2, 3, 4, 0 }));

	// Unchanged values keep the version; wrong names/types/counts are rejected.
	instance.SetTexture("BaseTexture", 17);
	SWIM_CHECK_EQUAL(instance.GetVersion(), first + 5);
	SWIM_CHECK_THROWS(instance.SetFloat("Missing", 1.0f), std::invalid_argument);
	SWIM_CHECK_THROWS(instance.SetFloat("Color", 1.0f), std::invalid_argument);
	SWIM_CHECK_THROWS(instance.SetUint("BaseTexture", 1), std::invalid_argument); // Textures go through SetTexture.
	SWIM_CHECK_THROWS(instance.SetSampler("BaseTexture", 1), std::invalid_argument);
	SWIM_CHECK_THROWS(instance.SetTexture("Layer", 1), std::invalid_argument);
	SWIM_CHECK_THROWS(instance.GetFloat("Color"), std::invalid_argument);
	SWIM_CHECK_THROWS(instance.GetInt("BaseTexture"), std::invalid_argument);
	SWIM_CHECK_EQUAL(instance.GetVersion(), first + 5);

	// Instances are independent; the template's defaults are not affected.
	MaterialInstance other(materialTemplate);
	SWIM_CHECK_EQUAL(other.GetFloat("Roughness"), 0.5f);
	SWIM_CHECK_THROWS(MaterialInstance(nullptr), std::invalid_argument);
}
