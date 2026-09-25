# Runtime text dependencies for Phase 20 (item 79): FreeType loads font faces,
# metrics and outlines, HarfBuzz shapes Unicode runs, and msdfgen turns glyph
# outlines into multi-channel signed distance fields for atlas pages. SheenBidi
# (bidi levels, script runs) and libunibreak (line, grapheme and word breaks)
# segment paragraphs before shaping.
#
# All of them are private implementation dependencies of the future text/UI
# module. Nothing outside that module (and its tests) may include their headers;
# public Swim text contracts expose Swim types only (architecture plan 4.1).
#
# Every optional integration is disabled so the libraries configure the same
# way on every machine without find_package() side effects: FreeType without
# zlib/bzip2/png/brotli/HarfBuzz, HarfBuzz as its single-file build without
# FreeType/ICU/GLib, msdfgen core only. Swim connects FreeType
# outlines to msdfgen shapes and font bytes to HarfBuzz faces itself.

set(SWIM_TEXT_DEPENDENCIES_AVAILABLE OFF)

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	return()
endif()

# A soft build configures with FETCHCONTENT_FULLY_DISCONNECTED=ON and cannot
# fetch a package its dependency cache has never seen.
set(SWIM_TEXT_DEPENDENCY_HINT
	" If the dependency cache predates the text libraries, configure once with downloads enabled "
	"(cmake --preset <preset> -DFETCHCONTENT_FULLY_DISCONNECTED=OFF) or run the clean build."
)

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_ENABLE_ERROR_STRINGS ON CACHE BOOL "" FORCE)

set(SWIM_SAVED_TEXT_CMAKE_FOLDER "${CMAKE_FOLDER}")
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Text/FreeType")
CPMAddPackage(
	NAME swim_freetype_source
	GITHUB_REPOSITORY freetype/freetype
	GIT_TAG VER-2-14-3
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
	OPTIONS "SKIP_INSTALL_ALL ON"
)

if(NOT TARGET freetype)
	message(FATAL_ERROR "FreeType 2.14.3 did not provide the freetype target" ${SWIM_TEXT_DEPENDENCY_HINT})
endif()
swim_set_solution_folder(freetype "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Text/FreeType")

# HarfBuzz's own CMake build is community-maintained and warns on every
# configure. The library documents a single translation unit build instead
# (src/harfbuzz.cc), which needs no configuration: without HB_HAVE_* defines it
# uses its built-in Unicode functions and OpenType shaper, and std::mutex where
# pthreads are not declared.
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Text/HarfBuzz")
CPMAddPackage(
	NAME swim_harfbuzz_source
	GITHUB_REPOSITORY harfbuzz/harfbuzz
	GIT_TAG 14.5.0
	DOWNLOAD_ONLY YES
	UPDATE_DISCONNECTED YES
)

set(SWIM_HARFBUZZ_SOURCE "${swim_harfbuzz_source_SOURCE_DIR}/src/harfbuzz.cc")
if(NOT EXISTS "${SWIM_HARFBUZZ_SOURCE}")
	message(FATAL_ERROR "HarfBuzz 14.5.0 source is missing src/harfbuzz.cc" ${SWIM_TEXT_DEPENDENCY_HINT})
endif()
add_library(SwimHarfBuzz STATIC EXCLUDE_FROM_ALL "${SWIM_HARFBUZZ_SOURCE}")
target_include_directories(SwimHarfBuzz SYSTEM PUBLIC "${swim_harfbuzz_source_SOURCE_DIR}/src")
target_compile_features(SwimHarfBuzz PRIVATE cxx_std_11)
if(MSVC)
	target_compile_options(SwimHarfBuzz PRIVATE /bigobj /utf-8 /W0)
else()
	target_compile_options(SwimHarfBuzz PRIVATE -w)
endif()
swim_set_solution_folder(SwimHarfBuzz "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Text/HarfBuzz")
unset(SWIM_HARFBUZZ_SOURCE)

set(MSDFGEN_CORE_ONLY ON CACHE BOOL "" FORCE)
set(MSDFGEN_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_VCPKG OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_OPENMP OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_SKIA OFF CACHE BOOL "" FORCE)
set(MSDFGEN_INSTALL OFF CACHE BOOL "" FORCE)

set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Text/msdfgen")
CPMAddPackage(
	NAME swim_msdfgen_source
	GITHUB_REPOSITORY Chlumsky/msdfgen
	GIT_TAG v1.13
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)
# Paragraph text (item 79, second checkpoint): SheenBidi implements the Unicode
# Bidirectional Algorithm (UAX #9) and Unicode script itemization; libunibreak
# implements line breaking (UAX #14), grapheme clusters and word boundaries
# (UAX #29). Both are plain C without configuration, so, like HarfBuzz, Swim
# compiles their documented source sets itself instead of running their builds
# (SheenBidi's unity TU; libunibreak's Makefile.am library sources).
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Text/SheenBidi")
CPMAddPackage(
	NAME swim_sheenbidi_source
	GITHUB_REPOSITORY Tehreer/SheenBidi
	GIT_TAG v3.0.0
	DOWNLOAD_ONLY YES
	UPDATE_DISCONNECTED YES
)
set(SWIM_SHEENBIDI_SOURCE "${swim_sheenbidi_source_SOURCE_DIR}/Source/SheenBidi.c")
if(NOT EXISTS "${SWIM_SHEENBIDI_SOURCE}")
	message(FATAL_ERROR "SheenBidi v3.0.0 source is missing Source/SheenBidi.c" ${SWIM_TEXT_DEPENDENCY_HINT})
