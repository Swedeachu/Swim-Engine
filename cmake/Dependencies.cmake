# All third-party code is resolved into the build tree. Nothing under this
# file is copied into Source/Library or committed to the repository.

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	foreach(SWIM_STUB_TARGET IN ITEMS
		SwimEnTT
		SwimSpdlog
	)
		add_library(${SWIM_STUB_TARGET} INTERFACE)
	endforeach()

	if(NOT TARGET glm::glm)
		message(FATAL_ERROR "glm::glm must be provided by cmake/MathDependencies.cmake before runtime dependencies")
	endif()
	add_library(EnTT::EnTT ALIAS SwimEnTT)
	add_library(spdlog::spdlog ALIAS SwimSpdlog)

	if(SWIM_ENABLE_PHYSX_BACKEND)
		include(cmake/PhysX.cmake)
	endif()
	return()
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
CPMAddPackage(
	NAME spdlog_source
	GITHUB_REPOSITORY gabime/spdlog
	GIT_TAG v1.15.3
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)
if(NOT TARGET spdlog::spdlog)
	message(FATAL_ERROR "spdlog v1.15.3 did not provide spdlog::spdlog")
endif()
swim_set_solution_folder(spdlog "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/spdlog")


function(swim_assert_cached_git_dependency_clean dependency_name source_dir)
	if(NOT EXISTS "${source_dir}/.git")
		return()
	endif()

	find_package(Git REQUIRED)
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" status --porcelain --untracked-files=all
		RESULT_VARIABLE SWIM_DEPENDENCY_STATUS_RESULT
		OUTPUT_VARIABLE SWIM_DEPENDENCY_STATUS
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_VARIABLE SWIM_DEPENDENCY_STATUS_ERROR
	)
	if(NOT SWIM_DEPENDENCY_STATUS_RESULT EQUAL 0)
		message(FATAL_ERROR
			"Could not verify cached dependency '${dependency_name}': ${SWIM_DEPENDENCY_STATUS_ERROR}"
		)
	endif()

	if(NOT SWIM_DEPENDENCY_STATUS STREQUAL "")
		message(FATAL_ERROR
			"Cached dependency '${dependency_name}' is dirty. Swim dependency sources are immutable; "
			"generated files must live in the build tree. Run the clean build to repopulate the cache.\n"
			"${SWIM_DEPENDENCY_STATUS}"
		)
	endif()
endfunction()

if(NOT TARGET glm::glm)
	message(FATAL_ERROR "glm::glm foundation dependency was not initialized")
endif()

CPMAddPackage(
	NAME entt_source
	GITHUB_REPOSITORY skypjack/entt
	GIT_TAG v3.13.2
	DOWNLOAD_ONLY YES
	UPDATE_DISCONNECTED YES
)
add_library(SwimEnTT INTERFACE)
target_include_directories(SwimEnTT SYSTEM INTERFACE
	"${entt_source_SOURCE_DIR}/src"
)
add_library(EnTT::EnTT ALIAS SwimEnTT)

if(SWIM_ENABLE_PHYSX_BACKEND)
	include(cmake/PhysX.cmake)
endif()

foreach(SWIM_CACHED_GIT_DEPENDENCY IN ITEMS
	mimalloc_source
	sdl3_source
	glm_source
	entt_source
	spdlog_source
	swim_physx_source
)
	set(SWIM_CACHED_GIT_SOURCE_VARIABLE "${SWIM_CACHED_GIT_DEPENDENCY}_SOURCE_DIR")
	if(DEFINED ${SWIM_CACHED_GIT_SOURCE_VARIABLE})
		swim_assert_cached_git_dependency_clean(
			"${SWIM_CACHED_GIT_DEPENDENCY}"
			"${${SWIM_CACHED_GIT_SOURCE_VARIABLE}}"
		)
	endif()
endforeach()
