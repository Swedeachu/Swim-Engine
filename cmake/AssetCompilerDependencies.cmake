# Asset compiler dependency boundary. fastgltf, meshoptimizer, Draco, libwebp,
# and compiler-side stb are source/import tooling only. They must never leak into
# Swim::Assets or renderer/runtime public contracts. simdjson is provided
# explicitly before fastgltf so fastgltf does not download single-header files
# into its CPM source cache.

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


# Draco is a source/import codec only. fastgltf parses KHR_draco_mesh_compression
# metadata, then SwimAssetCompiler uses Draco to decode that payload into the
# backend-neutral IntermediateModel before meshoptimizer/runtime cooking. The
# shipping runtime never links Draco for already-cooked assets.
set(DRACO_TESTS OFF CACHE BOOL "" FORCE)
set(DRACO_INSTALL OFF CACHE BOOL "" FORCE)
set(DRACO_JS_GLUE OFF CACHE BOOL "" FORCE)
set(DRACO_UNITY_PLUGIN OFF CACHE BOOL "" FORCE)
set(DRACO_MAYA_PLUGIN OFF CACHE BOOL "" FORCE)
set(DRACO_ANIMATION_ENCODING OFF CACHE BOOL "" FORCE)
set(DRACO_POINT_CLOUD_COMPRESSION OFF CACHE BOOL "" FORCE)
set(DRACO_MESH_COMPRESSION ON CACHE BOOL "" FORCE)
set(DRACO_GLTF_BITSTREAM ON CACHE BOOL "" FORCE)
set(DRACO_TRANSCODER_SUPPORTED OFF CACHE BOOL "" FORCE)
set(DRACO_DEBUG_COMPILER_WARNINGS OFF CACHE BOOL "" FORCE)

set(SWIM_SAVED_ASSET_COMPILER_FOLDER "${CMAKE_FOLDER}")
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Asset Compiler/Draco")
# Draco 1.5.7 still calls the removed FindPythonInterp module. Keep OLD policy
# behavior scoped to this third-party directory only so CMake 4.x can configure
# the pinned dependency without emitting a project-level developer warning.
if(POLICY CMP0148)
	cmake_policy(PUSH)
	cmake_policy(SET CMP0148 OLD)
endif()
CPMAddPackage(
	NAME draco_source
	GITHUB_REPOSITORY google/draco
	GIT_TAG 1.5.7
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)
if(POLICY CMP0148)
	cmake_policy(POP)
endif()
set(CMAKE_FOLDER "${SWIM_SAVED_ASSET_COMPILER_FOLDER}")
unset(SWIM_SAVED_ASSET_COMPILER_FOLDER)

if(TARGET draco::draco)
	set(SWIM_ASSET_COMPILER_DRACO_TARGET draco::draco)
elseif(TARGET draco)
	set(SWIM_ASSET_COMPILER_DRACO_TARGET draco)
else()
	message(FATAL_ERROR "Draco 1.5.7 did not provide a decoder library target")
endif()
if(TARGET draco)
	swim_set_solution_folder(draco "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Asset Compiler/Draco")
endif()

# Draco 1.5.7's draco::draco alias does not reliably publish the include roots
# needed by consumers when embedded with add_subdirectory/CPM. In particular,
# public headers live below <source>/src while draco/draco_features.h is
# generated below the top-level binary directory. Keep that quirk behind a
# Swim-owned compiler adapter rather than leaking package layout knowledge into
# GltfImporter or test targets.
set(SWIM_ASSET_COMPILER_DRACO_SOURCE_INCLUDE_DIR "${draco_source_SOURCE_DIR}/src")
set(SWIM_ASSET_COMPILER_DRACO_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}")
if(NOT EXISTS "${SWIM_ASSET_COMPILER_DRACO_SOURCE_INCLUDE_DIR}/draco/compression/decode.h")
	message(FATAL_ERROR "Draco 1.5.7 source include root is missing draco/compression/decode.h")
endif()
if(NOT EXISTS "${SWIM_ASSET_COMPILER_DRACO_GENERATED_INCLUDE_DIR}/draco/draco_features.h")
	message(FATAL_ERROR "Draco 1.5.7 did not generate draco/draco_features.h in the expected build root")
endif()

add_library(SwimAssetCompilerDraco INTERFACE)
add_library(Swim::AssetCompilerDraco ALIAS SwimAssetCompilerDraco)
target_link_libraries(SwimAssetCompilerDraco INTERFACE ${SWIM_ASSET_COMPILER_DRACO_TARGET})
target_include_directories(SwimAssetCompilerDraco SYSTEM INTERFACE
	"${SWIM_ASSET_COMPILER_DRACO_SOURCE_INCLUDE_DIR}"
	"${SWIM_ASSET_COMPILER_DRACO_GENERATED_INCLUDE_DIR}"
)
swim_set_solution_folder(SwimAssetCompilerDraco "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Asset Compiler/Draco")

