# All third-party code is resolved into the build tree. Nothing under this
# file is copied into Source/Library or committed to the repository.

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	foreach(SWIM_STUB_TARGET IN ITEMS
		SwimGlm
		SwimEnTT
		SwimJson
		SwimStb
		SwimTinyGltf
		SwimWebP
		SwimWebPBundle
		SwimDraco
		SwimZstd
		SwimBasis
		SwimGlad
	)
		add_library(${SWIM_STUB_TARGET} INTERFACE)
	endforeach()

	add_library(glm::glm ALIAS SwimGlm)
	add_library(EnTT::EnTT ALIAS SwimEnTT)
	add_library(nlohmann_json::nlohmann_json ALIAS SwimJson)
	add_library(stb::stb ALIAS SwimStb)
	add_library(tinygltf::tinygltf ALIAS SwimTinyGltf)
	add_library(WebP::webp ALIAS SwimWebP)
	add_library(Swim::WebP ALIAS SwimWebPBundle)
	add_library(draco::draco ALIAS SwimDraco)
	add_library(zstd::zstd ALIAS SwimZstd)
	add_library(Swim::Basis ALIAS SwimBasis)
	add_library(glad::glad ALIAS SwimGlad)

	include(cmake/PhysX.cmake)
	return()
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)


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
# GLM 1.0.0 - matches the previously committed headers.
# Header-only: do not execute the dependency's own CMake project. This avoids
# old-policy warnings/errors on newer CMake releases and keeps configuration
# limited to the files Swim Engine actually consumes.
# ---------------------------------------------------------------------------
CPMAddPackage(
	NAME glm_source
	GITHUB_REPOSITORY g-truc/glm
	GIT_TAG 1.0.0
	DOWNLOAD_ONLY YES
	UPDATE_DISCONNECTED YES
)
add_library(SwimGlm INTERFACE)
target_include_directories(SwimGlm SYSTEM INTERFACE
	"${glm_source_SOURCE_DIR}"
)
add_library(glm::glm ALIAS SwimGlm)

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
CPMAddPackage(
	NAME stb_source
	GITHUB_REPOSITORY nothings/stb
	GIT_TAG 2dfbe86
	DOWNLOAD_ONLY YES
	UPDATE_DISCONNECTED YES
)
add_library(SwimStb INTERFACE)
target_include_directories(SwimStb SYSTEM INTERFACE "${stb_source_SOURCE_DIR}")
add_library(stb::stb ALIAS SwimStb)

# ---------------------------------------------------------------------------
# Draco 1.5.7 - tinygltf's compressed-mesh decoder dependency.
# ---------------------------------------------------------------------------
set(DRACO_TESTS OFF CACHE BOOL "" FORCE)
set(DRACO_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DRACO_JS_GLUE OFF CACHE BOOL "" FORCE)
set(DRACO_UNITY_PLUGIN OFF CACHE BOOL "" FORCE)
set(DRACO_MAYA_PLUGIN OFF CACHE BOOL "" FORCE)
set(DRACO_TRANSCODER_SUPPORTED OFF CACHE BOOL "" FORCE)
# Draco 1.5.7 still calls the removed FindPythonInterp module. Its own
# cmake_minimum_required() resets policy state, so a parent cmake_policy(PUSH)
# does not suppress CMP0148 on modern CMake. Set the subproject policy default
# only while adding Draco, then restore the caller's value.
set(SWIM_HAD_CMP0148_DEFAULT FALSE)
if(DEFINED CMAKE_POLICY_DEFAULT_CMP0148)
	set(SWIM_HAD_CMP0148_DEFAULT TRUE)
	set(SWIM_SAVED_CMP0148_DEFAULT "${CMAKE_POLICY_DEFAULT_CMP0148}")
endif()
set(CMAKE_POLICY_DEFAULT_CMP0148 OLD)
set(SWIM_HAD_CMAKE_WARN_DEPRECATED FALSE)
if(DEFINED CMAKE_WARN_DEPRECATED)
	set(SWIM_HAD_CMAKE_WARN_DEPRECATED TRUE)
	set(SWIM_SAVED_CMAKE_WARN_DEPRECATED "${CMAKE_WARN_DEPRECATED}")
endif()
set(CMAKE_WARN_DEPRECATED OFF)
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Draco")
CPMAddPackage(
	NAME draco_source
	GITHUB_REPOSITORY google/draco
	GIT_TAG 1.5.7
	EXCLUDE_FROM_ALL YES
)
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}")
if(SWIM_HAD_CMAKE_WARN_DEPRECATED)
	set(CMAKE_WARN_DEPRECATED "${SWIM_SAVED_CMAKE_WARN_DEPRECATED}")
else()
	unset(CMAKE_WARN_DEPRECATED)
endif()
unset(SWIM_HAD_CMAKE_WARN_DEPRECATED)
unset(SWIM_SAVED_CMAKE_WARN_DEPRECATED)
if(SWIM_HAD_CMP0148_DEFAULT)
	set(CMAKE_POLICY_DEFAULT_CMP0148 "${SWIM_SAVED_CMP0148_DEFAULT}")
else()
	unset(CMAKE_POLICY_DEFAULT_CMP0148)
endif()
unset(SWIM_HAD_CMP0148_DEFAULT)
unset(SWIM_SAVED_CMP0148_DEFAULT)

if(TARGET draco::draco)
	set(SWIM_DRACO_TARGET draco::draco)
