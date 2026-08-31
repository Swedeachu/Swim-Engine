#!/usr/bin/env python3

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

VENDORED_DEPENDENCIES = (
    "EnTT",
    "basis",
    "draco",
    "glad",
    "glm",
    "json",
    "physx",
    "stb",
    "tiny_gltf",
    "webp",
    "zstd",
)

GENERATED_VISUAL_STUDIO_SUFFIXES = (
    ".sln",
    ".vcxproj",
    ".vcxproj.filters",
    ".vcxproj.user",
)

REQUIRED_CMAKE_FILES = (
    "CMakeLists.txt",
    "CMakePresets.json",
    "cmake/get_cpm.cmake",
    "cmake/Dependencies.cmake",
    "cmake/PhysX.cmake",
)

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inl"}


def fail(message: str, failures: list[str]) -> None:
    failures.append(message)


def check_generated_visual_studio_files(failures: list[str]) -> None:
    for path in ROOT.iterdir():
        if not path.is_file():
            continue

        if any(path.name.endswith(suffix) for suffix in GENERATED_VISUAL_STUDIO_SUFFIXES):
            fail(f"committed Visual Studio artifact remains: {path.name}", failures)


def check_vendored_dependencies(failures: list[str]) -> None:
    library_root = ROOT / "Source" / "Library"
    if not library_root.exists():
        return

    for dependency in VENDORED_DEPENDENCIES:
        path = library_root / dependency
        if path.exists():
            fail(f"vendored dependency remains: {path.relative_to(ROOT)}", failures)


def check_required_cmake_files(failures: list[str]) -> None:
    for relative_path in REQUIRED_CMAKE_FILES:
        if not (ROOT / relative_path).is_file():
            fail(f"required build file is missing: {relative_path}", failures)


def check_machine_specific_paths(failures: list[str]) -> None:
    patterns = (
        re.compile(r"[A-Za-z]:\\Users\\"),
        re.compile(r"[A-Za-z]:/Users/"),
        re.compile(r"\$\(ProjectDir\)Source\\Library"),
        re.compile(r"\$\(VULKAN_SDK\)"),
    )

    for path in ROOT.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES | {".cmake", ".txt", ".md", ".json"}:
            continue

        text = path.read_text(encoding="utf-8", errors="ignore")
        for pattern in patterns:
            if pattern.search(text):
                fail(f"machine/manual dependency path remains in {path.relative_to(ROOT)}: {pattern.pattern}", failures)
                break


def check_legacy_library_includes(failures: list[str]) -> None:
    include_pattern = re.compile(r'^\s*#\s*include\s*[<"]Library/', re.MULTILINE)

    for source_root in (ROOT / "Source" / "Engine", ROOT / "Source" / "Game"):
        if not source_root.exists():
            continue

        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue

            text = path.read_text(encoding="utf-8", errors="ignore")
            if include_pattern.search(text):
                fail(f"legacy Library/... include remains: {path.relative_to(ROOT)}", failures)


def check_tungsten_style_cmake(failures: list[str]) -> None:
    cmake_path = ROOT / "CMakeLists.txt"
    if not cmake_path.is_file():
        return

    text = cmake_path.read_text(encoding="utf-8", errors="ignore")
    required_fragments = (
        "GLOB_RECURSE",
        "CONFIGURE_DEPENDS",
        "source_group(TREE",
        "VS_STARTUP_PROJECT",
        "VS_DEBUGGER_WORKING_DIRECTORY",
        "include(cmake/get_cpm.cmake)",
        "include(cmake/Dependencies.cmake)",
    )

    for fragment in required_fragments:
        if fragment not in text:
            fail(f"CMakeLists.txt is missing Tungsten-style fragment: {fragment}", failures)