endif()
add_library(SwimSheenBidi STATIC EXCLUDE_FROM_ALL "${SWIM_SHEENBIDI_SOURCE}")
target_include_directories(SwimSheenBidi SYSTEM PUBLIC "${swim_sheenbidi_source_SOURCE_DIR}/Headers")
target_include_directories(SwimSheenBidi PRIVATE "${swim_sheenbidi_source_SOURCE_DIR}/Source")
target_compile_definitions(SwimSheenBidi PRIVATE SB_CONFIG_UNITY)
if(MSVC)
	target_compile_options(SwimSheenBidi PRIVATE /utf-8 /W0)
else()
	target_compile_options(SwimSheenBidi PRIVATE -w)
endif()
set_property(TARGET SwimSheenBidi PROPERTY MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
swim_set_solution_folder(SwimSheenBidi "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Text/SheenBidi")
unset(SWIM_SHEENBIDI_SOURCE)

set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Text/libunibreak")
CPMAddPackage(
	NAME swim_unibreak_source
	GITHUB_REPOSITORY adah1972/libunibreak
	GIT_TAG libunibreak_8_0
	DOWNLOAD_ONLY YES
	UPDATE_DISCONNECTED YES
)
set(SWIM_UNIBREAK_SOURCES "")
foreach(SWIM_UNIBREAK_FILE IN ITEMS unibreakbase.c unibreakdef.c linebreak.c linebreakdata.c linebreakdef.c
		eastasianwidthdef.c emojidef.c graphemebreak.c wordbreak.c)
	if(NOT EXISTS "${swim_unibreak_source_SOURCE_DIR}/src/${SWIM_UNIBREAK_FILE}")
		message(FATAL_ERROR "libunibreak 8.0 source is missing src/${SWIM_UNIBREAK_FILE}" ${SWIM_TEXT_DEPENDENCY_HINT})
	endif()
	list(APPEND SWIM_UNIBREAK_SOURCES "${swim_unibreak_source_SOURCE_DIR}/src/${SWIM_UNIBREAK_FILE}")
endforeach()
add_library(SwimUnibreak STATIC EXCLUDE_FROM_ALL ${SWIM_UNIBREAK_SOURCES})
target_include_directories(SwimUnibreak SYSTEM PUBLIC "${swim_unibreak_source_SOURCE_DIR}/src")
if(MSVC)
	target_compile_options(SwimUnibreak PRIVATE /utf-8 /W0)
else()
	target_compile_options(SwimUnibreak PRIVATE -w)
endif()
set_property(TARGET SwimUnibreak PROPERTY MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
swim_set_solution_folder(SwimUnibreak "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Text/libunibreak")
unset(SWIM_UNIBREAK_SOURCES)
unset(SWIM_UNIBREAK_FILE)

set(CMAKE_FOLDER "${SWIM_SAVED_TEXT_CMAKE_FOLDER}")
unset(SWIM_SAVED_TEXT_CMAKE_FOLDER)
# msdfgen's CMakeLists sets the global PREDEFINED_TARGETS_FOLDER to "meta",
# which moves ALL_BUILD/ZERO_CHECK out of Swim's "CMake" solution folder.
# Restore the project-wide layout from cmake/SolutionLayout.cmake.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")

if(NOT TARGET msdfgen::msdfgen-core)
	message(FATAL_ERROR "msdfgen v1.13 did not provide msdfgen::msdfgen-core" ${SWIM_TEXT_DEPENDENCY_HINT})
endif()
# msdfgen pins its own MSVC runtime (the Debug CRT in Debug). Swim links one
# runtime everywhere (CMAKE_MSVC_RUNTIME_LIBRARY), so follow it instead.
set_property(TARGET msdfgen-core PROPERTY MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
swim_set_solution_folder(msdfgen-core "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Text/msdfgen")

# The one private bundle the text/UI module (and its tests) link.
add_library(SwimTextDependencies INTERFACE)
add_library(Swim::TextDependencies ALIAS SwimTextDependencies)
target_link_libraries(SwimTextDependencies INTERFACE
	freetype
	SwimHarfBuzz
	msdfgen::msdfgen-core
	SwimSheenBidi
	SwimUnibreak
)
swim_set_solution_folder(SwimTextDependencies "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Text")

unset(SWIM_TEXT_DEPENDENCY_HINT)
set(SWIM_TEXT_DEPENDENCIES_AVAILABLE ON)
