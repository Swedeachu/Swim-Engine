#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanSwapchainColor.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanTransferUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <array>
#include <stdexcept>

using namespace Swim;

namespace
{
	constexpr VkSurfaceFormatKHR Sdr{ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
	constexpr VkSurfaceFormatKHR Pq{ VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT };
	constexpr VkSurfaceFormatKHR BgrPq{ VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT };
	constexpr VkSurfaceFormatKHR Linear{ VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT };

	Rhi::SwapchainDesc HdrDesc(Rhi::SwapchainColorMode mode = Rhi::SwapchainColorMode::RequireHdr)
	{
		Rhi::SwapchainDesc desc{};
		desc.ColorMode = mode;
		return desc;
	}

	struct SurfaceCapture;
	SurfaceCapture* active = nullptr;

	struct SurfaceCapture
	{
		RhiVulkan::VulkanDeviceState State;
		std::vector<VkSurfaceFormatKHR> Formats{ Sdr, Pq, Linear };
		VkResult SupportResult = VK_SUCCESS;
		VkResult CountResult = VK_SUCCESS;
		VkResult DataResult = VK_SUCCESS;
		VkBool32 Supported = VK_TRUE;
		unsigned SupportCalls = 0;
		unsigned CountCalls = 0;
		unsigned DataCalls = 0;
		unsigned IncompleteDataCalls = 0;
		std::uint32_t CountOverride = 0;
		bool Shrink = false;
		bool WrongSurface = false;
		VkSurfaceKHR Surface = RhiVulkan::FromNativeHandle<VkSurfaceKHR>(97);

		SurfaceCapture()
		{
			active = this;
			State.Instance = std::make_shared<RhiVulkan::VulkanInstanceState>();
			State.Instance->SwapchainColorSpaceEnabled = true;
			State.QueueFamilies.Graphics = 3;
			State.Instance->Dispatch.vkGetPhysicalDeviceSurfaceSupportKHR = +[](VkPhysicalDevice,
				std::uint32_t family, VkSurfaceKHR surface, VkBool32* supported) -> VkResult
			{
				++active->SupportCalls;
				active->WrongSurface |= surface != active->Surface || family != 3;
				*supported = active->Supported;
				return active->SupportResult;
			};
			State.Instance->Dispatch.vkGetPhysicalDeviceSurfaceFormatsKHR = +[](VkPhysicalDevice,
				VkSurfaceKHR surface, std::uint32_t* count, VkSurfaceFormatKHR* formats) -> VkResult
			{
				active->WrongSurface |= surface != active->Surface;
				if (!formats)
				{
					++active->CountCalls;
					*count = active->CountOverride ? active->CountOverride : static_cast<std::uint32_t>(active->Formats.size());
					return active->CountResult;
				}
				++active->DataCalls;
				if (active->DataCalls <= active->IncompleteDataCalls)
				{
					return VK_INCOMPLETE;
				}
				const auto size = active->Shrink ? 1u : static_cast<std::uint32_t>(active->Formats.size());
				*count = std::min(*count, size);
				std::copy_n(active->Formats.data(), *count, formats);
				return active->DataResult;
			};
		}

		Rhi::SwapchainSupport Query()
		{
			return RhiVulkan::QueryVulkanSwapchainSupport(State, Surface);
		}
	};
}

SWIM_TEST("RHI.Vulkan.SwapchainColor", "SdrDefaultNeverSelectsAdvertisedHdr")
{
	const auto support = RhiVulkan::BuildSwapchainSupport(std::array{ Pq, Linear, Sdr }, true);
	SWIM_CHECK(support.SupportsHdr());
	const auto selected = RhiVulkan::SelectSwapchainFormat(support, {});
	SWIM_REQUIRE(selected);
	SWIM_CHECK(!selected->IsHdr());
	SWIM_CHECK(selected->PixelFormat == Rhi::Format::BGRA8UnormSrgb);
	SWIM_CHECK(RhiVulkan::MatchesSwapchainFormat(*selected, Sdr.format, Sdr.colorSpace));
}

SWIM_TEST("RHI.Vulkan.SwapchainColor", "HdrPreferenceIsStableAndHonorsMatchingFormat")
{
	const auto support = RhiVulkan::BuildSwapchainSupport(std::array{ Linear, BgrPq, Sdr, Pq, Pq }, true);
	SWIM_CHECK_EQUAL(support.Formats.size(), 4u);
	auto desc = HdrDesc();
	auto selected = RhiVulkan::SelectSwapchainFormat(support, desc);
	SWIM_REQUIRE(selected);
	SWIM_CHECK(RhiVulkan::MatchesSwapchainFormat(*selected, Pq.format, Pq.colorSpace));
	desc.PreferredFormat = Rhi::Format::RGBA16Float;
	selected = RhiVulkan::SelectSwapchainFormat(support, desc);
	SWIM_REQUIRE(selected);
	SWIM_CHECK(RhiVulkan::MatchesSwapchainFormat(*selected, Linear.format, Linear.colorSpace));
	desc.PreferredFormat = Rhi::Format::BGR10A2Unorm;
	selected = RhiVulkan::SelectSwapchainFormat(support, desc);
	SWIM_REQUIRE(selected);
	SWIM_CHECK(RhiVulkan::MatchesSwapchainFormat(*selected, BgrPq.format, BgrPq.colorSpace));
}

SWIM_TEST("RHI.Vulkan.SwapchainColor", "StrictHdrRejectsSdrWhilePreferenceFallsBack")
{
	const auto support = RhiVulkan::BuildSwapchainSupport(std::array{ Sdr }, true);
	SWIM_CHECK(!support.SupportsHdr());
	SWIM_CHECK(!RhiVulkan::SelectSwapchainFormat(support, HdrDesc()));
	const auto fallback = RhiVulkan::SelectSwapchainFormat(support, HdrDesc(Rhi::SwapchainColorMode::PreferHdr));
	SWIM_REQUIRE(fallback);
	SWIM_CHECK(!fallback->IsHdr());
}

SWIM_TEST("RHI.Vulkan.SwapchainColor", "HdrRequiresEnabledInstanceExtensionAndExactPair")
{
	const auto disabled = RhiVulkan::BuildSwapchainSupport(std::array{ Pq, Linear, Sdr }, false);
	SWIM_CHECK(!disabled.SupportsHdr());
	SWIM_CHECK_EQUAL(disabled.Formats.size(), 1u);
	SWIM_CHECK(!RhiVulkan::SelectSwapchainFormat(disabled, HdrDesc()));
	const std::array invalid{
		VkSurfaceFormatKHR{ VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
		VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_HDR10_ST2084_EXT },
		VkSurfaceFormatKHR{ VK_FORMAT_A2B10G10R10_UINT_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT },
		VkSurfaceFormatKHR{ VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_HDR10_ST2084_EXT },
		VkSurfaceFormatKHR{ VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT },
	};
	const auto rejected = RhiVulkan::BuildSwapchainSupport(invalid, true);
	SWIM_CHECK(rejected.Formats.empty());
	SWIM_CHECK(!rejected.SupportsHdr());
	SWIM_CHECK(!RhiVulkan::SelectSwapchainFormat(rejected, HdrDesc()));
}

SWIM_TEST("RHI.Vulkan.SwapchainColor", "LinearOnlyHdrAndUnormOnlySdrRemainUsable")
{
	const auto linear = RhiVulkan::SelectSwapchainFormat(
		RhiVulkan::BuildSwapchainSupport(std::array{ Linear }, true), HdrDesc());
	SWIM_REQUIRE(linear);
	SWIM_CHECK(linear->ColorSpace == Rhi::SwapchainColorSpace::ExtendedSrgbLinear);
	SWIM_CHECK(!RhiVulkan::SelectSwapchainFormat(RhiVulkan::BuildSwapchainSupport(std::array{ Linear }, true), {}));
	const std::array unorm{ VkSurfaceFormatKHR{ VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };
	const auto sdr = RhiVulkan::SelectSwapchainFormat(RhiVulkan::BuildSwapchainSupport(unorm, false), {});
	SWIM_REQUIRE(sdr);
	SWIM_CHECK(sdr->PixelFormat == Rhi::Format::RGBA8Unorm);
}

SWIM_TEST("RHI.Vulkan.SwapchainColor", "NativeFallbackMustMatchBothFormatAndColorSpace")
{
	const Rhi::SwapchainSurfaceFormat selected{ Rhi::Format::RGB10A2Unorm, Rhi::SwapchainColorSpace::Hdr10St2084 };
	SWIM_CHECK(RhiVulkan::MatchesSwapchainFormat(selected, Pq.format, Pq.colorSpace));
	SWIM_CHECK(!RhiVulkan::MatchesSwapchainFormat(selected, Pq.format, Sdr.colorSpace));
	SWIM_CHECK(!RhiVulkan::MatchesSwapchainFormat(selected, BgrPq.format, Pq.colorSpace));
	SWIM_CHECK(!RhiVulkan::MatchesSwapchainFormat(selected, Sdr.format, Sdr.colorSpace));
	SWIM_CHECK_THROWS(RhiVulkan::ToVkSurfaceFormat({}), std::invalid_argument);
	SWIM_CHECK(RhiVulkan::FromVkFormat(BgrPq.format) == Rhi::Format::BGR10A2Unorm);
	SWIM_CHECK_EQUAL(RhiVulkan::ToVkFormat(Rhi::Format::BGR10A2Unorm), BgrPq.format);
	SWIM_CHECK_EQUAL(RhiVulkan::GetColorTexelBytes(Rhi::Format::BGR10A2Unorm), 4u);
	SWIM_CHECK(!RhiVulkan::IsIntegerColorFormat(Rhi::Format::BGR10A2Unorm));
}

SWIM_TEST("RHI.Vulkan.SwapchainColor", "InvalidModeAndUnsupportedPresentation")
{
	SWIM_CHECK(!RhiVulkan::SelectSwapchainFormat({}, {}));
	SWIM_CHECK(!Rhi::SwapchainSupport{}.SupportsHdr());
	auto desc = HdrDesc(static_cast<Rhi::SwapchainColorMode>(255));
	SWIM_CHECK_THROWS(RhiVulkan::SelectSwapchainFormat({}, desc), std::invalid_argument);
	SurfaceCapture capture;
	capture.Supported = VK_FALSE;
	const auto support = capture.Query();
	SWIM_CHECK(!support.PresentationSupported && support.Formats.empty());
	SWIM_CHECK_EQUAL(capture.CountCalls, 0u);
}

SWIM_TEST("RHI.Vulkan.SwapchainColor", "QueriesUseWindowSurfaceAndReflectDisplayChanges")
{
	SurfaceCapture capture;
	const auto before = capture.Query();
	SWIM_CHECK(before.SupportsHdr());
	capture.Formats = { Sdr };
	const auto after = capture.Query();
	SWIM_CHECK(!after.SupportsHdr());
	SWIM_CHECK(before.SupportsHdr());
	SWIM_CHECK(!RhiVulkan::SelectSwapchainFormat(after, HdrDesc()));
	const auto selected = RhiVulkan::SelectSwapchainFormat(after, HdrDesc(Rhi::SwapchainColorMode::PreferHdr));
	SWIM_REQUIRE(selected);
	SWIM_CHECK(!selected->IsHdr());
	SWIM_CHECK(!capture.WrongSurface);
	SWIM_CHECK_EQUAL(capture.SupportCalls, 2u);
}

SWIM_TEST("RHI.Vulkan.SwapchainColor", "IncompleteEnumerationRetriesAndShrinkingListDiscardsTail")
{
	SurfaceCapture capture;
	capture.IncompleteDataCalls = 2;
	SWIM_CHECK(capture.Query().SupportsHdr());
	SWIM_CHECK_EQUAL(capture.CountCalls, 3u);
	SWIM_CHECK_EQUAL(capture.DataCalls, 3u);
	capture.Shrink = true;
	const auto support = capture.Query();
	SWIM_CHECK_EQUAL(support.Formats.size(), 1u);
	SWIM_CHECK(!support.SupportsHdr());
}

SWIM_TEST("RHI.Vulkan.SwapchainColor", "UnstableEnumerationAndAllocationAreBounded")
{
	SurfaceCapture capture;
	capture.IncompleteDataCalls = 100;
	SWIM_CHECK_THROWS(capture.Query(), std::runtime_error);
	SWIM_CHECK_EQUAL(capture.DataCalls, 4u);
	capture.CountResult = VK_INCOMPLETE;
	SWIM_CHECK_THROWS(capture.Query(), std::runtime_error);
	SWIM_CHECK_EQUAL(capture.CountCalls, 8u);
	SWIM_CHECK_EQUAL(capture.DataCalls, 4u);
	capture.CountResult = VK_SUCCESS;
	capture.CountOverride = 4097;
	SWIM_CHECK_THROWS(capture.Query(), std::runtime_error);
	SWIM_CHECK_EQUAL(capture.DataCalls, 4u);
}

SWIM_TEST("RHI.Vulkan.SwapchainColor", "EmptyEnumerationAndExtensionDisabledSnapshot")
{
	SurfaceCapture capture;
	capture.State.Instance->SwapchainColorSpaceEnabled = false;
	SWIM_CHECK(!capture.Query().SupportsHdr());
	capture.Formats.clear();
	const auto empty = capture.Query();
	SWIM_CHECK(empty.PresentationSupported && empty.Formats.empty());
	SWIM_CHECK_EQUAL(capture.DataCalls, 1u);
}

SWIM_TEST("RHI.Vulkan.SwapchainColor", "NativeErrorsAreNotReportedAsUnsupportedHdr")
{
	SurfaceCapture capture;
	capture.SupportResult = VK_ERROR_SURFACE_LOST_KHR;
	SWIM_CHECK_THROWS(capture.Query(), std::runtime_error);
	SWIM_CHECK_EQUAL(capture.CountCalls, 0u);
	capture.SupportResult = VK_SUCCESS;
	capture.CountResult = VK_ERROR_OUT_OF_HOST_MEMORY;
	SWIM_CHECK_THROWS(capture.Query(), std::runtime_error);
	SWIM_CHECK_EQUAL(capture.DataCalls, 0u);
	capture.CountResult = VK_SUCCESS;
	capture.DataResult = VK_ERROR_SURFACE_LOST_KHR;
	SWIM_CHECK_THROWS(capture.Query(), std::runtime_error);
}

SWIM_TEST("RHI.Vulkan.SwapchainColor", "DeviceLossIsTypedStickyAndStopsNativeQueries")
{
	for (unsigned stage = 0; stage < 3; ++stage)
	{
		SurfaceCapture capture;
		if (stage == 0)
		{
			capture.SupportResult = VK_ERROR_DEVICE_LOST;
		}
		else if (stage == 1)
		{
			capture.CountResult = VK_ERROR_DEVICE_LOST;
		}
		else
		{
			capture.DataResult = VK_ERROR_DEVICE_LOST;
		}
		SWIM_CHECK_THROWS(capture.Query(), Rhi::DeviceLostError);
		SWIM_CHECK(capture.State.Diagnostics->IsLost());
		const auto calls = capture.SupportCalls + capture.CountCalls + capture.DataCalls;
		SWIM_CHECK_THROWS(capture.Query(), Rhi::DeviceLostError);
		SWIM_CHECK_EQUAL(capture.SupportCalls + capture.CountCalls + capture.DataCalls, calls);
	}
}
