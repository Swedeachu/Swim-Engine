if(NOT DEFINED SWIM_PHYSX_SOURCE_DIR OR NOT DEFINED SWIM_PHYSX_STAGE_DIR)
	message(FATAL_ERROR "BuildPhysX.cmake requires SWIM_PHYSX_SOURCE_DIR and SWIM_PHYSX_STAGE_DIR")
endif()

if(NOT WIN32)
	message(FATAL_ERROR "Swim Engine's pinned PhysX build is currently Windows-only")
endif()

set(SWIM_PHYSX_ROOT "${SWIM_PHYSX_SOURCE_DIR}/physx")
set(SWIM_PHYSX_PRESET "vc17win64-cpu-only")
set(SWIM_PHYSX_PRESET_FILE "${SWIM_PHYSX_ROOT}/buildtools/presets/public/${SWIM_PHYSX_PRESET}.xml")
set(SWIM_PHYSX_BUILD_DIR "${SWIM_PHYSX_ROOT}/compiler/${SWIM_PHYSX_PRESET}")
set(SWIM_PHYSX_GENERATOR "${SWIM_PHYSX_ROOT}/generate_projects.bat")

if(NOT EXISTS "${SWIM_PHYSX_GENERATOR}")
	message(FATAL_ERROR "Pinned PhysX source is incomplete: ${SWIM_PHYSX_GENERATOR} is missing")
endif()

if(NOT EXISTS "${SWIM_PHYSX_PRESET_FILE}")
	message(FATAL_ERROR
		"Pinned PhysX release is missing the required CPU-only preset '${SWIM_PHYSX_PRESET}'. "
		"Refusing to fall back to a GPU/CUDA preset because that would change Swim Engine's build contract."
	)
endif()

# NVIDIA's generator bootstraps the exact external packages needed by this
# PhysX release through Packman and creates the VS2022 CPU-only build tree.
if(NOT EXISTS "${SWIM_PHYSX_BUILD_DIR}/CMakeCache.txt")
	execute_process(
		COMMAND cmd.exe /d /c "call generate_projects.bat ${SWIM_PHYSX_PRESET}"
		WORKING_DIRECTORY "${SWIM_PHYSX_ROOT}"
		RESULT_VARIABLE SWIM_PHYSX_GENERATE_RESULT
		OUTPUT_VARIABLE SWIM_PHYSX_GENERATE_STDOUT
		ERROR_VARIABLE SWIM_PHYSX_GENERATE_STDERR
	)
	if(NOT SWIM_PHYSX_GENERATE_RESULT EQUAL 0)
		message(STATUS "${SWIM_PHYSX_GENERATE_STDOUT}")
		message(STATUS "${SWIM_PHYSX_GENERATE_STDERR}")
		message(FATAL_ERROR "PhysX ${SWIM_PHYSX_PRESET} project generation failed (${SWIM_PHYSX_GENERATE_RESULT})")
	endif()
endif()

# Reconfigure the generated project with Swim Engine's historical ABI contract:
# static PhysX, static non-debug CRT, CPU-only, no samples/snippets/GPU projects.
execute_process(
	COMMAND "${CMAKE_COMMAND}"
		-S "${SWIM_PHYSX_ROOT}/compiler/public"
		-B "${SWIM_PHYSX_BUILD_DIR}"
		-DPX_GENERATE_STATIC_LIBRARIES=ON
		-DNV_USE_STATIC_WINCRT=ON
		-DNV_USE_DEBUG_WINCRT=OFF
		-DPX_BUILDSNIPPETS=OFF
		-DPX_BUILDPVDRUNTIME=OFF
		-DPX_GENERATE_GPU_PROJECTS=OFF
		-DPX_GENERATE_GPU_PROJECTS_ONLY=OFF
		-DPX_FLOAT_POINT_PRECISE_MATH=OFF
	RESULT_VARIABLE SWIM_PHYSX_RECONFIGURE_RESULT
	OUTPUT_VARIABLE SWIM_PHYSX_RECONFIGURE_STDOUT
	ERROR_VARIABLE SWIM_PHYSX_RECONFIGURE_STDERR
)
if(NOT SWIM_PHYSX_RECONFIGURE_RESULT EQUAL 0)
	message(STATUS "${SWIM_PHYSX_RECONFIGURE_STDOUT}")
	message(STATUS "${SWIM_PHYSX_RECONFIGURE_STDERR}")
	message(FATAL_ERROR "PhysX static-CRT reconfigure failed (${SWIM_PHYSX_RECONFIGURE_RESULT})")
endif()

foreach(SWIM_PHYSX_CONFIG IN ITEMS checked release)
	execute_process(
		COMMAND "${CMAKE_COMMAND}" --build "${SWIM_PHYSX_BUILD_DIR}" --config ${SWIM_PHYSX_CONFIG} --parallel
		RESULT_VARIABLE SWIM_PHYSX_BUILD_RESULT
	)
	if(NOT SWIM_PHYSX_BUILD_RESULT EQUAL 0)
		message(FATAL_ERROR "PhysX ${SWIM_PHYSX_CONFIG} build failed (${SWIM_PHYSX_BUILD_RESULT})")
	endif()

	file(MAKE_DIRECTORY "${SWIM_PHYSX_STAGE_DIR}/${SWIM_PHYSX_CONFIG}")

	foreach(SWIM_PHYSX_LIBRARY_NAME IN ITEMS
		PhysXFoundation_static_64.lib
		PhysXCommon_static_64.lib
		PhysX_static_64.lib
		PhysXExtensions_static_64.lib
		PhysXCooking_static_64.lib
		PhysXCharacterKinematic_static_64.lib
		PhysXVehicle2_static_64.lib
		PhysXPvdSDK_static_64.lib
	)
		file(GLOB_RECURSE SWIM_PHYSX_LIBRARY_CANDIDATES
			LIST_DIRECTORIES FALSE
			"${SWIM_PHYSX_ROOT}/bin/*/${SWIM_PHYSX_CONFIG}/${SWIM_PHYSX_LIBRARY_NAME}"
			"${SWIM_PHYSX_BUILD_DIR}/*/${SWIM_PHYSX_CONFIG}/${SWIM_PHYSX_LIBRARY_NAME}"
			"${SWIM_PHYSX_BUILD_DIR}/${SWIM_PHYSX_CONFIG}/${SWIM_PHYSX_LIBRARY_NAME}"
		)

		list(LENGTH SWIM_PHYSX_LIBRARY_CANDIDATES SWIM_PHYSX_LIBRARY_COUNT)
		if(SWIM_PHYSX_LIBRARY_COUNT EQUAL 0)
			# PhysXPvdSDK may be omitted by a Release PhysX build. Swim Engine never
			# linked it in Release, so only require it for Checked.
			if(SWIM_PHYSX_CONFIG STREQUAL "release" AND SWIM_PHYSX_LIBRARY_NAME STREQUAL "PhysXPvdSDK_static_64.lib")
				continue()
			endif()
			message(FATAL_ERROR "Could not locate ${SWIM_PHYSX_LIBRARY_NAME} after PhysX ${SWIM_PHYSX_CONFIG} build")
		endif()

		list(GET SWIM_PHYSX_LIBRARY_CANDIDATES 0 SWIM_PHYSX_LIBRARY_SOURCE)
		file(COPY_FILE
			"${SWIM_PHYSX_LIBRARY_SOURCE}"
			"${SWIM_PHYSX_STAGE_DIR}/${SWIM_PHYSX_CONFIG}/${SWIM_PHYSX_LIBRARY_NAME}"
			ONLY_IF_DIFFERENT
		)
	endforeach()
endforeach()
