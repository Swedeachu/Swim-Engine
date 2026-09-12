#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDiagnostics.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanGraphicsSystem.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Fixtures/VulkanValidationCapture.h"
#include "Tests/Framework/Test.h"

#include <cstring>

using namespace Swim;

namespace
{
	constexpr RhiVulkan::VulkanValidationCapabilities Available{
		true, true, true, RhiVulkan::MinimumGpuValidationSettingsVersion };
	constexpr std::array<Rhi::ValidationChecks, 3> Requests{{ { true, false }, { false, true }, { true, true } }};
}

SWIM_TEST("RHI.Vulkan.ValidationSettings", "ExplicitChecksRequireSupportInEveryRequestMode")
{
	for (const auto& checks : Requests)
	{
		for (auto mode : { Rhi::ValidationMode::Default, Rhi::ValidationMode::IfAvailable, Rhi::ValidationMode::Required })
		{
			const auto policy = RhiVulkan::SelectDiagnosticsPolicy(mode, false, Available, checks);
			SWIM_CHECK(policy.Valid && policy.Validation && policy.DebugUtils);
			SWIM_CHECK(policy.Checks == checks);
			for (unsigned missing = 0; missing < 4; ++missing)
			{
				auto caps = Available;
				if (missing == 0)
				{
					caps.LayerAvailable = false;
				}
				if (missing == 1)
				{
					caps.DebugUtilsAvailable = false;
				}
				if (missing == 2)
				{
					caps.LayerSettingsAvailable = false;
				}
				if (missing == 3)
				{
					caps.LayerVersion = (checks.GpuAssisted ? RhiVulkan::MinimumGpuValidationSettingsVersion :
						RhiVulkan::MinimumValidationSettingsVersion) - 1;
				}
				const auto failed = RhiVulkan::SelectDiagnosticsPolicy(mode, true, caps, checks);
				SWIM_CHECK(!failed.Valid && !failed.Validation && !failed.Checks.Any());
				SWIM_CHECK(failed.Failure && failed.Failure[0] != '\0');
			}
		}
		SWIM_CHECK(!RhiVulkan::SelectDiagnosticsPolicy(Rhi::ValidationMode::Disabled, true, Available, checks).Valid);
	}
}

SWIM_TEST("RHI.Vulkan.ValidationSettings", "BasicValidationDoesNotRequireModernLayerSettings")
{
	auto old = Available;
	old.LayerVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
	old.LayerSettingsAvailable = false;
	SWIM_CHECK(RhiVulkan::SelectDiagnosticsPolicy(Rhi::ValidationMode::Required, false, old).Validation);
	SWIM_CHECK(RhiVulkan::SelectDiagnosticsPolicy(Rhi::ValidationMode::Default, true, old).Validation);
	SWIM_CHECK(!RhiVulkan::SelectDiagnosticsPolicy(Rhi::ValidationMode::Default, false, old).Validation);
	SWIM_CHECK(!RhiVulkan::SelectDiagnosticsPolicy(Rhi::ValidationMode::IfAvailable, true, {}).Validation);
	SWIM_CHECK(RhiVulkan::SelectDiagnosticsPolicy(Rhi::ValidationMode::IfAvailable, true, {}).Valid);
	SWIM_CHECK(!RhiVulkan::SelectDiagnosticsPolicy(static_cast<Rhi::ValidationMode>(255), true, Available).Valid);
}

