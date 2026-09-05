cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW)
endif()

if(POLICY CMP0141)
	cmake_policy(SET CMP0141 NEW)
endif()

# Every supported Swim workflow performs an explicit CMake configure immediately
# before compiling, so automatic regeneration is disabled for both generators.
#
# Ninja: third-party CPM glob-watch rules can otherwise trap the build in a
# perpetual VerifyGlobs -> RERUN_CMAKE cycle.
#
# Visual Studio: CMake attaches a `--check-stamp-file` custom build to *every*
# generated project. MSBuild builds projects in parallel, so a stale stamp makes
# N projects launch N concurrent configures of the same binary directory. That is
# never safe: they race on generated files and on the shared CPM dependency
# caches, which surfaces as `could not lock config file .git/config`, spurious
# `configure_file` failures, and an MSB8066 cascade that leaves libraries unbuilt.
#
# New/renamed sources and CMake edits are picked up by the explicit configure at
# the start of every clean/soft build, both of which also refresh the Visual
# Studio solution. After editing CMake files, re-run a build script (or
# `cmake --preset windows-vs`) rather than relying on the IDE to regenerate.
if(CMAKE_GENERATOR MATCHES "^Ninja" OR CMAKE_GENERATOR MATCHES "^Visual Studio ")
	set(CMAKE_SUPPRESS_REGENERATION ON CACHE BOOL
		"Swim build scripts configure explicitly; suppress automatic CMake regeneration" FORCE)
endif()

# The legacy x64 project used /MT in both Debug and Release. Set this before
# project()/dependencies so every CMake-built static dependency shares the ABI.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
	"$<$<CONFIG:Debug,Release,RelWithDebInfo>:ProgramDatabase>"
)

project(SwimEngine C CXX)

include(cmake/SolutionLayout.cmake)
include(cmake/Tests.cmake)

option(SWIM_WARNINGS_AS_ERRORS "Treat Swim Engine warnings as errors" OFF)
option(SWIM_ENABLE_PCH "Use Swim Engine's existing PCH.h" ON)
option(SWIM_OFFLINE_DEPENDENCY_STUBS "Configure without downloading dependencies (build-layout verification only)" OFF)

# Keep fetched third-party source trees outside individual build presets so a
# soft rebuild can reuse exactly the dependency revisions already downloaded.
# The clean-build scripts remove this directory deliberately before configuring.
if(NOT DEFINED CPM_SOURCE_CACHE OR CPM_SOURCE_CACHE STREQUAL "")
	set(CPM_SOURCE_CACHE "${CMAKE_SOURCE_DIR}/.cache/cpm" CACHE PATH
		"Shared source cache for CPM-managed Swim Engine dependencies")
endif()
set(CPM_USE_NAMED_CACHE_DIRECTORIES ON CACHE BOOL
	"Keep CPM dependency cache directories human-readable" FORCE)

if(WIN32)
	set(SWIM_BUILD_LEGACY_ENGINE_DEFAULT ON)
else()
	set(SWIM_BUILD_LEGACY_ENGINE_DEFAULT OFF)
endif()
option(SWIM_BUILD_LEGACY_ENGINE "Build the existing renderer/game runtime while it is being migrated" ${SWIM_BUILD_LEGACY_ENGINE_DEFAULT})
if(WIN32 AND MSVC)
	set(SWIM_ENABLE_PHYSX_BACKEND_DEFAULT ON)
else()
	set(SWIM_ENABLE_PHYSX_BACKEND_DEFAULT OFF)
endif()
option(SWIM_ENABLE_PHYSX_BACKEND "Compile the PhysX physics backend" ${SWIM_ENABLE_PHYSX_BACKEND_DEFAULT})
option(SWIM_ENABLE_JOLT_BACKEND "Compile the Jolt physics backend" ON)
option(SWIM_BUILD_PLATFORM_EXAMPLE "Build the cross-platform HelloWindow platform smoke test" ON)
option(SWIM_BUILD_FOUNDATION_TESTS "Build platform/input foundation tests" ON)
option(SWIM_BUILD_ASSET_COMPILER "Build offline asset compiler/import modules" ON)
option(SWIM_BUILD_SHADER_COMPILER "Build Slang shader compiler/reflection tooling" ON)
option(SWIM_ENABLE_VULKAN_RHI "Compile the modern Vulkan RHI backend" ON)
option(SWIM_ENABLE_DEV_ASSET_AUTOCOOK "Scan/cook loose source assets at engine startup (development only)" ON)

