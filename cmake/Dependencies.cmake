# All third-party code is resolved into the build tree. Nothing under this
# file is copied into Source/Library or committed to the repository.

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	foreach(SWIM_STUB_TARGET IN ITEMS
		SwimEnTT
		SwimJson
		SwimStb
		SwimZstd
		SwimBasisTranscoder
		SwimGlad
		SwimSpdlog
	)
		add_library(${SWIM_STUB_TARGET} INTERFACE)
	endforeach()

	if(NOT TARGET glm::glm)
		message(FATAL_ERROR "glm::glm must be provided by cmake/MathDependencies.cmake before legacy dependencies")
	endif()
	add_library(EnTT::EnTT ALIAS SwimEnTT)
	add_library(nlohmann_json::nlohmann_json ALIAS SwimJson)
	add_library(stb::stb ALIAS SwimStb)
	add_library(zstd::zstd ALIAS SwimZstd)
	add_library(Swim::BasisTranscoder ALIAS SwimBasisTranscoder)
	add_library(glad::glad ALIAS SwimGlad)
	add_library(spdlog::spdlog ALIAS SwimSpdlog)

	if(SWIM_ENABLE_PHYSX_BACKEND)
		include(cmake/PhysX.cmake)
	endif()
	return()
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# spdlog 1.15.3 - mirrors Tungsten's console + file logging surface.
# Keep the compiled static target so all runtime code shares one default logger
# and one set of sinks.
# ---------------------------------------------------------------------------
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


# ---------------------------------------------------------------------------
# Dependency source integrity
#
# CPM's source cache is immutable input. Generated code/build output belongs in
# CMake build trees, never inside a cached Git checkout. A dirty cache is not a
# harmless warning: it can hide incomplete clones, stale generated files, or a
# dependency build that modified its own source and then breaks a later soft
# build. Audit every Git-backed dependency at the end of configure and fail
# immediately with the exact dirty paths.
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# GLM is a cross-platform foundation dependency and is created earlier by
# cmake/MathDependencies.cmake so generic physics also compiles on Linux.
# ---------------------------------------------------------------------------
if(NOT TARGET glm::glm)
	message(FATAL_ERROR "glm::glm foundation dependency was not initialized")
endif()

# ---------------------------------------------------------------------------
# EnTT 3.13.2 - matches the previously committed headers.
# Header-only for Swim Engine, so expose the canonical target ourselves rather
# than coupling configure success to EnTT's project-level CMake policies.
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# nlohmann/json 3.10.4 - matches the previous Source/Library/json copy.
#
# Do not clone the full nlohmann/json repository on Windows. The v3.10.4 repo
# contains historical benchmark/report paths that can exceed Win32 path limits
# and leave CPM with a dirty/incomplete checkout even when Git long-path support
# is enabled. Swim Engine only includes <nlohmann/json.hpp>, so cache the
# official release single-header artifact instead. The release publishes this
# exact SHA-256 for json.hpp. This is smaller, deterministic, and makes a
# corrupted cache fail during configure instead of much later while compiling
# the engine PCH.
# ---------------------------------------------------------------------------
set(SWIM_NLOHMANN_JSON_VERSION "3.10.4")
set(SWIM_NLOHMANN_JSON_SHA256
	"c9ac7589260f36ea7016d4d51a6c95809803298c7caec9f55830a0214c5f9140"
)
set(SWIM_NLOHMANN_JSON_INCLUDE_ROOT
	"${CPM_SOURCE_CACHE}/nlohmann_json_source/${SWIM_NLOHMANN_JSON_VERSION}/include"
)
set(SWIM_NLOHMANN_JSON_HEADER
	"${SWIM_NLOHMANN_JSON_INCLUDE_ROOT}/nlohmann/json.hpp"
)