# PNG/JPEG decode is compiler-only here. Reuse the same stb revision as the
# legacy runtime dependency without exporting stb_image through compiler APIs.
if(NOT DEFINED stb_source_SOURCE_DIR OR NOT EXISTS "${stb_source_SOURCE_DIR}/stb_image.h")
	CPMAddPackage(
		NAME stb_source
		GITHUB_REPOSITORY nothings/stb
		GIT_TAG 2dfbe86
		DOWNLOAD_ONLY YES
		UPDATE_DISCONNECTED YES
	)
endif()
add_library(SwimAssetCompilerStb INTERFACE)
target_include_directories(SwimAssetCompilerStb SYSTEM INTERFACE "${stb_source_SOURCE_DIR}")

# WebP decode is an authoring/compiler concern. Only the core decoder library is
# built; runtime MaterialPool/TexturePool never receives libwebp.
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)

set(SWIM_HAD_WEBP_REQUIRED_QUIET FALSE)
if(DEFINED CMAKE_REQUIRED_QUIET)
	set(SWIM_HAD_WEBP_REQUIRED_QUIET TRUE)
	set(SWIM_SAVED_WEBP_REQUIRED_QUIET "${CMAKE_REQUIRED_QUIET}")
endif()
set(SWIM_HAD_WEBP_WARN_DEPRECATED FALSE)
if(DEFINED CMAKE_WARN_DEPRECATED)
	set(SWIM_HAD_WEBP_WARN_DEPRECATED TRUE)
	set(SWIM_SAVED_WEBP_WARN_DEPRECATED "${CMAKE_WARN_DEPRECATED}")
endif()
set(CMAKE_REQUIRED_QUIET TRUE)
set(CMAKE_WARN_DEPRECATED OFF)
set(SWIM_SAVED_ASSET_COMPILER_FOLDER "${CMAKE_FOLDER}")
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Asset Compiler/WebP")
CPMAddPackage(
	NAME webp_source
	GITHUB_REPOSITORY webmproject/libwebp
	GIT_TAG v1.5.0
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)
set(CMAKE_FOLDER "${SWIM_SAVED_ASSET_COMPILER_FOLDER}")
unset(SWIM_SAVED_ASSET_COMPILER_FOLDER)
if(SWIM_HAD_WEBP_REQUIRED_QUIET)
	set(CMAKE_REQUIRED_QUIET "${SWIM_SAVED_WEBP_REQUIRED_QUIET}")
else()
	unset(CMAKE_REQUIRED_QUIET)
endif()
if(SWIM_HAD_WEBP_WARN_DEPRECATED)
	set(CMAKE_WARN_DEPRECATED "${SWIM_SAVED_WEBP_WARN_DEPRECATED}")
else()
	unset(CMAKE_WARN_DEPRECATED)
endif()
unset(SWIM_HAD_WEBP_REQUIRED_QUIET)
unset(SWIM_SAVED_WEBP_REQUIRED_QUIET)
unset(SWIM_HAD_WEBP_WARN_DEPRECATED)
unset(SWIM_SAVED_WEBP_WARN_DEPRECATED)

if(TARGET WebP::webp)
	set(SWIM_ASSET_COMPILER_WEBP_TARGET WebP::webp)
elseif(TARGET webp)
	set(SWIM_ASSET_COMPILER_WEBP_TARGET webp)
else()
	message(FATAL_ERROR "libwebp v1.5.0 did not provide a WebP decoder target")
endif()

# One explicit private dependency bundle makes the compiler/runtime ownership
# visible at the CMake target boundary. SwimAssetCompiler is the only first-party
# production target that should consume this bundle; test fixtures may link an
# individual codec target only when they generate source-format test data.
add_library(SwimAssetCompilerDependencies INTERFACE)
add_library(Swim::AssetCompilerDependencies ALIAS SwimAssetCompilerDependencies)
target_link_libraries(SwimAssetCompilerDependencies INTERFACE
	fastgltf::fastgltf
	meshoptimizer
	Swim::AssetCompilerDraco
	SwimAssetCompilerStb
	${SWIM_ASSET_COMPILER_WEBP_TARGET}
)
swim_set_solution_folder(SwimAssetCompilerDependencies "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Asset Compiler")

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
swim_assert_asset_compiler_dependency_clean("draco" "${draco_source_SOURCE_DIR}")
swim_assert_asset_compiler_dependency_clean("stb" "${stb_source_SOURCE_DIR}")
swim_assert_asset_compiler_dependency_clean("webp" "${webp_source_SOURCE_DIR}")

set(SWIM_ASSET_COMPILER_DEPENDENCIES_AVAILABLE ON)
