# General job scheduler dependency. enkiTS is intentionally isolated behind
# Swim::Jobs so gameplay and renderer code never include TaskScheduler.h.

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	add_library(SwimEnkiTS INTERFACE)
	add_library(Swim::EnkiTS ALIAS SwimEnkiTS)
	set(SWIM_JOBS_USE_ENKITS OFF)
	return()
endif()

set(ENKITS_BUILD_C_INTERFACE OFF CACHE BOOL "" FORCE)
set(ENKITS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENKITS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ENKITS_INSTALL OFF CACHE BOOL "" FORCE)
set(ENKITS_SANITIZE OFF CACHE BOOL "" FORCE)
set(ENKITS_TASK_PRIORITIES_NUM 3 CACHE STRING "" FORCE)

set(SWIM_SAVED_JOBS_CMAKE_FOLDER "${CMAKE_FOLDER}")
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/enkiTS")
CPMAddPackage(
	NAME enkits_source
	GITHUB_REPOSITORY dougbinks/enkiTS
	GIT_TAG v1.12
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)
set(CMAKE_FOLDER "${SWIM_SAVED_JOBS_CMAKE_FOLDER}")
unset(SWIM_SAVED_JOBS_CMAKE_FOLDER)

if(NOT TARGET enkiTS)
	message(FATAL_ERROR "enkiTS v1.12 did not provide the expected enkiTS target")
endif()

add_library(Swim::EnkiTS ALIAS enkiTS)
set(SWIM_JOBS_USE_ENKITS ON)
swim_set_solution_folder(enkiTS "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/enkiTS")