function(swim_validate_nlohmann_json_header out_valid)
	set(SWIM_JSON_VALID FALSE)
	if(EXISTS "${SWIM_NLOHMANN_JSON_HEADER}")
		file(SHA256 "${SWIM_NLOHMANN_JSON_HEADER}" SWIM_JSON_ACTUAL_SHA256)
		if("${SWIM_JSON_ACTUAL_SHA256}" STREQUAL "${SWIM_NLOHMANN_JSON_SHA256}")
			set(SWIM_JSON_VALID TRUE)
		endif()
	endif()
	set(${out_valid} "${SWIM_JSON_VALID}" PARENT_SCOPE)
endfunction()

swim_validate_nlohmann_json_header(SWIM_NLOHMANN_JSON_VALID)
if(NOT SWIM_NLOHMANN_JSON_VALID)
	if(FETCHCONTENT_FULLY_DISCONNECTED)
		message(FATAL_ERROR
			"nlohmann/json ${SWIM_NLOHMANN_JSON_VERSION} is not present in the dependency cache or failed its SHA-256 check. "
			"The soft build is intentionally offline; run the clean build once to populate/repair the cache."
		)
	endif()

	file(REMOVE "${SWIM_NLOHMANN_JSON_HEADER}")
	file(MAKE_DIRECTORY "${SWIM_NLOHMANN_JSON_INCLUDE_ROOT}/nlohmann")
	set(SWIM_NLOHMANN_JSON_URL
		"https://github.com/nlohmann/json/releases/download/v${SWIM_NLOHMANN_JSON_VERSION}/json.hpp"
	)
	message(STATUS
		"Downloading nlohmann/json ${SWIM_NLOHMANN_JSON_VERSION} single header to ${SWIM_NLOHMANN_JSON_HEADER}"
	)
	file(DOWNLOAD
		"${SWIM_NLOHMANN_JSON_URL}"
		"${SWIM_NLOHMANN_JSON_HEADER}"
		EXPECTED_HASH "SHA256=${SWIM_NLOHMANN_JSON_SHA256}"
		STATUS SWIM_NLOHMANN_JSON_DOWNLOAD_STATUS
		TLS_VERIFY ON
		SHOW_PROGRESS
	)

	list(GET SWIM_NLOHMANN_JSON_DOWNLOAD_STATUS 0 SWIM_NLOHMANN_JSON_DOWNLOAD_CODE)
	if(NOT SWIM_NLOHMANN_JSON_DOWNLOAD_CODE EQUAL 0)
		list(GET SWIM_NLOHMANN_JSON_DOWNLOAD_STATUS 1 SWIM_NLOHMANN_JSON_DOWNLOAD_MESSAGE)
		file(REMOVE "${SWIM_NLOHMANN_JSON_HEADER}")
		message(FATAL_ERROR
			"Failed to download nlohmann/json ${SWIM_NLOHMANN_JSON_VERSION}: ${SWIM_NLOHMANN_JSON_DOWNLOAD_MESSAGE}"
		)
	endif()

	swim_validate_nlohmann_json_header(SWIM_NLOHMANN_JSON_VALID)
	if(NOT SWIM_NLOHMANN_JSON_VALID)
		file(REMOVE "${SWIM_NLOHMANN_JSON_HEADER}")
		message(FATAL_ERROR
			"nlohmann/json ${SWIM_NLOHMANN_JSON_VERSION} download completed but the cached json.hpp failed its SHA-256 check"
		)
	endif()
endif()

add_library(SwimJson INTERFACE)
target_include_directories(SwimJson SYSTEM INTERFACE
	"${SWIM_NLOHMANN_JSON_INCLUDE_ROOT}"
)
add_library(nlohmann_json::nlohmann_json ALIAS SwimJson)

