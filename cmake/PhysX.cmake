include(ExternalProject)

# PhysX is intentionally isolated from the main CMake target graph. The SDK has
# its own Debug/Checked/Profile/Release configuration model and NVIDIA strongly
# recommends Checked for day-to-day development. Swim Engine historically did
# exactly that: its Debug x64 executable used PhysX Checked static libraries
# with the release/static MSVC CRT, while Release used PhysX Release.
#
# Building PhysX as an ExternalProject preserves that contract without allowing
# PhysX's four configuration names or compiler settings to leak into the Swim
# Engine solution. The source checkout lives only under build/_deps.

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
)

set(SWIM_PHYSX_STAGE_DIR "${CMAKE_BINARY_DIR}/_deps/physx-stage")
set(SWIM_PHYSX_CHECKED_DIR "${SWIM_PHYSX_STAGE_DIR}/checked")
set(SWIM_PHYSX_RELEASE_DIR "${SWIM_PHYSX_STAGE_DIR}/release")

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
		-DSWIM_PHYSX_STAGE_DIR=${SWIM_PHYSX_STAGE_DIR}
		-P "${CMAKE_SOURCE_DIR}/cmake/BuildPhysX.cmake"
	INSTALL_COMMAND ""
	BUILD_BYPRODUCTS ${SWIM_PHYSX_BYPRODUCTS}
	USES_TERMINAL_BUILD YES
)

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