def check_preserved_build_contract(failures: list[str]) -> None:
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    physx_text = (ROOT / "cmake" / "PhysX.cmake").read_text(encoding="utf-8", errors="ignore")
    physx_build_text = (ROOT / "cmake" / "BuildPhysX.cmake").read_text(encoding="utf-8", errors="ignore")
    shader_text = (ROOT / "cmake" / "Shaders.cmake").read_text(encoding="utf-8", errors="ignore")
    dependency_text = (ROOT / "cmake" / "Dependencies.cmake").read_text(encoding="utf-8", errors="ignore")

    required_root_fragments = (
        'set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")',
        '$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:CXX>>:_ITERATOR_DEBUG_LEVEL=0>',
        '$<$<CONFIG:Debug>:_SWIM_DEBUG>',
        '$<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>',
        '$<$<CONFIG:Debug>:/U_DEBUG>',
        'PX_PHYSX_STATIC_LIB',
        'target_precompile_headers(SwimEngine',
        'SKIP_PRECOMPILE_HEADERS ON',
    )
    for fragment in required_root_fragments:
        if fragment not in cmake_text:
            fail(f"legacy x64 build contract is missing from CMakeLists.txt: {fragment}", failures)

    required_physx_fragments = (
        '107.3-omni-and-physx-5.6.1',
        'IMPORTED_LOCATION_DEBUG "${SWIM_PHYSX_CHECKED_DIR}',
        'IMPORTED_LOCATION_RELEASE "${SWIM_PHYSX_RELEASE_DIR}',
        '$<$<NOT:$<CONFIG:Release>>:SwimPhysXPvdSDK>',
    )
    for fragment in required_physx_fragments:
        if fragment not in physx_text:
            fail(f"PhysX mapping is missing required fragment: {fragment}", failures)

    required_physx_build_fragments = (
        'vc17win64-cpu-only',
        '-DPX_GENERATE_STATIC_LIBRARIES=ON',
        '-DNV_USE_STATIC_WINCRT=ON',
        '-DNV_USE_DEBUG_WINCRT=OFF',
        '-DPX_GENERATE_GPU_PROJECTS=OFF',
        '--config ${SWIM_PHYSX_CONFIG}',
        'foreach(SWIM_PHYSX_CONFIG IN ITEMS checked release)',
    )
    for fragment in required_physx_build_fragments:
        if fragment not in physx_build_text:
            fail(f"PhysX external build no longer preserves the old ABI/config mapping: {fragment}", failures)

    required_shader_fragments = (
        '-T vs_6_0',
        '-T ps_6_0',
        '-T cs_6_0',
        '-spirv',
        '-fspv-target-env=vulkan1.2',
        '$<TARGET_FILE_DIR:${target}>/Shaders/VertexShaders',
        '$<TARGET_FILE_DIR:${target}>/Shaders/FragmentShaders',
        '$<TARGET_FILE_DIR:${target}>/Shaders/ComputeShaders',
    )
    for fragment in required_shader_fragments:
        if fragment not in shader_text:
            fail(f"legacy Vulkan shader build behavior is missing: {fragment}", failures)

    required_dependency_contract_fragments = (
        'NAME glm_source',
        'add_library(SwimGlm INTERFACE)',
        'add_library(glm::glm ALIAS SwimGlm)',
        'NAME entt_source',
        'add_library(SwimEnTT INTERFACE)',
        'add_library(EnTT::EnTT ALIAS SwimEnTT)',
        'NAME nlohmann_json_source',
        'GIT_TAG v3.10.4',
        'DOWNLOAD_ONLY YES',
        'add_library(SwimJson INTERFACE)',
        'add_library(nlohmann_json::nlohmann_json ALIAS SwimJson)',
        'WEBP_BUILD_LIBWEBPMUX ON',
        'webpdemux',
        'libwebpmux',
        'add_library(Swim::WebP ALIAS SwimWebPBundle)',
    )
    for fragment in required_dependency_contract_fragments:
        if fragment not in dependency_text:
            fail(f"legacy third-party link contract is missing: {fragment}", failures)

    # The PhysX generator is a batch file. Running an absolute quoted path
    # through cmd.exe caused CMake/MSBuild to preserve the escape quotes and
    # Windows attempted to execute a command literally named \"C:/...bat\".
    # Invoke the local batch name from SWIM_PHYSX_ROOT instead.
    if '\\"${SWIM_PHYSX_GENERATOR}\\"' in physx_build_text:
        fail("PhysX generator still uses the broken escaped absolute-path cmd invocation", failures)

    required_physx_command_fragments = (
        'COMMAND cmd.exe /d /c "call generate_projects.bat ${SWIM_PHYSX_PRESET}"',
        'WORKING_DIRECTORY "${SWIM_PHYSX_ROOT}"',
    )
    for fragment in required_physx_command_fragments:
        if fragment not in physx_build_text:
            fail(f"PhysX Windows batch invocation is missing required fragment: {fragment}", failures)

    required_physx_preset_guard_fragments = (
        'buildtools/presets/public/${SWIM_PHYSX_PRESET}.xml',
        'Pinned PhysX release is missing the required CPU-only preset',
    )
    for fragment in required_physx_preset_guard_fragments:
        if fragment not in physx_build_text:
            fail(f"PhysX CPU-only preset guard is missing: {fragment}", failures)

    preset_data = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
    build_presets = {preset["name"]: preset for preset in preset_data.get("buildPresets", [])}
    required_vs_presets = {
        "windows-vs": ("windows-vs", "Release"),
        "windows-vs-debug": ("windows-vs", "Debug"),
    }
    for name, (configure_preset, configuration) in required_vs_presets.items():
        preset = build_presets.get(name)
        if preset is None:
            fail(f"Visual Studio build preset is missing: {name}", failures)
            continue
        if preset.get("configurePreset") != configure_preset or preset.get("configuration") != configuration:
            fail(
                f"Visual Studio build preset {name} must use configurePreset={configure_preset} and configuration={configuration}",
                failures,
            )

    pins = (
        'GIT_TAG 1.0.0',
        'GIT_TAG v3.13.2',
        'GIT_TAG v3.10.4',
        'GIT_TAG 1.5.7',
        'GIT_TAG v1.5.0',
        'GIT_TAG v1.4.9',
        'GIT_TAG v2.9.3',
        'GIT_TAG v1_60_snapshot_final',
        'GIT_TAG v2.0.8',
    )
    for fragment in pins:
        if fragment not in dependency_text:
            fail(f"dependency pin is missing: {fragment}", failures)