SWIM_TEST("RHI.Vulkan.ValidationSettings", "ModernBoolSettingsRetainValuesAcrossOtherConfigurations")
{
	const auto sync = RhiVulkan::GetVulkanValidationSettings({ true, false });
	const auto gpu = RhiVulkan::GetVulkanValidationSettings({ false, true });
	for (const auto& settings : { sync, gpu })
	{
		for (const auto& setting : settings)
		{
			SWIM_CHECK_EQUAL(std::string(setting.pLayerName), std::string("VK_LAYER_KHRONOS_validation"));
			SWIM_CHECK(setting.type == VK_LAYER_SETTING_TYPE_BOOL32_EXT);
			SWIM_CHECK_EQUAL(setting.valueCount, 1u);
			SWIM_REQUIRE(setting.pValues);
		}
		SWIM_CHECK_EQUAL(std::string(settings[0].pSettingName), std::string("validate_sync"));
		SWIM_CHECK_EQUAL(std::string(settings[1].pSettingName), std::string("gpuav_enable"));
		SWIM_CHECK_EQUAL(std::string(settings[2].pSettingName), std::string("syncval_submit_time_validation"));
		SWIM_CHECK_EQUAL(*static_cast<const VkBool32*>(settings[2].pValues), VK_TRUE);
	}
	SWIM_CHECK_EQUAL(*static_cast<const VkBool32*>(sync[0].pValues), VK_TRUE);
	SWIM_CHECK_EQUAL(*static_cast<const VkBool32*>(sync[1].pValues), VK_FALSE);
	SWIM_CHECK_EQUAL(*static_cast<const VkBool32*>(gpu[0].pValues), VK_FALSE);
	SWIM_CHECK_EQUAL(*static_cast<const VkBool32*>(gpu[1].pValues), VK_TRUE);
}

SWIM_TEST("RHI.Vulkan.ValidationSettings", "OnlyGpuAssistedSelectionAddsShaderAtomicRequirements")
{
	for (const auto& checks : { Rhi::ValidationChecks{}, { true, false }, { false, true }, { true, true } })
	{
		const auto features = RhiVulkan::GetValidationDeviceFeatures(checks);
		SWIM_CHECK_EQUAL(features.fragmentStoresAndAtomics != VK_FALSE, checks.GpuAssisted);
		SWIM_CHECK_EQUAL(features.vertexPipelineStoresAndAtomics != VK_FALSE, checks.GpuAssisted);
		SWIM_CHECK_EQUAL(features.robustBufferAccess, VK_FALSE);
		SWIM_CHECK_EQUAL(features.shaderInt64 != VK_FALSE, checks.GpuAssisted);
		SWIM_CHECK_EQUAL(features.shaderInt16 != VK_FALSE, checks.GpuAssisted);
		const auto features11 = RhiVulkan::GetValidationVulkan11Features(checks);
		const auto features12 = RhiVulkan::GetValidationVulkan12Features(checks);
		SWIM_CHECK_EQUAL(features11.storageBuffer16BitAccess != VK_FALSE, checks.GpuAssisted);
		SWIM_CHECK_EQUAL(features12.storageBuffer8BitAccess != VK_FALSE, checks.GpuAssisted);
		SWIM_CHECK_EQUAL(features12.shaderInt8 != VK_FALSE, checks.GpuAssisted);
		SWIM_CHECK_EQUAL(features12.scalarBlockLayout != VK_FALSE, checks.GpuAssisted);
		SWIM_CHECK_EQUAL(features12.vulkanMemoryModel != VK_FALSE, checks.GpuAssisted);
		SWIM_CHECK_EQUAL(features12.vulkanMemoryModelDeviceScope != VK_FALSE, checks.GpuAssisted);
	}
}

