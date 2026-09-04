if(
	NOT DEFINED SWIM_PHYSX_SOURCE_DIR
	OR NOT DEFINED SWIM_PHYSX_SHORT_SOURCE_DIR
	OR NOT DEFINED SWIM_PHYSX_STAGE_DIR
)
	message(FATAL_ERROR
		"BuildPhysX.cmake requires SWIM_PHYSX_SOURCE_DIR, SWIM_PHYSX_SHORT_SOURCE_DIR, and SWIM_PHYSX_STAGE_DIR"
	)
endif()

if(NOT WIN32)
	message(FATAL_ERROR "Swim Engine's pinned PhysX build is currently Windows-only")
endif()

find_package(Git REQUIRED)
if(NOT GIT_EXECUTABLE)
	message(FATAL_ERROR "Git is required to create Swim Engine's short PhysX build worktree")
endif()

# The CPM checkout is immutable dependency source. NVIDIA's generator writes its
# compiler/bin trees relative to the source root, so building directly from CPM
# would make that cache dirty and poison later soft builds. A detached Git
# worktree gives PhysX a separate generated working tree while sharing the
# already-downloaded Git object database. Its short path also avoids MSBuild's
# legacy MAX_PATH limit.
execute_process(
	COMMAND "${GIT_EXECUTABLE}" -C "${SWIM_PHYSX_SOURCE_DIR}" rev-parse HEAD
	RESULT_VARIABLE SWIM_PHYSX_SOURCE_HEAD_RESULT
	OUTPUT_VARIABLE SWIM_PHYSX_SOURCE_HEAD
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_VARIABLE SWIM_PHYSX_SOURCE_HEAD_ERROR
)
if(NOT SWIM_PHYSX_SOURCE_HEAD_RESULT EQUAL 0 OR SWIM_PHYSX_SOURCE_HEAD STREQUAL "")
	message(FATAL_ERROR
		"Could not resolve the pinned PhysX source revision: ${SWIM_PHYSX_SOURCE_HEAD_ERROR}"
	)
endif()

get_filename_component(SWIM_PHYSX_SHORT_SOURCE_PARENT "${SWIM_PHYSX_SHORT_SOURCE_DIR}" DIRECTORY)
file(MAKE_DIRECTORY "${SWIM_PHYSX_SHORT_SOURCE_PARENT}")

set(SWIM_PHYSX_REUSE_WORKTREE OFF)
if(
	EXISTS "${SWIM_PHYSX_SHORT_SOURCE_DIR}/.git"
	AND NOT IS_DIRECTORY "${SWIM_PHYSX_SHORT_SOURCE_DIR}/.git"
	AND EXISTS "${SWIM_PHYSX_SHORT_SOURCE_DIR}/physx/generate_projects.bat"
)
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${SWIM_PHYSX_SHORT_SOURCE_DIR}" rev-parse HEAD
		RESULT_VARIABLE SWIM_PHYSX_WORKTREE_HEAD_RESULT
		OUTPUT_VARIABLE SWIM_PHYSX_WORKTREE_HEAD
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)
	if(
		SWIM_PHYSX_WORKTREE_HEAD_RESULT EQUAL 0
		AND SWIM_PHYSX_WORKTREE_HEAD STREQUAL SWIM_PHYSX_SOURCE_HEAD
	)
		set(SWIM_PHYSX_REUSE_WORKTREE ON)
	endif()
endif()

if(NOT SWIM_PHYSX_REUSE_WORKTREE)
	# First ask Git to unregister a previous real worktree. Older Swim revisions
	# used a directory junction at this same path, so also issue a plain rmdir:
	# on Windows that removes the junction itself without traversing its target,
	# while harmlessly failing for a non-empty normal directory.
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${SWIM_PHYSX_SOURCE_DIR}" worktree remove --force "${SWIM_PHYSX_SHORT_SOURCE_DIR}"
		OUTPUT_QUIET
		ERROR_QUIET
	)

	file(TO_NATIVE_PATH "${SWIM_PHYSX_SHORT_SOURCE_DIR}" SWIM_PHYSX_SHORT_SOURCE_DIR_NATIVE)
	execute_process(
		COMMAND cmd.exe /d /c rmdir "${SWIM_PHYSX_SHORT_SOURCE_DIR_NATIVE}"
		OUTPUT_QUIET
		ERROR_QUIET
	)

	if(EXISTS "${SWIM_PHYSX_SHORT_SOURCE_DIR}")
		file(REMOVE_RECURSE "${SWIM_PHYSX_SHORT_SOURCE_DIR}")
	endif()

	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${SWIM_PHYSX_SOURCE_DIR}" worktree prune
		OUTPUT_QUIET
		ERROR_QUIET
	)

	# Disable LFS filters for this worktree as well. The pinned PhysX tag has a
	# handful of incorrectly attributed UX files; they are irrelevant to the
	# CPU-only SDK but can otherwise make a pristine checkout appear modified.
	execute_process(
		COMMAND "${GIT_EXECUTABLE}"
			-c "filter.lfs.process="
			-c "filter.lfs.smudge="
			-c "filter.lfs.clean="
			-c "filter.lfs.required=false"
			-C "${SWIM_PHYSX_SOURCE_DIR}"
			worktree add --force --detach "${SWIM_PHYSX_SHORT_SOURCE_DIR}" "${SWIM_PHYSX_SOURCE_HEAD}"
		RESULT_VARIABLE SWIM_PHYSX_WORKTREE_RESULT
		OUTPUT_VARIABLE SWIM_PHYSX_WORKTREE_STDOUT
		ERROR_VARIABLE SWIM_PHYSX_WORKTREE_STDERR
	)
	if(NOT SWIM_PHYSX_WORKTREE_RESULT EQUAL 0)
		message(STATUS "${SWIM_PHYSX_WORKTREE_STDOUT}")
		message(STATUS "${SWIM_PHYSX_WORKTREE_STDERR}")
		message(FATAL_ERROR
			"Could not create the short PhysX Git worktree '${SWIM_PHYSX_SHORT_SOURCE_DIR}'."
		)
	endif()
