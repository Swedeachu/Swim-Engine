#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanAdapterInfo.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDiagnostics.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"
#include "Tests/Fixtures/VulkanDescriptorCapture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <cstring>
#include <limits>

using namespace Swim;

namespace
{

	struct DebugCapture;
	DebugCapture* current = nullptr;

	struct DebugCapture : Testing::VulkanDescriptorCapture
	{
		struct Name
		{
			VkObjectType Type;
			std::uint64_t Handle;
			std::string Text;
		};
		std::vector<Name> Names;
		std::vector<std::string> Labels;
		std::array<float, 4> Color{};
		VkResult NameResult = VK_SUCCESS;

		DebugCapture()
		{
			current = this;
			State->Instance->Diagnostics.DebugUtilsEnabled = true;
			State->Instance->Diagnostics.Echo = false;
			State->Instance->Diagnostics.Log = std::make_shared<Rhi::DiagnosticLog>();
			State->Instance->Dispatch.vkSetDebugUtilsObjectNameEXT = +[](VkDevice, const VkDebugUtilsObjectNameInfoEXT* info) -> VkResult
			{
				current->Names.push_back({ info->objectType, info->objectHandle, info->pObjectName });
				return current->NameResult;
			};
			State->Instance->Dispatch.vkCmdBeginDebugUtilsLabelEXT = +[](VkCommandBuffer, const VkDebugUtilsLabelEXT* info)
			{
				current->Labels.push_back(std::string("begin:") + info->pLabelName);
				std::copy(std::begin(info->color), std::end(info->color), current->Color.begin());
			};
			State->Instance->Dispatch.vkCmdEndDebugUtilsLabelEXT = +[](VkCommandBuffer)
			{
				current->Labels.push_back("end");
			};
			State->Instance->Dispatch.vkCmdInsertDebugUtilsLabelEXT = +[](VkCommandBuffer, const VkDebugUtilsLabelEXT* info)
			{
				current->Labels.push_back(std::string("insert:") + info->pLabelName);
			};
		}

		bool HasName(VkObjectType type, std::string_view name) const
		{
			return std::any_of(Names.begin(), Names.end(), [&](const auto& value) { return value.Type == type && value.Text == name; });
		}
	};

} // namespace

SWIM_TEST("RHI.Vulkan.Diagnostics", "RequiredValidationNeverFallsBackToUnvalidatedExecution")
{
	using Rhi::ValidationMode;
	for (bool layers : { false, true })
	{
		for (bool debugUtils : { false, true })
		{
			const auto policy = RhiVulkan::SelectDiagnosticsPolicy(ValidationMode::Required, false, layers, debugUtils);
			SWIM_CHECK_EQUAL(policy.Valid, layers && debugUtils);
			SWIM_CHECK_EQUAL(policy.Validation, layers && debugUtils);
		}
	}
	SWIM_CHECK(RhiVulkan::SelectDiagnosticsPolicy(ValidationMode::Default, true, true, true).Validation);
	SWIM_CHECK(!RhiVulkan::SelectDiagnosticsPolicy(ValidationMode::Default, false, true, true).Validation);
	SWIM_CHECK(!RhiVulkan::SelectDiagnosticsPolicy(ValidationMode::Disabled, true, true, true).Validation);
	SWIM_CHECK(RhiVulkan::SelectDiagnosticsPolicy(ValidationMode::Disabled, true, true, true).DebugUtils);
	SWIM_CHECK(RhiVulkan::SelectDiagnosticsPolicy(ValidationMode::IfAvailable, false, false, false).Valid);
	SWIM_CHECK(!RhiVulkan::SelectDiagnosticsPolicy(static_cast<ValidationMode>(255), true, true, true).Valid);
}

SWIM_TEST("RHI.Vulkan.Diagnostics", "CallbackCopiesDriverMemoryAndSurvivesOwnerTeardown")
{
	auto log = std::make_shared<Rhi::DiagnosticLog>();
	{
		RhiVulkan::VulkanDiagnosticsState state{ log, false };
		char text[] = "driver message";
		VkDebugUtilsMessengerCallbackDataEXT data{};
		data.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT;
		data.pMessageIdName = "VUID-test";
		data.pMessage = text;
		SWIM_CHECK_EQUAL(RhiVulkan::VulkanDiagnosticCallback(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, &data, &state), VK_FALSE);
		text[0] = 'X';
		SWIM_CHECK_EQUAL(RhiVulkan::VulkanDiagnosticCallback(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
			0, nullptr, &state), VK_FALSE);
		SWIM_CHECK_EQUAL(RhiVulkan::VulkanDiagnosticCallback(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
			0, &data, nullptr), VK_FALSE);
	}
	const auto snapshot = log->Snapshot();
	SWIM_REQUIRE_EQUAL(snapshot.Messages.size(), 2u);
	SWIM_CHECK_EQUAL(snapshot.Messages[0].Text, std::string("driver message"));
	SWIM_CHECK_EQUAL(snapshot.Messages[0].Id, std::string("VUID-test"));
	SWIM_CHECK_EQUAL(snapshot.Errors, 1u);
	SWIM_CHECK_EQUAL(snapshot.Warnings, 1u);
}