SWIM_TEST("RHI.Vulkan.ValidationSettings", "GpuPassConfiguresOnlyEnabledApisAndKeepsCoreAsASeparatePass")
{
	for (auto version : { RhiVulkan::MinimumGpuValidationSettingsVersion, VK_MAKE_API_VERSION(0, 1, 4, 357) })
	{
		for (const auto& checks : { Rhi::ValidationChecks{}, { true, false }, { false, true }, { true, true } })
		{
			const auto settings = RhiVulkan::GetVulkanValidationSettings(checks, version);
			SWIM_REQUIRE(settings.size() >= 4);
			SWIM_CHECK_EQUAL(std::string(settings[3].pSettingName), std::string("validate_core"));
			SWIM_CHECK_EQUAL(*static_cast<const VkBool32*>(settings[3].pValues) != VK_FALSE, !checks.GpuAssisted);
			const auto expected = checks.GpuAssisted ? (version < VK_MAKE_API_VERSION(0, 1, 4, 357) ? 7u : 6u) : 4u;
			SWIM_REQUIRE_EQUAL(settings.size(), expected);
			for (std::size_t index = 4; index < settings.size(); ++index)
			{
				const std::string_view name(settings[index].pSettingName);
				SWIM_CHECK(name == "gpuav_validate_trace_ray" || name == "gpuav_mesh_shading" || name == "gpuav_validate_ray_query");
				SWIM_CHECK_EQUAL(*static_cast<const VkBool32*>(settings[index].pValues), VK_FALSE);
			}
		}
	}
}

SWIM_TEST("RHI.Vulkan.ValidationSettings", "OlderSettingsLayersStillSupportSyncButRejectGpuSetup")
{
	for (auto version : { RhiVulkan::MinimumValidationSettingsVersion, VK_MAKE_API_VERSION(0, 1, 4, 341) })
	{
		auto caps = Available;
		caps.LayerVersion = version;
		SWIM_CHECK(RhiVulkan::SelectDiagnosticsPolicy(Rhi::ValidationMode::Required, false, caps, { true, false }).Valid);
		SWIM_CHECK(!RhiVulkan::SelectDiagnosticsPolicy(Rhi::ValidationMode::Required, false, caps, { false, true }).Valid);
	}
}

SWIM_TEST("RHI.Vulkan.ValidationSettings", "SmokeAllowsOnlyTheReviewedGpuDescriptorLimitAdvisory")
{
	Rhi::DiagnosticLog log;
	const std::string advisory = "vkGetPhysicalDeviceProperties2(): Warning that validation is adjusting settings:\n"
		"\tSetting VkPhysicalDeviceDescriptorIndexingProperties::maxUpdateAfterBindDescriptorsInAllPools to 4194304\n";
	log.Record(Rhi::DiagnosticSeverity::Warning, "WARNING-Setting-Limit-Adjusted", advisory);
	const auto snapshot = log.Snapshot();
	SWIM_CHECK(!snapshot.IsClean()); // The original warning remains in the report.
	SWIM_CHECK(Testing::HasCleanVulkanSmokeDiagnostics(snapshot, { false, true }));
	SWIM_CHECK(!Testing::HasCleanVulkanSmokeDiagnostics(snapshot, {}));
	SWIM_CHECK(!Testing::HasCleanVulkanSmokeDiagnostics(snapshot, { true, false }));
	for (unsigned mutation = 0; mutation < 6; ++mutation)
	{
		auto invalid = snapshot;
		if (mutation == 0) invalid.Messages[0].Text += "Disabling shader instrumentation";
		if (mutation == 1) invalid.Messages[0].Id = "VUID-Test";
		if (mutation == 2) invalid.Messages[0].Text = "GPU validation is disabled";
		if (mutation == 3) ++invalid.Warnings;
		if (mutation == 4) ++invalid.Errors;
		if (mutation == 5) ++invalid.Dropped;
		SWIM_CHECK(!Testing::HasCleanVulkanSmokeDiagnostics(invalid, { false, true }));
	}
}

