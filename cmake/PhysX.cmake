include(ExternalProject)

# PhysX is intentionally isolated from the main CMake target graph. The SDK has
# its own Debug/Checked/Profile/Release configuration model and NVIDIA strongly
# recommends Checked for day-to-day development. Swim Engine historically did
# exactly that: its Debug x64 executable used PhysX Checked static libraries
# with the release/static MSVC CRT, while Release used PhysX Release.
#
# Building PhysX as an ExternalProject preserves that contract without allowing
# PhysX's four configuration names or compiler settings to leak into the Swim
# Engine solution. The immutable source checkout lives in the shared CPM cache;
# generated PhysX projects and binaries live only under build/.

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	add_library(SwimPhysX INTERFACE)
	add_library(Swim::PhysX ALIAS SwimPhysX)
	return()
endif()

if(NOT WIN32 OR NOT MSVC)
	message(FATAL_ERROR
		"The current Swim Engine PhysX backend is configured for Windows x64/MSVC, matching the legacy project. "
		"Port the platform layer before enabling this backend on another platform."
	)
endif()

CPMAddPackage(
	NAME swim_physx_source
	GITHUB_REPOSITORY NVIDIA-Omniverse/PhysX
	GIT_TAG 107.3-omni-and-physx-5.6.1
	DOWNLOAD_ONLY YES
	UPDATE_DISCONNECTED YES
	# PhysX's tag contains a few files whose Git-LFS attributes do not match
	# their stored blobs. A normal LFS filter can therefore make a brand-new
	# checkout report itself dirty immediately. Swim's CPU-only SDK build does
	# not consume those UX assets, so keep this source checkout byte-for-byte
	# identical to Git and let Packman handle PhysX's actual build dependencies.
	GIT_CONFIG
		"filter.lfs.process="
		"filter.lfs.smudge="
		"filter.lfs.clean="
		"filter.lfs.required=false"
)

# Persist the no-LFS checkout policy in this cached clone and normalize it to
# the pinned commit immediately. This handles machines with Git LFS installed
# globally and also repairs the five mis-attributed UX files in this upstream
# tag before CPM ever reuses the cache in the Visual Studio/soft configure.
find_package(Git REQUIRED)

# These steps write to a dependency cache that every build tree shares, so they
# are deliberately conditional: read the current state first and only write when
# the checkout is not already normalized. Unconditional `git config` / `reset
# --hard` / `clean -ffdx` on every configure took a lock on the shared repo even
# when there was nothing to do, which turns any concurrent configure into a
# `could not lock config file .git/config` failure. It also made every configure
# pay a full reset/clean of the PhysX tree.
function(swim_physx_normalize_git_config key expected)
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${swim_physx_source_SOURCE_DIR}" config --local --get "${key}"
		RESULT_VARIABLE SWIM_PHYSX_GIT_CONFIG_READ_RESULT
		OUTPUT_VARIABLE SWIM_PHYSX_GIT_CONFIG_VALUE
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)

	# `--get` exits non-zero when the key is unset, which is itself a state that
	# needs writing; only an exact match lets us skip the write.
	if(SWIM_PHYSX_GIT_CONFIG_READ_RESULT EQUAL 0
		AND "${SWIM_PHYSX_GIT_CONFIG_VALUE}" STREQUAL "${expected}")
		return()
	endif()

	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${swim_physx_source_SOURCE_DIR}" config --local
			"${key}" "${expected}"
		RESULT_VARIABLE SWIM_PHYSX_GIT_CONFIG_RESULT
	)
	if(NOT SWIM_PHYSX_GIT_CONFIG_RESULT EQUAL 0)
		message(FATAL_ERROR
			"Could not normalize PhysX Git-LFS configuration key '${key}'. "
			"If another CMake configure or Git process is using "
			"'${swim_physx_source_SOURCE_DIR}' concurrently, let it finish and retry."
		)
	endif()
endfunction()

foreach(SWIM_PHYSX_GIT_CONFIG_KEY IN ITEMS
	filter.lfs.process
	filter.lfs.smudge
	filter.lfs.clean
)
	swim_physx_normalize_git_config("${SWIM_PHYSX_GIT_CONFIG_KEY}" "")
endforeach()

swim_physx_normalize_git_config(filter.lfs.required false)

# Only reset/clean when the checkout has actually drifted. A pristine cache is
# the normal case, and this keeps the shared repository read-only in it.
execute_process(
	COMMAND "${GIT_EXECUTABLE}" -C "${swim_physx_source_SOURCE_DIR}" status --porcelain --untracked-files=all
	RESULT_VARIABLE SWIM_PHYSX_STATUS_RESULT
	OUTPUT_VARIABLE SWIM_PHYSX_STATUS_OUTPUT
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET
)

if(NOT SWIM_PHYSX_STATUS_RESULT EQUAL 0 OR NOT SWIM_PHYSX_STATUS_OUTPUT STREQUAL "")
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${swim_physx_source_SOURCE_DIR}" reset --hard HEAD
		RESULT_VARIABLE SWIM_PHYSX_RESET_RESULT
		OUTPUT_QUIET
	)
	if(NOT SWIM_PHYSX_RESET_RESULT EQUAL 0)
		message(FATAL_ERROR "Could not reset the pinned PhysX dependency checkout")
	endif()

	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${swim_physx_source_SOURCE_DIR}" clean -ffdx
		RESULT_VARIABLE SWIM_PHYSX_CLEAN_RESULT
		OUTPUT_QUIET
	)
	if(NOT SWIM_PHYSX_CLEAN_RESULT EQUAL 0)
		message(FATAL_ERROR "Could not clean generated files from the pinned PhysX dependency checkout")
	endif()