endif()

if(NOT EXISTS "${SWIM_PHYSX_SHORT_SOURCE_DIR}/physx/generate_projects.bat")
	message(FATAL_ERROR
		"The short PhysX worktree does not contain the pinned source: ${SWIM_PHYSX_SHORT_SOURCE_DIR}"
	)
endif()

# The CPM source checkout must remain pristine even after an earlier PhysX
# build. Catch accidental source-tree writes here before they become mysterious
# CPM dirty-cache warnings on the next configure.
execute_process(
	COMMAND "${GIT_EXECUTABLE}" -C "${SWIM_PHYSX_SOURCE_DIR}" status --porcelain --untracked-files=all
	RESULT_VARIABLE SWIM_PHYSX_SOURCE_STATUS_RESULT
	OUTPUT_VARIABLE SWIM_PHYSX_SOURCE_STATUS
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_VARIABLE SWIM_PHYSX_SOURCE_STATUS_ERROR
)
if(NOT SWIM_PHYSX_SOURCE_STATUS_RESULT EQUAL 0)
	message(FATAL_ERROR "Could not verify the PhysX dependency cache: ${SWIM_PHYSX_SOURCE_STATUS_ERROR}")
endif()
if(NOT SWIM_PHYSX_SOURCE_STATUS STREQUAL "")
	message(FATAL_ERROR
		"The pinned PhysX CPM checkout is dirty before the SDK build. A clean build must produce immutable dependency sources.\n"
		"${SWIM_PHYSX_SOURCE_STATUS}"
	)
endif()

set(SWIM_PHYSX_ROOT "${SWIM_PHYSX_SHORT_SOURCE_DIR}/physx")
set(SWIM_PHYSX_PRESET "vc17win64-cpu-only")
set(SWIM_PHYSX_PRESET_FILE "${SWIM_PHYSX_ROOT}/buildtools/presets/public/${SWIM_PHYSX_PRESET}.xml")
set(SWIM_PHYSX_BUILD_DIR "${SWIM_PHYSX_ROOT}/compiler/${SWIM_PHYSX_PRESET}")
set(SWIM_PHYSX_GENERATOR "${SWIM_PHYSX_ROOT}/generate_projects.bat")

if(NOT EXISTS "${SWIM_PHYSX_GENERATOR}")
	message(FATAL_ERROR "Pinned PhysX worktree is incomplete: ${SWIM_PHYSX_GENERATOR} is missing")
endif()

if(NOT EXISTS "${SWIM_PHYSX_PRESET_FILE}")
	message(FATAL_ERROR
		"Pinned PhysX release is missing the required CPU-only preset '${SWIM_PHYSX_PRESET}'. "
		"Refusing to fall back to a GPU/CUDA preset because that would change Swim Engine's build contract."
	)
endif()

# NVIDIA's generator bootstraps the exact external packages needed by this
# PhysX release through Packman and creates the VS2022 CPU-only build tree.
file(TO_NATIVE_PATH "${SWIM_PHYSX_GENERATOR}" SWIM_PHYSX_GENERATOR_NATIVE)
if(NOT EXISTS "${SWIM_PHYSX_BUILD_DIR}/CMakeCache.txt")
	# Invoke the generator through its absolute path. cmd.exe only searches the
	# working directory for a command when NoDefaultCurrentDirectoryInExePath is
	# unset, and some CI/sandbox environments set it, which would otherwise fail
	# here with a confusing "'generate_projects.bat' is not recognized" error.
	# The working directory still matters: the script resolves its own paths
	# relative to the PhysX root.
	execute_process(
		COMMAND cmd.exe /d /c "${SWIM_PHYSX_GENERATOR_NATIVE}" "${SWIM_PHYSX_PRESET}"
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

# Re-verify the immutable source checkout after both SDK configurations finish.
# Generated files belong exclusively to the short worktree.
execute_process(
	COMMAND "${GIT_EXECUTABLE}" -C "${SWIM_PHYSX_SOURCE_DIR}" status --porcelain --untracked-files=all
	RESULT_VARIABLE SWIM_PHYSX_FINAL_STATUS_RESULT
	OUTPUT_VARIABLE SWIM_PHYSX_FINAL_STATUS
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_VARIABLE SWIM_PHYSX_FINAL_STATUS_ERROR
)
if(NOT SWIM_PHYSX_FINAL_STATUS_RESULT EQUAL 0)
	message(FATAL_ERROR "Could not re-verify the PhysX dependency cache: ${SWIM_PHYSX_FINAL_STATUS_ERROR}")
endif()
if(NOT SWIM_PHYSX_FINAL_STATUS STREQUAL "")
	message(FATAL_ERROR
		"PhysX generation/build modified the CPM dependency checkout, which violates Swim's clean-build contract.\n"
		"${SWIM_PHYSX_FINAL_STATUS}"
	)
endif()