SWIM_TEST("RHI.Vulkan.Diagnostics", "ObjectNamesRespectExtensionAvailabilityAndStringViewBounds")
{
	DebugCapture capture;
	capture.State->Instance->Diagnostics.DebugUtilsEnabled = false;
	RhiVulkan::SetVulkanObjectName(*capture.State, VK_OBJECT_TYPE_BUFFER, 99, "disabled");
	SWIM_CHECK(capture.Names.empty());
	capture.State->Instance->Diagnostics.DebugUtilsEnabled = true;
	RhiVulkan::SetVulkanObjectName(*capture.State, VK_OBJECT_TYPE_BUFFER, 0, "null");
	RhiVulkan::SetVulkanObjectName(*capture.State, VK_OBJECT_TYPE_BUFFER, 99, {});
	SWIM_CHECK(capture.Names.empty());
	RhiVulkan::SetVulkanObjectName(*capture.State, VK_OBJECT_TYPE_BUFFER, 99, std::string_view("name-extra", 4));
	SWIM_REQUIRE_EQUAL(capture.Names.size(), 1u);
	SWIM_CHECK_EQUAL(capture.Names[0].Text, std::string("name"));
	SWIM_CHECK_EQUAL(capture.Names[0].Handle, 99u);
	capture.NameResult = VK_ERROR_OUT_OF_HOST_MEMORY;
	RhiVulkan::SetVulkanObjectName(*capture.State, VK_OBJECT_TYPE_IMAGE, 123, "failed");
	SWIM_CHECK_EQUAL(capture.State->Instance->Diagnostics.Log->Snapshot().Warnings, 1u);
	capture.State->Instance->Dispatch.vkSetDebugUtilsObjectNameEXT = nullptr;
	RhiVulkan::SetVulkanObjectName(*capture.State, VK_OBJECT_TYPE_BUFFER, 99, "unsupported dispatch");
	SWIM_CHECK_EQUAL(capture.Names.size(), 2u);
}

SWIM_TEST("RHI.Vulkan.Diagnostics", "ResourceAndPipelineCreationForwardOwnedDebugNames")
{
	DebugCapture capture;
	RhiVulkan::VulkanBuffer buffer(capture.State, RhiVulkan::FromNativeHandle<VkBuffer>(50), nullptr,
		{ 64, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, "upload" });
	Rhi::TextureDesc textureDesc{};
	textureDesc.DebugName = "color";
	RhiVulkan::VulkanTexture texture(capture.State, RhiVulkan::FromNativeHandle<VkImage>(51), textureDesc);
	Rhi::TextureViewDesc viewDesc{};
	viewDesc.DebugName = "color view";
	RhiVulkan::VulkanTextureView view(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(52), viewDesc);
	Rhi::SamplerDesc samplerDesc{};
	samplerDesc.DebugName = "nearest";
	auto sampler = RhiVulkan::VulkanSampler::Create(capture.State, samplerDesc);
	SWIM_REQUIRE(sampler);
	SWIM_CHECK(capture.HasName(VK_OBJECT_TYPE_BUFFER, "upload"));
	SWIM_CHECK(capture.HasName(VK_OBJECT_TYPE_IMAGE, "color"));
	SWIM_CHECK(capture.HasName(VK_OBJECT_TYPE_IMAGE_VIEW, "color view"));
	SWIM_CHECK(capture.HasName(VK_OBJECT_TYPE_SAMPLER, "nearest"));

	const std::array<std::uint32_t, 5> words{ 0x07230203, 0x00010500, 0, 1, 0 };
	const auto bytes = std::as_bytes(std::span(words));
	const std::array<Rhi::ShaderStageArtifact, 2> stages{{
		{ Rhi::ShaderStageMask::Vertex, "vertexMain", bytes },
		{ Rhi::ShaderStageMask::Fragment, "fragmentMain", bytes }
	}};
	Rhi::DescriptorSchemaDesc schema{ 0, { { 0, Rhi::DescriptorType::Sampler, 1, Rhi::ShaderStageMask::Fragment } } };
	auto program = RhiVulkan::VulkanShaderProgram::Create(capture.State, { stages, { { &schema, 1 }, {} }, "program" });
	SWIM_REQUIRE(program);
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), "layout" });
	SWIM_REQUIRE(layout);
	auto table = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 0, 0, "table" });
	SWIM_REQUIRE(table);
	const Rhi::Format format = Rhi::Format::RGBA8Unorm;
	Rhi::GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.Program = program.get();
	pipelineDesc.Layout = layout.get();
	pipelineDesc.ColorFormats = { &format, 1 };
	pipelineDesc.DepthStencil.DepthTest = pipelineDesc.DepthStencil.DepthWrite = false;
	pipelineDesc.DebugName = "pipeline";
	auto pipeline = RhiVulkan::VulkanGraphicsPipeline::Create(capture.State, pipelineDesc);
	SWIM_REQUIRE(pipeline);
	SWIM_CHECK(capture.HasName(VK_OBJECT_TYPE_SHADER_MODULE, "program"));
	SWIM_CHECK(capture.HasName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, "layout"));
	SWIM_CHECK(capture.HasName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "layout"));
	SWIM_CHECK(capture.HasName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, "table"));
	SWIM_CHECK(capture.HasName(VK_OBJECT_TYPE_DESCRIPTOR_SET, "table"));
	SWIM_CHECK(capture.HasName(VK_OBJECT_TYPE_PIPELINE, "pipeline"));
}

