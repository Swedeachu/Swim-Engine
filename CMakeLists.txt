cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW)
endif()

if(POLICY CMP0141)
	cmake_policy(SET CMP0141 NEW)
endif()

# Swim's supported Ninja workflows always perform an explicit CMake configure
# immediately before compiling. Disable Ninja's automatic regeneration graph
# entirely so third-party CPM glob-watch rules cannot trap the build in a
# perpetual VerifyGlobs -> RERUN_CMAKE cycle. New/renamed sources are still
# discovered by the explicit configure at the start of every
# clean or soft build.
if(CMAKE_GENERATOR MATCHES "^Ninja")
	set(CMAKE_SUPPRESS_REGENERATION ON CACHE BOOL
		"Swim build scripts configure explicitly; suppress Ninja CMake regeneration" FORCE)
endif()

# The legacy x64 project used /MT in both Debug and Release. Set this before
# project()/dependencies so every CMake-built static dependency shares the ABI.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
	"$<$<CONFIG:Debug,Release,RelWithDebInfo>:ProgramDatabase>"
)

project(SwimEngine C CXX)

include(cmake/SolutionLayout.cmake)

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
option(SWIM_BUILD_PLATFORM_EXAMPLE "Build the cross-platform HelloWindow platform smoke test" ON)
option(SWIM_BUILD_FOUNDATION_TESTS "Build platform/input foundation tests" ON)

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
file(GLOB_RECURSE SWIM_PLATFORM_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/Platform/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/Platform/*.h
)

add_library(SwimPlatform STATIC ${SWIM_PLATFORM_SOURCES})
add_library(Swim::Platform ALIAS SwimPlatform)
target_include_directories(SwimPlatform PUBLIC ${CMAKE_SOURCE_DIR}/Source)
target_link_libraries(SwimPlatform PRIVATE ${SWIM_SDL3_TARGET})
target_compile_features(SwimPlatform PUBLIC cxx_std_20)
target_compile_definitions(SwimPlatform PRIVATE
	$<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
	$<$<PLATFORM_ID:Windows>:NOMINMAX>
)
source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SWIM_PLATFORM_SOURCES})
swim_set_solution_folder(SwimPlatform "${SWIM_SOLUTION_FOLDER_ENGINE_MODULES}")

if(MSVC)
	target_compile_options(SwimPlatform PRIVATE /utf-8 /W3 /permissive-)
else()
	target_compile_options(SwimPlatform PRIVATE -Wall -Wextra -Wpedantic)
endif()

file(GLOB_RECURSE SWIM_INPUT_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/Input/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/Input/*.h
)

add_library(SwimInput STATIC ${SWIM_INPUT_SOURCES})
add_library(Swim::Input ALIAS SwimInput)
target_include_directories(SwimInput PUBLIC ${CMAKE_SOURCE_DIR}/Source)
target_link_libraries(SwimInput PUBLIC Swim::Platform)
target_compile_features(SwimInput PUBLIC cxx_std_20)
source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SWIM_INPUT_SOURCES})
swim_set_solution_folder(SwimInput "${SWIM_SOLUTION_FOLDER_ENGINE_MODULES}")

if(MSVC)
	target_compile_options(SwimInput PRIVATE /utf-8 /W3 /permissive-)
else()
	target_compile_options(SwimInput PRIVATE -Wall -Wextra -Wpedantic)
endif()


file(GLOB_RECURSE SWIM_MEMORY_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/Memory/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/Memory/*.h
)

add_library(SwimMemory STATIC ${SWIM_MEMORY_SOURCES})
add_library(Swim::Memory ALIAS SwimMemory)
target_include_directories(SwimMemory PUBLIC ${CMAKE_SOURCE_DIR}/Source)
target_link_libraries(SwimMemory PRIVATE Swim::Mimalloc)
target_compile_features(SwimMemory PUBLIC cxx_std_20)
target_compile_definitions(SwimMemory PRIVATE SWIM_MEMORY_USE_MIMALLOC=$<BOOL:${SWIM_MEMORY_USE_MIMALLOC}>)
source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SWIM_MEMORY_SOURCES})
swim_set_solution_folder(SwimMemory "${SWIM_SOLUTION_FOLDER_ENGINE_MODULES}")

if(MSVC)
	target_compile_options(SwimMemory PRIVATE /utf-8 /W3 /permissive-)
else()
	target_compile_options(SwimMemory PRIVATE -Wall -Wextra -Wpedantic)
endif()

file(GLOB_RECURSE SWIM_JOB_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/Jobs/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/Jobs/*.h
)

add_library(SwimJobs STATIC ${SWIM_JOB_SOURCES})
add_library(Swim::Jobs ALIAS SwimJobs)
target_include_directories(SwimJobs PUBLIC ${CMAKE_SOURCE_DIR}/Source)
target_link_libraries(SwimJobs PRIVATE Swim::EnkiTS Swim::Memory)
target_compile_features(SwimJobs PUBLIC cxx_std_20)
target_compile_definitions(SwimJobs PRIVATE SWIM_JOBS_USE_ENKITS=$<BOOL:${SWIM_JOBS_USE_ENKITS}>)
source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SWIM_JOB_SOURCES})
swim_set_solution_folder(SwimJobs "${SWIM_SOLUTION_FOLDER_ENGINE_MODULES}")

if(MSVC)
	target_compile_options(SwimJobs PRIVATE /utf-8 /W3 /permissive-)
else()
	target_compile_options(SwimJobs PRIVATE -Wall -Wextra -Wpedantic)
endif()
file(GLOB_RECURSE SWIM_IO_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/IO/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/IO/*.h
)

add_library(SwimIO STATIC ${SWIM_IO_SOURCES})
add_library(Swim::IO ALIAS SwimIO)
target_include_directories(SwimIO PUBLIC ${CMAKE_SOURCE_DIR}/Source)
target_link_libraries(SwimIO
	PUBLIC Swim::Platform
	PRIVATE Swim::Jobs
)
target_compile_features(SwimIO PUBLIC cxx_std_20)
source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SWIM_IO_SOURCES})
swim_set_solution_folder(SwimIO "${SWIM_SOLUTION_FOLDER_ENGINE_MODULES}")

if(MSVC)
	target_compile_options(SwimIO PRIVATE /utf-8 /W3 /permissive-)
else()
	target_compile_options(SwimIO PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(SwimCore STATIC
	Source/Engine/EngineConfig.cpp
	Source/Engine/EngineConfig.h
	Source/Engine/EngineState.h
)
add_library(Swim::Core ALIAS SwimCore)
target_include_directories(SwimCore PUBLIC ${CMAKE_SOURCE_DIR}/Source)
target_compile_features(SwimCore PUBLIC cxx_std_20)
swim_set_solution_folder(SwimCore "${SWIM_SOLUTION_FOLDER_ENGINE_MODULES}")

if(MSVC)
	target_compile_options(SwimCore PRIVATE /utf-8 /W3 /permissive-)
else()
	target_compile_options(SwimCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SWIM_BUILD_PLATFORM_EXAMPLE)
	add_executable(SwimHelloWindow EXCLUDE_FROM_ALL Source/Examples/HelloWindow.cpp)
	target_link_libraries(SwimHelloWindow PRIVATE Swim::Platform)
	target_include_directories(SwimHelloWindow PRIVATE ${CMAKE_SOURCE_DIR}/Source)
	swim_set_solution_folder(SwimHelloWindow "${SWIM_SOLUTION_FOLDER_EXAMPLES}")

	add_executable(SwimHeadlessPlatform EXCLUDE_FROM_ALL Source/Examples/HeadlessPlatform.cpp)
	target_link_libraries(SwimHeadlessPlatform PRIVATE Swim::Platform)
	target_include_directories(SwimHeadlessPlatform PRIVATE ${CMAKE_SOURCE_DIR}/Source)
	swim_set_solution_folder(SwimHeadlessPlatform "${SWIM_SOLUTION_FOLDER_EXAMPLES}")
endif()

add_library(SwimPlatformPublicHeaders OBJECT EXCLUDE_FROM_ALL Source/Tests/Platform/PublicHeaderCompile.cpp)
target_include_directories(SwimPlatformPublicHeaders PRIVATE ${CMAKE_SOURCE_DIR}/Source)
target_compile_features(SwimPlatformPublicHeaders PRIVATE cxx_std_20)
swim_set_solution_folder(SwimPlatformPublicHeaders "${SWIM_SOLUTION_FOLDER_TESTS}")

add_library(SwimIoPublicHeaders OBJECT EXCLUDE_FROM_ALL Source/Tests/IO/PublicHeaderCompile.cpp)
target_include_directories(SwimIoPublicHeaders PRIVATE ${CMAKE_SOURCE_DIR}/Source)
target_compile_features(SwimIoPublicHeaders PRIVATE cxx_std_20)
swim_set_solution_folder(SwimIoPublicHeaders "${SWIM_SOLUTION_FOLDER_TESTS}")

if(SWIM_BUILD_FOUNDATION_TESTS)
	add_executable(SwimInputTests EXCLUDE_FROM_ALL Source/Tests/Input/InputSystemTests.cpp)
	target_link_libraries(SwimInputTests PRIVATE Swim::Input)
	target_include_directories(SwimInputTests PRIVATE ${CMAKE_SOURCE_DIR}/Source)
	swim_set_solution_folder(SwimInputTests "${SWIM_SOLUTION_FOLDER_TESTS}")

	add_executable(SwimEngineConfigTests EXCLUDE_FROM_ALL Source/Tests/Core/EngineConfigTests.cpp)
	target_link_libraries(SwimEngineConfigTests PRIVATE Swim::Core)
	target_include_directories(SwimEngineConfigTests PRIVATE ${CMAKE_SOURCE_DIR}/Source)
	swim_set_solution_folder(SwimEngineConfigTests "${SWIM_SOLUTION_FOLDER_TESTS}")

	add_executable(SwimMemoryTests EXCLUDE_FROM_ALL Source/Tests/Memory/MemoryArenaTests.cpp)
	target_link_libraries(SwimMemoryTests PRIVATE Swim::Memory)
	target_include_directories(SwimMemoryTests PRIVATE ${CMAKE_SOURCE_DIR}/Source)
	swim_set_solution_folder(SwimMemoryTests "${SWIM_SOLUTION_FOLDER_TESTS}")

	add_executable(SwimJobSystemTests EXCLUDE_FROM_ALL Source/Tests/Jobs/JobSystemTests.cpp)
	target_link_libraries(SwimJobSystemTests PRIVATE Swim::Jobs)
	target_include_directories(SwimJobSystemTests PRIVATE ${CMAKE_SOURCE_DIR}/Source)
	swim_set_solution_folder(SwimJobSystemTests "${SWIM_SOLUTION_FOLDER_TESTS}")

	add_executable(SwimAsyncIoTests EXCLUDE_FROM_ALL Source/Tests/IO/AsyncIoServiceTests.cpp)
	target_link_libraries(SwimAsyncIoTests PRIVATE Swim::IO Swim::Jobs)
	target_include_directories(SwimAsyncIoTests PRIVATE ${CMAKE_SOURCE_DIR}/Source)
	swim_set_solution_folder(SwimAsyncIoTests "${SWIM_SOLUTION_FOLDER_TESTS}")
endif()

if(NOT SWIM_BUILD_LEGACY_ENGINE)
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

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	add_library(SwimVulkan INTERFACE)
	add_library(Vulkan::Vulkan ALIAS SwimVulkan)
	add_library(SwimOpenGL INTERFACE)
	add_library(OpenGL::GL ALIAS SwimOpenGL)
else()
	find_package(Vulkan REQUIRED)
	find_package(OpenGL REQUIRED)
endif()

file(GLOB_RECURSE SWIM_ENGINE_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Engine/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/*.h
	${CMAKE_SOURCE_DIR}/Source/Engine/*.hpp
)
list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Platform/")
list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Input/")
list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Memory/")
list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Jobs/")
list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/IO/")
list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/EngineConfig[.]cpp$")

file(GLOB_RECURSE SWIM_GAME_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Game/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Game/*.h
	${CMAKE_SOURCE_DIR}/Source/Game/*.hpp
)

file(GLOB_RECURSE SWIM_SHADER_SOURCES
	${CMAKE_SOURCE_DIR}/Source/Shaders/*.hlsl
	${CMAKE_SOURCE_DIR}/Source/Shaders/*.glsl
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

target_compile_definitions(SwimEngine PRIVATE
	NDEBUG
	_CONSOLE
	UNICODE
	_UNICODE
	_CRT_SECURE_NO_WARNINGS
	PX_PHYSX_STATIC_LIB
	$<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
	$<$<PLATFORM_ID:Windows>:NOMINMAX>
	$<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR>
	$<$<CONFIG:Debug>:_SWIM_DEBUG>
	$<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>
)

if(SWIM_ENABLE_PCH)
	target_precompile_headers(SwimEngine PRIVATE
		"$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/Source/Engine/Utility/PCH.h>"
	)
endif()

# These implementation TUs own third-party implementation macros and must not
# inherit PCH definitions that can instantiate the same headers prematurely.
set_source_files_properties(
	Source/Engine/ThirdParty/TinyGltfImplementation.cpp
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

if(TARGET SwimPhysXBuild)
	# Interface-target dependencies are not handled uniformly by every generator.
	# Keep the external SDK build explicit so its staged .lib files exist before
	# Swim Engine reaches the link step.
	add_dependencies(SwimEngine SwimPhysXBuild)
endif()

target_link_libraries(SwimEngine PRIVATE
	Swim::Core
	Swim::Memory
	Swim::Jobs
	Swim::IO
	Swim::Platform
	Swim::Input
	Vulkan::Vulkan
	OpenGL::GL
	glm::glm
	EnTT::EnTT
	nlohmann_json::nlohmann_json
	stb::stb
	tinygltf::tinygltf
	Swim::WebP
	draco::draco
	zstd::zstd
	Swim::Basis
	glad::glad
	Swim::PhysX
)

include(cmake/Shaders.cmake)
swim_configure_shaders(SwimEngine)


if(NOT SWIM_OFFLINE_DEPENDENCY_STUBS)
	add_custom_command(TARGET SwimEngine POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_directory
			"${CMAKE_SOURCE_DIR}/Assets"
			"$<TARGET_FILE_DIR:SwimEngine>/Assets"
		COMMAND "${CMAKE_COMMAND}" -E copy_directory
			"${CMAKE_SOURCE_DIR}/Source/Shaders/OpenGL"
			"$<TARGET_FILE_DIR:SwimEngine>/Shaders/OpenGL"
		VERBATIM
	)
endif()
