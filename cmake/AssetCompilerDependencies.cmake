# fastgltf and meshoptimizer are tool/compiler-only. They must never leak into
# Swim::Assets or runtime renderer targets. simdjson is provided explicitly
# before fastgltf so fastgltf does not download single-header files into its CPM
# source cache.

set(SWIM_ASSET_COMPILER_DEPENDENCIES_AVAILABLE OFF)

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	return()
endif()

set(SIMDJSON_DEVELOPER_MODE OFF CACHE BOOL "" FORCE)
set(SIMDJSON_DEVELOPMENT_CHECKS OFF CACHE BOOL "" FORCE)
set(SIMDJSON_SINGLEHEADER OFF CACHE BOOL "" FORCE)
set(SIMDJSON_BUILD_STATIC_LIB OFF CACHE BOOL "" FORCE)

CPMAddPackage(
	NAME swim_simdjson_source
	GITHUB_REPOSITORY simdjson/simdjson
	GIT_TAG v3.12.3
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)

if(NOT TARGET simdjson::simdjson)
	message(FATAL_ERROR "simdjson v3.12.3 did not provide simdjson::simdjson")
endif()
swim_set_solution_folder(simdjson "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Asset Compiler/simdjson")

set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_GLTF_RS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_ASSIMP OFF CACHE BOOL "" FORCE)
set(FASTGLTF_COMPILE_AS_CPP20 ON CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_CPP_MODULES OFF CACHE BOOL "" FORCE)

CPMAddPackage(
	NAME swim_fastgltf_source
	GITHUB_REPOSITORY spnda/fastgltf
	GIT_TAG v0.9.0
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)

if(NOT TARGET fastgltf::fastgltf)
	message(FATAL_ERROR "fastgltf v0.9.0 did not provide fastgltf::fastgltf")
endif()
swim_set_solution_folder(fastgltf "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Asset Compiler/fastgltf")

set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
set(MESHOPT_WERROR OFF CACHE BOOL "" FORCE)

CPMAddPackage(
	NAME swim_meshoptimizer_source
	GITHUB_REPOSITORY zeux/meshoptimizer
	GIT_TAG v1.1
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)

if(NOT TARGET meshoptimizer)
	message(FATAL_ERROR "meshoptimizer v1.1 did not provide meshoptimizer")
endif()
swim_set_solution_folder(meshoptimizer "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Asset Compiler/meshoptimizer")

# CPM cache entries are immutable input. The explicit simdjson target above is
# important because fastgltf v0.9.0 otherwise file(DOWNLOAD)s simdjson into its
# source tree during configure.
function(swim_assert_asset_compiler_dependency_clean dependency_name source_dir)
	if(NOT EXISTS "${source_dir}/.git")
		return()
	endif()

	find_package(Git REQUIRED)
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" status --porcelain --untracked-files=all
		RESULT_VARIABLE SWIM_ASSET_DEPENDENCY_STATUS_RESULT
		OUTPUT_VARIABLE SWIM_ASSET_DEPENDENCY_STATUS
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_VARIABLE SWIM_ASSET_DEPENDENCY_STATUS_ERROR
	)
	if(NOT SWIM_ASSET_DEPENDENCY_STATUS_RESULT EQUAL 0)
		message(FATAL_ERROR
			"Could not verify cached asset-compiler dependency '${dependency_name}': ${SWIM_ASSET_DEPENDENCY_STATUS_ERROR}"
		)
	endif()
	if(NOT SWIM_ASSET_DEPENDENCY_STATUS STREQUAL "")
		message(FATAL_ERROR
			"Cached asset-compiler dependency '${dependency_name}' is dirty. Run the clean build to repopulate the cache.\n"
			"${SWIM_ASSET_DEPENDENCY_STATUS}"
		)
	endif()
endfunction()

swim_assert_asset_compiler_dependency_clean("simdjson" "${swim_simdjson_source_SOURCE_DIR}")
swim_assert_asset_compiler_dependency_clean("fastgltf" "${swim_fastgltf_source_SOURCE_DIR}")
swim_assert_asset_compiler_dependency_clean("meshoptimizer" "${swim_meshoptimizer_source_SOURCE_DIR}")

set(SWIM_ASSET_COMPILER_DEPENDENCIES_AVAILABLE ON)
