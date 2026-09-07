#include "Tests/Fixtures/VulkanPipelineCapture.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

SWIM_TEST("RHI.Vulkan.VertexInput", "PipelineCopiesSparseInterleavedAndInstanceLayout")
{
	Testing::VulkanPipelineCapture capture;
	std::array<Rhi::VertexBindingDesc, 2> bindings{{ { 2, 16 }, { 5, 8, Rhi::VertexInputRate::Instance } }};
	std::array<Rhi::VertexAttributeDesc, 3> attributes{{
		{ 0, 2, Rhi::Format::RGB32Float, 0 }, { 3, 2, Rhi::Format::RGBA8Unorm, 12 },
		{ 6, 5, Rhi::Format::RG32Float, 0 }
	}};
	auto pipeline = capture.MakePipeline(Rhi::Format::RGBA8Unorm, bindings, attributes);
	SWIM_REQUIRE(pipeline);
	SWIM_REQUIRE_EQUAL(capture.VertexBindings.size(), 2u);
	SWIM_REQUIRE_EQUAL(capture.VertexAttributes.size(), 3u);
	SWIM_CHECK_EQUAL(capture.VertexBindings[0].binding, 2u);
	SWIM_CHECK_EQUAL(capture.VertexBindings[0].stride, 16u);
	SWIM_CHECK(capture.VertexBindings[1].inputRate == VK_VERTEX_INPUT_RATE_INSTANCE);
	SWIM_CHECK_EQUAL(capture.VertexAttributes[1].location, 3u);
	SWIM_CHECK_EQUAL(capture.VertexAttributes[1].offset, 12u);
	SWIM_CHECK(capture.VertexAttributes[1].format == VK_FORMAT_R8G8B8A8_UNORM);
	bindings[0].Stride = 999;
	attributes[0].Offset = 999;
	const auto owned = pipeline->GetVertexRequirements();
	SWIM_REQUIRE_EQUAL(owned.size(), 2u);
	SWIM_CHECK_EQUAL(owned[0].Stride, 16u);
	SWIM_CHECK_EQUAL(owned[0].ElementBytes, 16u);
	SWIM_CHECK_EQUAL(owned[0].Alignment, 4u);
	SWIM_CHECK_EQUAL(owned[1].ElementBytes, 8u);
	SWIM_CHECK(owned[1].Rate == Rhi::VertexInputRate::Instance);
}

SWIM_TEST("RHI.Vulkan.VertexInput", "InvalidBindingsAndLocationsNeverCreateNativePipeline")
{
	Testing::VulkanPipelineCapture capture;
	const std::array<Rhi::VertexBindingDesc, 1> binding{{ { 0, 16 } }};
	const std::array<Rhi::VertexAttributeDesc, 1> attribute{{ { 0, 0, Rhi::Format::RGBA32Float, 0 } }};
	for (auto bad : { Rhi::VertexBindingDesc{ 32, 16 }, { 0, 2049 }, { 0, 16, static_cast<Rhi::VertexInputRate>(255) } })
	{
		SWIM_CHECK(!capture.MakePipeline(Rhi::Format::RGBA8Unorm, { &bad, 1 }, attribute));
	}
	const std::array duplicates{ binding[0], binding[0] };
	SWIM_CHECK(!capture.MakePipeline(Rhi::Format::RGBA8Unorm, duplicates, attribute));
	const std::array duplicateLocations{ attribute[0], attribute[0] };
	SWIM_CHECK(!capture.MakePipeline(Rhi::Format::RGBA8Unorm, binding, duplicateLocations));
	for (auto bad : { Rhi::VertexAttributeDesc{ 32, 0, Rhi::Format::RGBA32Float, 0 },
		{ 0, 2, Rhi::Format::RGBA32Float, 0 }, { 0, 0, Rhi::Format::R8Unorm, 2048 } })
	{
		SWIM_CHECK(!capture.MakePipeline(Rhi::Format::RGBA8Unorm, binding, { &bad, 1 }));
	}
	SWIM_CHECK(!capture.MakePipeline(Rhi::Format::RGBA8Unorm, {}, attribute));
	SWIM_CHECK_EQUAL(capture.PipelinesCreated, 0u);
}

