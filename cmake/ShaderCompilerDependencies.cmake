# Shader compiler dependency boundary.
#
# Slang is a build/tool dependency only. Swim invokes the pinned slangc binary
# to produce compiled shader artifacts and reflection metadata; no Slang C++
# implementation types or shared libraries are linked into the runtime.

set(SWIM_SHADER_COMPILER_DEPENDENCIES_AVAILABLE OFF)
set(SWIM_SLANG_AVAILABLE OFF)

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	return()
endif()

# Reflection JSON is parsed only inside SwimShaderCompiler. Reuse the same
# simdjson dependency as the asset compiler when it is already configured, but
# keep this module independently buildable when the asset compiler is disabled.
if(NOT TARGET simdjson::simdjson)
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
endif()

set(SWIM_SLANG_VERSION "2026.16.1" CACHE INTERNAL "Pinned Slang compiler version")
set(SWIM_SLANG_CACHE_ROOT
	"${CPM_SOURCE_CACHE}/slang_sdk/${SWIM_SLANG_VERSION}"
	CACHE INTERNAL "Pinned Slang SDK cache root")

if(WIN32)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
		set(SWIM_SLANG_ARCHIVE_NAME "slang-${SWIM_SLANG_VERSION}-windows-aarch64.zip")
		set(SWIM_SLANG_ARCHIVE_SHA256 "315a18a2ee56803bf558778d91481b47cefb51df14207342afdc9a4d9166c588")
	else()
		set(SWIM_SLANG_ARCHIVE_NAME "slang-${SWIM_SLANG_VERSION}-windows-x86_64.zip")
		set(SWIM_SLANG_ARCHIVE_SHA256 "0fd3e6a9a5d05ed4cdd000d467f1ffb5d9701b827e83bfb428902a45c37ef8a5")
	endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
		set(SWIM_SLANG_ARCHIVE_NAME "slang-${SWIM_SLANG_VERSION}-linux-aarch64-glibc-2.28.zip")
		set(SWIM_SLANG_ARCHIVE_SHA256 "fea76008e3d6f527028991b5effdaacab70619b8270e8cbad031746a6c152708")
	else()
		set(SWIM_SLANG_ARCHIVE_NAME "slang-${SWIM_SLANG_VERSION}-linux-x86_64-glibc-2.27.zip")
		set(SWIM_SLANG_ARCHIVE_SHA256 "95eb246e758131545915406e5ac9e41ecd29a6bc1f83ae0b112d31e390fc9d33")
	endif()
else()
	message(STATUS
		"SwimShaderCompiler: pinned Slang SDK bootstrap is currently defined for Windows/Linux only; "
		"shader compiler target disabled on ${CMAKE_SYSTEM_NAME}."
	)
	return()
endif()

set(SWIM_SLANG_ARCHIVE_PATH "${SWIM_SLANG_CACHE_ROOT}/${SWIM_SLANG_ARCHIVE_NAME}")
set(SWIM_SLANG_SDK_ROOT "${SWIM_SLANG_CACHE_ROOT}/sdk")
set(SWIM_SLANGC_EXECUTABLE "${SWIM_SLANG_SDK_ROOT}/bin/slangc${CMAKE_EXECUTABLE_SUFFIX}")

function(swim_find_slang_compiler root out_path)
	set(SWIM_FOUND_SLANGC "")
	if(EXISTS "${root}/bin/slangc${CMAKE_EXECUTABLE_SUFFIX}")
		set(SWIM_FOUND_SLANGC "${root}/bin/slangc${CMAKE_EXECUTABLE_SUFFIX}")
	else()
		file(GLOB_RECURSE SWIM_SLANGC_CANDIDATES
			LIST_DIRECTORIES FALSE
			"${root}/*/slangc${CMAKE_EXECUTABLE_SUFFIX}"
		)
		foreach(SWIM_SLANGC_CANDIDATE IN LISTS SWIM_SLANGC_CANDIDATES)
			get_filename_component(SWIM_SLANGC_DIRECTORY "${SWIM_SLANGC_CANDIDATE}" DIRECTORY)
			get_filename_component(SWIM_SLANGC_DIRECTORY_NAME "${SWIM_SLANGC_DIRECTORY}" NAME)
			if(SWIM_SLANGC_DIRECTORY_NAME STREQUAL "bin")
				set(SWIM_FOUND_SLANGC "${SWIM_SLANGC_CANDIDATE}")
				break()
			endif()
		endforeach()
	endif()
	set(${out_path} "${SWIM_FOUND_SLANGC}" PARENT_SCOPE)
