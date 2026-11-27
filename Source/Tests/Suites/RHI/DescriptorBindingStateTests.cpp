// The descriptor table binding contract every backend shares (Rhi::DescriptorBindingState).
#include "Engine/Systems/Renderer/RHI/RhiDescriptorBindings.h"
#include "Tests/Framework/Test.h"

#include <stdexcept>

using namespace Swim;

namespace
{

	Rhi::DescriptorBindingDesc Binding(std::uint32_t binding, Rhi::DescriptorType type)
	{
		Rhi::DescriptorBindingDesc desc;
		desc.Binding = binding;
		desc.Type = type;
		desc.Stages = Rhi::ShaderStageMask::Fragment;
		return desc;
	}

	// Space 0: a storage buffer; space 1: a sampled texture; a 16-byte push constant range.
	Rhi::ShaderProgramInterface Layout(Rhi::DescriptorType space1 = Rhi::DescriptorType::SampledTexture, std::uint32_t push = 16)
	{
		Rhi::ShaderProgramInterface layout;
		layout.DescriptorSchemas.push_back({ 0, { Binding(0, Rhi::DescriptorType::StorageBuffer) } });
		layout.DescriptorSchemas.push_back({ 1, { Binding(0, space1) } });
		layout.PushConstants.push_back({ 0, push, Rhi::ShaderStageMask::Fragment });
		return layout;
	}

	int tableA = 0;
	int tableB = 1;

} // namespace

SWIM_TEST("RHI.DescriptorBindings", "TablesStayBoundAcrossAPipelineWithACompatibleLayout")
{
	const auto first = Layout();
	const auto same = Layout(); // Another layout object, defined identically.
	Rhi::DescriptorBindingState bound;
	bound.Bind(0, &tableA, first, first);
	bound.Bind(1, &tableB, first, first);
	bound.Require(first);
	// Binding a pipeline unbinds nothing: an identically defined layout reads the same tables.
	bound.Require(same);
	SWIM_CHECK(bound.Get(1, same) == &tableB);
}

SWIM_TEST("RHI.DescriptorBindings", "ALayoutCompatibleOnlyForLowerSpacesNeedsTheHigherTablesAgain")
{
	const auto first = Layout();
	const auto other = Layout(Rhi::DescriptorType::StorageTexture); // Space 1 differs.
	Rhi::DescriptorBindingState bound;
	bound.Bind(0, &tableA, first, first);
	bound.Bind(1, &tableB, first, first);
	SWIM_CHECK(bound.Get(0, other) == &tableA);
	SWIM_CHECK(bound.Get(1, other) == nullptr);
	SWIM_CHECK_THROWS(bound.Require(other), std::logic_error);
	// A table made for the first layout's space 1 does not fit the other's.
	SWIM_CHECK_THROWS(bound.Bind(1, &tableB, first, other), std::invalid_argument);
	bound.Bind(1, &tableB, other, other);
	bound.Require(other);
}

SWIM_TEST("RHI.DescriptorBindings", "DifferentPushConstantsMakeEverySpaceIncompatible")
{
	const auto first = Layout();
	const auto wider = Layout(Rhi::DescriptorType::SampledTexture, 32);
	Rhi::DescriptorBindingState bound;
	bound.Bind(0, &tableA, first, first);
	bound.Bind(1, &tableB, first, first);
	SWIM_CHECK_THROWS(bound.Require(wider), std::logic_error);
	SWIM_CHECK(bound.Get(0, wider) == nullptr);
}

SWIM_TEST("RHI.DescriptorBindings", "BindingWithAnIncompatibleLayoutUnbindsTheOtherSpaces")
{
	const auto first = Layout();
	const auto wider = Layout(Rhi::DescriptorType::SampledTexture, 32);
	Rhi::DescriptorBindingState bound;
	bound.Bind(0, &tableA, first, first);
	bound.Bind(1, &tableB, first, first);
	// Space 0 bound again under a layout incompatible for space 0: space 1 (bound under the
	// first layout) is unbound, even for the first layout.
	bound.Bind(0, &tableA, wider, wider);
	SWIM_CHECK(bound.Get(1, first) == nullptr);
	SWIM_CHECK(bound.Get(1, wider) == nullptr);
	SWIM_CHECK(bound.Get(0, wider) == &tableA);
	bound.Reset();
	SWIM_CHECK(bound.Get(0, wider) == nullptr);
}