SWIM_TEST("RHI.Vulkan.VertexInput", "FormatsAlignmentAndRecordExtentsAreValidated")
{
	Testing::VulkanPipelineCapture capture;
	Rhi::VertexBindingDesc binding{ 0, 16 };
	Rhi::VertexAttributeDesc attribute{ 0, 0, Rhi::Format::RGBA32Float, 0 };
	for (auto format : { Rhi::Format::Undefined, Rhi::Format::D32Float, Rhi::Format::BC1RGBAUnorm,
		Rhi::Format::RGBA8UnormSrgb, static_cast<Rhi::Format>(65535) })
	{
		attribute.DataFormat = format;
		SWIM_CHECK(!capture.MakePipeline(Rhi::Format::RGBA8Unorm, { &binding, 1 }, { &attribute, 1 }));
	}
	attribute.DataFormat = Rhi::Format::RG32Float;
	attribute.Offset = 2;
	SWIM_CHECK(!capture.MakePipeline(Rhi::Format::RGBA8Unorm, { &binding, 1 }, { &attribute, 1 }));
	attribute.Offset = 12;
	SWIM_CHECK(!capture.MakePipeline(Rhi::Format::RGBA8Unorm, { &binding, 1 }, { &attribute, 1 }));
	attribute.Offset = 0;
	binding.Stride = 15;
	SWIM_CHECK(!capture.MakePipeline(Rhi::Format::RGBA8Unorm, { &binding, 1 }, { &attribute, 1 }));
	binding.Stride = 16;
	capture.VertexFormatFeatures = 0;
	SWIM_CHECK(!capture.MakePipeline(Rhi::Format::RGBA8Unorm, { &binding, 1 }, { &attribute, 1 }));
	SWIM_CHECK_EQUAL(capture.PipelinesCreated, 0u);
}

SWIM_TEST("RHI.Vulkan.VertexInput", "DeviceCountsAndBoundaryValuesApplyToNativeLayout")
{
	Testing::VulkanPipelineCapture capture;
	auto& limits = capture.State->Device.physical_device.properties.limits;
	limits.maxVertexInputBindings = limits.maxVertexInputAttributes = 2;
	limits.maxVertexInputBindingStride = 8;
	limits.maxVertexInputAttributeOffset = 4;
	const std::array<Rhi::VertexBindingDesc, 1> bindings{{ { 1, 8 } }};
	const std::array<Rhi::VertexAttributeDesc, 1> attributes{{ { 1, 1, Rhi::Format::R32Float, 4 } }};
	SWIM_REQUIRE(capture.MakePipeline(Rhi::Format::RGBA8Unorm, bindings, attributes));
	std::array<Rhi::VertexBindingDesc, 3> tooManyBindings{};
	std::array<Rhi::VertexAttributeDesc, 3> tooManyAttributes{};
	SWIM_CHECK(!capture.MakePipeline(Rhi::Format::RGBA8Unorm, tooManyBindings, attributes));
	SWIM_CHECK(!capture.MakePipeline(Rhi::Format::RGBA8Unorm, bindings, tooManyAttributes));
	SWIM_CHECK_EQUAL(capture.PipelinesCreated, 1u);
}

SWIM_TEST("RHI.Vulkan.VertexInput", "ZeroStridePackedFormatsAndUnusedBindings")
{
	Testing::VulkanPipelineCapture capture;
	const std::array<Rhi::VertexBindingDesc, 2> bindings{{ { 0, 0 }, { 7, 64 } }};
	for (auto format : { Rhi::Format::RGB10A2Unorm, Rhi::Format::BGR10A2Unorm, Rhi::Format::RGBA16Float,
		Rhi::Format::R8Uint, Rhi::Format::RG16Snorm })
	{
		const Rhi::VertexAttributeDesc attribute{ 0, 0, format, 0 };
		auto pipeline = capture.MakePipeline(Rhi::Format::RGBA8Unorm, bindings, { &attribute, 1 });
		SWIM_REQUIRE(pipeline);
		SWIM_CHECK_EQUAL(pipeline->GetVertexRequirements().size(), 1u);
		SWIM_CHECK_EQUAL(pipeline->GetVertexRequirements()[0].Stride, 0u);
	}
	auto generated = capture.MakePipeline();
	SWIM_REQUIRE(generated);
	SWIM_CHECK(generated->GetVertexRequirements().empty());
	SWIM_CHECK(capture.VertexBindings.empty() && capture.VertexAttributes.empty());
}