endfunction()

function(swim_validate_slang_archive out_valid)
	set(SWIM_ARCHIVE_VALID FALSE)
	if(EXISTS "${SWIM_SLANG_ARCHIVE_PATH}")
		file(SHA256 "${SWIM_SLANG_ARCHIVE_PATH}" SWIM_ARCHIVE_ACTUAL_SHA256)
		if("${SWIM_ARCHIVE_ACTUAL_SHA256}" STREQUAL "${SWIM_SLANG_ARCHIVE_SHA256}")
			set(SWIM_ARCHIVE_VALID TRUE)
		endif()
	endif()
	set(${out_valid} "${SWIM_ARCHIVE_VALID}" PARENT_SCOPE)
endfunction()

swim_find_slang_compiler("${SWIM_SLANG_SDK_ROOT}" SWIM_SLANGC_EXECUTABLE)

if(NOT SWIM_SLANGC_EXECUTABLE)
	swim_validate_slang_archive(SWIM_SLANG_ARCHIVE_VALID)
	if(NOT SWIM_SLANG_ARCHIVE_VALID)
		if(FETCHCONTENT_FULLY_DISCONNECTED)
			message(FATAL_ERROR
				"Slang ${SWIM_SLANG_VERSION} is not present in the dependency cache or failed its SHA-256 check. "
				"The soft build is intentionally offline; run the clean build once to populate/repair the cache."
			)
		endif()

		file(REMOVE "${SWIM_SLANG_ARCHIVE_PATH}")
		file(MAKE_DIRECTORY "${SWIM_SLANG_CACHE_ROOT}")
		set(SWIM_SLANG_DOWNLOAD_URL
			"https://github.com/shader-slang/slang/releases/download/v${SWIM_SLANG_VERSION}/${SWIM_SLANG_ARCHIVE_NAME}"
		)
		message(STATUS
			"Downloading Slang ${SWIM_SLANG_VERSION} compiler SDK to ${SWIM_SLANG_ARCHIVE_PATH}"
		)
		file(DOWNLOAD
			"${SWIM_SLANG_DOWNLOAD_URL}"
			"${SWIM_SLANG_ARCHIVE_PATH}"
			EXPECTED_HASH "SHA256=${SWIM_SLANG_ARCHIVE_SHA256}"
			STATUS SWIM_SLANG_DOWNLOAD_STATUS
			TLS_VERIFY ON
			SHOW_PROGRESS
		)

		list(GET SWIM_SLANG_DOWNLOAD_STATUS 0 SWIM_SLANG_DOWNLOAD_CODE)
		if(NOT SWIM_SLANG_DOWNLOAD_CODE EQUAL 0)
			list(GET SWIM_SLANG_DOWNLOAD_STATUS 1 SWIM_SLANG_DOWNLOAD_MESSAGE)
			file(REMOVE "${SWIM_SLANG_ARCHIVE_PATH}")
			message(FATAL_ERROR
				"Failed to download Slang ${SWIM_SLANG_VERSION}: ${SWIM_SLANG_DOWNLOAD_MESSAGE}"
			)
		endif()
	endif()

	file(REMOVE_RECURSE "${SWIM_SLANG_SDK_ROOT}")
	file(MAKE_DIRECTORY "${SWIM_SLANG_SDK_ROOT}")
	file(ARCHIVE_EXTRACT
		INPUT "${SWIM_SLANG_ARCHIVE_PATH}"
		DESTINATION "${SWIM_SLANG_SDK_ROOT}"
	)
	swim_find_slang_compiler("${SWIM_SLANG_SDK_ROOT}" SWIM_SLANGC_EXECUTABLE)
endif()

if(NOT SWIM_SLANGC_EXECUTABLE OR NOT EXISTS "${SWIM_SLANGC_EXECUTABLE}")
	message(FATAL_ERROR
		"Slang ${SWIM_SLANG_VERSION} SDK was extracted but slangc could not be located below ${SWIM_SLANG_SDK_ROOT}"
	)
endif()

if(NOT TARGET SwimSlangCompiler)
	add_executable(SwimSlangCompiler IMPORTED GLOBAL)
	set_target_properties(SwimSlangCompiler PROPERTIES
		IMPORTED_LOCATION "${SWIM_SLANGC_EXECUTABLE}"
	)
endif()

set(SWIM_SLANG_AVAILABLE ON)
set(SWIM_SHADER_COMPILER_DEPENDENCIES_AVAILABLE ON)