def check_modern_cmake_dependency_compatibility(failures: list[str]) -> None:
    dependency_text = (ROOT / "cmake" / "Dependencies.cmake").read_text(encoding="utf-8", errors="ignore")

    if 'target_compile_options(${SWIM_DRACO_TARGET}' in dependency_text:
        fail("Draco compile options are still applied through a possibly-aliased target", failures)

    if 'ALIASED_TARGET' not in dependency_text:
        fail("Draco target customization does not resolve aliases to their real target", failures)

    if 'cmake_policy(SET CMP0148 OLD)' in dependency_text:
        fail("Draco compatibility still uses an ineffective parent CMP0148 policy scope", failures)

    if 'set(CMAKE_POLICY_DEFAULT_CMP0148 OLD)' not in dependency_text:
        fail("Draco 1.5.7 is not isolated from CMake 4.x FindPythonInterp policy changes", failures)

    draco_package_match = re.search(
        r'CPMAddPackage\(\s*NAME draco_source(?P<body>.*?)\n\)',
        dependency_text,
        re.DOTALL,
    )
    if draco_package_match is None or 'EXCLUDE_FROM_ALL YES' not in draco_package_match.group('body'):
        fail("Draco dependency is not EXCLUDE_FROM_ALL; its unused CLI tools can run pwsh-only post-build hooks", failures)

    if '"${draco_source_SOURCE_DIR}/src"' not in dependency_text:
        fail("TinyGLTF/Draco boundary does not export Draco's src include root for <draco/...> headers", failures)

    if '"${CMAKE_BINARY_DIR}"' not in dependency_text:
        fail("TinyGLTF/Draco boundary does not export Draco's generated-header root for <draco/draco_features.h>", failures)

    if 'add_subdirectory("${zstd_source_SOURCE_DIR}/build/cmake"' in dependency_text:
        fail("zstd 1.4.9 still executes its obsolete upstream CMake project", failures)

    if '${zstd_source_SOURCE_DIR}/contrib/single_file_libs/zstd-in.c' in dependency_text:
        fail("zstd integration still compiles the ungenerated zstd-in.c amalgamation template", failures)

    required_zstd_fragments = (
        'file(GLOB SWIM_ZSTD_COMMON_SOURCES',
        'lib/common/*.c',
        'file(GLOB SWIM_ZSTD_COMPRESS_SOURCES',
        'lib/compress/*.c',
        'file(GLOB SWIM_ZSTD_DECOMPRESS_SOURCES',
        'lib/decompress/*.c',
        'file(GLOB SWIM_ZSTD_DICTBUILDER_SOURCES',
        'lib/dictBuilder/*.c',
        'add_library(SwimZstd STATIC',
        'ZSTD_DISABLE_ASM',
        'ZSTD_MULTITHREAD',
        'target_include_directories(SwimZstd SYSTEM',
        'add_library(zstd::zstd ALIAS SwimZstd)',
    )
    for fragment in required_zstd_fragments:
        if fragment not in dependency_text:
            fail(f"modern-CMake zstd integration is missing: {fragment}", failures)