endif()

set(SWIM_PHYSX_STAGE_DIR "${CMAKE_BINARY_DIR}/_deps/physx-stage")
set(SWIM_PHYSX_CHECKED_DIR "${SWIM_PHYSX_STAGE_DIR}/checked")
set(SWIM_PHYSX_RELEASE_DIR "${SWIM_PHYSX_STAGE_DIR}/release")

# NVIDIA's public Windows generator writes CMake/MSBuild output underneath the
# source tree. Never run it in CPM's cached checkout: that would make the cache
# dirty after every successful build. Instead BuildPhysX.cmake creates a short
# detached Git worktree at build/.px. The worktree shares Git objects with the
# pinned cache checkout, keeps MSBuild paths below MAX_PATH, and contains every
# NVIDIA-generated compiler/bin artifact outside the dependency source cache.
set(SWIM_PHYSX_SHORT_SOURCE_DIR "${CMAKE_SOURCE_DIR}/build/.px" CACHE PATH
	"Short generated PhysX Git worktree used while generating/building PhysX")

set(SWIM_PHYSX_LIBRARY_NAMES
	PhysXFoundation_static_64.lib
	PhysXCommon_static_64.lib
	PhysX_static_64.lib
	PhysXExtensions_static_64.lib
	PhysXCooking_static_64.lib
	PhysXCharacterKinematic_static_64.lib
	PhysXVehicle2_static_64.lib
	PhysXPvdSDK_static_64.lib
)

set(SWIM_PHYSX_BYPRODUCTS)
foreach(SWIM_PHYSX_LIBRARY_NAME IN LISTS SWIM_PHYSX_LIBRARY_NAMES)
	list(APPEND SWIM_PHYSX_BYPRODUCTS
		"${SWIM_PHYSX_CHECKED_DIR}/${SWIM_PHYSX_LIBRARY_NAME}"
	)
	if(NOT SWIM_PHYSX_LIBRARY_NAME STREQUAL "PhysXPvdSDK_static_64.lib")
		list(APPEND SWIM_PHYSX_BYPRODUCTS
			"${SWIM_PHYSX_RELEASE_DIR}/${SWIM_PHYSX_LIBRARY_NAME}"
		)
	endif()
endforeach()

ExternalProject_Add(SwimPhysXBuild
	SOURCE_DIR "${swim_physx_source_SOURCE_DIR}"
	CONFIGURE_COMMAND ""
	BUILD_COMMAND
		"${CMAKE_COMMAND}"
		-DSWIM_PHYSX_SOURCE_DIR=<SOURCE_DIR>
		-DSWIM_PHYSX_SHORT_SOURCE_DIR=${SWIM_PHYSX_SHORT_SOURCE_DIR}
		-DSWIM_PHYSX_STAGE_DIR=${SWIM_PHYSX_STAGE_DIR}
		-P "${CMAKE_SOURCE_DIR}/cmake/BuildPhysX.cmake"
	INSTALL_COMMAND ""
	BUILD_BYPRODUCTS ${SWIM_PHYSX_BYPRODUCTS}
	USES_TERMINAL_BUILD YES
)
swim_set_solution_folder(SwimPhysXBuild "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/PhysX")

function(swim_add_physx_import target_name library_name)
	add_library(${target_name} STATIC IMPORTED GLOBAL)
	set_target_properties(${target_name} PROPERTIES
		IMPORTED_CONFIGURATIONS "Debug;RelWithDebInfo;Release"
		IMPORTED_LOCATION_DEBUG "${SWIM_PHYSX_CHECKED_DIR}/${library_name}"
		IMPORTED_LOCATION_RELWITHDEBINFO "${SWIM_PHYSX_CHECKED_DIR}/${library_name}"
		IMPORTED_LOCATION_RELEASE "${SWIM_PHYSX_RELEASE_DIR}/${library_name}"
	)
endfunction()

swim_add_physx_import(SwimPhysXFoundation PhysXFoundation_static_64.lib)
swim_add_physx_import(SwimPhysXCommon PhysXCommon_static_64.lib)
swim_add_physx_import(SwimPhysXCore PhysX_static_64.lib)
swim_add_physx_import(SwimPhysXExtensions PhysXExtensions_static_64.lib)
swim_add_physx_import(SwimPhysXCooking PhysXCooking_static_64.lib)
swim_add_physx_import(SwimPhysXCharacterKinematic PhysXCharacterKinematic_static_64.lib)
swim_add_physx_import(SwimPhysXVehicle2 PhysXVehicle2_static_64.lib)
swim_add_physx_import(SwimPhysXPvdSDK PhysXPvdSDK_static_64.lib)

add_library(SwimPhysX INTERFACE)
add_dependencies(SwimPhysX SwimPhysXBuild)
target_include_directories(SwimPhysX SYSTEM INTERFACE
	"${swim_physx_source_SOURCE_DIR}/physx/include"
)
target_compile_definitions(SwimPhysX INTERFACE PX_PHYSX_STATIC_LIB)
target_link_libraries(SwimPhysX INTERFACE
	SwimPhysXFoundation
	SwimPhysXCommon
	SwimPhysXCore
	SwimPhysXExtensions
	SwimPhysXCooking
	SwimPhysXCharacterKinematic
	SwimPhysXVehicle2
	$<$<NOT:$<CONFIG:Release>>:SwimPhysXPvdSDK>
	ws2_32
)
add_library(Swim::PhysX ALIAS SwimPhysX)