if(SWIM_BUILD_LEGACY_ENGINE AND NOT SWIM_ENABLE_PHYSX_BACKEND AND NOT SWIM_ENABLE_JOLT_BACKEND)
	message(FATAL_ERROR
		"The SwimEngine executable requires at least one compiled physics backend. "
		"Enable SWIM_ENABLE_PHYSX_BACKEND and/or SWIM_ENABLE_JOLT_BACKEND."
	)
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The old Debug x64 project intentionally combined debug code generation with
# release-style MSVC CRT/STL ABI settings. Apply those ABI switches to CMake
# dependencies too, otherwise a Debug-built C++ static library can emit
# _ITERATOR_DEBUG_LEVEL=2 mismatch records while Swim Engine expects 0.
if(MSVC)
	add_compile_definitions(
		$<$<CONFIG:Debug>:NDEBUG>
		$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:CXX>>:_ITERATOR_DEBUG_LEVEL=0>
	)
	add_compile_options(
		"$<$<CONFIG:Debug>:/U_DEBUG>"
	)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT SWIM_OFFLINE_DEPENDENCY_STUBS)
	include(cmake/get_cpm.cmake)
endif()

set(SWIM_SAVED_CMAKE_FOLDER "${CMAKE_FOLDER}")
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/GLM")
include(cmake/MathDependencies.cmake)
set(CMAKE_FOLDER "${SWIM_SAVED_CMAKE_FOLDER}")
unset(SWIM_SAVED_CMAKE_FOLDER)

set(SWIM_SAVED_CMAKE_FOLDER "${CMAKE_FOLDER}")
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/mimalloc")
include(cmake/MemoryDependencies.cmake)
set(CMAKE_FOLDER "${SWIM_SAVED_CMAKE_FOLDER}")
unset(SWIM_SAVED_CMAKE_FOLDER)

set(SWIM_SAVED_CMAKE_FOLDER "${CMAKE_FOLDER}")
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/enkiTS")
include(cmake/JobDependencies.cmake)
set(CMAKE_FOLDER "${SWIM_SAVED_CMAKE_FOLDER}")
unset(SWIM_SAVED_CMAKE_FOLDER)

set(SWIM_SAVED_CMAKE_FOLDER "${CMAKE_FOLDER}")
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/SDL3")
include(cmake/PlatformDependencies.cmake)
set(CMAKE_FOLDER "${SWIM_SAVED_CMAKE_FOLDER}")
unset(SWIM_SAVED_CMAKE_FOLDER)