elseif(TARGET draco_static)
	set(SWIM_DRACO_TARGET draco_static)
	add_library(draco::draco ALIAS draco_static)
elseif(TARGET draco)
	set(SWIM_DRACO_TARGET draco)
	add_library(draco::draco ALIAS draco)
else()
	message(FATAL_ERROR "Draco 1.5.7 did not provide its expected static target")
endif()

# The old Debug project explicitly linked draco_release.lib. Keep that
# optimized/release-style Draco behavior while retaining CMake's Debug symbols.
# draco::draco may be an ALIAS, and CMake intentionally rejects mutating alias
# targets. Resolve it to the real build target before applying compile options.
get_target_property(SWIM_DRACO_REAL_TARGET "${SWIM_DRACO_TARGET}" ALIASED_TARGET)
if(NOT SWIM_DRACO_REAL_TARGET)
	set(SWIM_DRACO_REAL_TARGET "${SWIM_DRACO_TARGET}")
endif()

if(MSVC AND TARGET ${SWIM_DRACO_REAL_TARGET})
	target_compile_options(${SWIM_DRACO_REAL_TARGET} PRIVATE
		"$<$<CONFIG:Debug>:/O2>"
		"$<$<CONFIG:Debug>:/Ob2>"
	)
endif()

# ---------------------------------------------------------------------------
# WebP 1.5.0 - runtime WebP decoding used by MaterialPool.
# ---------------------------------------------------------------------------
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX ON CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)

# libwebp 1.5.0 probes SIMD/OS capabilities at configure time. Unsupported
# architectures intentionally report failed tests; keep those checks but do not
# spam normal Swim Engine configure output with their expected probe results or
# the dependency's old cmake_minimum_required() deprecation notice.
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
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/WebP")
CPMAddPackage(
	NAME webp_source
	GITHUB_REPOSITORY webmproject/libwebp
	GIT_TAG v1.5.0
	EXCLUDE_FROM_ALL YES
)
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}")
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
	set(SWIM_WEBP_CORE_TARGET WebP::webp)
elseif(TARGET webp)
	set(SWIM_WEBP_CORE_TARGET webp)
	add_library(WebP::webp ALIAS webp)
else()
	message(FATAL_ERROR "libwebp v1.5.0 did not provide target 'webp'")
endif()

if(NOT TARGET webpdemux)
	message(FATAL_ERROR "libwebp v1.5.0 did not provide target 'webpdemux'")
endif()
if(NOT TARGET libwebpmux)
	message(FATAL_ERROR "libwebp v1.5.0 did not provide target 'libwebpmux'")
endif()

# The legacy project linked all three WebP libraries. Keep the same link
# contract even though the current runtime path primarily calls the decoder.
add_library(SwimWebPBundle INTERFACE)
target_link_libraries(SwimWebPBundle INTERFACE
	${SWIM_WEBP_CORE_TARGET}
	webpdemux
	libwebpmux
)
add_library(Swim::WebP ALIAS SwimWebPBundle)

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
# tinygltf 2.9.x - kept header-only; one Swim-owned implementation TU defines
# the implementation/STB/Draco feature macros exactly once.
# ---------------------------------------------------------------------------
CPMAddPackage(
	NAME tinygltf_source
	GITHUB_REPOSITORY syoyo/tinygltf
	GIT_TAG v2.9.3
	DOWNLOAD_ONLY YES
	UPDATE_DISCONNECTED YES
)
add_library(SwimTinyGltf INTERFACE)
# tiny_gltf.h directly includes <draco/...> when TINYGLTF_ENABLE_DRACO is
# enabled in our implementation TU. Draco splits its public header contract:
# checked-in headers live under <checkout>/src/draco, while draco_features.h
# is generated at configure time as <top-level-build>/draco/draco_features.h.
# Export both roots here so engine source never needs to know about _deps or
# generated dependency paths.
target_include_directories(SwimTinyGltf SYSTEM INTERFACE
	"${tinygltf_source_SOURCE_DIR}"
	"${draco_source_SOURCE_DIR}/src"
	"${CMAKE_BINARY_DIR}"
)
target_link_libraries(SwimTinyGltf INTERFACE
	nlohmann_json::nlohmann_json
	stb::stb
	draco::draco
)
add_library(tinygltf::tinygltf ALIAS SwimTinyGltf)

# ---------------------------------------------------------------------------
# Basis Universal 1.60 - KTX2 transcoder path used by MaterialPool. Only the
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

add_library(SwimBasis STATIC
	"${SWIM_BASIS_GENERATED_SOURCE}"
)
target_include_directories(SwimBasis SYSTEM PUBLIC
	"${basis_source_SOURCE_DIR}/transcoder"
)
target_compile_definitions(SwimBasis PUBLIC
	BASISD_SUPPORT_KTX2=1
	BASISD_SUPPORT_KTX2_ZSTD=1
)
target_link_libraries(SwimBasis PUBLIC zstd::zstd)
add_library(Swim::Basis ALIAS SwimBasis)
swim_set_solution_folder(SwimBasis "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Basis Universal")

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

include(cmake/PhysX.cmake)

foreach(SWIM_CACHED_GIT_DEPENDENCY IN ITEMS
	mimalloc_source
	sdl3_source
	glm_source
	entt_source
	stb_source
	draco_source
	webp_source
	zstd_source
	tinygltf_source
	basis_source
	glad_source
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