def check_windows_compile_contract_and_warning_hygiene(failures: list[str]) -> None:
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    dependency_text = (ROOT / "cmake" / "Dependencies.cmake").read_text(encoding="utf-8", errors="ignore")
    physx_build_text = (ROOT / "cmake" / "BuildPhysX.cmake").read_text(encoding="utf-8", errors="ignore")
    vulkan_renderer_text = (
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Vulkan" / "VulkanRenderer.h"
    ).read_text(encoding="utf-8", errors="ignore")

    for definition in ("UNICODE", "_UNICODE"):
        if definition not in cmake_text:
            fail(f"Win32 Unicode compile definition is missing: {definition}", failures)

    if "/W3" not in cmake_text or "/W4" in cmake_text:
        fail("MSVC warning level no longer matches the legacy x64 /W3 project", failures)

    required_wgl_extensions = (
        "WGL_ARB_create_context",
        "WGL_ARB_create_context_profile",
        "WGL_ARB_extensions_string",
        "WGL_ARB_multisample",
        "WGL_ARB_pixel_format",
        "WGL_EXT_extensions_string",
        "WGL_EXT_swap_control",
    )
    for extension in required_wgl_extensions:
        if extension not in dependency_text:
            fail(f"GLAD generation is missing legacy WGL extension: {extension}", failures)

    if "const VkDevice& GetDevice() const" in vulkan_renderer_text:
        fail("VulkanRenderer::GetDevice still returns a reference to a temporary handle", failures)
    if "const VkPhysicalDevice& GetPhysicalDevice() const" in vulkan_renderer_text:
        fail("VulkanRenderer::GetPhysicalDevice still returns a reference to a temporary handle", failures)

    if "PX_BUILDPUBLICSAMPLES" in physx_build_text:
        fail("PhysX reconfigure still passes the unused PX_BUILDPUBLICSAMPLES option", failures)

    if "CMAKE_REQUIRED_QUIET" not in dependency_text:
        fail("WebP architecture-probe chatter is not locally quieted", failures)
    if "CMAKE_WARN_DEPRECATED" not in dependency_text:
        fail("legacy third-party CMake deprecation chatter is not locally quieted", failures)


def check_source_files_are_utf8(failures: list[str]) -> None:
    for source_root in (ROOT / "Source",):
        if not source_root.exists():
            continue
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES | {".glsl", ".hlsl"}:
                continue
            try:
                path.read_text(encoding="utf-8")
            except UnicodeDecodeError as exc:
                fail(f"source file is not valid UTF-8: {path.relative_to(ROOT)} ({exc})", failures)

def main() -> int:
    failures: list[str] = []

    check_generated_visual_studio_files(failures)
    check_vendored_dependencies(failures)
    check_required_cmake_files(failures)
    check_machine_specific_paths(failures)
    check_legacy_library_includes(failures)
    check_tungsten_style_cmake(failures)
    check_preserved_build_contract(failures)
    check_modern_cmake_dependency_compatibility(failures)
    check_windows_compile_contract_and_warning_hygiene(failures)
    check_source_files_are_utf8(failures)

    if failures:
        print("Swim Engine build-layout verification FAILED:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("Swim Engine build-layout verification passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