# ---------------------------------------------------------------------------
# stb - headers only. This pin keeps stb_image 2.30 and the resize2 API used
# by the branch without compiling a second image implementation.
# ---------------------------------------------------------------------------
if(NOT DEFINED stb_source_SOURCE_DIR OR NOT EXISTS "${stb_source_SOURCE_DIR}/stb_image.h")
	CPMAddPackage(
		NAME stb_source
		GITHUB_REPOSITORY nothings/stb
		GIT_TAG 2dfbe86
		DOWNLOAD_ONLY YES
		UPDATE_DISCONNECTED YES
	)
endif()
add_library(SwimStb INTERFACE)
target_include_directories(SwimStb SYSTEM INTERFACE "${stb_source_SOURCE_DIR}")
add_library(stb::stb ALIAS SwimStb)

# ---------------------------------------------------------------------------
# Zstandard 1.4.9 - matches the previously committed zstd.c/zstd.h version.
#
# Do not add zstd's v1.4.9 CMake project as a subdirectory: its historical
# cmake_minimum_required() is rejected by modern CMake. Also do not compile
# contrib/single_file_libs/zstd-in.c directly: that file is the *input* to
# zstd's amalgamation generator, not the generated single-file library, and
# compiling it directly causes duplicate dictionary-builder definitions.
#
# Instead, mirror libzstd's normal source sets under Swim Engine's CMake. This
# keeps the exact v1.4.9 implementation without vendoring generated sources or
# evaluating zstd's obsolete project-level CMake.
# ---------------------------------------------------------------------------
CPMAddPackage(
	NAME zstd_source
	GITHUB_REPOSITORY facebook/zstd
	GIT_TAG v1.4.9
	DOWNLOAD_ONLY YES
	UPDATE_DISCONNECTED YES
)

file(GLOB SWIM_ZSTD_COMMON_SOURCES
	"${zstd_source_SOURCE_DIR}/lib/common/*.c"
)
file(GLOB SWIM_ZSTD_COMPRESS_SOURCES
	"${zstd_source_SOURCE_DIR}/lib/compress/*.c"
)
file(GLOB SWIM_ZSTD_DECOMPRESS_SOURCES
	"${zstd_source_SOURCE_DIR}/lib/decompress/*.c"
)
file(GLOB SWIM_ZSTD_DICTBUILDER_SOURCES
	"${zstd_source_SOURCE_DIR}/lib/dictBuilder/*.c"
)

add_library(SwimZstd STATIC
	${SWIM_ZSTD_COMMON_SOURCES}
	${SWIM_ZSTD_COMPRESS_SOURCES}
	${SWIM_ZSTD_DECOMPRESS_SOURCES}
	${SWIM_ZSTD_DICTBUILDER_SOURCES}
)
target_include_directories(SwimZstd SYSTEM
	PUBLIC
		"${zstd_source_SOURCE_DIR}/lib"
	PRIVATE
		"${zstd_source_SOURCE_DIR}/lib/common"
		"${zstd_source_SOURCE_DIR}/lib/compress"
		"${zstd_source_SOURCE_DIR}/lib/decompress"
		"${zstd_source_SOURCE_DIR}/lib/dictBuilder"
)
target_compile_definitions(SwimZstd PRIVATE
	ZSTD_DISABLE_ASM
	ZSTD_MULTITHREAD
	ZSTD_HEAPMODE=0
	_CRT_SECURE_NO_WARNINGS
)
add_library(zstd::zstd ALIAS SwimZstd)
swim_set_solution_folder(SwimZstd "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Zstd")