# Build scripts always run an explicit configure before compiling. Keep source
# discovery configure-time only instead of using CONFIGURE_DEPENDS: the latter
# adds Ninja VerifyGlobs manifest edges, which can participate in a perpetual
# RERUN_CMAKE cycle when the same dependency cache is also configured by the
# Visual Studio solution tree. New files are still discovered on every normal
# clean/soft build because both workflows configure first.
# --- Engine module source discovery -----------------------------------------
#
# These used to each be their own add_library(...) target, which is what made
# every one of them a separate Visual Studio project under "Engine Modules".
# They are still discovered here, at the same points in the configure, with
# the same glob patterns and the same third-party dependency setup -- but the
# .cpp/.h lists below are only ever *compiled* as part of a real final binary
# (SwimEngine, SwimTests, an example, or a Tools target). A module's source
# list is a build-graph concept now, not a build-graph *target*. See
# docs/VisualStudioProjectStructure.md and architecture plan §0.2/§4.3 for the
# full rationale and the resulting Visual Studio layout.
#
# Two rules keep this safe:
#  1. Every consumer binary compiles a given module's sources at most once.
#     Where more than one thing in the same binary would otherwise want a
#     module (SwimTests linking both SwimAssetCompiler and needing Assets
#     directly, for example), exactly one of them actually compiles the
#     module and the other gets it by linking that one.
#  2. A module's own third-party PRIVATE dependency (SDL3 for Platform,
#     mimalloc for Memory, enkiTS for Jobs, Jolt/PhysX/volk/vk-bootstrap/VMA
#     for their respective backends) moves onto whichever target now compiles
#     that module's sources, exactly mirroring what the module's own
#     target_link_libraries() used to declare.
file(GLOB_RECURSE SWIM_PLATFORM_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/Platform/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/Platform/*.h
)

# In-process commands are independent of platform input, file IO, and Machine.
set(SWIM_COMMANDS_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/Commands/CommandRegistry.h
	${CMAKE_SOURCE_DIR}/Source/Engine/Commands/CommandRegistry.cpp
)

file(GLOB_RECURSE SWIM_INPUT_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/Input/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/Input/*.h
)

file(GLOB_RECURSE SWIM_MEMORY_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/Memory/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/Memory/*.h
)

file(GLOB_RECURSE SWIM_ASSET_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/Assets/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/Assets/*.h
)

# Source-format importers are tooling dependencies, not runtime asset
# dependencies. Keep fastgltf and its parser dependency on this side of the
# boundary so shipping/runtime targets do not inherit importer object graphs.
if(SWIM_BUILD_ASSET_COMPILER)
	set(SWIM_SAVED_CMAKE_FOLDER "${CMAKE_FOLDER}")
	set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Asset Compiler")
	include(cmake/AssetCompilerDependencies.cmake)
	set(CMAKE_FOLDER "${SWIM_SAVED_CMAKE_FOLDER}")
	unset(SWIM_SAVED_CMAKE_FOLDER)
endif()

if(SWIM_BUILD_ASSET_COMPILER AND SWIM_ASSET_COMPILER_DEPENDENCIES_AVAILABLE)
	file(GLOB_RECURSE SWIM_ASSET_COMPILER_SOURCES
		${CMAKE_SOURCE_DIR}/Source/Tools/AssetCompiler/*.cpp
		${CMAKE_SOURCE_DIR}/Source/Tools/AssetCompiler/*.h
	)

	# The asset compiler is one module with two build outputs: a library every
	# consumer links, and the thin standalone cooker front end under Cli/. Only
	# the front end owns a main(), so it must stay out of the library archive.
	set(SWIM_ASSET_COOKER_SOURCES ${CMAKE_SOURCE_DIR}/Source/Tools/AssetCompiler/Cli/AssetCookerMain.cpp)
	list(FILTER SWIM_ASSET_COMPILER_SOURCES EXCLUDE REGEX "/Source/Tools/AssetCompiler/Cli/")

	# SwimAssets is no longer its own target (see "Engine module source
	# discovery" above), so the compiler embeds the runtime asset module's
	# sources directly: SwimAssetCompiler is the only Tools-side consumer of
	# Assets, so this is the one place that compiles them, and every other
	# consumer (SwimTests, SwimEngine) either gets Assets the same way or
	# gets it by linking Swim::AssetCompiler -- never both, to avoid a
	# duplicate-symbol link error within the same binary.
	add_library(SwimAssetCompiler STATIC ${SWIM_ASSET_COMPILER_SOURCES} ${SWIM_ASSET_SOURCES})
	add_library(Swim::AssetCompiler ALIAS SwimAssetCompiler)
	target_include_directories(SwimAssetCompiler PUBLIC ${CMAKE_SOURCE_DIR}/Source)
	target_link_libraries(SwimAssetCompiler PRIVATE Swim::AssetCompilerDependencies)
	target_compile_features(SwimAssetCompiler PUBLIC cxx_std_20)
	source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SWIM_ASSET_COMPILER_SOURCES} ${SWIM_ASSET_SOURCES})
	swim_set_solution_folder(SwimAssetCompiler "${SWIM_SOLUTION_FOLDER_TOOLS}")

	if(MSVC)
		target_compile_options(SwimAssetCompiler PRIVATE /utf-8 /W3 /permissive-)
	else()
		target_compile_options(SwimAssetCompiler PRIVATE -Wall -Wextra -Wpedantic)
	endif()

	add_executable(SwimAssetCooker ${SWIM_ASSET_COOKER_SOURCES})
	target_link_libraries(SwimAssetCooker PRIVATE Swim::AssetCompiler)
	target_include_directories(SwimAssetCooker PRIVATE ${CMAKE_SOURCE_DIR}/Source)
	target_compile_features(SwimAssetCooker PRIVATE cxx_std_20)
	source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SWIM_ASSET_COOKER_SOURCES})
	swim_set_solution_folder(SwimAssetCooker "${SWIM_SOLUTION_FOLDER_TOOLS}")
endif()

# Slang is a build-only shader compiler dependency. The first-party tool target
# owns reflection parsing while deterministic CMake output rules own shader
# compilation; runtime renderer targets do not link Slang.
if(SWIM_BUILD_SHADER_COMPILER AND NOT SWIM_OFFLINE_DEPENDENCY_STUBS)
	set(SWIM_SAVED_CMAKE_FOLDER "${CMAKE_FOLDER}")
	set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Shader Compiler")
	include(cmake/ShaderCompilerDependencies.cmake)
	set(CMAKE_FOLDER "${SWIM_SAVED_CMAKE_FOLDER}")
	unset(SWIM_SAVED_CMAKE_FOLDER)
endif()

if(SWIM_BUILD_SHADER_COMPILER AND SWIM_SHADER_COMPILER_DEPENDENCIES_AVAILABLE)
	file(GLOB_RECURSE SWIM_SHADER_COMPILER_SOURCES
		${CMAKE_SOURCE_DIR}/Source/Tools/ShaderCompiler/*.cpp
		${CMAKE_SOURCE_DIR}/Source/Tools/ShaderCompiler/*.h
	)

	add_library(SwimShaderCompiler STATIC ${SWIM_SHADER_COMPILER_SOURCES})
	add_library(Swim::ShaderCompiler ALIAS SwimShaderCompiler)
	target_include_directories(SwimShaderCompiler PUBLIC ${CMAKE_SOURCE_DIR}/Source)
	# Swim::Rhi was a header-only interface target providing nothing beyond the
	# ${CMAKE_SOURCE_DIR}/Source include dir already set above, so it no longer
	# needs to exist as its own target (see "Engine module source discovery").
	target_link_libraries(SwimShaderCompiler PRIVATE simdjson::simdjson)
	target_compile_features(SwimShaderCompiler PUBLIC cxx_std_20)
	source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SWIM_SHADER_COMPILER_SOURCES})
	swim_set_solution_folder(SwimShaderCompiler "${SWIM_SOLUTION_FOLDER_TOOLS}")

	if(MSVC)
		target_compile_options(SwimShaderCompiler PRIVATE /utf-8 /W3 /permissive-)
	else()
		target_compile_options(SwimShaderCompiler PRIVATE -Wall -Wextra -Wpedantic)
	endif()

	include(cmake/SlangShaders.cmake)
	swim_add_slang_program(SwimBasicRaster
		SOURCE Source/Shaders/Slang/Basic/Basic.slang
		OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Generated/Shaders/Basic"
		PROFILE spirv_1_5
	)
	add_custom_target(SwimSlangReflectionSample
		DEPENDS "${SwimBasicRaster_SPIRV}" "${SwimBasicRaster_REFLECTION}"
		SOURCES Source/Shaders/Slang/Basic/Basic.slang
	)
	swim_set_solution_folder(SwimSlangReflectionSample "${SWIM_SOLUTION_FOLDER_TESTS}")
	set(SWIM_SLANG_SPIRV_SAMPLE "${SwimBasicRaster_SPIRV}" CACHE INTERNAL
		"Generated SPIR-V path for the Slang reflection sample" FORCE)
	set(SWIM_SLANG_REFLECTION_SAMPLE "${SwimBasicRaster_REFLECTION}" CACHE INTERNAL
		"Generated reflection path for the Slang reflection sample" FORCE)
endif()

# RHI smoke shaders use the same pinned Slang compiler and artifact path as engine shaders.
if(TARGET SwimSlangCompiler AND SWIM_ENABLE_VULKAN_RHI)
	include(cmake/SlangShaders.cmake)
	swim_add_slang_program(SwimRhiTriangle
		SOURCE Source/Shaders/Slang/RhiSmoke/Triangle.slang
		PROFILE spirv_1_5
	)
	swim_add_slang_program(SwimRhiTexture
		SOURCE Source/Shaders/Slang/RhiSmoke/Texture.slang
		PROFILE spirv_1_5
	)
	add_custom_target(SwimRhiSmokeShaders
		DEPENDS "${SwimRhiTriangle_SPIRV}" "${SwimRhiTriangle_REFLECTION}"
			"${SwimRhiTexture_SPIRV}" "${SwimRhiTexture_REFLECTION}"
		SOURCES Source/Shaders/Slang/RhiSmoke/Triangle.slang Source/Shaders/Slang/RhiSmoke/Texture.slang
	)
	swim_set_solution_folder(SwimRhiSmokeShaders "${SWIM_SOLUTION_FOLDER_TESTS}")
endif()

file(GLOB_RECURSE SWIM_JOB_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/Jobs/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/Jobs/*.h
)

file(GLOB_RECURSE SWIM_IO_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/IO/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/IO/*.h
)

set(SWIM_CORE_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/EngineConfig.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/EngineConfig.h
	${CMAKE_SOURCE_DIR}/Source/Engine/EngineState.h
)

# Swim::Rhi used to be a header-only INTERFACE target that existed only to
# carry these three files' include path -- ${CMAKE_SOURCE_DIR}/Source, which
# every consumer below already sets directly. It is not re-declared: the RHI
# contract headers are picked up automatically wherever Source/Engine/... is
# globbed (SwimEngine's own source list, for example), same as any other
# engine header.

# The modern Vulkan backend is a cross-platform foundation target, independent
# from the still-Windows-only legacy renderer. Its third-party bootstrap stack
# stays private so Vulkan implementation types cannot leak through Swim::Rhi.
if(SWIM_ENABLE_VULKAN_RHI AND NOT SWIM_OFFLINE_DEPENDENCY_STUBS)
	set(SWIM_SAVED_CMAKE_FOLDER "${CMAKE_FOLDER}")
	set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Vulkan RHI")
	include(cmake/VulkanRhiDependencies.cmake)
	set(CMAKE_FOLDER "${SWIM_SAVED_CMAKE_FOLDER}")
	unset(SWIM_SAVED_CMAKE_FOLDER)

	file(GLOB_RECURSE SWIM_VULKAN_RHI_SOURCES
		${CMAKE_SOURCE_DIR}/Source/Engine/Systems/Renderer/RHI/Backends/Vulkan/*.cpp
		${CMAKE_SOURCE_DIR}/Source/Engine/Systems/Renderer/RHI/Backends/Vulkan/*.h
	)
	set(SWIM_VULKAN_RHI_AVAILABLE TRUE)

	# Whichever target compiles ${SWIM_VULKAN_RHI_SOURCES} needs these PRIVATE
	# third-party bootstrap dependencies and definitions -- previously private
	# to the now-retired SwimRhiVulkan target, now applied at each consumer.
	set(SWIM_VULKAN_RHI_PRIVATE_LIBS
		volk::volk vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator
	)
	set(SWIM_VULKAN_RHI_PRIVATE_DEFINITIONS
		VMA_STATIC_VULKAN_FUNCTIONS=0
		VMA_DYNAMIC_VULKAN_FUNCTIONS=1
		VMA_VULKAN_VERSION=1003000
		$<$<CONFIG:Debug>:SWIM_VULKAN_VALIDATION=1>
	)
endif()

# Generic physics is part of the cross-platform foundation. Keep the concrete
# PhysX implementation behind the legacy/runtime gate, but compile this API and
# its shared backend contract on Linux too so portable C++ errors are caught
# before an expensive Windows dependency build.
# Under offline dependency stubs there is no real physics implementation to
# compile (build-layout verification only); SWIM_PHYSICS_SOURCES stays empty
# and consumers still get glm::glm's include path wherever they need physics
# headers, matching the old SwimPhysics INTERFACE stub's only real content.
set(SWIM_PHYSICS_SOURCES "")
if(NOT SWIM_OFFLINE_DEPENDENCY_STUBS)
	file(GLOB_RECURSE SWIM_PHYSICS_SOURCES
		${CMAKE_SOURCE_DIR}/Source/Engine/Systems/Physics/*.cpp
		${CMAKE_SOURCE_DIR}/Source/Engine/Systems/Physics/*.h
	)
	list(FILTER SWIM_PHYSICS_SOURCES EXCLUDE REGEX "/Source/Engine/Systems/Physics/Backends/")
endif()
# Every consumer that compiles SWIM_PHYSICS_SOURCES (or just needs the
# generic physics headers) links glm::glm PRIVATE -- this was SwimPhysics's
# only real dependency (PUBLIC glm::glm, no PhysX/Jolt/EnTT/Scene coupling).

# Jolt is the first concrete cross-platform physics backend. Keep its dependency
# and implementation before the legacy-runtime return so Linux compiles and runs
# the same backend contract used on Windows.
if(SWIM_ENABLE_JOLT_BACKEND AND NOT SWIM_OFFLINE_DEPENDENCY_STUBS)
	set(SWIM_SAVED_CMAKE_FOLDER "${CMAKE_FOLDER}")
	set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Jolt")
	include(cmake/JoltDependencies.cmake)
	set(CMAKE_FOLDER "${SWIM_SAVED_CMAKE_FOLDER}")
	unset(SWIM_SAVED_CMAKE_FOLDER)

	file(GLOB_RECURSE SWIM_JOLT_BACKEND_SOURCES
		${CMAKE_SOURCE_DIR}/Source/Engine/Systems/Physics/Backends/Jolt/*.cpp
		${CMAKE_SOURCE_DIR}/Source/Engine/Systems/Physics/Backends/Jolt/*.h
	)
	set(SWIM_JOLT_BACKEND_AVAILABLE TRUE)
	# Whichever target compiles ${SWIM_JOLT_BACKEND_SOURCES} links Swim::Jolt
	# PRIVATE and also needs SWIM_PHYSICS_SOURCES compiled in somewhere in the
	# same binary -- previously PUBLIC Swim::Physics on the retired
	# SwimPhysicsJolt target.
endif()

# Examples are deliberately small consumers proving modules are usable
# outside SwimEngine. They now compile the module sources they need directly
# (see "Engine module source discovery" above) instead of linking a module
# library -- each is its own final binary, so there is no duplicate-symbol
# risk in doing so.
if(SWIM_BUILD_PLATFORM_EXAMPLE)
	add_executable(SwimHelloWindow EXCLUDE_FROM_ALL Source/Examples/HelloWindow.cpp ${SWIM_PLATFORM_SOURCES})
	target_link_libraries(SwimHelloWindow PRIVATE ${SWIM_SDL3_TARGET})
	target_include_directories(SwimHelloWindow PRIVATE ${CMAKE_SOURCE_DIR}/Source)
	target_compile_definitions(SwimHelloWindow PRIVATE
		$<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
		$<$<PLATFORM_ID:Windows>:NOMINMAX>
	)
	source_group(TREE ${CMAKE_SOURCE_DIR} FILES Source/Examples/HelloWindow.cpp ${SWIM_PLATFORM_SOURCES})
	swim_set_solution_folder(SwimHelloWindow "${SWIM_SOLUTION_FOLDER_EXAMPLES}")

	add_executable(SwimHeadlessPlatform EXCLUDE_FROM_ALL Source/Examples/HeadlessPlatform.cpp ${SWIM_PLATFORM_SOURCES})
	target_link_libraries(SwimHeadlessPlatform PRIVATE ${SWIM_SDL3_TARGET})
	target_include_directories(SwimHeadlessPlatform PRIVATE ${CMAKE_SOURCE_DIR}/Source)
	target_compile_definitions(SwimHeadlessPlatform PRIVATE
		$<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
		$<$<PLATFORM_ID:Windows>:NOMINMAX>
	)
	source_group(TREE ${CMAKE_SOURCE_DIR} FILES Source/Examples/HeadlessPlatform.cpp ${SWIM_PLATFORM_SOURCES})
	swim_set_solution_folder(SwimHeadlessPlatform "${SWIM_SOLUTION_FOLDER_EXAMPLES}")
endif()

if(SWIM_BUILD_FOUNDATION_TESTS)
	add_executable(SwimHeadlessCoreAssets EXCLUDE_FROM_ALL
		Source/Examples/HeadlessCoreAssets.cpp
		${SWIM_CORE_SOURCES}
		${SWIM_MEMORY_SOURCES}
		${SWIM_JOB_SOURCES}
		${SWIM_ASSET_SOURCES}
	)
	target_link_libraries(SwimHeadlessCoreAssets PRIVATE Swim::Mimalloc Swim::EnkiTS)
	target_include_directories(SwimHeadlessCoreAssets PRIVATE ${CMAKE_SOURCE_DIR}/Source)
	target_compile_definitions(SwimHeadlessCoreAssets PRIVATE
		SWIM_MEMORY_USE_MIMALLOC=$<BOOL:${SWIM_MEMORY_USE_MIMALLOC}>
		SWIM_JOBS_USE_ENKITS=$<BOOL:${SWIM_JOBS_USE_ENKITS}>
	)
	source_group(TREE ${CMAKE_SOURCE_DIR} FILES
		Source/Examples/HeadlessCoreAssets.cpp
		${SWIM_CORE_SOURCES}
		${SWIM_MEMORY_SOURCES}
		${SWIM_JOB_SOURCES}
		${SWIM_ASSET_SOURCES}
	)
	swim_set_solution_folder(SwimHeadlessCoreAssets "${SWIM_SOLUTION_FOLDER_EXAMPLES}")
endif()

if(NOT SWIM_BUILD_LEGACY_ENGINE)
	# Foundation-only configure: define the test program now, with whatever
	# module targets this configuration actually has.
	if(SWIM_BUILD_FOUNDATION_TESTS)
		swim_configure_tests()
	endif()
	return()
endif()

if(NOT WIN32)
	message(FATAL_ERROR
		"SWIM_BUILD_LEGACY_ENGINE is still Windows-only while the renderer backends are being migrated. "
		"The Swim::Platform module and SwimHelloWindow are cross-platform."
	)
endif()

set(SWIM_SAVED_CMAKE_FOLDER "${CMAKE_FOLDER}")
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}")
include(cmake/Dependencies.cmake)
set(CMAKE_FOLDER "${SWIM_SAVED_CMAKE_FOLDER}")
unset(SWIM_SAVED_CMAKE_FOLDER)

if(SWIM_ENABLE_PHYSX_BACKEND)
	file(GLOB_RECURSE SWIM_PHYSX_BACKEND_SOURCES
		${CMAKE_SOURCE_DIR}/Source/Engine/Systems/Physics/Backends/PhysX/*.cpp
		${CMAKE_SOURCE_DIR}/Source/Engine/Systems/Physics/Backends/PhysX/*.h
	)
	set(SWIM_PHYSX_BACKEND_AVAILABLE TRUE)
	# Whichever target compiles ${SWIM_PHYSX_BACKEND_SOURCES} links Swim::PhysX
	# PRIVATE, needs SWIM_PHYSICS_SOURCES compiled in somewhere in the same
	# binary (previously PUBLIC Swim::Physics on the retired
	# SwimPhysicsPhysX target), and -- if SwimPhysXBuild exists -- must
	# add_dependencies() on it so the external PhysX SDK build runs first.
endif()

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	add_library(SwimVulkan INTERFACE)
	add_library(Vulkan::Vulkan ALIAS SwimVulkan)
	add_library(SwimOpenGL INTERFACE)
	add_library(OpenGL::GL ALIAS SwimOpenGL)
else()
	find_package(Vulkan REQUIRED)
	find_package(OpenGL REQUIRED)
endif()

# Platform, Input, Commands, Memory, Jobs, IO, and Core (EngineConfig/
# EngineState) all used to be excluded here because they were compiled once
# by their own module target and linked in. They are no longer separate
# targets, so nothing excludes them any more: this recursive glob just picks
# them up along with the rest of Source/Engine directly. Their former
# module-private link libraries (SDL3, mimalloc, enkiTS, ...) and compile
# definitions are applied directly to SwimEngine below instead of coming
# from a module's own target_link_libraries()/target_compile_definitions().
#
# Systems/Physics/ and Systems/Renderer/RHI/Backends/ stay excluded: those
# subtrees are feature-gated (physics backend choice, SWIM_ENABLE_VULKAN_RHI)
# and are appended explicitly, by availability, right below.
#
# Assets/ also stays excluded here: when development auto-cook is enabled,
# SwimEngine links Swim::AssetCompiler, which now embeds Assets' sources
# itself (see the AssetCompiler block above) -- compiling Assets/ into
# SwimEngine too would duplicate every symbol at link time. Whether Assets
# needs to be added directly to SwimEngine's own sources instead is decided
# below, once we know whether AssetCompiler will actually be linked.
file(GLOB_RECURSE SWIM_ENGINE_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/*.h
	${CMAKE_SOURCE_DIR}/Source/Engine/*.hpp
)
list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Systems/Physics/")
list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Systems/Renderer/RHI/Backends/")
list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Assets/")

list(APPEND SWIM_ENGINE_SOURCES ${SWIM_PHYSICS_SOURCES})
if(SWIM_JOLT_BACKEND_AVAILABLE)
	list(APPEND SWIM_ENGINE_SOURCES ${SWIM_JOLT_BACKEND_SOURCES})
endif()
if(SWIM_PHYSX_BACKEND_AVAILABLE)
	list(APPEND SWIM_ENGINE_SOURCES ${SWIM_PHYSX_BACKEND_SOURCES})
endif()
if(SWIM_VULKAN_RHI_AVAILABLE)
	list(APPEND SWIM_ENGINE_SOURCES ${SWIM_VULKAN_RHI_SOURCES})
endif()

file(GLOB_RECURSE SWIM_GAME_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Game/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Game/*.h
	${CMAKE_SOURCE_DIR}/Source/Game/*.hpp
)

file(GLOB_RECURSE SWIM_SHADER_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Shaders/*.slang
)

set_source_files_properties(${SWIM_SHADER_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)

add_executable(SwimEngine
	Source/main.cpp
	${SWIM_ENGINE_SOURCES}
	${SWIM_GAME_SOURCES}
	${SWIM_SHADER_SOURCES}
)

# The final process owns the general allocator override. On Windows Swim uses
# the static /MT CRT, matching mimalloc's static override contract; the object
# target is preferred over an archive so malloc/free/new/delete resolve here.
if(SWIM_MEMORY_USE_MIMALLOC AND TARGET mimalloc-obj)
	target_sources(SwimEngine PRIVATE $<TARGET_OBJECTS:mimalloc-obj>)
endif()

set_property(TARGET SwimEngine PROPERTY EXPORT_COMPILE_COMMANDS ON)
set_target_properties(SwimEngine PROPERTIES
	OUTPUT_NAME "Swim Engine"
)

# Exactly mirror the physical Source/... directory tree in generated Visual
# Studio filters, matching Tungsten's generated-solution workflow.
source_group(TREE ${CMAKE_SOURCE_DIR} FILES
	${CMAKE_SOURCE_DIR}/Source/main.cpp
	${SWIM_ENGINE_SOURCES}
	${SWIM_GAME_SOURCES}
	${SWIM_SHADER_SOURCES}
)

set_property(
	DIRECTORY ${CMAKE_SOURCE_DIR}
	PROPERTY VS_STARTUP_PROJECT SwimEngine
)

set_property(
	TARGET SwimEngine
	PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

target_include_directories(SwimEngine PRIVATE
	${CMAKE_SOURCE_DIR}/Source
	${CMAKE_SOURCE_DIR}/Source/Engine/Utility
)

set(SWIM_DEV_ASSET_AUTOCOOK_ENABLED OFF)
if(SWIM_ENABLE_DEV_ASSET_AUTOCOOK AND TARGET SwimAssetCompiler)
	set(SWIM_DEV_ASSET_AUTOCOOK_ENABLED ON)
endif()

# Assets was excluded from SWIM_ENGINE_SOURCES above specifically to avoid a
# duplicate-symbol link error against Swim::AssetCompiler's embedded copy.
# When AssetCompiler is not going to be linked, SwimEngine needs its own
# compile of Assets instead.
if(NOT SWIM_DEV_ASSET_AUTOCOOK_ENABLED)
	target_sources(SwimEngine PRIVATE ${SWIM_ASSET_SOURCES})
	source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SWIM_ASSET_SOURCES})
endif()

target_compile_definitions(SwimEngine PRIVATE
	NDEBUG
	_CONSOLE
	UNICODE
	_UNICODE
	_CRT_SECURE_NO_WARNINGS
	$<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
	$<$<PLATFORM_ID:Windows>:NOMINMAX>
	$<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR>
	$<$<CONFIG:Debug>:_SWIM_DEBUG>
	$<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>
	SWIM_ENABLE_DEV_ASSET_AUTOCOOK=$<BOOL:${SWIM_DEV_ASSET_AUTOCOOK_ENABLED}>
	SWIM_ENABLE_PHYSX_BACKEND=$<BOOL:${SWIM_PHYSX_BACKEND_AVAILABLE}>
	SWIM_ENABLE_JOLT_BACKEND=$<BOOL:${SWIM_JOLT_BACKEND_AVAILABLE}>
	# Module-level compile definitions that used to be PRIVATE to the module's
	# own now-retired target (SwimMemory, SwimJobs, SwimRhiVulkan); their
	# sources compile as part of SwimEngine now, so the definitions move here.
	SWIM_MEMORY_USE_MIMALLOC=$<BOOL:${SWIM_MEMORY_USE_MIMALLOC}>
	SWIM_JOBS_USE_ENKITS=$<BOOL:${SWIM_JOBS_USE_ENKITS}>
)
if(SWIM_VULKAN_RHI_AVAILABLE)
	target_compile_definitions(SwimEngine PRIVATE ${SWIM_VULKAN_RHI_PRIVATE_DEFINITIONS})
endif()

if(SWIM_ENABLE_PCH)
	target_precompile_headers(SwimEngine PRIVATE
		"$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/Source/Engine/Utility/PCH.h>"
	)
endif()

# These implementation TUs own third-party implementation macros and must not
# inherit PCH definitions that can instantiate the same headers prematurely.
set_source_files_properties(
	Source/Engine/ThirdParty/StbImageImplementation.cpp
	Source/Engine/ThirdParty/StbImageResizeImplementation.cpp
	PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

if(MSVC)
	target_compile_options(SwimEngine PRIVATE
		/MP
		/utf-8
		/W3
		/sdl
		/permissive-
		/external:anglebrackets
		/external:W0
		/wd4100
		/wd4244
		/wd4702
		"$<$<CONFIG:Debug>:/U_DEBUG>"
		"$<$<CONFIG:Release>:/Oi>"
		"$<$<CONFIG:Release>:/Gy>"
	)

	if(SWIM_ENABLE_PCH AND CMAKE_GENERATOR MATCHES "^Visual Studio ")
		target_compile_options(SwimEngine PRIVATE
			"$<$<COMPILE_LANGUAGE:CXX>:/Fp$(IntDir)cmake_pch.pch>"
		)
	endif()

	target_link_options(SwimEngine PRIVATE
		/SUBSYSTEM:CONSOLE
		"$<$<CONFIG:Release>:/DEBUG:FULL>"
		"$<$<CONFIG:Release>:/OPT:REF>"
		"$<$<CONFIG:Release>:/OPT:ICF>"
	)

	if(SWIM_WARNINGS_AS_ERRORS)
		target_compile_options(SwimEngine PRIVATE /WX)
	endif()
else()
	target_compile_options(SwimEngine PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
	if(SWIM_WARNINGS_AS_ERRORS)
		target_compile_options(SwimEngine PRIVATE -Werror)
	endif()
endif()

if(SWIM_DEV_ASSET_AUTOCOOK_ENABLED)
	target_link_libraries(SwimEngine PRIVATE Swim::AssetCompiler)
endif()

# Third-party dependencies that used to be PRIVATE to a module's own
# now-retired target (SDL3/Platform, mimalloc/Memory, enkiTS/Jobs) move
# directly onto SwimEngine, since that module's sources compile as part of
# SwimEngine's own translation units now. Swim::Core/Memory/Assets/Jobs/IO/
# Platform/Input/Commands/Physics no longer exist as separate targets to link.
target_link_libraries(SwimEngine PRIVATE
	${SWIM_SDL3_TARGET}
	Swim::Mimalloc
	Swim::EnkiTS
	spdlog::spdlog
	Vulkan::Vulkan
	OpenGL::GL
	glm::glm
	EnTT::EnTT
	nlohmann_json::nlohmann_json
	stb::stb
	zstd::zstd
	Swim::BasisTranscoder
	glad::glad
)

if(SWIM_VULKAN_RHI_AVAILABLE)
	target_link_libraries(SwimEngine PRIVATE ${SWIM_VULKAN_RHI_PRIVATE_LIBS})
endif()
if(SWIM_PHYSX_BACKEND_AVAILABLE)
	target_link_libraries(SwimEngine PRIVATE Swim::PhysX)
	if(TARGET SwimPhysXBuild)
		add_dependencies(SwimEngine SwimPhysXBuild)
	endif()
endif()
if(SWIM_JOLT_BACKEND_AVAILABLE)
	target_link_libraries(SwimEngine PRIVATE Swim::Jolt)
endif()

include(cmake/Shaders.cmake)
swim_configure_shaders(SwimEngine)


if(NOT SWIM_OFFLINE_DEPENDENCY_STUBS)
	add_custom_command(TARGET SwimEngine POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_directory
			"${CMAKE_SOURCE_DIR}/Assets"
			"$<TARGET_FILE_DIR:SwimEngine>/Assets"
		VERBATIM
	)
endif()

# The full Windows/legacy configure has the widest dependency surface (EnTT,
# GLM, and the PhysX backend), so the single test program is defined last and
# picks up every suite group this configuration can build.
if(SWIM_BUILD_FOUNDATION_TESTS)
	swim_configure_tests()
endif()