SWIM_TEST("RHI.Vulkan.ValidationSettings", "InstanceSetupAndPublicReportingPreserveExactChecks")
{
	Testing::VulkanValidationCapture capture;
	for (const auto& checks : Requests)
	{
		auto instance = std::make_shared<RhiVulkan::VulkanInstanceState>();
		instance->Diagnostics.Log = std::make_shared<Rhi::DiagnosticLog>();
		Rhi::GraphicsSystemDesc desc{};
		desc.Validation = Rhi::ValidationMode::IfAvailable;
		desc.Checks = checks;
		vkb::InstanceBuilder builder{ &Testing::VulkanValidationCapture::GetInstanceProcAddress };
		SWIM_REQUIRE(RhiVulkan::ConfigureInstanceDiagnostics(builder, instance->Diagnostics, desc,
			&Testing::VulkanValidationCapture::GetInstanceProcAddress));
		RhiVulkan::VulkanGraphicsSystem graphics(instance, {});
		SWIM_CHECK(graphics.IsValidationEnabled());
		const auto configuration = graphics.GetValidationConfiguration();
		SWIM_CHECK(configuration.Enabled && configuration.Checks == checks);
		const auto snapshot = graphics.GetDiagnostics()->Snapshot();
		SWIM_CHECK(snapshot.IsClean());
		SWIM_REQUIRE_EQUAL(snapshot.Messages.size(), 1u);
		SWIM_CHECK(snapshot.Messages[0].Text.find("layer API=1.4.350") != std::string::npos);
		SWIM_CHECK(snapshot.Messages[0].Text.find(checks.GpuAssisted ? "gpu-assisted=on" : "gpu-assisted=off") != std::string::npos);
	}
}

SWIM_TEST("RHI.Vulkan.ValidationSettings", "SetupFailureReportsReasonWithoutConfiguredChecks")
{
	Testing::VulkanValidationCapture capture;
	capture.ValidationExtensions.clear();
	RhiVulkan::VulkanDiagnosticsState state{ std::make_shared<Rhi::DiagnosticLog>(), false };
	Rhi::GraphicsSystemDesc desc{};
	desc.Checks = { true, true };
	vkb::InstanceBuilder builder{ &Testing::VulkanValidationCapture::GetInstanceProcAddress };
	SWIM_CHECK(!RhiVulkan::ConfigureInstanceDiagnostics(builder, state, desc,
		&Testing::VulkanValidationCapture::GetInstanceProcAddress));
	SWIM_CHECK(!state.ValidationEnabled && !state.Checks.Any());
	const auto snapshot = state.Log->Snapshot();
	SWIM_CHECK_EQUAL(snapshot.Errors, 1u);
	SWIM_CHECK(snapshot.Messages[0].Text.find("VK_EXT_layer_settings") != std::string::npos);
}

SWIM_TEST("RHI.Vulkan.ValidationSettings", "SmokeProfilesAreExplicitAndRejectTypos")
{
	SWIM_CHECK(!Testing::ParseVulkanSmokeChecks("").Any());
	SWIM_CHECK(!Testing::ParseVulkanSmokeChecks("core").Any());
	SWIM_CHECK(Testing::ParseVulkanSmokeChecks("sync") == Rhi::ValidationChecks(true, false));
	SWIM_CHECK(Testing::ParseVulkanSmokeChecks("gpu") == Rhi::ValidationChecks(false, true));
	SWIM_CHECK(Testing::ParseVulkanSmokeChecks("all") == Rhi::ValidationChecks(true, true));
	SWIM_CHECK_THROWS(Testing::ParseVulkanSmokeChecks("snyc"), std::invalid_argument);
}

SWIM_TEST("RHI.Vulkan.ValidationSettings", "LayerProvidedDebugUtilsRequiresEnablingItsProvider")
{
	auto caps = Available;
	caps.DebugUtilsAvailable = false;
	caps.LayerDebugUtilsAvailable = true;
	const auto enabled = RhiVulkan::SelectDiagnosticsPolicy(Rhi::ValidationMode::Required, false, caps, { true, true });
	SWIM_CHECK(enabled.Valid && enabled.Validation && enabled.DebugUtils);
	const auto disabled = RhiVulkan::SelectDiagnosticsPolicy(Rhi::ValidationMode::Disabled, true, caps);
	SWIM_CHECK(disabled.Valid && !disabled.Validation && !disabled.DebugUtils);
	const auto release = RhiVulkan::SelectDiagnosticsPolicy(Rhi::ValidationMode::Default, false, caps);
	SWIM_CHECK(release.Valid && !release.Validation && !release.DebugUtils);
}
