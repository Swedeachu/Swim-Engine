cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW)
endif()

if(POLICY CMP0141)
	cmake_policy(SET CMP0141 NEW)
endif()

# The legacy x64 project used /MT in both Debug and Release. Set this before
# project()/dependencies so every CMake-built static dependency shares the ABI.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
	"$<$<CONFIG:Debug,Release,RelWithDebInfo>:ProgramDatabase>"
)

project(SwimEngine C CXX)

option(SWIM_WARNINGS_AS_ERRORS "Treat Swim Engine warnings as errors" OFF)
option(SWIM_ENABLE_PCH "Use Swim Engine's existing PCH.h" ON)
option(SWIM_OFFLINE_DEPENDENCY_STUBS "Configure without downloading dependencies (build-layout verification only)" OFF)

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

if(NOT SWIM_OFFLINE_DEPENDENCY_STUBS AND NOT WIN32)
	message(FATAL_ERROR
		"This compute-v2 snapshot still contains Win32 platform/input/window code. "
		"The CMake migration preserves the currently supported Windows x64 build; Linux support belongs to the platform refactor."
	)
endif()

if(NOT SWIM_OFFLINE_DEPENDENCY_STUBS)
	include(cmake/get_cpm.cmake)
endif()
include(cmake/Dependencies.cmake)

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	add_library(SwimVulkan INTERFACE)
	add_library(Vulkan::Vulkan ALIAS SwimVulkan)
	add_library(SwimOpenGL INTERFACE)
	add_library(OpenGL::GL ALIAS SwimOpenGL)
else()
	find_package(Vulkan REQUIRED)
	find_package(OpenGL REQUIRED)
endif()

file(GLOB_RECURSE SWIM_ENGINE_SOURCES CONFIGURE_DEPENDS
	${CMAKE_SOURCE_DIR}/Source/Engine/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Engine/*.h
	${CMAKE_SOURCE_DIR}/Source/Engine/*.hpp
)

file(GLOB_RECURSE SWIM_GAME_SOURCES CONFIGURE_DEPENDS
	${CMAKE_SOURCE_DIR}/Source/Game/*.cpp
	${CMAKE_SOURCE_DIR}/Source/Game/*.h
	${CMAKE_SOURCE_DIR}/Source/Game/*.hpp
)

file(GLOB_RECURSE SWIM_SHADER_SOURCES CONFIGURE_DEPENDS
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
