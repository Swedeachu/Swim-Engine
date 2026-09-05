if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	return()
endif()

# Pin the Vulkan headers for the modern RHI instead of consuming whichever
# Vulkan SDK happens to be installed. volk and vk-bootstrap track Vulkan header
# releases closely and reference structures that only exist from a matching
# header version, so a developer on an older SDK would otherwise fail to build
# the backend at all.
#
# The legacy renderer keeps using the system SDK through find_package(Vulkan).
# The two never exchange Vulkan types: the RHI boundary is opaque handles, and
# the platform WSI shim resolves everything through SDL without including
# vulkan.h. Loader symbols come from volk at runtime, so the modern backend
# links no Vulkan import library.
CPMAddPackage(
	NAME vulkan_headers_source
	GITHUB_REPOSITORY KhronosGroup/Vulkan-Headers
	GIT_TAG v1.4.350
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)
if(NOT TARGET Vulkan::Headers)
	message(FATAL_ERROR "Vulkan-Headers v1.4.350 did not provide the Vulkan::Headers target")
endif()

# volk resolves headers through this path; vk-bootstrap reuses the target above.
set(VULKAN_HEADERS_INSTALL_DIR "${vulkan_headers_source_SOURCE_DIR}" CACHE PATH
	"Pinned Vulkan headers used by the modern RHI backend" FORCE)

# Keep volk isolated from the legacy renderer while both Vulkan paths coexist.
# The namespace build prevents volk's vk* globals from colliding with the
# directly-linked Vulkan loader still used by the legacy renderer.
set(VOLK_NAMESPACE ON CACHE BOOL "" FORCE)
set(VOLK_PULL_IN_VULKAN ON CACHE BOOL "" FORCE)
set(VOLK_INSTALL OFF CACHE BOOL "" FORCE)
set(VOLK_HEADERS_ONLY OFF CACHE BOOL "" FORCE)
CPMAddPackage(
	NAME volk_source
	GITHUB_REPOSITORY zeux/volk
	GIT_TAG 1.4.350
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)
if(NOT TARGET volk::volk)
	message(FATAL_ERROR "volk 1.4.350 did not provide volk::volk")
endif()

set(VK_BOOTSTRAP_TEST OFF CACHE BOOL "" FORCE)
set(VK_BOOTSTRAP_INSTALL OFF CACHE BOOL "" FORCE)
set(VK_BOOTSTRAP_DISABLE_WARNINGS ON CACHE BOOL "" FORCE)
CPMAddPackage(
	NAME vk_bootstrap_source
	GITHUB_REPOSITORY charles-lunarg/vk-bootstrap
	GIT_TAG v1.4.350
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)
if(NOT TARGET vk-bootstrap::vk-bootstrap)
	message(FATAL_ERROR "vk-bootstrap v1.4.350 did not provide vk-bootstrap::vk-bootstrap")
endif()

# VMA owns normal Vulkan buffer/image allocation for the modern RHI. Keep it
# private to Swim::RhiVulkan so allocator implementation details never leak
# through the backend-neutral resource contracts.
set(VMA_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
CPMAddPackage(
	NAME vma_source
	GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
	GIT_TAG v3.4.0
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)
if(NOT TARGET GPUOpen::VulkanMemoryAllocator)
	message(FATAL_ERROR "Vulkan Memory Allocator v3.4.0 did not provide GPUOpen::VulkanMemoryAllocator")
endif()