SWIM_TEST("RHI.Vulkan.Diagnostics", "NestedLabelsValidateRecordingBalanceAndColorBeforeDispatch")
{
	DebugCapture capture;
	auto& commands = *capture.Commands;
	SWIM_CHECK_THROWS(commands.BeginDebugLabel("outside"), std::logic_error);
	commands.Begin();
	SWIM_CHECK_THROWS(commands.EndDebugLabel(), std::logic_error);
	SWIM_CHECK_THROWS(commands.BeginDebugLabel(""), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.InsertDebugLabel("invalid", { 0, 0, 0, std::numeric_limits<float>::quiet_NaN() }), std::invalid_argument);
	SWIM_CHECK(capture.Labels.empty());
	commands.BeginDebugLabel(std::string_view("outer-extra", 5), { 0.2f, 0.4f, 0.6f, 1.0f });
	SWIM_CHECK_EQUAL(capture.Color[1], 0.4f);
	commands.BeginDebugLabel("inner");
	commands.InsertDebugLabel("draw");
	SWIM_CHECK_THROWS(commands.End(), std::logic_error);
	commands.EndDebugLabel();
	commands.EndDebugLabel();
	commands.End();
	SWIM_CHECK(capture.Labels == std::vector<std::string>({ "begin:outer", "begin:inner", "insert:draw", "end", "end" }));
	SWIM_CHECK_THROWS(commands.InsertDebugLabel("after"), std::logic_error);
}

SWIM_TEST("RHI.Vulkan.Diagnostics", "LabelsRemainBalancedWithoutDebugUtilsAndResetWithPoolGeneration")
{
	DebugCapture capture;
	capture.State->Instance->Diagnostics.DebugUtilsEnabled = false;
	auto& commands = *capture.Commands;
	commands.Begin();
	commands.BeginDebugLabel("optional");
	commands.InsertDebugLabel("optional marker");
	SWIM_CHECK_THROWS(commands.End(), std::logic_error);
	commands.EndDebugLabel();
	commands.End();
	SWIM_CHECK(capture.Labels.empty());
	++capture.Pool->Generation;
	commands.Begin();
	commands.BeginDebugLabel("discarded recording");
	++capture.Pool->Generation;
	SWIM_CHECK_THROWS(commands.EndDebugLabel(), std::logic_error);
	commands.Begin();
	SWIM_CHECK_THROWS(commands.EndDebugLabel(), std::logic_error);
	commands.End();
	SWIM_CHECK(capture.Labels.empty());
}

SWIM_TEST("RHI.Vulkan.Diagnostics", "AdapterReportsDriverPropertiesWithoutAssumingDriverVersionEncoding")
{
	volk::VolkInstanceTable dispatch{};
	dispatch.vkGetPhysicalDeviceFeatures2 = +[](VkPhysicalDevice, VkPhysicalDeviceFeatures2*) {};
	dispatch.vkGetPhysicalDeviceProperties2 = +[](VkPhysicalDevice, VkPhysicalDeviceProperties2* props)
	{
		std::strcpy(props->properties.deviceName, "Test GPU");
		props->properties.apiVersion = VK_MAKE_API_VERSION(0, 1, 3, 350);
		props->properties.driverVersion = 0x12345678;
		props->properties.vendorID = 0x1234;
		props->properties.deviceID = 0x5678;
		for (auto* item = static_cast<VkBaseOutStructure*>(props->pNext); item; item = item->pNext)
		{
			if (item->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES)
			{
				auto* driver = reinterpret_cast<VkPhysicalDeviceDriverProperties*>(item);
				std::strcpy(driver->driverName, "Test driver");
				std::strcpy(driver->driverInfo, "Test driver build");
			}
		}
	};
	dispatch.vkGetPhysicalDeviceMemoryProperties = +[](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*) {};
	const auto info = RhiVulkan::BuildAdapterInfo(dispatch, {});
	SWIM_CHECK_EQUAL(info.Name, std::string("Test GPU"));
	SWIM_CHECK_EQUAL(info.DriverName, std::string("Test driver"));
	SWIM_CHECK_EQUAL(info.DriverInfo, std::string("Test driver build"));
	SWIM_CHECK_EQUAL(info.ApiVersion, std::string("1.3.350"));
	SWIM_CHECK_EQUAL(info.DriverVersion, 0x12345678u);
	RhiVulkan::VulkanDiagnosticsState state{ std::make_shared<Rhi::DiagnosticLog>(), false };
	RhiVulkan::ReportAdapterInfo(state, info);
	SWIM_CHECK(state.Log->Snapshot().IsClean());
	SWIM_CHECK(state.Log->Snapshot().Messages.front().Text.find("Test driver build") != std::string::npos);
}
