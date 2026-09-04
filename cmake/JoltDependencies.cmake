# Cross-platform Jolt Physics dependency. Jolt is a foundation backend rather
# than a legacy-renderer dependency, so this file is included before the
# Windows-only runtime gate and its parity contract also runs on Linux.

if(TARGET Swim::Jolt)
	return()
endif()

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	return()
endif()

# Keep Jolt's own CMake from changing Swim's compiler policy or producing its
# samples/tools. The backend only needs the static physics library.
CPMAddPackage(
	NAME jolt_source
	GITHUB_REPOSITORY jrouwe/JoltPhysics
	GIT_TAG v5.6.0
	SOURCE_SUBDIR Build
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
	OPTIONS
		"JPH_BUILD_SHARED_LIBS OFF"
		"DOUBLE_PRECISION OFF"
		"USE_STATIC_MSVC_RUNTIME_LIBRARY ON"
		"OVERRIDE_CXX_FLAGS OFF"
		"INTERPROCEDURAL_OPTIMIZATION OFF"
		"ENABLE_ALL_WARNINGS OFF"
		"DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF"
		"DEBUG_RENDERER_IN_DISTRIBUTION OFF"
		"PROFILER_IN_DEBUG_AND_RELEASE OFF"
		"PROFILER_IN_DISTRIBUTION OFF"
		"ENABLE_OBJECT_STREAM OFF"
		"ENABLE_INSTALL OFF"
		"TARGET_UNIT_TESTS OFF"
		"TARGET_HELLO_WORLD OFF"
		"TARGET_PERFORMANCE_TEST OFF"
		"TARGET_SAMPLES OFF"
		"TARGET_VIEWER OFF"
		"JPH_USE_DX12 OFF"
		"JPH_USE_VK OFF"
		"JPH_USE_MTL OFF"
		"JPH_USE_CPU_COMPUTE OFF"
)

if(NOT TARGET Jolt::Jolt)
	message(FATAL_ERROR "Jolt Physics v5.6.0 did not provide the expected Jolt::Jolt target")
endif()

add_library(SwimJolt INTERFACE)
target_link_libraries(SwimJolt INTERFACE Jolt::Jolt)
add_library(Swim::Jolt ALIAS SwimJolt)

swim_set_solution_folder(Jolt "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Jolt")