# ---------------------------------------------------------------------------
# Basis Universal 1.60 - KTX2 transcoder path used by legacy TexturePool residency. Only the
# transcoder TU is built; the encoder/tools are intentionally omitted.
# ---------------------------------------------------------------------------
CPMAddPackage(
	NAME basis_source
	GITHUB_REPOSITORY BinomialLLC/basis_universal
	GIT_TAG v1_60_snapshot_final
	DOWNLOAD_ONLY YES
	UPDATE_DISCONNECTED YES
)
set(SWIM_BASIS_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/basis")
file(MAKE_DIRECTORY "${SWIM_BASIS_GENERATED_DIR}")
file(READ "${basis_source_SOURCE_DIR}/transcoder/basisu_transcoder.cpp" SWIM_BASIS_TRANSCODER_SOURCE)
string(REPLACE
	"#include \"../zstd/zstd.h\""
	"#include <zstd.h>"
	SWIM_BASIS_TRANSCODER_SOURCE
	"${SWIM_BASIS_TRANSCODER_SOURCE}"
)
set(SWIM_BASIS_GENERATED_SOURCE "${SWIM_BASIS_GENERATED_DIR}/basisu_transcoder.cpp")
set(SWIM_BASIS_GENERATED_SOURCE_CURRENT "")
if(EXISTS "${SWIM_BASIS_GENERATED_SOURCE}")
	file(READ "${SWIM_BASIS_GENERATED_SOURCE}" SWIM_BASIS_GENERATED_SOURCE_CURRENT)
endif()
if(NOT SWIM_BASIS_GENERATED_SOURCE_CURRENT STREQUAL SWIM_BASIS_TRANSCODER_SOURCE)
	file(WRITE "${SWIM_BASIS_GENERATED_SOURCE}" "${SWIM_BASIS_TRANSCODER_SOURCE}")
endif()

add_library(SwimBasisTranscoder STATIC
	"${SWIM_BASIS_GENERATED_SOURCE}"
)
target_include_directories(SwimBasisTranscoder SYSTEM PUBLIC
	"${basis_source_SOURCE_DIR}/transcoder"
)
target_compile_definitions(SwimBasisTranscoder PUBLIC
	BASISU_FORCE_DEVEL_MESSAGES=0
	BASISD_SUPPORT_KTX2=1
	BASISD_SUPPORT_KTX2_ZSTD=1
	BASISD_SUPPORT_BASIS=1
	BASISD_SUPPORT_ZSTD=1
	BASISD_SUPPORT_ETC1S=1
	BASISD_SUPPORT_UASTC=1
)
target_link_libraries(SwimBasisTranscoder PUBLIC zstd::zstd)
add_library(Swim::BasisTranscoder ALIAS SwimBasisTranscoder)
swim_set_solution_folder(SwimBasisTranscoder "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Basis Universal")

# ---------------------------------------------------------------------------
# GLAD 2.0.8 - regenerate the exact OpenGL 4.6 + WGL loader previously
# committed under Source/Library/glad. Python is required by glad's generator.
# ---------------------------------------------------------------------------
CPMAddPackage(
	NAME glad_source
	GITHUB_REPOSITORY Dav1dde/glad
	GIT_TAG v2.0.8
	DOWNLOAD_ONLY YES
	UPDATE_DISCONNECTED YES
)
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/GLAD")
add_subdirectory("${glad_source_SOURCE_DIR}/cmake" "${glad_source_BINARY_DIR}/cmake" EXCLUDE_FROM_ALL)
glad_add_library(SwimGlad STATIC REPRODUCIBLE LOADER DEBUG
	API gl:core=4.6 wgl=1.0
	EXTENSIONS
		GL_KHR_debug
		WGL_ARB_create_context
		WGL_ARB_create_context_profile
		WGL_ARB_extensions_string
		WGL_ARB_multisample
		WGL_ARB_pixel_format
		WGL_EXT_extensions_string
		WGL_EXT_swap_control
)
add_library(glad::glad ALIAS SwimGlad)
swim_set_solution_folder(SwimGlad "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/GLAD")
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}")

if(SWIM_ENABLE_PHYSX_BACKEND)
	include(cmake/PhysX.cmake)
endif()

foreach(SWIM_CACHED_GIT_DEPENDENCY IN ITEMS
	mimalloc_source
	sdl3_source
	glm_source
	entt_source
	stb_source
	zstd_source
	basis_source
	glad_source
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
