#!/usr/bin/env python3

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

VENDORED_DEPENDENCIES = (
    "EnTT",
    "basis",
    "draco",
    "fastgltf",
    "freetype",
    "glad",
    "glm",
    "harfbuzz",
    "json",
    "meshoptimizer",
    "msdfgen",
    "physx",
    "simdjson",
    "spdlog",
    "SDL3",
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
    "cmake/MemoryDependencies.cmake",
    "cmake/JobDependencies.cmake",
    "cmake/PlatformDependencies.cmake",
    "cmake/MathDependencies.cmake",
    "cmake/AssetCompilerDependencies.cmake",
    "cmake/TextDependencies.cmake",
    "cmake/PhysX.cmake",
    "cmake/SolutionLayout.cmake",
)

REQUIRED_BUILD_SCRIPTS = (
    "scripts/build-windows-clean.ps1",
    "scripts/build-windows-soft.ps1",
    "scripts/build-linux-clean.sh",
    "scripts/build-linux-soft.sh",
    "scripts/build-windows-clean.bat",
    "scripts/build-windows-soft.bat",
    "scripts/build-linux-clean.bat",
    "scripts/build-linux-soft.bat",
    "scripts/windows-build-common.ps1",
    "scripts/generate-solution.ps1",
)

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inl"}


def fail(message: str, failures: list[str]) -> None:
    failures.append(message)


def read_tests_cmake() -> str:
    tests_cmake = ROOT / "cmake" / "Tests.cmake"
    if not tests_cmake.is_file():
        return ""
    return tests_cmake.read_text(encoding="utf-8", errors="ignore")


def check_suite_is_compiled(suite_group: str, suite_file: str, failures: list[str]) -> None:
    """The whole runnable corpus is one `SwimTests` target whose suite sources are
    globbed per dependency group, so representation in the build means the file
    lives in the right group directory and that group is collected."""
    suite_path = ROOT / "Source" / "Tests" / "Suites" / Path(suite_group) / suite_file
    if not suite_path.is_file():
        fail(f"test suite source is missing from its dependency group: {suite_path.relative_to(ROOT)}", failures)

    tests_cmake_text = read_tests_cmake()
    if f"swim_collect_test_suite_sources" not in tests_cmake_text:
        fail("cmake/Tests.cmake no longer collects test suite sources", failures)
        return

    group_token = suite_group.replace("\\", "/")
    if group_token not in tests_cmake_text:
        fail(f"cmake/Tests.cmake does not collect the '{group_token}' test suite group", failures)


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


def check_build_workflow(failures: list[str]) -> None:
    for relative_path in REQUIRED_BUILD_SCRIPTS:
        if not (ROOT / relative_path).is_file():
            fail(f"required clean/soft build entry point is missing: {relative_path}", failures)

    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    platform_dependency_text = (ROOT / "cmake" / "PlatformDependencies.cmake").read_text(encoding="utf-8", errors="ignore")
    dependency_text = (ROOT / "cmake" / "Dependencies.cmake").read_text(encoding="utf-8", errors="ignore")
    physx_text = (ROOT / "cmake" / "PhysX.cmake").read_text(encoding="utf-8", errors="ignore")
    solution_layout_text = (ROOT / "cmake" / "SolutionLayout.cmake").read_text(encoding="utf-8", errors="ignore")

    for fragment in (
        "USE_FOLDERS ON",
        'PREDEFINED_TARGETS_FOLDER "CMake"',
        'SWIM_SOLUTION_FOLDER_TESTS "Tests"',
        'SWIM_SOLUTION_FOLDER_EXAMPLES "Examples"',
        'SWIM_SOLUTION_FOLDER_THIRD_PARTY "Third Party"',
        "function(swim_set_solution_folder",
    ):
        if fragment not in solution_layout_text:
            fail(f"Visual Studio solution layout contract is missing: {fragment}", failures)

    # "Engine Modules" is deliberately not checked here: the 2026-09-05 engine
    # module collapse (docs/VisualStudioProjectStructure.md section 11)
    # retired every per-module target (SwimCore, SwimPlatform, SwimInput,
    # etc.) that used to be placed in that solution folder. Their sources now
    # compile directly into SwimEngine and are organized with source_group
    # filters instead, so the contract below checks for that instead of a
    # solution-folder placement.
    if 'SWIM_SOLUTION_FOLDER_ENGINE_MODULES "Engine Modules"' in solution_layout_text:
        fail(
            "Visual Studio solution layout still defines the retired 'Engine Modules' folder variable "
            "(cmake/SolutionLayout.cmake); the engine module collapse removed its last consumer",
            failures,
        )

    for fragment in (
        'include(cmake/SolutionLayout.cmake)',
        "# --- Engine module source discovery",
        "file(GLOB_RECURSE SWIM_ENGINE_SOURCES",
        'source_group(TREE ${CMAKE_SOURCE_DIR} FILES',
        'add_executable(SwimHelloWindow EXCLUDE_FROM_ALL',
        'add_executable(SwimHeadlessPlatform EXCLUDE_FROM_ALL',
        '"${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/SDL3"',
    ):
        if fragment not in cmake_text:
            fail(f"first-party/IDE target organization is missing: {fragment}", failures)

    for retired_fragment in (
        "add_library(SwimCore",
        "add_library(SwimPlatform",
        "add_library(SwimInput",
        "add_library(SwimCommands",
        "add_library(SwimMemory",
        "add_library(SwimJobs",
        "add_library(SwimIO",
        "add_library(SwimAssets",
        "add_library(SwimPhysics ",
        "add_library(SwimRhi ",
        "add_library(SwimRhiVulkan",
        "add_library(Swim::Core ALIAS",
    ):
        if retired_fragment in cmake_text:
            fail(
                f"CMakeLists.txt still declares a retired Engine Modules target: {retired_fragment} "
                "(the 2026-09-05 engine module collapse folded these into SwimEngine)",
                failures,
            )

    for fragment in (
    ):
        if fragment not in dependency_text:
            fail(f"third-party Visual Studio target organization is missing: {fragment}", failures)

    if 'swim_set_solution_folder(SwimPhysXBuild "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/PhysX")' not in physx_text:
        fail("PhysX build orchestration is not grouped under Third Party/PhysX", failures)

    required_cache_fragments = (
        'set(CPM_SOURCE_CACHE "${CMAKE_SOURCE_DIR}/.cache/cpm"',
        'set(CPM_USE_NAMED_CACHE_DIRECTORIES ON',
    )
    for fragment in required_cache_fragments:
        if fragment not in cmake_text:
            fail(f"repository-local CPM cache contract is missing: {fragment}", failures)

    cpm_bootstrap_text = (ROOT / "cmake" / "get_cpm.cmake").read_text(encoding="utf-8", errors="ignore")
    for fragment in (
        "STATUS SWIM_CPM_DOWNLOAD_STATUS",
        "TLS_VERIFY ON",
        "file(REMOVE ${CPM_DOWNLOAD_LOCATION})",
        "if(FETCHCONTENT_FULLY_DISCONNECTED)",
        "Soft builds never download dependencies",
    ):
        if fragment not in cpm_bootstrap_text:
            fail(f"CPM bootstrap does not fail cleanly on download errors: {fragment}", failures)

    required_sdl_fragments = (
        'GITHUB_REPOSITORY libsdl-org/SDL',
        'GIT_TAG release-3.4.14',
        'UPDATE_DISCONNECTED YES',
        'set(SDL_AUDIO OFF',
        'set(SDL_CAMERA OFF',
        'set(SDL_GPU OFF',
        'set(SDL_RENDER OFF',
        'set(SDL_SENSOR OFF',
        'set(SDL_DIALOG OFF',
        'set(SDL_TRAY OFF',
    )
    for fragment in required_sdl_fragments:
        if fragment not in platform_dependency_text:
            fail(f"SDL3 CMake dependency contract is missing: {fragment}", failures)

    clean_scripts = (
        ROOT / "scripts" / "build-windows-clean.ps1",
        ROOT / "scripts" / "build-linux-clean.sh",
    )
    for path in clean_scripts:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if ".cache" not in text:
            fail(f"clean build does not clear the complete repository dependency cache: {path.relative_to(ROOT)}", failures)
        if "build/.px" not in text:
            fail(f"clean build does not clear shared PhysX generated state before dependency reset: {path.relative_to(ROOT)}", failures)
        if "Clean state verified" not in text:
            fail(f"clean build does not verify that generated state was actually removed: {path.relative_to(ROOT)}", failures)
        if "FETCHCONTENT_FULLY_DISCONNECTED=OFF" not in text:
            fail(f"clean build does not explicitly allow a fresh dependency pull: {path.relative_to(ROOT)}", failures)

    batch_launchers = (
        ROOT / "scripts" / "build-windows-clean.bat",
        ROOT / "scripts" / "build-windows-soft.bat",
        ROOT / "scripts" / "build-linux-clean.bat",
        ROOT / "scripts" / "build-linux-soft.bat",
    )
    for path in batch_launchers:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="ignore").lower()
        if "pause" not in text:
            fail(f"one-click batch launcher does not keep the console open: {path.relative_to(ROOT)}", failures)
        if "build_exit_code" not in text or "exit /b %build_exit_code%" not in text:
            fail(f"one-click batch launcher does not preserve the underlying build exit code: {path.relative_to(ROOT)}", failures)

    windows_toolchain_helper = ROOT / "scripts" / "windows-build-common.ps1"
    if windows_toolchain_helper.is_file():
        helper_text = windows_toolchain_helper.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "VsDevCmd.bat",
            "Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\Ninja\\ninja.exe",
            "Get-SwimNinjaPath",
            "Enable-SwimGitLongPaths",
            "core.longpaths",
            "ConvertTo-SwimExtendedWindowsPath",
            "Test-SwimPathEntryExists",
            "Invoke-SwimWindowsDirectoryRemove",
            "System.Diagnostics.ProcessStartInfo",
            "rd /s /q",
            "Remove-SwimGeneratedDirectory",
            "MayBeDirectoryLink",
            "Assert-SwimVisualStudioSolutionLayout",
            "Invoke-SwimWindowsTestSuite",
            "Invoke-SwimWindowsAssetCookValidation",
            '"SwimTests"',
            '"SwimPlatformPublicHeaders"',
            '"SwimIoPublicHeaders"',
            '"SwimAssetPublicHeaders"',
            '"SwimAssetCompilerPublicHeaders"',
            '"SwimPhysicsPublicHeaders"',
            '"SwimPhysicsBackendContractCompile"',
            '"SwimAssetCooker"',
            'Join-Path $Root "Assets"',
            'SwimTests reported failures',
            'Repository asset cooking failed',
            '"Tests", "Third Party", "CMake"',
            "Ninja was not found; using the Visual Studio generator fallback",
            "[switch]$DebugBuild",
        ):
            if fragment not in helper_text:
                fail(f"Windows build toolchain auto-discovery is missing: {fragment}", failures)

        if re.search(r"\[switch\]\$Debug(?:\s|$)", helper_text):
            fail("Windows build helper redeclares PowerShell's built-in Debug common parameter", failures)

        if "Remove-Item -LiteralPath $Path -Recurse" in helper_text:
            fail(
                "Windows clean helper regressed to PowerShell recursive deletion; use the idempotent extended-length native remover for dependency/build trees",
                failures,
            )

        if "\\\\?\\" not in helper_text:
            fail("Windows clean helper no longer uses the extended-length path prefix for MAX_PATH-safe deletion", failures)

    for path in (
        ROOT / "scripts" / "build-windows-clean.ps1",
        ROOT / "scripts" / "build-windows-soft.ps1",
    ):
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "windows-build-common.ps1",
            "Get-SwimWindowsBuildPlan",
            "BuildPlan.CMakePath",
            "-DebugBuild:$Debug",
            "Invoke-SwimWindowsTestSuite -BuildPlan $BuildPlan",
            "Invoke-SwimWindowsAssetCookValidation -Root $Root -BuildPlan $BuildPlan",
        ):
            if fragment not in text:
                fail(f"Windows build script bypasses toolchain auto-discovery: {path.relative_to(ROOT)}: {fragment}", failures)

    windows_clean = ROOT / "scripts" / "build-windows-clean.ps1"
    if windows_clean.is_file():
        clean_text = windows_clean.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            'build/windows-release',
            'build/windows-debug',
            'build/windows-vs',
            'build/.px',
            'Join-Path $Root ".cache"',
            'Remove-SwimGeneratedDirectory -Path $PhysXShortWorktree -MayBeDirectoryLink',
            'Test-SwimPathEntryExists -Path $RemovedPath',
            'SwimEngine.sln',
            '--preset windows-vs',
            'FETCHCONTENT_FULLY_DISCONNECTED=ON',
            'Visual Studio solution ready',
            'Assert-SwimVisualStudioSolutionLayout -SolutionPath $VisualStudioSolution',
        ):
            if fragment not in clean_text:
                fail(f"Windows clean build does not guarantee Visual Studio solution generation: {fragment}", failures)

    windows_soft = ROOT / "scripts" / "build-windows-soft.ps1"
    if windows_soft.is_file():
        soft_text = windows_soft.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            'build/windows-vs',
            'SwimEngine.sln',
            '--preset windows-vs',
            'FETCHCONTENT_FULLY_DISCONNECTED=ON',
            'Visual Studio solution synchronized',
            'Assert-SwimVisualStudioSolutionLayout -SolutionPath $VisualStudioSolution',
        ):
            if fragment not in soft_text:
                fail(f"Windows soft build does not keep the Visual Studio solution synchronized: {fragment}", failures)

        build_index = soft_text.find('--build --preset $BuildPlan.BuildPreset --parallel')
        solution_index = soft_text.find('--preset windows-vs -DFETCHCONTENT_FULLY_DISCONNECTED=ON')
        if build_index == -1 or solution_index == -1 or build_index > solution_index:
            fail(
                "Windows soft build must compile the primary Ninja tree before refreshing the secondary Visual Studio tree",
                failures,
            )

    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    shader_cmake_text = (ROOT / "cmake" / "Shaders.cmake").read_text(encoding="utf-8", errors="ignore")
    if "CONFIGURE_DEPENDS" in cmake_text.replace(
        "# discovery configure-time only instead of using CONFIGURE_DEPENDS: the latter",
        "",
    ) or "CONFIGURE_DEPENDS" in shader_cmake_text:
        fail(
            "First-party CONFIGURE_DEPENDS glob verification is enabled; supported workflows already configure before build",
            failures,
        )

    # Both generators must suppress automatic regeneration. Ninja can otherwise
    # loop on a dependency's VerifyGlobs edge; Visual Studio attaches a stamp
    # check to every project, so a stale stamp launches one concurrent configure
    # per project and they corrupt each other and the shared dependency caches.
    for fragment in (
        'if(CMAKE_GENERATOR MATCHES "^Ninja" OR CMAKE_GENERATOR MATCHES "^Visual Studio ")',
        "CMAKE_SUPPRESS_REGENERATION ON CACHE BOOL",
    ):
        if fragment not in cmake_text:
            fail(
                f"automatic CMake regeneration suppression is missing: {fragment}",
                failures,
            )

    physx_cmake_text = (ROOT / "cmake" / "PhysX.cmake").read_text(encoding="utf-8", errors="ignore")
    for fragment in (
        "swim_physx_normalize_git_config",
        "config --local --get",
        "status --porcelain --untracked-files=all",
    ):
        if fragment not in physx_cmake_text:
            fail(
                "PhysX configure-time normalization must read the shared dependency cache before "
                f"writing to it so concurrent configures cannot collide on Git locks: {fragment}",
                failures,
            )

    presets_text = (ROOT / "CMakePresets.json").read_text(encoding="utf-8", errors="ignore")
    if '"CMAKE_SUPPRESS_REGENERATION": "ON"' not in presets_text:
        fail(
            "Windows Ninja preset does not pin CMAKE_SUPPRESS_REGENERATION=ON",
            failures,
        )

    windows_common_text = (ROOT / "scripts" / "windows-build-common.ps1").read_text(encoding="utf-8", errors="ignore")
    for fragment in (
        "function Assert-SwimNinjaManifestStable",
        '"RERUN_CMAKE"',
        '"VerifyGlobs.cmake"',
        '"cmake.verify_globs"',
    ):
        if fragment not in windows_common_text:
            fail(
                f"Windows build manifest loop guard is missing: {fragment}",
                failures,
            )

    for script_path in (windows_soft, windows_clean):
        if script_path.is_file():
            script_text = script_path.read_text(encoding="utf-8", errors="ignore")
            if "Assert-SwimNinjaManifestStable -BuildDirectory $BuildPlan.BuildDirectory" not in script_text:
                fail(
                    f"{script_path.name} does not validate the Ninja manifest before building",
                    failures,
                )

    dependencies_text = (ROOT / "cmake" / "Dependencies.cmake").read_text(encoding="utf-8", errors="ignore")
    if 'file(WRITE "${SWIM_BASIS_GENERATED_DIR}/basisu_transcoder.cpp"' in dependencies_text:
        fail(
            "Basis generated source is rewritten unconditionally during configure instead of preserving its timestamp when content is unchanged",
            failures,
        )

    generate_solution = ROOT / "scripts" / "generate-solution.ps1"
    if generate_solution.is_file():
        generate_text = generate_solution.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "windows-build-common.ps1",
            "Get-SwimWindowsBuildPlan",
            "--preset windows-vs",
            "FETCHCONTENT_FULLY_DISCONNECTED=$Disconnected",
            "Assert-SwimVisualStudioSolutionLayout",
            "Organized Visual Studio solution ready",
        ):
            if fragment not in generate_text:
                fail(f"manual Visual Studio solution generation bypasses the organized CMake workflow: {fragment}", failures)

    soft_scripts = (
        ROOT / "scripts" / "build-windows-soft.ps1",
        ROOT / "scripts" / "build-linux-soft.sh",
    )
    for path in soft_scripts:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if "FETCHCONTENT_FULLY_DISCONNECTED=ON" not in text:
            fail(f"soft build is not dependency-disconnected: {path.relative_to(ROOT)}", failures)
        if "CPM_*" not in text:
            fail(f"soft build does not require a cached CPM bootstrap: {path.relative_to(ROOT)}", failures)

    preset_data = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
    configure_presets = {preset["name"]: preset for preset in preset_data.get("configurePresets", [])}
    build_presets = {preset["name"]: preset for preset in preset_data.get("buildPresets", [])}
    for name in ("windows-debug", "windows-release", "linux-debug", "linux-release"):
        if name not in configure_presets:
            fail(f"configure preset is missing for clean/soft builds: {name}", failures)
        if name not in build_presets:
            fail(f"build preset is missing for clean/soft builds: {name}", failures)


def check_machine_specific_paths(failures: list[str]) -> None:
    patterns = (
        re.compile(r"[A-Za-z]:\\Users\\"),
        re.compile(r"[A-Za-z]:/Users/"),
        re.compile(r"\$\(ProjectDir\)Source\\Library"),
        re.compile(r"\$\(VULKAN_SDK\)"),
    )

    # CMake caches, generated shader reflection, third-party sources, and
    # archived code are not maintained source. Prune them before traversal:
    # generated files necessarily contain absolute build-machine paths, and
    # walking a populated CPM cache is especially costly from WSL.
    for directory, children, files in os.walk(ROOT):
        if Path(directory) == ROOT:
            children[:] = [name for name in children if name not in {".git", ".cache", "build", "Deprecated"}]
        if Path(directory) == ROOT / "Assets":
            children[:] = [name for name in children if name != "Cooked"]
        for name in files:
            path = Path(directory) / name
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
    math_dependency_text = (ROOT / "cmake" / "MathDependencies.cmake").read_text(encoding="utf-8", errors="ignore")
    dependency_contract_text = dependency_text + "\n" + math_dependency_text

    required_root_fragments = (
        'set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")',
        '$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:CXX>>:_ITERATOR_DEBUG_LEVEL=0>',
        '$<$<CONFIG:Debug>:_SWIM_DEBUG>',
        '$<$<CONFIG:Debug>:/U_DEBUG>',
        'target_precompile_headers(SwimEngine',
    )
    for fragment in required_root_fragments:
        if fragment not in cmake_text:
            fail(f"legacy x64 build contract is missing from CMakeLists.txt: {fragment}", failures)

    required_physx_fragments = (
        '107.3-omni-and-physx-5.6.1',
        'GIT_CONFIG',
        '"filter.lfs.process="',
        '"filter.lfs.required=false"',
        'config --local',
        'reset --hard HEAD',
        'clean -ffdx',
        'SWIM_PHYSX_SHORT_SOURCE_DIR "${CMAKE_SOURCE_DIR}/build/.px"',
        '-DSWIM_PHYSX_SHORT_SOURCE_DIR=${SWIM_PHYSX_SHORT_SOURCE_DIR}',
        'IMPORTED_LOCATION_DEBUG "${SWIM_PHYSX_CHECKED_DIR}',
        'IMPORTED_LOCATION_RELEASE "${SWIM_PHYSX_RELEASE_DIR}',
        '$<$<NOT:$<CONFIG:Release>>:SwimPhysXPvdSDK>',
        'target_compile_definitions(SwimPhysX INTERFACE PX_PHYSX_STATIC_LIB)',
    )
    for fragment in required_physx_fragments:
        if fragment not in physx_text:
            fail(f"PhysX mapping is missing required fragment: {fragment}", failures)

    required_physx_build_fragments = (
        'vc17win64-cpu-only',
        'worktree add --force --detach',
        'worktree remove --force',
        'worktree prune',
        'set(SWIM_PHYSX_ROOT "${SWIM_PHYSX_SHORT_SOURCE_DIR}/physx")',
        'The CPM checkout is immutable dependency source.',
        'status --porcelain --untracked-files=all',
        'PhysX generation/build modified the CPM dependency checkout',
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

    # Phase 23: the runtime shader set (Slang -> SPIR-V through SwimSlangCompiler) is
    # staged once and deployed next to the executable as Shaders/Runtime.
    required_shader_fragments = (
        'function(swim_define_runtime_shader_set)',
        'add_custom_target(SwimRuntimeShaders',
        'function(swim_configure_shaders target)',
        '$<TARGET_FILE_DIR:${target}>/Shaders/Runtime',
    )
    for fragment in required_shader_fragments:
        if fragment not in shader_text:
            fail(f"Slang runtime shader build behavior is missing: {fragment}", failures)
    for forbidden in ('SWIM_DXC_EXECUTABLE', 'dxc.exe', 'PRE_BUILD'):
        if forbidden in shader_text:
            fail(f"retired DXC/PRE_BUILD shader path is still active: {forbidden}", failures)

    required_dependency_contract_fragments = (
        'NAME glm_source',
        'add_library(SwimGlm INTERFACE)',
        'add_library(glm::glm ALIAS SwimGlm)',
        'NAME entt_source',
        'add_library(SwimEnTT INTERFACE)',
        'add_library(EnTT::EnTT ALIAS SwimEnTT)',
        'function(swim_assert_cached_git_dependency_clean dependency_name source_dir)',
        'status --porcelain --untracked-files=all',
        "Cached dependency '${dependency_name}' is dirty",
        'swim_physx_source',
    )
    for fragment in required_dependency_contract_fragments:
        if fragment not in dependency_contract_text:
            fail(f"legacy/foundation third-party link contract is missing: {fragment}", failures)

    if 'GITHUB_REPOSITORY nlohmann/json' in dependency_text:
        fail(
            "nlohmann/json reverted to a full Git checkout; use the pinned release single-header artifact to avoid Windows path/dirty-cache failures",
            failures,
        )

    for path in (ROOT / "Source").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in {".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx"}:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if "nlohmann/json_fwd.hpp" in text:
            fail(
                f"first-party source assumes unavailable nlohmann/json split headers; the dependency contract provides only json.hpp: {path.relative_to(ROOT)}",
                failures,
            )

    # The PhysX generator is a batch file, and two invocation shapes are known
    # to be broken. Embedding an escaped quoted path inside the single cmd.exe
    # argument makes CMake preserve the escapes, so Windows tries to run a
    # command literally named \"C:/...bat\". Passing the bare batch name and
    # relying on the working directory breaks wherever
    # NoDefaultCurrentDirectoryInExePath is set (common in CI/sandboxes), which
    # surfaces as "'generate_projects.bat' is not recognized". The supported
    # form passes the native absolute path as its own argument.
    if '\\"${SWIM_PHYSX_GENERATOR}\\"' in physx_build_text:
        fail("PhysX generator still uses the broken escaped absolute-path cmd invocation", failures)

    if 'cmd.exe /d /c "call generate_projects.bat' in physx_build_text:
        fail(
            "PhysX generator resolves the batch file from the working directory; that fails when "
            "NoDefaultCurrentDirectoryInExePath is set. Pass the absolute native path instead.",
            failures,
        )

    if 'mklink /J' in physx_build_text:
        fail("PhysX reverted to a source-cache junction; use the isolated short Git worktree so builds cannot dirty CPM source", failures)

    required_physx_command_fragments = (
        'file(TO_NATIVE_PATH "${SWIM_PHYSX_GENERATOR}" SWIM_PHYSX_GENERATOR_NATIVE)',
        'COMMAND cmd.exe /d /c "${SWIM_PHYSX_GENERATOR_NATIVE}" "${SWIM_PHYSX_PRESET}"',
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
    )
    for fragment in pins:
        if fragment not in dependency_contract_text:
            fail(f"dependency pin is missing: {fragment}", failures)


def check_modern_cmake_dependency_compatibility(failures: list[str]) -> None:
    dependency_text = (ROOT / "cmake" / "Dependencies.cmake").read_text(encoding="utf-8", errors="ignore")

    # Phase 22/23 retired the legacy renderer and its third-party stack (GLAD/OpenGL,
    # zstd, Basis Universal, nlohmann/json, stb, the legacy font/ImGui helpers). Their
    # CMake lives in Deprecated/cmake/LegacyDependencies.cmake for reference only.
    for retired in ("SwimZstd", "SwimBasisTranscoder", "SwimGlad", "SwimJson", "glad_add_library", "nlohmann_json"):
        if retired in dependency_text:
            fail(f"runtime dependency list regained a retired legacy dependency: {retired}", failures)
    for fragment in ("NAME entt_source", "add_library(EnTT::EnTT ALIAS SwimEnTT)"):
        if fragment not in dependency_text:
            fail(f"runtime dependency list is missing: {fragment}", failures)
    if (ROOT / "cmake" / "LegacyDependencies.cmake").exists():
        fail("cmake/LegacyDependencies.cmake must stay archived under Deprecated/cmake", failures)
    if not (ROOT / "Deprecated" / "cmake" / "LegacyDependencies.cmake").is_file():
        fail("archived Deprecated/cmake/LegacyDependencies.cmake is missing", failures)


def check_windows_compile_contract_and_warning_hygiene(failures: list[str]) -> None:
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    dependency_text = (ROOT / "cmake" / "Dependencies.cmake").read_text(encoding="utf-8", errors="ignore")
    physx_build_text = (ROOT / "cmake" / "BuildPhysX.cmake").read_text(encoding="utf-8", errors="ignore")

    for definition in ("UNICODE", "_UNICODE"):
        if definition not in cmake_text:
            fail(f"Win32 Unicode compile definition is missing: {definition}", failures)

    if "/W3" not in cmake_text or "/W4" in cmake_text:
        fail("MSVC warning level no longer matches the legacy x64 /W3 project", failures)

    # OpenGL (and WGL context creation) was removed in Phase 22.
    for retired in ("glad", "GLAD", "WGL_"):
        if retired in dependency_text:
            fail(f"OpenGL/WGL dependency returned to cmake/Dependencies.cmake: {retired}", failures)
    for path in (ROOT / "Source").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if re.search(r"#\s*include\s*[<\"](?:glad/|GL/gl)", text):
            fail(f"active source includes OpenGL headers again: {path.relative_to(ROOT)}", failures)

    if "PX_BUILDPUBLICSAMPLES" in physx_build_text:
        fail("PhysX reconfigure still passes the unused PX_BUILDPUBLICSAMPLES option", failures)




def check_foundation_architecture_boundaries(failures: list[str]) -> None:
    pch_path = ROOT / "Source" / "Engine" / "Utility" / "PCH.h"
    pch_text = pch_path.read_text(encoding="utf-8", errors="ignore")
    banned_pch_fragments = (
        "Windows.h",
        "vulkan/vulkan",
        "vulkan_win32",
        "glad/gl.h",
        "glad/wgl.h",
        "SDL3/",
    )
    for fragment in banned_pch_fragments:
        if fragment in pch_text:
            fail(f"generic PCH still injects platform/backend dependency: {fragment}", failures)

    public_header_roots = (
        ROOT / "Source" / "Engine" / "Platform",
        ROOT / "Source" / "Engine" / "Input",
        ROOT / "Source" / "Engine" / "IO",
    )
    banned_public_fragments = (
        "<Windows.h>",
        "<SDL3/",
        "<vulkan/",
        "<glad/",
    )
    for root in public_header_roots:
        for path in root.glob("*.h"):
            text = path.read_text(encoding="utf-8", errors="ignore")
            for fragment in banned_public_fragments:
                if fragment in text:
                    fail(
                        f"public foundation header leaks an implementation dependency: {path.relative_to(ROOT)} -> {fragment}",
                        failures,
                    )

    generic_contract_files = (
        ROOT / "Source" / "Engine" / "SwimEngine.h",
        ROOT / "Source" / "Engine" / "Input" / "InputSystem.h",
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Runtime" / "RenderDevice.h",
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Runtime" / "FrameRenderer.h",
    )
    win32_contract_tokens = re.compile(r"\b(?:HWND|HINSTANCE|WPARAM|LPARAM|LRESULT|WNDPROC)\b|\bWM_[A-Z0-9_]+\b|\bVK_[A-Z0-9_]+\b")
    for path in generic_contract_files:
        text = path.read_text(encoding="utf-8", errors="ignore")
        if win32_contract_tokens.search(text):
            fail(f"generic engine contract still exposes Win32/message vocabulary: {path.relative_to(ROOT)}", failures)

    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    required_foundation_fragments = (
        "file(GLOB_RECURSE SWIM_PLATFORM_SOURCES",
        "file(GLOB_RECURSE SWIM_INPUT_SOURCES",
        "target_link_libraries(SwimEngine PRIVATE",
        "${SWIM_SDL3_TARGET}",
    )
    for fragment in required_foundation_fragments:
        if fragment not in cmake_text:
            fail(f"foundation CMake boundary is missing: {fragment}", failures)

    tests_text = read_tests_cmake()
    for fragment in ("${SWIM_PLATFORM_SOURCES}", "${SWIM_INPUT_SOURCES}", "${SWIM_SDL3_TARGET}"):
        if fragment not in tests_text:
            fail(f"platform/input test source or private dependency is missing: {fragment}", failures)

    window_system_header = (ROOT / "Source" / "Engine" / "Platform" / "WindowSystem.h").read_text(
        encoding="utf-8", errors="ignore"
    )
    if "CreateWindow(" in window_system_header:
        fail("WindowSystem public API uses CreateWindow, which collides with the Win32 CreateWindow macro", failures)

    window_internal_header = (ROOT / "Source" / "Engine" / "Platform" / "Internal" / "WindowInternal.h").read_text(
        encoding="utf-8", errors="ignore"
    )
    if '#include "Engine/Platform/Window.h"' not in window_internal_header:
        fail("WindowInternal.h must use the include-root path so GCC/Clang can resolve Window.h", failures)

    windows_api_path = ROOT / "Source" / "Engine" / "Platform" / "Internal" / "WindowsApi.h"
    if not windows_api_path.is_file():
        fail("centralized WindowsApi.h include boundary is missing", failures)
    else:
        windows_api_text = windows_api_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in ("WIN32_LEAN_AND_MEAN", "NOMINMAX", "#include <Windows.h>"):
            if fragment not in windows_api_text:
                fail(f"WindowsApi.h is missing required Win32 include guard behavior: {fragment}", failures)

    raw_windows_include = re.compile(r'#\s*include\s*[<"](?:Windows|windows)\.h[>"]')
    windows_api_resolved = windows_api_path.resolve()
    source_engine_root = ROOT / "Source" / "Engine"
    for path in source_engine_root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        if path.resolve() == windows_api_resolved:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if raw_windows_include.search(text):
            fail(
                f"first-party source bypasses the NOMINMAX-safe WindowsApi.h boundary: {path.relative_to(ROOT)}",
                failures,
            )

    for compile_definition in ("WIN32_LEAN_AND_MEAN", "NOMINMAX", "VK_USE_PLATFORM_WIN32_KHR"):
        if f"$<$<PLATFORM_ID:Windows>:{compile_definition}>" not in cmake_text:
            fail(f"SwimEngine is missing Windows compile definition: {compile_definition}", failures)

    scene_text = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.cpp").read_text(
        encoding="utf-8", errors="ignore"
    )
    for stale_editor_command_fragment in ("const wchar_t*", 'send(L"'):
        if stale_editor_command_fragment in scene_text:
            fail(
                "Scene editor-command hotkeys must use the UTF-8 std::string_view transport, not wide strings",
                failures,
            )

    platform_compile_definition_block = re.search(
        r"target_compile_definitions\(SwimEngine PRIVATE(?P<body>.*?)\n\)",
        cmake_text,
        re.DOTALL,
    )
    if platform_compile_definition_block is None:
        fail("SwimEngine is missing its private Windows compile-definition block", failures)
    else:
        platform_definition_text = platform_compile_definition_block.group("body")
        for compile_definition in ("WIN32_LEAN_AND_MEAN", "NOMINMAX"):
            if f"$<$<PLATFORM_ID:Windows>:{compile_definition}>" not in platform_definition_text:
                fail(f"SwimEngine is missing Windows compile definition: {compile_definition}", failures)

    platform_input_types = (ROOT / "Source" / "Engine" / "Platform" / "InputTypes.h").read_text(
        encoding="utf-8", errors="ignore"
    )
    for high_level_input_type in ("InputAction", "InputBinding", "InputMap"):
        if high_level_input_type in platform_input_types:
            fail(
                f"high-level input mapping type leaked into Swim::Platform: {high_level_input_type}",
                failures,
            )

    public_header_compile_text = (
        ROOT / "Source" / "Tests" / "HeaderBoundary" / "PlatformPublicHeaders.cpp"
    ).read_text(encoding="utf-8", errors="ignore")
    for public_input_header in ("Engine/Input/InputMap.h", "Engine/Input/InputSystem.h"):
        if public_input_header not in public_header_compile_text:
            fail(f"foundation public-header compile coverage is missing: {public_input_header}", failures)

    hardcoded_asset_path = re.compile(r'Assets\\\\')
    for source_root in (ROOT / "Source" / "Engine", ROOT / "Source" / "Game"):
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            if hardcoded_asset_path.search(text):
                fail(f"hard-coded Windows asset path remains: {path.relative_to(ROOT)}", failures)



def check_phase2_engine_architecture(failures: list[str]) -> None:
    """Phase 2 explicit ownership, as reshaped by Phase 22/23 (runtime, no editor, no OpenGL)."""
    engine_header = (ROOT / "Source" / "Engine" / "SwimEngine.h").read_text(encoding="utf-8", errors="ignore")
    engine_source = (ROOT / "Source" / "Engine" / "SwimEngine.cpp").read_text(encoding="utf-8", errors="ignore")
    config_header = (ROOT / "Source" / "Engine" / "EngineConfig.h").read_text(encoding="utf-8", errors="ignore")
    config_source = (ROOT / "Source" / "Engine" / "EngineConfig.cpp").read_text(encoding="utf-8", errors="ignore")
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")

    for fragment in (
        "enum class GraphicsBackend",
        "enum class PhysicsBackend",
        "struct EngineConfig",
        "GraphicsBackend Graphics",
        "PhysicsBackend Physics",
        "Swim::Platform::WindowDesc Window",
        "EngineState InitialState",
        "double FixedRate",
        "double TimeScale",
        "bool Render",
        "std::uint64_t MaxFrames",
        "double FixedFrameDelta",
        "std::vector<std::string> StartupCommands",
    ):
        if fragment not in config_header:
            fail(f"runtime configuration contract is missing: {fragment}", failures)

    for fragment in ('"--graphics"', '"--physics"', "ResolveGraphicsBackend", "ResolvePhysicsBackend", '"--no-render"', '"--exec"'):
        if fragment not in config_source:
            fail(f"command-line runtime selection is missing: {fragment}", failures)
    if "OpenGL" in config_header or "GraphicsBackend::OpenGL" in config_source:
        fail("the OpenGL backend returned to the runtime configuration", failures)

    if "SystemManager" in engine_header or "systemManager" in engine_source or "AddSystem<" in engine_source:
        fail("SwimEngine core lifecycle regressed to stringly typed SystemManager ownership", failures)

    for fragment in (
        "int SwimEngine::Start()",
        "int SwimEngine::Run()",
        "bool SwimEngine::Tick()",
        "int SwimEngine::Awake()",
        "int SwimEngine::Init()",
        "void SwimEngine::Update(double realDelta)",
        "int SwimEngine::Exit()",
        "int SwimEngine::InitRenderer()",
        "void SwimEngine::RegisterEngineCommands()",
    ):
        if fragment not in engine_source:
            fail(f"runtime lifecycle contract is missing: {fragment}", failures)

    for owner_type in (
        "Swim::Input::InputSystem",
        "Swim::Commands::CommandRegistry",
        "SceneSystem",
        "CameraSystem",
        "PhysicsSystem",
        "RenderDevice",
        "FrameRenderer",
        "SceneRenderBridge",
        "UiRuntime",
    ):
        if f"std::unique_ptr<{owner_type}>" not in engine_header:
            fail(f"core owner is not uniquely owned: {owner_type}", failures)
        if f"std::shared_ptr<{owner_type}>" in engine_header:
            fail(f"core owner regressed to shared ownership: {owner_type}", failures)
    for fragment in ("EngineStateMachine stateMachine", "SimulationClock clock", "TagRegistry tagRegistry"):
        if fragment not in engine_header:
            fail(f"engine-owned runtime state is missing: {fragment}", failures)

    for retired in ("VulkanRenderer", "OpenGLRenderer", "EditorCamera", "EngineState::Editing", "Gizmo", "RenderContext"):
        if retired in engine_header or retired in engine_source:
            fail(f"SwimEngine still references retired legacy/editor machinery: {retired}", failures)

    for fragment in ("set(SWIM_CORE_SOURCES", "Source/Engine/EngineConfig.cpp", "option(SWIM_BUILD_ENGINE"):
        if fragment not in cmake_text:
            fail(f"Core CMake/test boundary is missing: {fragment}", failures)
    if "SWIM_BUILD_LEGACY_ENGINE" in cmake_text:
        fail("CMakeLists.txt still offers the retired SWIM_BUILD_LEGACY_ENGINE switch", failures)

    runtime_locator_patterns = (
        "SwimEngine::GetInstance()",
        "MeshPool::GetInstance()",
        "TexturePool::GetInstance()",
        "MaterialPool::GetInstance()",
        "FontPool::GetInstance()",
        "EntityFactory::GetInstance()",
    )
    for source_root in (ROOT / "Source" / "Engine", ROOT / "Source" / "Game"):
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            for locator in runtime_locator_patterns:
                if locator in text:
                    fail(f"runtime service locator returned in {path.relative_to(ROOT)}: {locator}", failures)

    for shared_engine_pattern in ("std::shared_ptr<SwimEngine>", "std::weak_ptr<SwimEngine>", "enable_shared_from_this<SwimEngine>"):
        if shared_engine_pattern in engine_header or shared_engine_pattern in engine_source:
            fail(f"SwimEngine lifetime regressed to process-shared ownership: {shared_engine_pattern}", failures)

    scene_header = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.h").read_text(encoding="utf-8", errors="ignore")
    scene_source = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.cpp").read_text(encoding="utf-8", errors="ignore")
    for fragment in ("Scene();", "explicit Scene(const std::string& name);", "~Scene() override;"):
        if fragment not in scene_header:
            fail(f"Scene incomplete-type ownership boundary is missing declaration: {fragment}", failures)
    for fragment in ("Scene::Scene()", "Scene::Scene(const std::string&", "Scene::~Scene()"):
        if fragment not in scene_source:
            fail(f"Scene incomplete-type ownership boundary is missing out-of-line definition: {fragment}", failures)

    # Gameplay translation units include the concrete service headers they dereference.
    game_service_include_rules = (
        ("scene->", "Engine/Systems/Scene/Scene.h"),
        ("input->", "Engine/Input/InputSystem.h"),
        ("cameraSystem->", "Engine/Systems/Camera/CameraSystem.h"),
        ("sceneSystem->", "Engine/Systems/Scene/SceneSystem.h"),
    )
    for path in (ROOT / "Source" / "Game").rglob("*.cpp"):
        text = path.read_text(encoding="utf-8", errors="ignore").replace("\\", "/")
        for expression, include_path in game_service_include_rules:
            if expression in text and include_path not in text:
                fail(
                    f"gameplay source dereferences a forward-declared service without its concrete header: "
                    f"{path.relative_to(ROOT)}: {expression} requires {include_path}",
                    failures,
                )


def check_phase20_text_dependencies(failures: list[str]) -> None:
    """Item 79 groundwork: FreeType, HarfBuzz and msdfgen are pinned CPM
    dependencies bundled as Swim::TextDependencies and stay private to the
    text/UI module and its tests."""
    dependency_path = ROOT / "cmake" / "TextDependencies.cmake"
    if not dependency_path.is_file():
        fail("Phase 20 text dependency module is missing: cmake/TextDependencies.cmake", failures)
        return
    dependency_text = dependency_path.read_text(encoding="utf-8", errors="ignore")
    for fragment in (
        "GITHUB_REPOSITORY freetype/freetype",
        "GIT_TAG VER-2-14-3",
        "GITHUB_REPOSITORY harfbuzz/harfbuzz",
        "GIT_TAG 14.5.0",
        "src/harfbuzz.cc",
        "GITHUB_REPOSITORY Chlumsky/msdfgen",
        "GIT_TAG v1.13",
        "GITHUB_REPOSITORY Tehreer/SheenBidi",
        "GIT_TAG v3.0.0",
        "Source/SheenBidi.c",
        "SB_CONFIG_UNITY",
        "GITHUB_REPOSITORY adah1972/libunibreak",
        "GIT_TAG libunibreak_8_0",
        "add_library(SwimSheenBidi STATIC",
        "add_library(SwimUnibreak STATIC",
        'set(MSDFGEN_CORE_ONLY ON CACHE BOOL "" FORCE)',
        'PROPERTY MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}"',
        'set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")',
        "add_library(Swim::TextDependencies ALIAS SwimTextDependencies)",
        "set(SWIM_TEXT_DEPENDENCIES_AVAILABLE ON)",
    ):
        if fragment not in dependency_text:
            fail(f"Phase 20 text dependency contract is missing: {fragment}", failures)
    if dependency_text.count("UPDATE_DISCONNECTED YES") < 5:
        fail("Phase 20 text dependencies must all be UPDATE_DISCONNECTED", failures)

    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    if "include(cmake/TextDependencies.cmake)" not in cmake_text:
        fail("CMakeLists.txt does not include cmake/TextDependencies.cmake", failures)
    if "Swim::TextDependencies" not in read_tests_cmake():
        fail("SwimTests must link Swim::TextDependencies privately for the Text suites", failures)
    check_suite_is_compiled("Text", "TextDependencyTests.cpp", failures)

    for group, name in (("Text", "FontFaceTests.cpp"), ("Text", "GlyphAtlasTests.cpp"), ("UI", "UiDocumentTests.cpp"),
                        ("Text", "TextSegmentationTests.cpp"), ("Text", "TextLayoutTests.cpp"), ("UI", "UiLayoutFlexTests.cpp"),
                        ("UI", "UiTextEditTests.cpp")):
        check_suite_is_compiled(group, name, failures)
    for fragment in ("SWIM_TEXT_UI_SOURCES", "target_link_libraries(SwimEngine PRIVATE Swim::TextDependencies)"):
        if fragment not in cmake_text:
            fail(f"text/UI runtime build wiring is missing: {fragment}", failures)
    for fragment in ("${SWIM_TEXT_UI_SOURCES}", "SwimTextUiPublicHeaders", "SWIM_TEXT_FONT_FIXTURE_PATH"):
        if fragment not in read_tests_cmake():
            fail(f"text/UI test wiring is missing: {fragment}", failures)

    # No scene/renderer/platform dependency, and no third-party types in public headers.
    text_ui_roots = (ROOT / "Source/Engine/Systems/Text", ROOT / "Source/Engine/Systems/UI")
    for module in text_ui_roots:
        for path in module.rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            content = path.read_text(encoding="utf-8", errors="ignore")
            for forbidden in ("Engine/Systems/Renderer/", "Engine/Systems/Scene/", "Engine/Platform/",
                              "Engine/Components/", "<SDL", "<vulkan", "<entt", "SwimEngine::GetInstance"):
                if forbidden in content:
                    fail(f"text/UI dependency boundary violated: {path.relative_to(ROOT)} ({forbidden})", failures)
            if path.suffix == ".h":
                for forbidden in ("ft2build.h", "FT_FREETYPE_H", "<freetype/", "<hb", "<msdfgen", "FT_Face", "hb_font_t", "msdfgen::",
                                  "<SheenBidi/", "SBParagraphRef", "<linebreak.h>", "<graphemebreak.h>", "<wordbreak.h>"):
                    if forbidden in content:
                        fail(f"text implementation type leaked into public header: {path.relative_to(ROOT)} ({forbidden})", failures)

    # Only the text/UI module (Systems/Text, Systems/UI) and its tests may see the libraries.
    allowed_roots = (
        ROOT / "Source" / "Engine" / "Systems" / "Text",
        ROOT / "Source" / "Engine" / "Systems" / "UI",
        ROOT / "Source" / "Tests" / "Suites" / "Text",
    )
    library_includes = ("<ft2build.h>", "FT_FREETYPE_H", "<freetype/", "<hb.h>", "<hb-", "<msdfgen", "<SheenBidi/",
                        "<linebreak.h>", "<graphemebreak.h>", "<wordbreak.h>", "<unibreak")
    for source_root in (ROOT / "Source" / "Engine", ROOT / "Source" / "Game", ROOT / "Source" / "Tools", ROOT / "Source" / "Tests"):
        if not source_root.exists():
            continue
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            if any(path.is_relative_to(allowed) for allowed in allowed_roots):
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            if any(f"#include {token}" in text or f"#include{token}" in text for token in library_includes):
                fail(f"text library headers leaked outside the text/UI module: {path.relative_to(ROOT)}", failures)


def check_phase20_ui_rendering(failures: list[str]) -> None:
    """Item 79 rendering and input: Renderer/UiRendering turns UiDocument paint into one
    graph pass (atlas residency, CPU reference, SwimUiQuad); Systems/UiInput adapts the
    Input snapshot. Both compile only with the text/UI module."""
    renderer = ROOT / "Source/Engine/Systems/Renderer"
    ui_rendering = renderer / "UiRendering"
    for relative in ("UiRenderRecords.h", "UiRenderBindings.h", "UiRenderSettings.h", "UiRenderReference.cpp", "UiAtlasTextures.cpp",
                     "UiRenderer.cpp", "UiRenderSurfaces.cpp"):
        if not (ui_rendering / relative).is_file():
            fail(f"UI rendering unit is missing: Renderer/UiRendering/{relative}", failures)
    for shader in ("UiRecords", "UiQuad"):
        if not (ROOT / f"Source/Shaders/Slang/Ui/{shader}.slang").is_file():
            fail(f"UI shader is missing: Shaders/Slang/Ui/{shader}.slang", failures)
    for path in ui_rendering.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        for include in re.findall(r'#\s*include\s*[<"]([^>"\n]+)', path.read_text(encoding="utf-8")):
            if "Renderer/" in include and not re.search(r"Renderer/(UiRendering|RenderGraph|RHI|Resources)/", include):
                fail(f"UiRendering may include only UiRendering, RenderGraph, RHI and Resources renderer headers: {path.relative_to(ROOT)} -> {include}", failures)
            if any(part in include for part in ("Backends/", "vulkan", "entt", "Systems/Scene", "Engine/Platform", "Engine/Assets", "Engine/IO",
                                                "Engine/Jobs", "<hb", "ft2build", "msdfgen", "SheenBidi", "linebreak.h")):
                fail(f"UiRendering must not depend on {include}: {path.relative_to(ROOT)}", failures)
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials", "GpuMaterials", "Environment", "Lights",
                   "ClusteredLighting", "Shadows", "ForwardPlus", "PostProcess", "Temporal", "ScreenSpace", "Particles", "Skinning", "Residency"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*(Renderer/UiRendering/|Systems/UI/|Systems/Text/)', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on UI rendering or the text/UI module: {path.relative_to(ROOT)}", failures)
    ui_input = ROOT / "Source/Engine/Systems/UiInput"
    if not (ui_input / "UiInputBridge.cpp").is_file():
        fail("the Input -> UiDocument bridge (Systems/UiInput/UiInputBridge.cpp) is missing", failures)
    for path in ui_input.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        for include in re.findall(r'#\s*include\s*[<"]([^>"\n]+)', path.read_text(encoding="utf-8")):
            if include.startswith("Engine/") and not re.match(r"Engine/(Systems/UiInput/|Systems/UI/|Systems/Text/|Input/)", include):
                fail(f"UiInput may include only UiInput, UI, Text and Input headers: {path.relative_to(ROOT)} -> {include}", failures)
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    tests_text = read_tests_cmake()
    for fragment in ("SWIM_RENDER_UI_SOURCES", "SWIM_UI_INPUT_SOURCES", "swim_add_slang_program(SwimUiQuad",
                     "Source/Shaders/Slang/Ui/UiQuad.slang", "Systems/(Text|UI|UiInput|Renderer/UiRendering)/"):
        if fragment not in cmake_text:
            fail(f"UI rendering build wiring is missing from CMakeLists.txt: {fragment}", failures)
    for fragment in ("${SWIM_RENDER_UI_SOURCES}", "${SWIM_UI_INPUT_SOURCES}", "SWIM_UI_QUAD_SPIRV_PATH", "SWIM_UI_QUAD_REFLECTION_PATH",
                     "SWIM_TEXT_FALLBACK_FONT_FIXTURE_PATH"):
        if fragment not in tests_text:
            fail(f"UI rendering test wiring is missing from cmake/Tests.cmake: {fragment}", failures)
    for group, name in (("RenderUi", "UiRenderReferenceTests.cpp"), ("RenderUi", "UiRendererTests.cpp"),
                        ("UiInput", "UiInputBridgeTests.cpp"), ("Input", "InputTextEditingTests.cpp"),
                        ("RHIVulkan", "VulkanUiSmokeTests.cpp"), ("ShaderCompiler", "ShaderUiLayoutTests.cpp")):
        check_suite_is_compiled(group, name, failures)
    if not (ROOT / "Source/Tests/Fixtures/Fonts/SwimTextFallbackFixture.ttf").is_file():
        fail("the Hebrew/Greek fallback font fixture is missing", failures)


def check_phase20_controls_and_canvases(failures: list[str]) -> None:
    """Item 79 controls, themes and canvases: behaviour, visual states, themes, canvas
    math and multi-canvas input live in Systems/UI, which stays independent of the
    renderer, scene, platform and input modules (world-space drawing is UiRendering's)."""
    ui = ROOT / "Source/Engine/Systems/UI"
    for relative in ("UiControls.cpp", "UiVisuals.cpp", "UiTheme.h", "UiTheme.cpp", "UiWidgets.cpp", "UiCanvas.h", "UiCanvas.cpp",
                     "UiCanvasRouter.h", "UiCanvasRouter.cpp", "UiPopups.cpp", "UiSelection.cpp"):
        if not (ui / relative).is_file():
            fail(f"UI controls/canvas unit is missing: Systems/UI/{relative}", failures)
    for path in ui.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        for include in re.findall(r'#\s*include\s*[<"]([^>"\n]+)', path.read_text(encoding="utf-8")):
            if include.startswith("Engine/") and not re.match(r"Engine/Systems/(UI|Text)/", include):
                fail(f"Systems/UI may include only UI and Text headers: {path.relative_to(ROOT)} -> {include}", failures)
            if any(part in include for part in ("SDL", "vulkan", "entt", "glm/", "<hb", "ft2build", "msdfgen")):
                fail(f"Systems/UI must not depend on {include}: {path.relative_to(ROOT)}", failures)
    for group, name in (("UI", "UiControlTests.cpp"), ("UI", "UiCanvasTests.cpp"), ("UI", "UiWidgetTests.cpp"),
                        ("UI", "UiSelectionTests.cpp"), ("UI", "UiPopupTests.cpp"), ("RenderUi", "UiWorldRenderingTests.cpp")):
        check_suite_is_compiled(group, name, failures)
    # Popups, selection controls and virtual lists (the remaining item 79 widgets).
    header = (ui / "UiDocument.h").read_text(encoding="utf-8") if (ui / "UiDocument.h").is_file() else ""
    for fragment in ("void OpenPopup(", "void SetTooltip(", "bool OpenContextMenu(", "Option,",
                     "RadioGroup,", "ListView,", "Dropdown,"):
        if fragment not in header:
            fail(f"UiDocument is missing the popup/selection contract: {fragment}", failures)
    widgets = (ui / "UiWidgets.h").read_text(encoding="utf-8") if (ui / "UiWidgets.h").is_file() else ""
    for fragment in ("CreateRadioGroup(", "CreateListView(", "CreateDropdown(", "CreateMenu(", "CreateTooltip(", "CreateModal(",
                     "class UiVirtualList", "EditableValue"):
        if fragment not in widgets:
            fail(f"UiWidgets is missing a widget: {fragment}", failures)
    smoke = ROOT / "Source/Tests/Suites/RHIVulkan/VulkanUiSmokeTests.cpp"
    if smoke.is_file() and "UiWorldCanvasesMatchTheCpuReference" not in smoke.read_text(encoding="utf-8"):
        fail("the world-canvas native smoke (UiWorldCanvasesMatchTheCpuReference) is not registered", failures)


def check_phase3_job_architecture(failures: list[str]) -> None:
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    dependency_path = ROOT / "cmake" / "JobDependencies.cmake"
    if not dependency_path.is_file():
        fail("Phase 3 job dependency module is missing: cmake/JobDependencies.cmake", failures)
        return

    dependency_text = dependency_path.read_text(encoding="utf-8", errors="ignore")
    job_header = (ROOT / "Source" / "Engine" / "Jobs" / "JobSystem.h").read_text(encoding="utf-8", errors="ignore")
    job_source = (ROOT / "Source" / "Engine" / "Jobs" / "JobSystem.cpp").read_text(encoding="utf-8", errors="ignore")
    parallel_utils = (ROOT / "Source" / "Engine" / "Utility" / "ParallelUtils.h").read_text(encoding="utf-8", errors="ignore")

    for fragment in (
        "GITHUB_REPOSITORY dougbinks/enkiTS",
        "GIT_TAG v1.12",
        "UPDATE_DISCONNECTED YES",
        "add_library(Swim::EnkiTS ALIAS enkiTS)",
    ):
        if fragment not in dependency_text:
            fail(f"Phase 3 enkiTS dependency contract is missing: {fragment}", failures)

    for fragment in (
        "file(GLOB_RECURSE SWIM_JOB_SOURCES",
        "target_link_libraries(SwimEngine PRIVATE",
        "Swim::EnkiTS",
        "SWIM_JOBS_USE_ENKITS=$<BOOL:${SWIM_JOBS_USE_ENKITS}>",
    ):
        if fragment not in cmake_text:
            fail(f"Phase 3 JobSystem CMake boundary is missing: {fragment}", failures)

    if "${SWIM_JOB_SOURCES}" not in read_tests_cmake():
        fail("SwimTests must compile the job source list directly", failures)

    for fragment in (
        "class JobSystem",
        "class JobHandle",
        "class TaskGroup",
        "CreateParallelFor",
        "AddDependency",
        "JobPriority",
        "CreateMainThreadJob",
        "CreateBlockingJob",
        "RegisterCurrentExternalThread",
        "JobShutdownMode",
    ):
        if fragment not in job_header:
            fail(f"Phase 3 public job contract is missing: {fragment}", failures)

    if "TaskScheduler.h" not in job_source:
        fail("JobSystem implementation no longer binds the scheduler backend", failures)

    for source_root in (ROOT / "Source" / "Engine", ROOT / "Source" / "Game"):
        if not source_root.exists():
            continue
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            if "TaskScheduler.h" in text and path != ROOT / "Source" / "Engine" / "Jobs" / "JobSystem.cpp":
                fail(f"enkiTS leaked outside Swim::Jobs: {path.relative_to(ROOT)}", failures)
            if "RenderThreadPool" in text:
                fail(f"renderer-only CPU worker pool returned: {path.relative_to(ROOT)}", failures)

    for fragment in (
        "Swim::Jobs::JobSystem& jobs",
        "jobs.ParallelFor(",
    ):
        if fragment not in parallel_utils:
            fail(f"renderer ParallelFor adapter is not backed by Swim::Jobs: {fragment}", failures)

    engine_header = (ROOT / "Source" / "Engine" / "SwimEngine.h").read_text(encoding="utf-8", errors="ignore")
    engine_source = (ROOT / "Source" / "Engine" / "SwimEngine.cpp").read_text(encoding="utf-8", errors="ignore")
    for fragment in (
        "std::unique_ptr<Swim::Jobs::JobSystem> jobSystem",
        "jobSystem->Initialize(jobDesc)",
        "services.Jobs = jobSystem.get()",
        "sceneSystem->SetServices(std::move(services))",
        "jobSystem->Shutdown(Swim::Jobs::JobShutdownMode::Drain)",
    ):
        target = engine_header if "unique_ptr" in fragment else engine_source
        if fragment not in target:
            fail(f"engine-owned JobSystem lifecycle/injection is missing: {fragment}", failures)

    if ".ShutdownNow(" in job_source:
        fail("JobSystem cancellation regressed to enkiTS ShutdownNow, which invalidates queued task state", failures)
    if "Scheduler.WaitforAll();" in job_source:
        fail("JobSystem WaitForAll regressed to scheduler-wide WaitforAll while permanent blocking lanes are alive", failures)


def check_phase3_io_architecture(failures: list[str]) -> None:
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    io_header_path = ROOT / "Source" / "Engine" / "IO" / "AsyncIoService.h"
    io_source_path = ROOT / "Source" / "Engine" / "IO" / "AsyncIoService.cpp"
    io_test_path = ROOT / "Source" / "Tests" / "Suites" / "IO" / "AsyncIoServiceTests.cpp"
    if not io_header_path.is_file() or not io_source_path.is_file() or not io_test_path.is_file():
        fail("Phase 3 Async IO service/tests are missing", failures)
        return

    io_header = io_header_path.read_text(encoding="utf-8", errors="ignore")
    io_source = io_source_path.read_text(encoding="utf-8", errors="ignore")
    engine_header = (ROOT / "Source" / "Engine" / "SwimEngine.h").read_text(encoding="utf-8", errors="ignore")
    engine_source = (ROOT / "Source" / "Engine" / "SwimEngine.cpp").read_text(encoding="utf-8", errors="ignore")
    # Phase 22: one SceneServices struct (Scene.h) replaced the legacy renderer runtime services.
    renderer_services = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.h").read_text(encoding="utf-8", errors="ignore")
    scene_header = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.h").read_text(
        encoding="utf-8", errors="ignore"
    )
    scene_system_header = renderer_services + (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "SceneSystem.h").read_text(
        encoding="utf-8", errors="ignore"
    )

    for fragment in (
        "file(GLOB_RECURSE SWIM_IO_SOURCES",
        "${SWIM_SDL3_TARGET}",
        "Swim::EnkiTS",
    ):
        if fragment not in cmake_text:
            fail(f"Phase 3 Async IO CMake boundary is missing: {fragment}", failures)

    for fragment in (
        "class AsyncIoService",
        "class ReadRequest",
        "ReadFileAsync",
        "ReadRangeAsync",
        "ReadRangesAsync",
        "MaxCoalesceGapBytes",
        "PumpCompletions",
        "ReadFileBlocking",
        "ReadRangeBlocking",
        "MapFileReadOnlyBlocking",
        "IoShutdownMode",
        "RequestCancel",
    ):
        if fragment not in io_header:
            fail(f"Phase 3 Async IO public contract is missing: {fragment}", failures)

    for fragment in (
        "ScheduleBlocking",
        "OwnerThread",
        "QueueCompletion",
        "ReadRangesBlockingImpl",
        "maxCoalesceGapBytes",
        "IoStatus::Cancelled",
    ):
        if fragment not in io_source:
            fail(f"Phase 3 Async IO implementation contract is missing: {fragment}", failures)

    if "std::async" in io_source or "detach()" in io_source:
        fail("Async IO created an unmanaged thread/future path instead of using Swim::Jobs blocking lanes", failures)

    for fragment in (
        "std::unique_ptr<Swim::IO::AsyncIoService> ioSystem",
        "ioSystem->Initialize(platformSystem->GetFileSystem(), *jobSystem)",
        "services.IO = ioSystem.get()",
        "sceneSystem->SetServices(std::move(services))",
        "ioSystem->PumpCompletions()",
        "ioSystem->Shutdown(Swim::IO::IoShutdownMode::Drain)",
    ):
        target = engine_header if "unique_ptr" in fragment else engine_source
        if fragment not in target:
            fail(f"engine-owned Async IO lifecycle/injection is missing: {fragment}", failures)

    if "Swim::IO::AsyncIoService* IO" not in renderer_services:
        fail("SceneServices do not expose the engine-owned Async IO service", failures)
    if "Swim::IO::AsyncIoService* IO" not in scene_system_header:
        fail("SceneSystem services do not expose the engine-owned Async IO service", failures)
    if "GetIoSystem()" not in scene_header:
        fail("Scene runtime service view does not expose Async IO without global discovery", failures)

    io_shutdown_index = engine_source.find("ioSystem->Shutdown(Swim::IO::IoShutdownMode::Drain)")
    scene_exit_index = engine_source.find('record("SceneSystem", sceneSystem->Exit())')
    jobs_shutdown_index = engine_source.find("jobSystem->Shutdown(Swim::Jobs::JobShutdownMode::Drain)")
    if io_shutdown_index == -1 or scene_exit_index == -1 or jobs_shutdown_index == -1:
        fail("cannot validate Async IO shutdown ordering", failures)
    elif not (io_shutdown_index < scene_exit_index < jobs_shutdown_index):
        fail("Async IO must drain while consumers are alive and before JobSystem shutdown", failures)



def check_phase3_memory_architecture(failures: list[str]) -> None:
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    dependency_path = ROOT / "cmake" / "MemoryDependencies.cmake"
    arena_header_path = ROOT / "Source" / "Engine" / "Memory" / "LinearArena.h"
    arena_source_path = ROOT / "Source" / "Engine" / "Memory" / "LinearArena.cpp"
    frame_header_path = ROOT / "Source" / "Engine" / "Memory" / "FrameArena.h"
    scratch_header_path = ROOT / "Source" / "Engine" / "Memory" / "ScratchArena.h"
    scratch_source_path = ROOT / "Source" / "Engine" / "Memory" / "ScratchArena.cpp"
    test_path = ROOT / "Source" / "Tests" / "Suites" / "Memory" / "MemoryArenaTests.cpp"

    required_paths = (dependency_path, arena_header_path, arena_source_path, frame_header_path, scratch_header_path, scratch_source_path, test_path)
    if not all(path.is_file() for path in required_paths):
        fail("Phase 3 transient-memory module/tests are incomplete", failures)
        return

    dependency_text = dependency_path.read_text(encoding="utf-8", errors="ignore")
    arena_header = arena_header_path.read_text(encoding="utf-8", errors="ignore")
    arena_source = arena_source_path.read_text(encoding="utf-8", errors="ignore")
    frame_header = frame_header_path.read_text(encoding="utf-8", errors="ignore")
    scratch_header = scratch_header_path.read_text(encoding="utf-8", errors="ignore")
    scratch_source = scratch_source_path.read_text(encoding="utf-8", errors="ignore")
    job_source = (ROOT / "Source" / "Engine" / "Jobs" / "JobSystem.cpp").read_text(encoding="utf-8", errors="ignore")
    engine_header = (ROOT / "Source" / "Engine" / "SwimEngine.h").read_text(encoding="utf-8", errors="ignore")
    engine_source = (ROOT / "Source" / "Engine" / "SwimEngine.cpp").read_text(encoding="utf-8", errors="ignore")
    renderer_services = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.h").read_text(encoding="utf-8", errors="ignore")
    scene_system_header = renderer_services + (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "SceneSystem.h").read_text(encoding="utf-8", errors="ignore")
    scene_header = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.h").read_text(encoding="utf-8", errors="ignore")

    for fragment in (
        "GITHUB_REPOSITORY microsoft/mimalloc",
        "GIT_TAG v3.4.5",
        "MI_OVERRIDE ON",
        "MI_BUILD_SHARED OFF",
        "MI_BUILD_STATIC ON",
        "MI_BUILD_OBJECT ON",
        "MI_BUILD_TESTS OFF",
        "MI_WIN_REDIRECT OFF",
        "MI_OPT_ARCH OFF",
        "add_library(Swim::Mimalloc ALIAS SwimMimalloc)",
    ):
        if fragment not in dependency_text:
            fail(f"mimalloc dependency contract is missing: {fragment}", failures)

    for fragment in (
        "file(GLOB_RECURSE SWIM_MEMORY_SOURCES",
        "Swim::Mimalloc",
        "SWIM_MEMORY_USE_MIMALLOC",
        "target_sources(SwimEngine PRIVATE $<TARGET_OBJECTS:mimalloc-obj>)",
    ):
        if fragment not in cmake_text:
            fail(f"Phase 3 memory CMake boundary is missing: {fragment}", failures)

    for fragment in ("class LinearArena", "ArenaMarker", "ArenaStats", "Allocate", "Rewind", "Reset"):
        if fragment not in arena_header:
            fail(f"LinearArena contract is missing: {fragment}", failures)

    for fragment in ("mi_malloc_aligned", "mi_free", "SWIM_MEMORY_USE_MIMALLOC"):
        if fragment not in arena_source:
            fail(f"LinearArena does not use the mimalloc backing path: {fragment}", failures)

    if "class FrameArena" not in frame_header or "BeginFrame" not in frame_header:
        fail("per-frame arena contract is missing", failures)
    if "class ScratchScope" not in scratch_header or "GetThreadScratchArena" not in scratch_header:
        fail("thread scratch contract is missing", failures)
    if "thread_local LinearArena" not in scratch_source:
        fail("thread scratch arena is not thread-local", failures)
    if "Swim::Memory::ScratchScope scratch" not in job_source:
        fail("JobSystem does not establish automatic per-job scratch scopes", failures)

    for fragment in (
        "Swim::Memory::FrameArena frameArena",
        "GetFrameArena()",
    ):
        if fragment not in engine_header:
            fail(f"engine-owned frame arena is missing: {fragment}", failures)
    for fragment in (
        "frameArena.BeginFrame",
        "services.FrameMemory = &frameArena",
        "sceneSystem->SetServices(std::move(services))",
    ):
        if fragment not in engine_source:
            fail(f"frame arena lifecycle/injection is missing: {fragment}", failures)

    if "Swim::Memory::FrameArena* FrameMemory" not in renderer_services:
        fail("SceneServices do not expose frame memory", failures)
    if "Swim::Memory::FrameArena* FrameMemory" not in scene_system_header:
        fail("SceneSystem services do not expose frame memory", failures)
    if "GetFrameArena()" not in scene_header:
        fail("Scene runtime service view does not expose frame memory", failures)

    broad_filters = (
        'EXCLUDE REGEX "/IO/"',
        'EXCLUDE REGEX "/Jobs/"',
        'EXCLUDE REGEX "/Input/"',
        'EXCLUDE REGEX "/Platform/"',
        'EXCLUDE REGEX "/Memory/"',
        'EXCLUDE REGEX "/Assets/"',
    )
    for fragment in broad_filters:
        if fragment in cmake_text:
            fail(f"legacy source exclusion is too broad and can remove unrelated implementation files: {fragment}", failures)



def check_phase4_asset_architecture(failures: list[str]) -> None:
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    asset_root = ROOT / "Source" / "Engine" / "Assets"
    asset_compiler_root = ROOT / "Source" / "Tools" / "AssetCompiler"
    asset_compiler_dependencies = ROOT / "cmake" / "AssetCompilerDependencies.cmake"
    required_paths = (
        asset_root / "AssetId.h",
        asset_root / "AssetHandle.h",
        asset_root / "AssetState.h",
        asset_root / "ContentHash.h",
        asset_root / "ContentHash.cpp",
        asset_root / "AssetDatabase.h",
        asset_root / "AssetDatabase.cpp",
        asset_root / "AssetSystem.h",
        asset_root / "AssetSystem.cpp",
        asset_root / "MeshAsset.h",
        asset_root / "TextureAsset.h",
        asset_root / "MaterialAsset.h",
        asset_root / "ModelAsset.h",
        asset_root / "Ktx2Container.h",
        asset_root / "Ktx2Container.cpp",
        asset_root / "SassetFormat.h",
        asset_root / "SassetFormat.cpp",
        ROOT / "Source" / "Tests" / "Suites" / "Assets" / "AssetSystemTests.cpp",
        ROOT / "Source" / "Tests" / "Suites" / "Assets" / "Ktx2ContainerTests.cpp",
        ROOT / "Source" / "Tests" / "HeaderBoundary" / "AssetsPublicHeaders.cpp",
        ROOT / "Source" / "Examples" / "HeadlessCoreAssets.cpp",
        asset_compiler_root / "IntermediateModel.h",
        asset_compiler_root / "GltfImporter.h",
        asset_compiler_root / "GltfImporter.cpp",
        asset_compiler_root / "MeshOptimizer.h",
        asset_compiler_root / "MeshOptimizer.cpp",
        asset_compiler_root / "Ktx2TextureCompiler.h",
        asset_compiler_root / "Ktx2TextureCompiler.cpp",
        asset_compiler_root / "SourceImageTextureCompiler.h",
        asset_compiler_root / "SourceImageTextureCompiler.cpp",
        asset_compiler_root / "SassetWriter.h",
        asset_compiler_root / "SassetWriter.cpp",
        asset_compiler_root / "StaticModelCompiler.h",
        asset_compiler_root / "StaticModelCompiler.cpp",
        asset_compiler_root / "DevelopmentAssetPipeline.h",
        asset_compiler_root / "DevelopmentAssetPipeline.cpp",
        ROOT / "Source" / "Tools" / "AssetCompiler" / "Cli" / "AssetCookerMain.cpp",
        ROOT / "Source" / "Tests" / "HeaderBoundary" / "AssetCompilerPublicHeaders.cpp",
        ROOT / "Source" / "Tests" / "Suites" / "AssetCompiler" / "GltfImporterTests.cpp",
        ROOT / "Source" / "Tests" / "Suites" / "AssetCompiler" / "MeshOptimizerTests.cpp",
        ROOT / "Source" / "Tests" / "Suites" / "AssetCompiler" / "Ktx2TextureCompilerTests.cpp",
        ROOT / "Source" / "Tests" / "Suites" / "AssetCompiler" / "SourceImageTextureCompilerTests.cpp",
        ROOT / "Source" / "Tests" / "Suites" / "AssetCompiler" / "SassetFormatTests.cpp",
        ROOT / "Source" / "Tests" / "Suites" / "AssetCompiler" / "StaticModelCompilerTests.cpp",
        ROOT / "Source" / "Tests" / "Suites" / "AssetCompiler" / "DevelopmentAssetPipelineTests.cpp",
        asset_compiler_dependencies,
    )
    if not all(path.is_file() for path in required_paths):
        fail("Phase 4 asset identity/runtime CPU asset foundation is incomplete", failures)
        return

    asset_system_header = (asset_root / "AssetSystem.h").read_text(encoding="utf-8", errors="ignore")
    asset_database_header = (asset_root / "AssetDatabase.h").read_text(encoding="utf-8", errors="ignore")
    content_hash_header = (asset_root / "ContentHash.h").read_text(encoding="utf-8", errors="ignore")
    mesh_header = (asset_root / "MeshAsset.h").read_text(encoding="utf-8", errors="ignore")
    material_header = (asset_root / "MaterialAsset.h").read_text(encoding="utf-8", errors="ignore")
    model_header = (asset_root / "ModelAsset.h").read_text(encoding="utf-8", errors="ignore")
    ktx_header = (asset_root / "Ktx2Container.h").read_text(encoding="utf-8", errors="ignore")
    ktx_source = (asset_root / "Ktx2Container.cpp").read_text(encoding="utf-8", errors="ignore")
    sasset_header = (asset_root / "SassetFormat.h").read_text(encoding="utf-8", errors="ignore")
    sasset_source = (asset_root / "SassetFormat.cpp").read_text(encoding="utf-8", errors="ignore")
    engine_header = (ROOT / "Source" / "Engine" / "SwimEngine.h").read_text(encoding="utf-8", errors="ignore")
    engine_source = (ROOT / "Source" / "Engine" / "SwimEngine.cpp").read_text(encoding="utf-8", errors="ignore")
    renderer_services = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.h").read_text(encoding="utf-8", errors="ignore")
    scene_header = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.h").read_text(encoding="utf-8", errors="ignore")
    scene_system_header = renderer_services + (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "SceneSystem.h").read_text(encoding="utf-8", errors="ignore")

    for fragment in (
        "file(GLOB_RECURSE SWIM_ASSET_SOURCES",
        "target_sources(SwimEngine PRIVATE ${SWIM_ASSET_SOURCES})",
        "add_executable(SwimHeadlessCoreAssets EXCLUDE_FROM_ALL",
        'list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Assets/")',
        "add_library(SwimAssetCompiler STATIC ${SWIM_ASSET_COMPILER_SOURCES} ${SWIM_ASSET_SOURCES})",
    ):
        if fragment not in cmake_text:
            fail(f"Phase 4 asset CMake boundary is missing: {fragment}", failures)

    for fragment in (
        "struct AssetId",
        "template<typename T>",
        "class AssetHandle",
        "AssetLoadState",
        "AssetErrorCode",
        "Declare(std::string_view logicalPath)",
        "FindByContentHash",
        "ComputeDependencyRevisionHash",
        "GetDependents",
        "SetDependencies",
        "Publish(",
        "Forget(",
        "Resolve(",
    ):
        if fragment not in asset_system_header and fragment not in (asset_root / "AssetId.h").read_text(encoding="utf-8", errors="ignore") and fragment not in (asset_root / "AssetHandle.h").read_text(encoding="utf-8", errors="ignore") and fragment not in (asset_root / "AssetState.h").read_text(encoding="utf-8", errors="ignore"):
            fail(f"Phase 4 asset identity/load-state contract is missing: {fragment}", failures)

    for fragment in ("GetOrCreate", "Bind", "Rebind", "FindId", "FindPath", "Snapshot"):
        if fragment not in asset_database_header:
            fail(f"Asset path database contract is missing: {fragment}", failures)

    for fragment in ("struct ContentHash", "ComputeContentHash", "ToHex", "FromHex"):
        if fragment not in content_hash_header:
            fail(f"content hash contract is missing: {fragment}", failures)

    for fragment in ("struct MeshAsset", "VertexStreamDesc", "MeshPrimitive", "MeshLod", "MeshletDesc"):
        if fragment not in mesh_header:
            fail(f"backend-neutral MeshAsset CPU schema is missing: {fragment}", failures)
    for fragment in ("struct MaterialTemplateAsset", "struct MaterialInstanceAsset", "AssetHandle<TextureAsset>"):
        if fragment not in material_header:
            fail(f"backend-neutral material schema is missing: {fragment}", failures)
    for fragment in ("struct ModelAsset", "AssetHandle<MeshAsset>", "AssetHandle<MaterialInstanceAsset>"):
        if fragment not in model_header:
            fail(f"model mesh/material identity separation is missing: {fragment}", failures)

    forbidden_fragments = (
        "<vulkan/",
        "<glad/",
        "VkBuffer",
        "VkImage",
        "GLuint",
        "Engine/Systems/Renderer/Core/Textures/Texture2D.h",
        "MeshBufferData",
        "VulkanRenderer",
        "OpenGLRenderer",
        "tinygltf",
        "fastgltf",
        "entt::",
        "std::shared_ptr",
    )
    for path in asset_root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for fragment in forbidden_fragments:
            if fragment in text:
                fail(f"backend/importer/ownership type leaked into runtime asset module: {path.relative_to(ROOT)}: {fragment}", failures)

    for fragment in (
        "std::unique_ptr<Swim::Assets::AssetSystem> assetSystem",
        "GetAssetSystem()",
    ):
        if fragment not in engine_header:
            fail(f"engine-owned AssetSystem declaration is missing: {fragment}", failures)
    for fragment in (
        "assetSystem->Initialize()",
        "services.Assets = assetSystem.get()",
        "sceneSystem->SetServices(std::move(services))",
        "assetSystem->Shutdown()",
    ):
        if fragment not in engine_source:
            fail(f"engine-owned AssetSystem lifecycle/injection is missing: {fragment}", failures)

    if "Swim::Assets::AssetSystem* Assets" not in renderer_services:
        fail("SceneServices do not expose the engine-owned AssetSystem", failures)
    if "Swim::Assets::AssetSystem* Assets" not in scene_system_header:
        fail("SceneSystem services do not expose the engine-owned AssetSystem", failures)
    if "GetAssetSystem()" not in scene_header:
        fail("Scene runtime service view does not expose AssetSystem without global discovery", failures)

    # Phase 23 retired the legacy mesh/material/texture/font pools, their loose-image
    # (stb) compatibility TUs and the VulkanRenderer draw paths that consumed them. The
    # runtime streams cooked/procedural geometry and textures through the
    # AssetResidencyService (MeshLibrary) and GPU material tables (MaterialLibrary).
    for retired in (
        "Source/Engine/Systems/Renderer/Core/Material/MaterialPool.cpp",
        "Source/Engine/Systems/Renderer/Core/Meshes/MeshPool.cpp",
        "Source/Engine/Systems/Renderer/Core/Textures/TexturePool.cpp",
        "Source/Engine/Systems/Renderer/Core/Font/FontPool.cpp",
        "Source/Engine/Systems/Renderer/Vulkan/VulkanRenderer.cpp",
        "Source/Engine/Systems/Renderer/OpenGL/OpenGLRenderer.cpp",
        "Source/Engine/Components/Material.h",
        "Source/Engine/ThirdParty/StbImageImplementation.cpp",
    ):
        if (ROOT / retired).exists():
            fail(f"retired legacy renderer/resource file is back in the active tree: {retired}", failures)
    runtime_root = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Runtime"
    for name, fragments in (
        ("MeshLibrary.h", ("class MeshLibrary", "AssetResidencyService")),
        ("MaterialLibrary.h", ("class MaterialLibrary", "struct MaterialDesc")),
        ("FrameRenderer.h", ("class FrameRenderer", "struct RenderFrameInput")),
    ):
        path = runtime_root / name
        if not path.is_file():
            fail(f"modern runtime renderer file is missing: {path.relative_to(ROOT)}", failures)
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for fragment in fragments:
            if fragment not in text:
                fail(f"modern runtime renderer contract is missing from {name}: {fragment}", failures)

    for stale_link in ("tinygltf::tinygltf", "Swim::WebP"):
        if stale_link in cmake_text:
            fail(f"legacy runtime target still links source-import dependency: {stale_link}", failures)

    dependency_text = (ROOT / "cmake" / "Dependencies.cmake").read_text(encoding="utf-8", errors="ignore")
    for stale_package in ("NAME draco_source", "NAME webp_source", "NAME tinygltf_source"):
        if stale_package in dependency_text:
            fail(f"obsolete runtime source-import package remains in Dependencies.cmake: {stale_package}", failures)


    for source_root in (ROOT / "Source" / "Game",):
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            for stale_fragment in ("RegisterMaterialData", "GetMaterialData", "std::shared_ptr<Engine::MaterialData>", "LoadAndRegisterCompositeMaterialFromGLB"):
                if stale_fragment in text:
                    fail(f"game code still uses removed material/geometry-coupled compatibility API: {path.relative_to(ROOT)} -> {stale_fragment}", failures)

            # Scene::AddComponent<T> takes an already-constructed T by value.
            # LegacyRenderBinding -> Material is intentionally explicit, so a
            # bare shared_ptr here compiles neither on MSVC nor standard C++.
            for line_number, line in enumerate(text.splitlines(), start=1):
                stripped = line.strip()
                if stripped.startswith("//"):
                    continue
                if (
                    ("AddComponent<Engine::Material>" in stripped or "AddComponent<Material>" in stripped)
                    and "Material(" not in stripped
                ):
                    fail(
                        f"game Material AddComponent call bypasses the explicit Material wrapper: "
                        f"{path.relative_to(ROOT)}:{line_number}",
                        failures,
                    )

    asset_compiler_dependency_text = asset_compiler_dependencies.read_text(encoding="utf-8", errors="ignore")
    intermediate_model_text = (asset_compiler_root / "IntermediateModel.h").read_text(encoding="utf-8", errors="ignore")
    gltf_importer_header_text = (asset_compiler_root / "GltfImporter.h").read_text(encoding="utf-8", errors="ignore")
    gltf_importer_source_text = (asset_compiler_root / "GltfImporter.cpp").read_text(encoding="utf-8", errors="ignore")
    source_image_compiler_header_text = (asset_compiler_root / "SourceImageTextureCompiler.h").read_text(encoding="utf-8", errors="ignore")
    source_image_compiler_source_text = (asset_compiler_root / "SourceImageTextureCompiler.cpp").read_text(encoding="utf-8", errors="ignore")
    static_model_compiler_source_text = (asset_compiler_root / "StaticModelCompiler.cpp").read_text(encoding="utf-8", errors="ignore")

    required_asset_compiler_cmake_fragments = (
        'option(SWIM_BUILD_ASSET_COMPILER "Build offline asset compiler/import modules" ON)',
        "include(cmake/AssetCompilerDependencies.cmake)",
        "add_library(SwimAssetCompiler STATIC",
        "add_library(Swim::AssetCompiler ALIAS SwimAssetCompiler)",
        "PRIVATE Swim::AssetCompilerDependencies",
        "add_executable(SwimAssetCooker ${SWIM_ASSET_COOKER_SOURCES})",
        'list(FILTER SWIM_ASSET_COMPILER_SOURCES EXCLUDE REGEX "/Source/Tools/AssetCompiler/Cli/")',
        'option(SWIM_ENABLE_DEV_ASSET_AUTOCOOK "Scan/cook loose source assets at engine startup (development only)" ON)',
        "SWIM_ENABLE_DEV_ASSET_AUTOCOOK=$<BOOL:${SWIM_DEV_ASSET_AUTOCOOK_ENABLED}>",
        "target_link_libraries(SwimEngine PRIVATE Swim::AssetCompiler)",
    )
    for fragment in required_asset_compiler_cmake_fragments:
        if fragment not in cmake_text:
            fail(f"Phase 4 asset-compiler CMake boundary is missing: {fragment}", failures)

    required_asset_compiler_dependency_fragments = (
        "GITHUB_REPOSITORY simdjson/simdjson",
        "GIT_TAG v3.12.3",
        "TARGET simdjson::simdjson",
        "GITHUB_REPOSITORY spnda/fastgltf",
        "GIT_TAG v0.9.0",
        "TARGET fastgltf::fastgltf",
        "GITHUB_REPOSITORY zeux/meshoptimizer",
        "GIT_TAG v1.1",
        "TARGET meshoptimizer",
        "GITHUB_REPOSITORY nothings/stb",
        "GIT_TAG 2dfbe86",
        "GITHUB_REPOSITORY google/draco",
        "GIT_TAG 1.5.7",
        "DRACO_GLTF_BITSTREAM ON",
        "DRACO_POINT_CLOUD_COMPRESSION OFF",
        "SWIM_ASSET_COMPILER_DRACO_TARGET",
        "add_library(SwimAssetCompilerDraco INTERFACE)",
        "add_library(Swim::AssetCompilerDraco ALIAS SwimAssetCompilerDraco)",
        "target_link_libraries(SwimAssetCompilerDraco INTERFACE ${SWIM_ASSET_COMPILER_DRACO_TARGET})",
        'set(SWIM_ASSET_COMPILER_DRACO_SOURCE_INCLUDE_DIR "${draco_source_SOURCE_DIR}/src")',
        'set(SWIM_ASSET_COMPILER_DRACO_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}")',
        '"${SWIM_ASSET_COMPILER_DRACO_SOURCE_INCLUDE_DIR}/draco/compression/decode.h"',
        '"${SWIM_ASSET_COMPILER_DRACO_GENERATED_INCLUDE_DIR}/draco/draco_features.h"',
        "cmake_policy(SET CMP0148 OLD)",
        "add_library(SwimAssetCompilerDependencies INTERFACE)",
        "add_library(Swim::AssetCompilerDependencies ALIAS SwimAssetCompilerDependencies)",
        "target_link_libraries(SwimAssetCompilerDependencies INTERFACE",
        "fastgltf::fastgltf",
        "meshoptimizer",
        "Swim::AssetCompilerDraco",
        "${SWIM_ASSET_COMPILER_WEBP_TARGET}",
        "GITHUB_REPOSITORY webmproject/libwebp",
        "GIT_TAG v1.5.0",
        "WEBP_BUILD_CWEBP OFF",
        "WEBP_BUILD_DWEBP OFF",
        "MESHOPT_BUILD_GLTFPACK OFF",
        "MESHOPT_INSTALL OFF",
        "function(swim_assert_asset_compiler_dependency_clean dependency_name source_dir)",
        "status --porcelain --untracked-files=all",
    )
    for fragment in required_asset_compiler_dependency_fragments:
        if fragment not in asset_compiler_dependency_text:
            fail(f"asset-compiler dependency contract is missing: {fragment}", failures)

    # The Draco-consuming suites now live in SwimTests, so the adapter contract
    # moved with them: test code must reach Draco through Swim::AssetCompilerDraco
    # rather than the raw package target, whose include-root layout is a quirk the
    # adapter exists to hide.
    tests_cmake_text = read_tests_cmake()
    if "Swim::AssetCompilerDraco" not in tests_cmake_text:
        fail("Draco-consuming test suites no longer link the Swim-owned Draco adapter", failures)
    if "${SWIM_ASSET_COMPILER_DRACO_TARGET}" in tests_cmake_text:
        fail("Draco consumers must use the Swim-owned include/link adapter instead of the raw package target", failures)

    simdjson_target_position = asset_compiler_dependency_text.find("if(NOT TARGET simdjson::simdjson)")
    fastgltf_package_position = asset_compiler_dependency_text.find("NAME swim_fastgltf_source")
    if simdjson_target_position < 0 or fastgltf_package_position < 0 or simdjson_target_position > fastgltf_package_position:
        fail("simdjson must be established before fastgltf so fastgltf cannot mutate its CPM source cache", failures)

    for path, text in (
        (asset_compiler_root / "IntermediateModel.h", intermediate_model_text),
        (asset_compiler_root / "GltfImporter.h", gltf_importer_header_text),
    ):
        for fragment in ("fastgltf", "tinygltf"):
            if fragment in text:
                fail(f"importer library type leaked through Swim-owned asset compiler header: {path.relative_to(ROOT)} -> {fragment}", failures)

    for path in asset_compiler_root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if path.name != "GltfImporter.cpp" and ("#include <fastgltf" in text or "fastgltf::" in text):
            fail(f"fastgltf escaped the GltfImporter.cpp implementation boundary: {path.relative_to(ROOT)}", failures)
        if path.name != "MeshOptimizer.cpp" and ("#include <meshoptimizer" in text or "meshopt_" in text):
            fail(f"meshoptimizer escaped the MeshOptimizer.cpp implementation boundary: {path.relative_to(ROOT)}", failures)
        if path.name != "GltfImporter.cpp" and ("#include <draco/" in text or "draco::" in text):
            fail(f"Draco escaped the GltfImporter.cpp compiler implementation boundary: {path.relative_to(ROOT)}", failures)
        if path.name != "SourceImageTextureCompiler.cpp" and ("#include <webp/" in text or "WebPDecode" in text or "WebPGetInfo" in text):
            fail(f"libwebp escaped the SourceImageTextureCompiler.cpp implementation boundary: {path.relative_to(ROOT)}", failures)
        if path.name != "SourceImageTextureCompiler.cpp" and "STB_IMAGE_IMPLEMENTATION" in text:
            fail(f"compiler-side stb_image implementation escaped its source-image compiler TU: {path.relative_to(ROOT)}", failures)

    for fragment in (
        "struct IntermediateModel",
        "struct SourcePrimitive",
        "struct SourceMaterial",
        "struct SourceImage",
        "struct SourceSampler",
        "struct SourceTexture",
        "struct SourceNode",
    ):
        if fragment not in intermediate_model_text:
            fail(f"Swim-owned glTF intermediate model contract is missing: {fragment}", failures)

    for fragment in (
        "fastgltf::Parser",
        "fastgltf::MappedGltfFile::FromPath",
        "fastgltf::Options::LoadExternalBuffers",
        "fastgltf::Options::LoadExternalImages",
        "fastgltf::Options::GenerateMeshIndices",
        "ImportPrimitive",
        "KHR_mesh_quantization",
        "KHR_texture_basisu",
        "KHR_texture_transform",
        "KHR_draco_mesh_compression",
        "EXT_texture_webp",
        "MSFT_texture_dds",
        "KHR_materials_unlit",
    ):
        if fragment not in gltf_importer_source_text:
            fail(f"fastgltf source importer implementation is missing: {fragment}", failures)


    gltf_importer_test_text = (ROOT / "Source" / "Tests" / "Suites" / "AssetCompiler" / "GltfImporterTests.cpp").read_text(encoding="utf-8", errors="ignore")
    draco_fixture_text = (ROOT / "Source" / "Tests" / "Fixtures" / "DracoTriangleFixture.h").read_text(encoding="utf-8", errors="ignore")
    development_asset_test_text = (ROOT / "Source" / "Tests" / "Suites" / "AssetCompiler" / "DevelopmentAssetPipelineTests.cpp").read_text(encoding="utf-8", errors="ignore")
    if '"extensionsRequired":["KHR_texture_transform"]' not in gltf_importer_test_text:
        fail("glTF importer regression test no longer requires KHR_texture_transform", failures)
    for fragment in (
        '"extensionsRequired":["KHR_texture_basisu","EXT_texture_webp"]',
        "SourceImageMimeType::Ktx2",
        "SourceImageMimeType::WebP",
        "Textures[0].ImageIndex",
        "Textures[1].ImageIndex",
    ):
        if fragment not in gltf_importer_test_text:
            fail(f"glTF Basis/WebP extension regression coverage is missing: {fragment}", failures)
    for fragment in (
        '"extensionsRequired":["KHR_draco_mesh_compression"]',
        "draco::Encoder",
        "SetAttributeUniqueId",
    ):
        if fragment not in draco_fixture_text:
            fail(f"glTF Draco decode regression fixture is missing: {fragment}", failures)
    for fragment in (
        "ImportDracoPrimitive",
        "draco::Decoder",
        "DecodeMeshFromBuffer",
        "GetAttributeByUniqueId",
        "DecodeDracoAttribute<3>",
    ):
        if fragment not in gltf_importer_source_text:
            fail(f"compiler-side Draco decode implementation is missing: {fragment}", failures)
    for fragment in (
        "WriteDracoTriangleFixture",
        "Stats.SourcesCooked",
        "Stats.SourcesSkippedUnsupported",
        'Find<ModelAsset>("Models/Draco.model")',
    ):
        if fragment not in development_asset_test_text:
            fail(f"development Draco cook regression coverage is missing: {fragment}", failures)

    for fragment in (
        "DetectSourceImageMimeType",
        "CompileSourceImageTexture",
        "SourceImageTextureCompileErrorCode",
    ):
        if fragment not in source_image_compiler_header_text:
            fail(f"source-image texture compiler contract is missing: {fragment}", failures)
    for fragment in (
        "STBI_ONLY_JPEG",
        "STBI_ONLY_PNG",
        "stbi_load_from_memory",
        "WebPGetInfo",
        "WebPDecodeRGBAInto",
        "BuildMipChain",
        "SrgbToLinear",
        "LinearToSrgb",
        "TextureSemantic::Normal",
        "TextureContainerFormat::NativeMipData",
        "TexturePayloadFormat::RGBA8SRgb",
    ):
        if fragment not in source_image_compiler_source_text:
            fail(f"PNG/JPEG/WebP compiler implementation is missing: {fragment}", failures)
    for fragment in (
        "DetectSourceImageMimeType(image.EncodedBytes, image.MimeType)",
        "CompileSourceImageTexture",
        "StaticModelCompileErrorCode::Ktx2CompileFailed",
        "StaticModelCompileErrorCode::InvalidSourceData",
        "texture=ktx2-or-rgba8-mips-v4;basisu=1.60-rgba8",
    ):
        if fragment not in static_model_compiler_source_text:
            fail(f"static-model compiler does not route ordinary source images through cooked TextureAsset output: {fragment}", failures)

    mesh_optimizer_header_text = (asset_compiler_root / "MeshOptimizer.h").read_text(encoding="utf-8", errors="ignore")
    mesh_optimizer_source_text = (asset_compiler_root / "MeshOptimizer.cpp").read_text(encoding="utf-8", errors="ignore")
    if "meshoptimizer" in mesh_optimizer_header_text:
        fail("meshoptimizer implementation types leaked through MeshOptimizer.h", failures)
    for fragment in (
        "struct MeshOptimizationOptions",
        "struct MeshOptimizationStats",
        "struct MeshOptimizationResult",
        "class MeshOptimizer",
    ):
        if fragment not in mesh_optimizer_header_text:
            fail(f"Swim-owned mesh optimization contract is missing: {fragment}", failures)
    for fragment in (
        "meshopt_optimizeVertexCache",
        "meshopt_optimizeOverdraw",
        "meshopt_optimizeVertexFetch",
        "RecalculateBounds",
    ):
        if fragment not in mesh_optimizer_source_text:
            fail(f"meshoptimizer offline pass is missing: {fragment}", failures)
    cache_position = mesh_optimizer_source_text.find("meshopt_optimizeVertexCache")
    overdraw_position = mesh_optimizer_source_text.find("meshopt_optimizeOverdraw")
    fetch_position = mesh_optimizer_source_text.find("meshopt_optimizeVertexFetch")
    if not (0 <= cache_position < overdraw_position < fetch_position):
        fail("meshoptimizer passes must run vertex cache -> overdraw -> vertex fetch", failures)

    for fragment in (
        "struct Ktx2Metadata",
        "TexturePayloadFormat PayloadFormat",
        "TextureSupercompression Supercompression",
        "std::vector<TextureMipDesc> Mips",
        "ParseKtx2Metadata",
    ):
        if fragment not in ktx_header:
            fail(f"KTX2 runtime metadata contract is missing: {fragment}", failures)
    for fragment in (
        "VK_FORMAT_BC7_UNORM_BLOCK",
        "VK_FORMAT_ASTC_4x4_UNORM_BLOCK",
        "VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK",
        "TextureSupercompression::BasisLz",
        "TextureSupercompression::Zstandard",
    ):
        if fragment not in ktx_source:
            fail(f"KTX2 backend-neutral format/supercompression mapping is missing: {fragment}", failures)

    for fragment in (
        "SassetSchemaVersion = 1",
        "SassetHeaderSize = 160",
        "enum class SassetAssetType",
        "struct SassetMetadata",
        "CompilerProfileHash",
        "SourceDependencies",
        "ParseSasset",
        "LoadSasset",
    ):
        if fragment not in sasset_header:
            fail(f".sasset v1 runtime contract is missing: {fragment}", failures)
    for fragment in (
        "SassetMagic",
        "HashMismatch",
        "GetSassetChunkBytes",
        "PublishDecoded",
        "assets.Publish",
    ):
        if fragment not in sasset_source:
            fail(f".sasset v1 runtime validation/load implementation is missing: {fragment}", failures)

    static_model_source = (asset_compiler_root / "StaticModelCompiler.cpp").read_text(encoding="utf-8", errors="ignore")
    development_pipeline_source = (asset_compiler_root / "DevelopmentAssetPipeline.cpp").read_text(encoding="utf-8", errors="ignore")
    for fragment in (
        "GetStaticModelCompilerProfileHash",
        "SerializeAssetPayload",
        "SassetAssetType::Mesh",
        "SassetAssetType::MaterialInstance",
        "SassetAssetType::Model",
    ):
        if fragment not in static_model_source:
            fail(f"static-model .sasset compiler contract is missing: {fragment}", failures)

    for fragment in (
        "GltfImporter importer",
        "GltfImportErrorCode::UnsupportedFeature",
        "SourcesSkippedUnsupported",
        "MeshOptimizer optimizer",
        "StaticModelCompiler compiler",
        "BuildSourceDependencies",
        "InspectCooked",
        "ValidateCookedGraph",
        "PublishCookedAssets",
        "LoadSassetGraph",
        'const std::filesystem::path cookedRoot = assetRoot / "Cooked"',
    ):
        if fragment not in development_pipeline_source:
            fail(f"development source->.sasset bootstrap is missing: {fragment}", failures)
    importer_position = development_pipeline_source.find("importer.Import(source)")
    optimizer_position = development_pipeline_source.find("optimizer.Optimize(optimizedModel)")
    compiler_position = development_pipeline_source.find("compiler.Compile(optimizedModel")
    publish_position = development_pipeline_source.find("PublishCookedAssets(", compiler_position)
    load_position = development_pipeline_source.find("LoadSassetGraph(", publish_position)
    if not (0 <= importer_position < optimizer_position < compiler_position < publish_position < load_position):
        fail("development asset bootstrap must run fastgltf import -> mesh optimization -> .sasset compile -> publish -> runtime load", failures)

    if "RunDevelopmentAssetBootstrap(" not in engine_source:
        fail("engine startup does not run the development asset bootstrap when enabled", failures)
    if "platformSystem->GetFileSystem().GetAssetRoot()" not in engine_source:
        fail("development asset bootstrap is not rooted through the platform filesystem asset root", failures)



def check_phase5_scene_architecture(failures: list[str]) -> None:
    scene_root = ROOT / "Source" / "Engine" / "Systems" / "Scene"
    scene_system_header = (scene_root / "SceneSystem.h").read_text(encoding="utf-8", errors="ignore")
    scene_system_source = (scene_root / "SceneSystem.cpp").read_text(encoding="utf-8", errors="ignore")
    scene_catalog_header = (scene_root / "SceneCatalog.h").read_text(encoding="utf-8", errors="ignore")
    scene_id_header = (scene_root / "SceneId.h").read_text(encoding="utf-8", errors="ignore")
    scene_header = (scene_root / "Scene.h").read_text(encoding="utf-8", errors="ignore")
    scene_source = (scene_root / "Scene.cpp").read_text(encoding="utf-8", errors="ignore")
    behavior_header = (ROOT / "Source" / "Engine" / "Systems" / "Entity" / "Behavior.h").read_text(encoding="utf-8", errors="ignore")
    behavior_source = (ROOT / "Source" / "Engine" / "Systems" / "Entity" / "Behavior.cpp").read_text(encoding="utf-8", errors="ignore")
    main_source = (ROOT / "Source" / "main.cpp").read_text(encoding="utf-8", errors="ignore")
    engine_header = (ROOT / "Source" / "Engine" / "SwimEngine.h").read_text(encoding="utf-8", errors="ignore")
    engine_source = (ROOT / "Source" / "Engine" / "SwimEngine.cpp").read_text(encoding="utf-8", errors="ignore")
    sandbox_header = (ROOT / "Source" / "Game" / "Scenes" / "Sandbox.h").read_text(encoding="utf-8", errors="ignore")
    sandbox_source = (ROOT / "Source" / "Game" / "Scenes" / "Sandbox.cpp").read_text(encoding="utf-8", errors="ignore")
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")

    for fragment in (
        "class SceneCatalog",
        "using Factory = std::function<std::shared_ptr<Scene>(const std::string&)>",
        "void Register(std::string name, Factory factory)",
        "bool Contains(std::string_view name) const",
        "const std::vector<Descriptor>& GetDescriptors() const",
    ):
        if fragment not in scene_catalog_header:
            fail(f"Phase 5 explicit scene catalog contract is missing: {fragment}", failures)

    for forbidden in (
        "static std::vector",
        "inline SceneRegistrar",
        "REGISTER_SCENE",
        "DEFINE_SCENE",
        "Preregister(",
    ):
        if forbidden in scene_system_header or forbidden in scene_system_source:
            fail(f"Phase 5 scene registration regained mutable/static preregistration: {forbidden}", failures)

    for fragment in (
        "SceneCatalog sceneCatalog",
        "RegisterSceneType(const std::string& name)",
        "sceneCatalog.Register",
        "sceneCatalog.GetDescriptors()",
        "SetStartupScene(std::string name)",
        "SceneId GetActiveSceneId() const",
        "SceneId FindSceneId(std::string_view name) const",
        "LoadedScene",
        "SceneId Id",
    ):
        if fragment not in scene_system_header and fragment not in scene_system_source:
            fail(f"Phase 5 scene ownership/identity seam is missing: {fragment}", failures)

    for fragment in (
        "class SceneId",
        "bool IsValid() const",
        "std::uint64_t GetValue() const",
    ):
        if fragment not in scene_id_header:
            fail(f"Phase 5 runtime SceneId contract is missing: {fragment}", failures)

    game_source = (ROOT / "Source" / "Game" / "Game.cpp").read_text(encoding="utf-8", errors="ignore")
    if "Game::Register(*engine.GetSceneSystem())" not in main_source:
        fail("application-owned scene registration is missing from main.cpp: Game::Register(*engine.GetSceneSystem())", failures)
    for fragment in ('scenes.RegisterSceneType<Sandbox>("Sandbox")', 'scenes.SetStartupScene("Sandbox")'):
        if fragment not in game_source:
            fail(f"application-owned scene registration/startup selection is missing from Game.cpp: {fragment}", failures)

    # SceneSystem exists from construction so scenes can be registered before Start.
    scene_system_construction = "sceneSystem = std::make_unique<SceneSystem>();"
    constructor_begin = engine_source.find("SwimEngine::SwimEngine(")
    constructor_end = engine_source.find("SwimEngine::~SwimEngine()", constructor_begin)
    if engine_source.count(scene_system_construction) != 1:
        fail("SceneSystem must have exactly one SwimEngine-owned construction site", failures)
    elif not (0 <= constructor_begin < engine_source.find(scene_system_construction) < constructor_end):
        fail("SceneSystem must be constructed in the SwimEngine constructor so pre-Start scene registration is valid", failures)
    if "Engine/Systems/Scene/SceneSystem.h" not in engine_source:
        fail("SwimEngine.cpp must explicitly include SceneSystem.h for its owned SceneSystem construction", failures)

    if "DEFINE_SCENE" in sandbox_header or "REGISTER_SCENE" in sandbox_header:
        fail("Sandbox scene regained static scene-registration macros", failures)
    if "SetScene(" in sandbox_source:
        fail("Sandbox regained self-selection as the active scene", failures)

    check_suite_is_compiled("Scene/Headless", "SceneCatalogTests.cpp", failures)
    check_suite_is_compiled("Engine", "SceneRuntimeTests.cpp", failures)
    check_suite_is_compiled("Game", "SandboxRuntimeTests.cpp", failures)

    for forbidden in (
        "VulkanRenderer",
        "OpenGLRenderer",
        "FrameRenderer",
        "Renderer* renderer",
        "GetRenderer()",
        "CubeMapController",
        "MeshPool",
        "MaterialPool",
    ):
        if forbidden in scene_header or forbidden in scene_source:
            fail(f"Scene regained a renderer/backend dependency: {forbidden}", failures)
    if "Renderer* renderer" in behavior_header or "scene->GetRenderer()" in behavior_source:
        fail("Behavior base regained cached renderer ownership/discovery", failures)

    # One SceneServices struct: the headless core (required) plus optional presentation
    # and tool services. Scenes run headless without input, camera or rendering.
    for fragment in (
        "struct SceneServices",
        "bool HasCore() const",
        "const EngineStateMachine* State",
        "const SimulationFrame* Time",
        "TagRegistry* Tags",
        "Swim::Input::InputSystem* Input = nullptr;",
        "CameraSystem* Camera = nullptr;",
        "RenderServices* Render = nullptr;",
        "Swim::Commands::CommandRegistry* Commands = nullptr;",
        "std::function<bool(std::string_view)> DispatchCommand;",
        "EngineState GetExecutionState() const;",
    ):
        if fragment not in scene_header:
            fail(f"SceneServices/runtime contract is missing from Scene.h: {fragment}", failures)
    core_block = scene_header.split("struct SceneServices", 1)[-1].split("// Presentation (optional).", 1)[0]
    for forbidden in ("Swim::Input::InputSystem*", "CameraSystem*", "RenderServices*", "Swim::Commands::CommandRegistry*"):
        if forbidden in core_block:
            fail(f"headless Scene core services regained a presentation/tool dependency: {forbidden}", failures)

    for fragment in (
        "void OnEngineStateChanged(EngineState previous, EngineState current);",
        "void RequestScene(std::string name)",
        "void RequestReload()",
        "void BeginFrame();",
    ):
        if fragment not in scene_system_header:
            fail(f"SceneSystem state/reload contract is missing: {fragment}", failures)

    physics_header = (ROOT / "Source" / "Engine" / "Systems" / "Physics" / "PhysicsSystem.h").read_text(encoding="utf-8", errors="ignore")
    physics_source = (ROOT / "Source" / "Engine" / "Systems" / "Physics" / "PhysicsSystem.cpp").read_text(encoding="utf-8", errors="ignore")
    renderer_root = ROOT / "Source" / "Engine" / "Systems" / "Renderer"
    for forbidden in ("SceneSystem*", "GetActiveScene()"):
        if forbidden in physics_header or forbidden in physics_source:
            fail(f"PhysicsSystem regained low-level active-scene discovery: {forbidden}", failures)

    for path in renderer_root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if path.name == "Renderer.h":
            text = text.replace("SceneSystem::GetActiveScene()", "")
        if "GetActiveScene()" in text or "SceneSystem*" in text:
            fail(f"renderer regained SceneSystem/active-scene discovery: {path.relative_to(ROOT)}", failures)

    engine_source = (ROOT / "Source" / "Engine" / "SwimEngine.cpp").read_text(encoding="utf-8", errors="ignore")
    for fragment in (
        "scene->UpdatePhysics(*physicsSystem,",
        "scene->FixedUpdatePhysics(*physicsSystem,",
        "renderBridge->Update(",
    ):
        if fragment not in engine_source:
            fail(f"application-level explicit scene traversal input is missing: {fragment}", failures)

    for forbidden in (
        "SetSceneSystem(this)",
        "GetSceneSystem() const",
        "SceneSystem* sceneSystem",
    ):
        if forbidden in scene_header or forbidden in scene_source or forbidden in behavior_header or forbidden in behavior_source:
            fail(f"Scene/Behavior regained SceneSystem ownership/discovery: {forbidden}", failures)

    for fragment in (
        "DispatchCommand",
        "DispatchCommand(std::string_view command) const",
    ):
        if fragment not in scene_header and fragment not in scene_system_source:
            fail(f"isolated Scene command callback seam is missing: {fragment}", failures)


    transform_header = (ROOT / "Source" / "Engine" / "Components" / "Transform.h").read_text(encoding="utf-8", errors="ignore")
    transform_source = (ROOT / "Source" / "Engine" / "Components" / "Transform.cpp").read_text(encoding="utf-8", errors="ignore")
    transform_system_path = scene_root / "TransformSystem.h"
    transform_system_test = ROOT / "Source" / "Tests" / "Suites" / "Scene" / "Ecs" / "TransformSystemTests.cpp"
    if not transform_system_path.is_file() or not transform_system_test.is_file():
        fail("Phase 5 scene-owned TransformSystem/tests are missing", failures)
    else:
        transform_system_header = transform_system_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "class TransformSystem",
            "void BeginFrame()",
            "bool QueueDirty(entt::entity entity",
            "GetDirtyEntities() const",
            "GetMutationVersion() const",
        ):
            if fragment not in transform_system_header:
                fail(f"Phase 5 TransformSystem contract is missing: {fragment}", failures)

    for forbidden in (
        "inline static bool TransformsDirty",
        "inline static std::vector<entt::entity> DirtyEntities",
        "inline static uint64_t DirtyEpoch",
        "inline static uint64_t GlobalMutationVersion",
        "Transform::GetDirtyEntities()",
        "Transform::GetGlobalMutationVersion()",
        "Transform::BeginFrameDirtyTracking()",
    ):
        if forbidden in transform_header or forbidden in transform_source:
            fail(f"Transform regained process-global dirty tracking: {forbidden}", failures)
        for root in (ROOT / "Source" / "Engine" / "Systems" / "Renderer", scene_root):
            for path in root.rglob("*"):
                if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                    continue
                if forbidden in path.read_text(encoding="utf-8", errors="ignore"):
                    fail(f"Phase 5 consumer regained global Transform dirty tracking: {path.relative_to(ROOT)}: {forbidden}", failures)

    for fragment in (
        "TransformSystem transformSystem",
        "GetTransformSystem()",
        "BeginFrameTransformTracking()",
    ):
        if fragment not in scene_header:
            fail(f"Scene does not own/expose TransformSystem correctly: {fragment}", failures)
    if "sceneSystem->BeginFrame()" not in engine_source:
        fail("engine frame orchestration does not begin per-scene transform tracking", failures)
    check_suite_is_compiled("Scene/Ecs", "TransformSystemTests.cpp", failures)

    # Phase 23: culling is GPU visibility over the GPU scene (Renderer/Visibility); the
    # CPU Frustum/SceneBVH of the legacy renderer are archived under Deprecated/.
    for retired in (
        scene_root / "SubSceneSystems" / "SceneBVH.cpp",
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Camera" / "Frustum.h",
    ):
        if retired.exists():
            fail(f"retired CPU culling path is back in the active tree: {retired.relative_to(ROOT)}", failures)
    visibility_root = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Visibility"
    if not (visibility_root / "RenderViewDesc.h").is_file():
        fail("GPU visibility view contract (Renderer/Visibility/RenderViewDesc.h) is missing", failures)

    # Scene-owned mutation boundary.
    deferred_command_path = scene_root / "DeferredCommandBuffer.h"
    scene_command_header_path = scene_root / "SceneCommandBuffer.h"
    scene_command_source_path = scene_root / "SceneCommandBuffer.cpp"
    deferred_command_test_path = ROOT / "Source" / "Tests" / "Suites" / "Scene" / "Headless" / "DeferredCommandBufferTests.cpp"
    for path in (deferred_command_path, scene_command_header_path, scene_command_source_path, deferred_command_test_path):
        if not path.is_file():
            fail(f"Phase 5 SceneCommandBuffer implementation/test is missing: {path.relative_to(ROOT)}", failures)

    if deferred_command_path.is_file():
        deferred_text = deferred_command_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "class DeferredCommandBuffer",
            "void Enqueue(Func&& command)",
            "std::size_t Flush(Context& context)",
            "commands.swap(pendingCommands)",
            "DeferredCommandBuffer cannot be flushed recursively.",
        ):
            if fragment not in deferred_text:
                fail(f"Deferred scene command contract is missing: {fragment}", failures)

    if scene_command_header_path.is_file():
        scene_command_header = scene_command_header_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "class SceneCommandBuffer",
            "void Defer(Func&& func, Args&&... args)",
            "void Create(Func&& func, Args&&... args)",
            "DeferredCommandBuffer<Scene> commands",
        ):
            if fragment not in scene_command_header:
                fail(f"SceneCommandBuffer contract is missing: {fragment}", failures)

    for fragment in (
        "SceneCommandBuffer& GetCommandBuffer() const",
        "std::unique_ptr<SceneCommandBuffer> sceneCommandBuffer",
    ):
        if fragment not in scene_header:
            fail(f"Scene does not own the command buffer correctly: {fragment}", failures)
    for fragment in ("GetCommandBuffer().Flush()", "GetCommandBuffer().Clear()"):
        if fragment not in scene_source:
            fail(f"Scene lifecycle does not enforce its command-buffer boundary: {fragment}", failures)
    check_suite_is_compiled("Scene/Headless", "DeferredCommandBufferTests.cpp", failures)

    old_entity_factory_paths = (
        ROOT / "Source" / "Engine" / "Systems" / "Entity" / "EntityFactory.h",
        ROOT / "Source" / "Engine" / "Systems" / "Entity" / "EntityFactory.cpp",
    )
    if any(path.exists() for path in old_entity_factory_paths):
        fail("legacy EntityFactory files returned after the SceneCommandBuffer migration", failures)
    for source_root in (ROOT / "Source" / "Game", scene_root):
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            if source_root == scene_root and path.name == "Scene.cpp":
                text = text.replace("registry.create()", "")
            if source_root == scene_root and path.name == "SceneDebugDraw.cpp":
                text = text.replace("immediateModeRegistry.create()", "")
            if "registry.create()" in text or "reg.create()" in text:
                fail(f"scene/game entity creation bypasses Scene ownership in {path.relative_to(ROOT)}", failures)

    # Behavior registration is runtime-owned and deterministic.
    behavior_registry_path = ROOT / "Source" / "Engine" / "Systems" / "Entity" / "BehaviorRegistry.h"
    if not behavior_registry_path.is_file():
        fail("Phase 5 BehaviorRegistry is missing", failures)
    else:
        behavior_registry_text = behavior_registry_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "class BehaviorRegistry",
            "std::vector<Descriptor> descriptors",
            "void Register(std::string name, Factory factory",
            "bool Contains(std::string_view name) const",
            "std::unique_ptr<Behavior> Create",
        ):
            if fragment not in behavior_registry_text:
                fail(f"BehaviorRegistry contract is missing: {fragment}", failures)
    for legacy_name in ("BehaviorFactory.h", "BehaviorRegistrar.h"):
        if (ROOT / "Source" / "Engine" / "Systems" / "Entity" / legacy_name).exists():
            fail(f"legacy global behavior registration file returned: {legacy_name}", failures)
    for forbidden in ("BehaviorFactory::GetInstance", "REGISTER_BEHAVIOR", "DEFINE_BEHAVIOR"):
        for source_root in (ROOT / "Source" / "Engine", ROOT / "Source" / "Game"):
            for path in source_root.rglob("*"):
                if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
                    if forbidden in path.read_text(encoding="utf-8", errors="ignore"):
                        fail(f"behavior registration regained process-global/static state: {forbidden} in {path.relative_to(ROOT)}", failures)
    for fragment in (
        "BehaviorRegistry behaviorRegistry",
        "RegisterBehaviorType(const std::string& name)",
        'RegisterBehaviorType<Spin>("Spin")',
        'RegisterBehaviorType<BallShooter>("BallShooter")',
    ):
        if fragment not in scene_system_header and fragment not in game_source:
            fail(f"explicit behavior registration seam is missing: {fragment}", failures)
    check_suite_is_compiled("Scene/Ecs", "BehaviorRegistryTests.cpp", failures)

    # Playing-mode performance invariants: initialization shares the behavior pass,
    # and high-churn spatial updates batch duplicate BVH ancestor work.
    for fragment in (
        "void ForEachInitializedBehavior",
        "behavior->InitIfNeeded()",
        "behavior->Update(behavior->UsesRealTime() ? realDelta : dt)",
        "ForEachInitializedBehavior(&Behavior::FixedUpdate, tickThisSecond)",
    ):
        if fragment not in scene_header and fragment not in scene_source:
            fail(f"scene behavior update regained a duplicate initialization traversal: {fragment}", failures)
    if "ForEachBehavior(&Behavior::InitIfNeeded);\n\t\tForEachBehavior(&Behavior::Update" in scene_source:
        fail("Scene Update performs two full BehaviorComponents traversals instead of fusing initialization with update", failures)

    scene_bvh_header_path = scene_root / "SubSceneSystems" / "SceneBVH.h"
    scene_bvh_source_path = scene_root / "SubSceneSystems" / "SceneBVH.cpp"
    if scene_bvh_header_path.is_file() and scene_bvh_source_path.is_file():
        scene_bvh_perf_text = scene_bvh_header_path.read_text(encoding="utf-8", errors="ignore") + scene_bvh_source_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "MarkBinaryAncestorsForRefit",
            "RefitMarkedBinaryAncestors",
            "MarkWideAncestorsForRefit",
            "RefitMarkedWideAncestors",
            "binaryRefitMarks",
            "wideRefitMarks",
            "GetTopologyVersion() const",
            "GetWideBoundsVersion() const",
            "topologyVersion",
            "wideBoundsVersion",
            "++topologyVersion",
            "++wideBoundsVersion",
        ):
            if fragment not in scene_bvh_perf_text:
                fail(f"SceneBVH lost batched dirty-ancestor refitting: {fragment}", failures)
        if "RefitBinaryAncestors(leafIndex)" in scene_bvh_source_path.read_text(encoding="utf-8", errors="ignore"):
            fail("SceneBVH regressed to recomputing the full binary parent chain once per dirty leaf", failures)

    vulkan_index_draw_path = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Vulkan" / "VulkanIndexDraw.cpp"
    if vulkan_index_draw_path.is_file():
        vulkan_index_draw_source = vulkan_index_draw_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "sceneBVH->GetTopologyVersion() != gpuWorldBvhTopologyVersion",
            "sceneBVH->GetWideBoundsVersion() != gpuWorldBvhBoundsVersion",
            "gpuWorldBvhTopologyVersion = sceneBVH->GetTopologyVersion()",
            "gpuWorldBvhBoundsVersion = sceneBVH->GetWideBoundsVersion()",
        ):
            if fragment not in vulkan_index_draw_source:
                fail(f"Vulkan GPU BVH lost SceneBVH-owned revision gating: {fragment}", failures)
        if "scene->GetTransformSystem().GetMutationVersion() != gpuWorldBvhBoundsVersion" in vulkan_index_draw_source:
            fail("Vulkan GPU BVH regressed to rebuilding/copying the full BVH snapshot on every Transform mutation", failures)

    # Durable entity IDs remain active, while the old external-editor scene JSON/IPC
    # experiment is intentionally retained only as dormant/reference code.
    serialization_root = ROOT / "Deprecated" / "Engine" / "Systems" / "Scene" / "Serialization"
    for name in ("SerializedEntityId.h", "EntityIdentityMap.h"):
        if not (scene_root / "Identity" / name).is_file():
            fail(f"active scene identity contract is missing: {name}", failures)
    required_serialization_files = (
        "SceneSerializer.h",
        "SceneSerializer.cpp",
        "SceneStorage.h",
        "SceneStorage.cpp",
        "SceneToolingBridge.h",
        "SceneSyncTracker.h",
        "SceneSyncTracker.cpp",
    )
    for file_name in required_serialization_files:
        if not (serialization_root / file_name).is_file():
            fail(f"dormant Phase 5 persistence/tooling reference module is missing: {file_name}", failures)

    legacy_serialized_manager = scene_root / "SubSceneSystems" / "SerializedSceneManager.h"
    if legacy_serialized_manager.exists() or (scene_root / "SubSceneSystems" / "SerializedSceneManager.cpp").exists():
        fail("monolithic SerializedSceneManager returned after serializer/storage/tooling split", failures)

    # Keep the old experiment internally coherent even though runtime code does not use it.
    if (serialization_root / "SceneSerializer.cpp").is_file():
        serializer_source = (serialization_root / "SceneSerializer.cpp").read_text(encoding="utf-8", errors="ignore")
        for forbidden in ("WM_COPYDATA", "GetExecutableDirectory", "std::ofstream", "MaterialPool"):
            if forbidden in serializer_source:
                fail(f"dormant SceneSerializer regained transport/storage/legacy-pool policy: {forbidden}", failures)
        for fragment in (
            'root["schemaVersion"] = SchemaVersion',
            'jsonEntity["id"] = id.Value',
            'jsonEntity["parent"] = parentId.Value',
            'material.ModelAssetId.Value',
            'material.binding->MeshAssetId.Value',
            'material.binding->MaterialAssetId.Value',
            "registry->any_of<DoNotSerialize>(entity)",
        ):
            if fragment not in serializer_source:
                fail(f"dormant stable-ID/AssetId scene serialization contract is missing: {fragment}", failures)

    if (serialization_root / "SceneStorage.cpp").is_file():
        storage_source = (serialization_root / "SceneStorage.cpp").read_text(encoding="utf-8", errors="ignore")
        for forbidden in ("scene sync:", "scene load:", "SendEditorMessage", "CommandSystem"):
            if forbidden in storage_source:
                fail(f"dormant SceneStorage regained editor/tool transport policy: {forbidden}", failures)

    if (serialization_root / "SceneToolingBridge.h").is_file():
        tooling_header = (serialization_root / "SceneToolingBridge.h").read_text(encoding="utf-8", errors="ignore")
        for forbidden in ("SceneSerializer", "nlohmann", "filesystem", "FileSystem"):
            if forbidden in tooling_header:
                fail(f"dormant SceneToolingBridge owns serialization/storage policy: {forbidden}", failures)

    # This is now a hard runtime boundary: legacy external editor IPC and automatic scene
    # JSON persistence stay in source control but are not owned, initialized, or called by
    # the engine/scene/material runtime. Future editor work is in-process engine UI.
    dormant_runtime_forbidden = {
        "Scene.h": (
            "std::unique_ptr<SceneSerializer>",
            "std::unique_ptr<SceneStorage>",
            "std::unique_ptr<SceneToolingBridge>",
            "std::unique_ptr<SceneSyncTracker>",
            "SetEditorMessageSender",
            "SetEditorConnectionProvider",
        ),
        "Scene.cpp": (
            'Serialization/SceneSerializer.h',
            'Serialization/SceneStorage.h',
            'Serialization/SceneSyncTracker.h',
            'Serialization/SceneToolingBridge.h',
            "sceneSyncTracker",
            "sceneToolingBridge",
            "sceneSerializer",
            "sceneStorage",
            "SerializeScene()",
        ),
        "SwimEngine.h": (
            'Engine/Platform/EditorIpcBridge.h',
            "editorIpcBridge",
            "SendEditorMessage(",
            "OnEditorCommand(",
        ),
        "SwimEngine.cpp": (
            "std::make_unique<Swim::Platform::EditorIpcBridge>",
            "editorIpcBridge->",
            "sceneServices.Tools.SendEditorMessage =",
            "sceneServices.Tools.IsEditorConnected =",
        ),
    }
    dormant_sources = {
        "Scene.h": scene_header,
        "Scene.cpp": scene_source,
        "SwimEngine.h": engine_header,
        "SwimEngine.cpp": engine_source,
    }
    for label, forbidden_fragments in dormant_runtime_forbidden.items():
        text = dormant_sources[label]
        for fragment in forbidden_fragments:
            if fragment in text:
                fail(f"legacy external-editor/scene-JSON runtime wiring returned in {label}: {fragment}", failures)

    material_pool_header_path = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Material" / "MaterialPool.h"
    material_pool_source_path = material_pool_header_path.with_suffix(".cpp")
    if material_pool_header_path.is_file() and material_pool_source_path.is_file():
        material_pool_text = material_pool_header_path.read_text(encoding="utf-8", errors="ignore") + material_pool_source_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in ("EditorMessageCallback", "sendEditorMessage"):
            if fragment in material_pool_text:
                fail(f"MaterialPool regained legacy external-editor messaging: {fragment}", failures)

    if "\n\t\tRegisterEditorCommands();" in scene_system_source or "\n\t\tSendBehaviorsToEditor();" in scene_system_source:
        fail("SceneSystem actively registers the dormant external-editor command protocol", failures)

    scene_system_header_text = scene_system_header
    for forbidden in ("SendEditorMessage;", "IsEditorConnected;", "bool SendEditorMessage("):
        if forbidden in scene_system_header_text:
            fail(f"SceneSystem exposes active legacy external-editor tooling seam: {forbidden}", failures)
    if "#if 0" in scene_system_header_text or "DORMANT LEGACY EXTERNAL-EDITOR PROTOCOL" in scene_system_source:
        fail("retired editor command blocks must live outside the active SceneSystem", failures)

    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")

    for fragment in (
        "ownsWindow = !config.Window.ExternalWindow.IsValid();",
        "windowDesc.ExternalParent = {};",
    ):
        if fragment not in engine_source:
            fail(f"SwimEngine no longer hard-disables legacy ExternalParent embedding: {fragment}", failures)
    if "SyncExternalParentSize()" in engine_source:
        fail("SwimEngine reactivated legacy ExternalParent size synchronization", failures)

    for fragment in (
        "EntityIdentityMap entityIdentities",
        "CreateEntityWithSerializedId(SerializedEntityId id)",
        "FindEntityBySerializedId(SerializedEntityId id) const",
    ):
        if fragment not in scene_header:
            fail(f"Scene durable identity seam is missing: {fragment}", failures)

    for fragment in (
        "decltype(auto) AddComponent",
        "decltype(auto) EmplaceComponent",
        "using EmplaceResult = decltype(registry.emplace<T>",
        "if constexpr (std::is_void_v<EmplaceResult>)",
    ):
        if fragment not in scene_header:
            fail(f"Scene component insertion wrapper lost EnTT empty-tag return compatibility: {fragment}", failures)
    if "T& result = registry.emplace<T>" in scene_header:
        fail("Scene component insertion wrapper assumes EnTT emplace always returns T&; empty/tag components return void with ETO", failures)

    if "cmd.Register<unsigned int" in scene_system_source or "static_cast<entt::entity>(entityId)" in scene_system_source:
        fail("editor scene command transport regressed to raw/recyclable EnTT entity IDs", failures)
    for path in scene_root.rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and "to_integral" in path.read_text(encoding="utf-8", errors="ignore"):
            fail(f"raw EnTT integral identity leaked back into scene persistence/tooling: {path.relative_to(ROOT)}", failures)
    check_suite_is_compiled("Scene/Ecs", "EntityIdentityMapTests.cpp", failures)

    # Canonical camera/clip-space contract: right-handed, +Y up, -Z forward, the
    # renderer's infinite-far reverse-Z projection (Renderer/Visibility/RenderViewDesc.h).
    camera_header_path = ROOT / "Source" / "Engine" / "Systems" / "Camera" / "Camera.h"
    camera_source_path = ROOT / "Source" / "Engine" / "Systems" / "Camera" / "Camera.cpp"
    if not camera_header_path.is_file() or not camera_source_path.is_file():
        fail("runtime camera (Systems/Camera/Camera.h/.cpp) is missing", failures)
    else:
        camera_text = camera_header_path.read_text(encoding="utf-8", errors="ignore") + camera_source_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in ("right-handed", "reverse-Z", "PerspectiveReverseZRowMajor"):
            if fragment not in camera_text:
                fail(f"camera convention documentation/contract is missing: {fragment}", failures)
        for forbidden in ("GraphicsBackend", "projMatrix[1][1] *= -1", "perspectiveRH_NO"):
            if forbidden in camera_text:
                fail(f"Camera math regained backend-specific behavior: {forbidden}", failures)
    for forbidden in ("Presentation.ClipDepth", "SetClipSpaceDepthRange", "GetClipSpaceDepthRange", "MinusOneToOne"):
        if forbidden in scene_header or forbidden in scene_system_header or forbidden in scene_system_source or forbidden in engine_source:
            fail(f"scene/camera convention regained backend-dependent clip-depth policy: {forbidden}", failures)
    check_suite_is_compiled("Engine", "CameraAndProceduralMeshTests.cpp", failures)


def check_phase6_physics_architecture(failures: list[str]) -> None:
    physics_root = ROOT / "Source" / "Engine" / "Systems" / "Physics"
    backends_root = physics_root / "Backends"
    physx_root = backends_root / "PhysX"
    jolt_root = backends_root / "Jolt"
    scene_bridge_root = ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Physics"
    test_root = ROOT / "Source" / "Tests"

    required_paths = (
        physics_root / "PhysicsHandles.h",
        physics_root / "PhysicsTypes.h",
        physics_root / "IPhysicsBackend.h",
        physics_root / "PhysicsSystem.h",
        physics_root / "PhysicsSystem.cpp",
        physics_root / "PhysicsWorld.h",
        physics_root / "PhysicsWorld.cpp",
        physics_root / "RigidBody.h",
        physics_root / "Internal" / "GenerationalHandleTable.h",
        physx_root / "PhysXBackendFactory.h",
        physx_root / "PhysXBackendFactory.cpp",
        physx_root / "PhysXBackend.h",
        physx_root / "PhysXBackend.cpp",
        physx_root / "PhysXWorldBackend.h",
        physx_root / "PhysXWorldBackend.cpp",
        jolt_root / "JoltBackendFactory.h",
        jolt_root / "JoltBackendFactory.cpp",
        jolt_root / "JoltBackend.h",
        jolt_root / "JoltBackend.cpp",
        jolt_root / "JoltWorldBackend.h",
        jolt_root / "JoltWorldBackend.cpp",
        scene_bridge_root / "ScenePhysicsBridge.h",
        scene_bridge_root / "ScenePhysicsBridge.cpp",
        test_root / "Suites" / "Physics" / "Generic" / "PhysicsHandleTests.cpp",
        test_root / "Suites" / "Physics" / "PhysX" / "PhysXBackendTests.cpp",
        test_root / "Suites" / "Physics" / "Jolt" / "JoltBackendTests.cpp",
        test_root / "Fixtures" / "PhysicsBackendContract.h",
        test_root / "HeaderBoundary" / "PhysicsPublicHeaders.cpp",
        test_root / "HeaderBoundary" / "PhysicsBackendContractCompile.cpp",
    )
    for path in required_paths:
        if not path.is_file():
            fail(f"Phase 6 physics architecture file is missing: {path.relative_to(ROOT)}", failures)

    generic_forbidden = ("physx::", "PxPhysicsAPI", "#include <Px", "#include \"Px", "JPH::", "Jolt/")
    if physics_root.exists():
        for path in physics_root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            if backends_root in path.parents:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            for fragment in generic_forbidden:
                if fragment in text:
                    fail(
                        f"generic physics boundary leaked backend implementation type/include '{fragment}': {path.relative_to(ROOT)}",
                        failures,
                    )

    rigidbody_path = physics_root / "RigidBody.h"
    if rigidbody_path.is_file():
        rigidbody_text = rigidbody_path.read_text(encoding="utf-8", errors="ignore")
        if "BodyHandle body{};" not in rigidbody_text:
            fail("Rigidbody runtime identity is not a backend-neutral BodyHandle", failures)
        for fragment in ("PxRigidActor", "PxShape", "JPH::BodyID", "void* actor", "void* shape"):
            if fragment in rigidbody_text:
                fail(f"Rigidbody regained backend-owned runtime state: {fragment}", failures)

    world_header_path = physics_root / "PhysicsWorld.h"
    if world_header_path.is_file():
        world_text = world_header_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in ("entt::", "entt/", "Transform", "Rigidbody"):
            if fragment in world_text:
                fail(f"generic PhysicsWorld regained Scene/ECS integration: {fragment}", failures)

    bridge_header_path = scene_bridge_root / "ScenePhysicsBridge.h"
    bridge_source_path = scene_bridge_root / "ScenePhysicsBridge.cpp"
    if bridge_header_path.is_file() and bridge_source_path.is_file():
        bridge_text = bridge_header_path.read_text(encoding="utf-8", errors="ignore") + bridge_source_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in ("entt::registry", "Rigidbody", "std::unique_ptr<PhysicsWorld>"):
            if fragment not in bridge_text:
                fail(f"ScenePhysicsBridge is missing the scene/generic-physics boundary fragment: {fragment}", failures)
        if "to_integral" in bridge_text:
            fail("ScenePhysicsBridge must not turn recyclable EnTT identities into durable/tooling physics identity", failures)
        if "registry.patch<Transform>" in bridge_text:
            fail("ScenePhysicsBridge redundantly emits EnTT Transform patches after Transform setters already queue spatial dirtiness", failures)

    handles_path = physics_root / "PhysicsHandles.h"
    if handles_path.is_file():
        handles_text = handles_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in ("BodyHandle", "ShapeHandle", "PhysicsMaterialHandle", "ConstraintHandle", "CharacterHandle", "Generation"):
            if fragment not in handles_text:
                fail(f"generic physics handle contract is missing: {fragment}", failures)

    types_path = physics_root / "PhysicsTypes.h"
    if types_path.is_file():
        types_text = types_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "MotionType", "ShapeType", "ForceMode", "CollisionLayer", "PhysicsPose",
            "ShapeDesc", "PhysicsMaterialDesc", "BodyDesc", "PhysicsWorldDesc",
            "RaycastHit", "SweepHit", "OverlapHit", "CollisionEvent", "TriggerEvent",
        ):
            if fragment not in types_text:
                fail(f"generic physics public concept is missing: {fragment}", failures)

    backend_path = physics_root / "IPhysicsBackend.h"
    if backend_path.is_file():
        backend_text = backend_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "class IPhysicsWorldBackend", "class IPhysicsBackend", "CreateMaterial", "CreateShape", "CreateBody",
            "SetKinematicTarget", "AddForce", "BeginSimulation", "FetchResults", "Raycast", "Sweep", "Overlap",
            "GetCollisionEvents", "GetTriggerEvents", "CreateWorld",
        ):
            if fragment not in backend_text:
                fail(f"generic physics backend contract is missing: {fragment}", failures)

    physx_world_path = physx_root / "PhysXWorldBackend.cpp"
    if physx_world_path.is_file():
        # Backend types now live in role subfolders; preserve the same checks
        # across the active source tree rather than requiring a monolithic TU.
        physx_text = "\n".join(
            path.read_text(encoding="utf-8", errors="ignore")
            for path in sorted(physx_root.rglob("*"))
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
        )
        for stale in ("(*shapePtr)->setSimulationFilterData", "(*shapePtr)->setQueryFilterData"):
            if stale in physx_text:
                fail(
                    "PhysX writes per-body collision filter data onto the shared ShapeHandle template; "
                    "bodies built from one handle would re-filter each other",
                    failures,
                )
        for fragment in (
            "PxSimulationEventCallback", "SwimSimulationFilterShader", "LayerQueryFilter",
            "PhysXWorldBackend::Raycast", "PhysXWorldBackend::Sweep", "PhysXWorldBackend::Overlap",
            "CollisionEvent", "TriggerEvent", "pendingDestroy",
            "if (!actor->attachShape(*instancedShape))", "ReleaseActor(actor)",
            # Collision filtering is per-body state, so it must be written onto a
            # body-owned shape instance and never onto the shared ShapeHandle template.
            "CreateInstancedShape", "instancedShape->setSimulationFilterData(filterData)",
            "ResolveBody(const physx::PxActor* actor)", "actor->is<physx::PxRigidActor>()",
        ):
            if fragment not in physx_text:
                fail(f"PhysX backend parity implementation is missing: {fragment}", failures)

    jolt_world_path = jolt_root / "JoltWorldBackend.cpp"
    if jolt_world_path.is_file():
        jolt_text = "\n".join(
            path.read_text(encoding="utf-8", errors="ignore")
            for path in sorted(jolt_root.rglob("*"))
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
        )
        for fragment in (
            "JPH::PhysicsSystem", "JPH::ContactListener", "QueryBodyFilter", "QueryObjectLayerFilter",
            "JoltWorldBackend::Raycast", "JoltWorldBackend::Sweep", "JoltWorldBackend::Overlap",
            "CollisionEvent", "TriggerEvent", "pendingDestroy",
            "SetKinematicTarget", "MoveKinematic", "registeredLayerLookup", "LayersMatch",
            "EPhysicsUpdateError", "GetPtr()", "centerOfMassTransform", "PreTranslated(nativeShape->GetCenterOfMass())",
            "ShapeType::ConvexMesh", "ShapeType::TriangleMesh",
        ):
            if fragment not in jolt_text:
                fail(f"Jolt backend parity implementation is missing: {fragment}", failures)

    for path in jolt_root.rglob("*") if jolt_root.exists() else ():
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for forbidden in ("physx::", "PxPhysicsAPI", "#include <Px", "#include \"Px"):
            if forbidden in text:
                fail(f"Jolt backend leaked PhysX implementation dependency: {path.relative_to(ROOT)}: {forbidden}", failures)

    contract_path = test_root / "Fixtures" / "PhysicsBackendContract.h"
    if contract_path.is_file():
        contract_text = contract_path.read_text(encoding="utf-8", errors="ignore")
        # The word "PhysX" may legitimately appear in the usage comment, so the
        # backend-neutrality check targets implementation identifiers only.
        for forbidden in ("physx::", "PxPhysicsAPI", "JPH::", "Jolt/", "CreatePhysXBackend", "CreateJoltBackend"):
            if forbidden in contract_text:
                fail(f"shared physics backend contract test is backend-specific: {forbidden}", failures)
        for fragment in (
            "RunPhysicsWorldLifecycleContract", "RunPhysicsSceneQueryContract",
            "RunPhysicsSimulationContract", "RunPhysicsTriggerContract",
            "RunPhysicsSharedShapeContract", "RunPhysicsInFlightWriteContract",
            "RunPhysicsContactEventContract",
            "Raycast", "Sweep", "Overlap", "CollisionEvent",
            "TriggerEvent", "SetKinematicTarget", "AddForce", "IsBodyValid",
            "offsetOverlapShape.LocalPose.Position", "foundOffsetDynamicOverlap",
            "triggerShape.IsValid()", "triggerBody.IsValid()", "triggerMoverShape.IsValid()",
            "triggerMover.IsValid()", "deferredShape.IsValid()", "staleBody.IsValid()",
        ):
            if fragment not in contract_text:
                fail(f"shared physics backend contract test is missing coverage seam: {fragment}", failures)
        # Handles expose an explicit operator bool; relying on implicit conversion
        # inside a check macro would not compile for every backend handle type.
        for forbidden in (
            "SWIM_REQUIRE(triggerShape)", "SWIM_REQUIRE(triggerBody)",
            "SWIM_REQUIRE(triggerMoverShape)", "SWIM_REQUIRE(triggerMover)",
            "SWIM_REQUIRE(deferredShape)", "SWIM_REQUIRE(staleBody)",
        ):
            if forbidden in contract_text:
                fail(f"shared physics backend contract relies on implicit conversion of an explicit handle bool: {forbidden}", failures)

    jolt_test_path = test_root / "Suites" / "Physics" / "Jolt" / "JoltBackendTests.cpp"
    if jolt_test_path.is_file():
        jolt_test_text = jolt_test_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "MultipleBackendInstancesShareTheRuntime",
            "second->Shutdown()",
            "survivingWorld = first->CreateWorld",
        ):
            if fragment not in jolt_test_text:
                fail(f"Jolt backend lifetime parity test is missing: {fragment}", failures)

    cmake_path = ROOT / "CMakeLists.txt"
    if cmake_path.is_file():
        cmake_text = cmake_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "file(GLOB_RECURSE SWIM_PHYSICS_SOURCES",
            "file(GLOB_RECURSE SWIM_PHYSX_BACKEND_SOURCES",
            "file(GLOB_RECURSE SWIM_JOLT_BACKEND_SOURCES",
            "list(APPEND SWIM_ENGINE_SOURCES ${SWIM_PHYSICS_SOURCES})",
            "list(APPEND SWIM_ENGINE_SOURCES ${SWIM_PHYSX_BACKEND_SOURCES})",
            "list(APPEND SWIM_ENGINE_SOURCES ${SWIM_JOLT_BACKEND_SOURCES})",
            "include(cmake/JoltDependencies.cmake)",
            'list(FILTER SWIM_PHYSICS_SOURCES EXCLUDE REGEX "/Source/Engine/Systems/Physics/Backends/")',
            'list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Systems/Physics/")',
            "include(cmake/MathDependencies.cmake)",
            "include(cmake/Tests.cmake)",
        ):
            if fragment not in cmake_text:
                fail(f"Phase 6 physics target boundary is missing from CMake: {fragment}", failures)

        # Concrete SDK dependencies now belong to the final consumer, while
        # source/header checks above enforce the generic physics API boundary.
        engine_links = re.findall(r"target_link_libraries\(SwimEngine\s+([^\)]*)\)", cmake_text, re.DOTALL)
        link_text = "\n".join(engine_links)
        for fragment in ("Swim::PhysX", "Swim::Jolt", "glm::glm"):
            if fragment not in link_text:
                fail(f"runtime physics dependency is missing: {fragment}", failures)
        tests_text = read_tests_cmake()
        for fragment in ("${SWIM_PHYSICS_SOURCES}", "${SWIM_JOLT_BACKEND_SOURCES}", "${SWIM_PHYSX_BACKEND_SOURCES}"):
            if fragment not in tests_text:
                fail(f"physics test source list is missing: {fragment}", failures)

    cmake_path = ROOT / "CMakeLists.txt"
    if cmake_path.is_file():
        cmake_text = cmake_path.read_text(encoding="utf-8", errors="ignore")
        legacy_return = cmake_text.find("if(NOT SWIM_BUILD_ENGINE)")
        generic_physics = cmake_text.find("# Generic physics is part of the cross-platform foundation")
        if legacy_return < 0 or generic_physics < 0 or generic_physics > legacy_return:
            fail("generic Swim::Physics is not compiled before the foundation-only (SWIM_BUILD_ENGINE=OFF) return", failures)

        # The foundation-only configure must still define the test targets before
        # it returns, otherwise the shared physics backend contract and the
        # portable suites stop being validated on Linux.
        foundation_tests = cmake_text.find("swim_configure_tests()", legacy_return)
        foundation_return = cmake_text.find("return()", legacy_return)
        if legacy_return < 0 or foundation_tests < 0 or foundation_return < 0 or foundation_tests > foundation_return:
            fail("the foundation/Linux configure no longer defines the Swim test targets before returning", failures)

        tests_cmake_text = read_tests_cmake()
        for fragment in (
            "swim_add_header_boundary(SwimPhysicsBackendContractCompile",
            "Source/Tests/HeaderBoundary/PhysicsBackendContractCompile.cpp",
            "swim_add_header_boundary(SwimPhysicsPublicHeaders",
            "Physics/PhysX",
            "Physics/Jolt",
        ):
            if fragment not in tests_cmake_text:
                fail(f"Phase 6 physics test boundary is missing from cmake/Tests.cmake: {fragment}", failures)

    math_dependencies_path = ROOT / "cmake" / "MathDependencies.cmake"
    if math_dependencies_path.is_file():
        math_text = math_dependencies_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in ("GITHUB_REPOSITORY g-truc/glm", "GIT_TAG 1.0.0", "add_library(glm::glm ALIAS SwimGlm)"):
            if fragment not in math_text:
                fail(f"cross-platform GLM foundation dependency is missing: {fragment}", failures)

    jolt_dependencies_path = ROOT / "cmake" / "JoltDependencies.cmake"
    if not jolt_dependencies_path.is_file():
        fail("Jolt dependency boundary is missing: cmake/JoltDependencies.cmake", failures)
    else:
        jolt_dependency_text = jolt_dependencies_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "GITHUB_REPOSITORY jrouwe/JoltPhysics", "GIT_TAG v5.6.0",
            "SOURCE_SUBDIR Build", '"JPH_BUILD_SHARED_LIBS OFF"', '"DOUBLE_PRECISION OFF"',
            '"JPH_USE_DX12 OFF"', '"JPH_USE_VK OFF"', '"JPH_USE_MTL OFF"',
            '"JPH_USE_CPU_COMPUTE OFF"', "TARGET Jolt::Jolt",
            "target_link_libraries(SwimJolt INTERFACE Jolt::Jolt)", "add_library(Swim::Jolt ALIAS SwimJolt)",
        ):
            if fragment not in jolt_dependency_text:
                fail(f"Jolt dependency contract is missing: {fragment}", failures)

    dependencies_path = ROOT / "cmake" / "Dependencies.cmake"
    if dependencies_path.is_file():
        dependencies_text = dependencies_path.read_text(encoding="utf-8", errors="ignore")
        if "if(SWIM_ENABLE_PHYSX_BACKEND)" not in dependencies_text or "include(cmake/PhysX.cmake)" not in dependencies_text:
            fail("PhysX dependency acquisition is not guarded by SWIM_ENABLE_PHYSX_BACKEND", failures)

    engine_source_path = ROOT / "Source" / "Engine" / "SwimEngine.cpp"
    if engine_source_path.is_file():
        engine_text = engine_source_path.read_text(encoding="utf-8", errors="ignore")
        if "CreatePhysXBackend()" not in engine_text:
            fail("runtime composition no longer selects the PhysX implementation through its backend factory", failures)
        if "CreateJoltBackend()" not in engine_text:
            fail("runtime composition does not select Jolt through its backend factory", failures)
        if "case PhysicsBackend::Jolt:" not in engine_text:
            fail("runtime physics backend switch does not include PhysicsBackend::Jolt", failures)

    windows_helper_path = ROOT / "scripts" / "windows-build-common.ps1"
    if windows_helper_path.is_file():
        helper_text = windows_helper_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "Invoke-SwimWindowsTestSuite", "SwimTests",
            "SwimPhysicsPublicHeaders", "SwimPhysicsBackendContractCompile",
        ):
            if fragment not in helper_text:
                fail(f"Windows physics validation gate is missing: {fragment}", failures)


def check_phase7_shader_architecture(failures: list[str]) -> None:
    required_paths = (
        ROOT / "cmake" / "ShaderCompilerDependencies.cmake",
        ROOT / "cmake" / "SlangShaders.cmake",
        ROOT / "Source" / "Tools" / "ShaderCompiler" / "ShaderReflection.h",
        ROOT / "Source" / "Tools" / "ShaderCompiler" / "ShaderReflection.cpp",
        ROOT / "Source" / "Shaders" / "Slang" / "Basic" / "Basic.slang",
        ROOT / "Source" / "Tests" / "Suites" / "ShaderCompiler" / "ShaderReflectionTests.cpp",
        ROOT / "Source" / "Tests" / "HeaderBoundary" / "ShaderCompilerPublicHeaders.cpp",
    )
    for path in required_paths:
        if not path.is_file():
            fail(f"Slang shader/compiler foundation file is missing: {path.relative_to(ROOT)}", failures)

    if any(not path.is_file() for path in required_paths):
        return

    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    deps_text = (ROOT / "cmake" / "ShaderCompilerDependencies.cmake").read_text(encoding="utf-8", errors="ignore")
    rules_text = (ROOT / "cmake" / "SlangShaders.cmake").read_text(encoding="utf-8", errors="ignore")
    public_text = (ROOT / "Source" / "Tools" / "ShaderCompiler" / "ShaderReflection.h").read_text(encoding="utf-8", errors="ignore")
    implementation_text = (ROOT / "Source" / "Tools" / "ShaderCompiler" / "ShaderReflection.cpp").read_text(encoding="utf-8", errors="ignore")
    sample_text = (ROOT / "Source" / "Shaders" / "Slang" / "Basic" / "Basic.slang").read_text(encoding="utf-8", errors="ignore")
    tests_text = (ROOT / "cmake" / "Tests.cmake").read_text(encoding="utf-8", errors="ignore")

    for fragment in (
        'option(SWIM_BUILD_SHADER_COMPILER',
        'include(cmake/ShaderCompilerDependencies.cmake)',
        'add_library(SwimShaderCompiler STATIC',
        'add_library(Swim::ShaderCompiler ALIAS SwimShaderCompiler)',
        'target_link_libraries(SwimShaderCompiler PRIVATE simdjson::simdjson)',
        'swim_add_slang_program(SwimBasicRaster',
    ):
        if fragment not in cmake_text:
            fail(f"Slang shader/compiler CMake boundary is missing: {fragment}", failures)

    for fragment in (
        'set(SWIM_SLANG_VERSION "2026.16.1"',
        'EXPECTED_HASH "SHA256=${SWIM_SLANG_ARCHIVE_SHA256}"',
        'slang-${SWIM_SLANG_VERSION}-windows-x86_64.zip',
        '0fd3e6a9a5d05ed4cdd000d467f1ffb5d9701b827e83bfb428902a45c37ef8a5',
        'slang-${SWIM_SLANG_VERSION}-linux-x86_64-glibc-2.27.zip',
        '95eb246e758131545915406e5ac9e41ecd29a6bc1f83ae0b112d31e390fc9d33',
        'add_executable(SwimSlangCompiler IMPORTED GLOBAL)',
    ):
        if fragment not in deps_text:
            fail(f"pinned Slang compiler dependency contract is missing: {fragment}", failures)

    for fragment in (
        'add_custom_command(',
        'OUTPUT',
        '-target "${SWIM_SLANG_PROGRAM_TARGET}"',
        '-profile "${SWIM_SLANG_PROGRAM_PROFILE}"',
        '-reflection-json "${SWIM_SLANG_PROGRAM_REFLECTION}"',
        '-depfile "${SWIM_SLANG_PROGRAM_DEPFILE}"',
        'DEPFILE "${SWIM_SLANG_PROGRAM_DEPFILE}"',
    ):
        if fragment not in rules_text:
            fail(f"deterministic Slang shader build rule is missing: {fragment}", failures)
    if 'PRE_BUILD' in rules_text:
        fail("Slang shader compilation regressed to PRE_BUILD; keep explicit OUTPUT/DEPFILE artifacts", failures)

    source_shader_root = ROOT / "Source" / "Shaders"
    retired_shader_sources = tuple(source_shader_root.rglob("*.hlsl")) + tuple(source_shader_root.rglob("*.glsl"))
    for path in retired_shader_sources:
        fail(f"first-party shader source must be Slang: {path.relative_to(ROOT)}", failures)

    # The pre-Slang HLSL/GLSL sources are archived outside Source/ so no glob can
    # reach them. Keep that structural: an archive the build can see is not an
    # archive, and a stale shader compiling again would be silent.
    deprecated_root = ROOT / "Deprecated" / "Shaders"
    if deprecated_root.is_dir():
        for build_file in list(ROOT.glob("cmake/*.cmake")) + [ROOT / "CMakeLists.txt"]:
            if not build_file.is_file():
                continue
            build_code = re.sub(r"#[^\n]*", "", build_file.read_text(encoding="utf-8", errors="ignore"))
            if "Deprecated" in build_code:
                fail(
                    f"build system references the deprecated shader archive: {build_file.relative_to(ROOT)}",
                    failures,
                )
    runtime_rules_text = (ROOT / "cmake" / "Shaders.cmake").read_text(encoding="utf-8", errors="ignore")
    for fragment in ('SWIM_RUNTIME_SHADER_PROGRAMS', 'SwimRuntimeShaders', 'Shaders/Runtime'):
        if fragment not in runtime_rules_text:
            fail(f"all-Slang runtime shader pipeline is missing: {fragment}", failures)
    for forbidden in ('DXC', 'dxc', '*.hlsl', '*.glsl', 'PRE_BUILD'):
        if forbidden in runtime_rules_text:
            fail(f"retired shader compiler/source path remains in runtime shader rules: {forbidden}", failures)

    slang_rules_text = (ROOT / "cmake" / "SlangShaders.cmake").read_text(encoding="utf-8", errors="ignore")
    if "-matrix-layout-column-major" not in slang_rules_text:
        fail("Slang runtime compilation lost DXC-compatible column-major matrix memory layout", failures)

    for forbidden in ('#include <slang', 'Slang::', 'slang::', 'simdjson'):
        if forbidden in public_text:
            fail(f"ShaderReflection public contract leaks implementation dependency: {forbidden}", failures)
    if '#include <simdjson.h>' not in implementation_text:
        fail("ShaderReflection implementation no longer privately owns simdjson parsing", failures)

    for fragment in ('ParameterBlock<', '[shader("vertex")]', '[shader("fragment")]'):
        if fragment not in sample_text:
            fail(f"minimal Slang raster sample is missing reflection-first source contract: {fragment}", failures)

    # Draws that index by vertex/instance use the raw Vulkan semantics. Slang lowers
    # SV_VertexID/SV_InstanceID to (index - base), which needs the DrawParameters
    # capability that some drivers (SwiftShader) lack; the runtime's non-indexed draws
    # start at vertex/instance 0, so the raw indices are equal and portable.
    raw_index_shader_paths = (
        ROOT / "Source" / "Shaders" / "Slang" / "Runtime" / "Present.slang",
        ROOT / "Source" / "Shaders" / "Slang" / "Runtime" / "SkyBackground.slang",
        ROOT / "Source" / "Shaders" / "Slang" / "Ui" / "UiQuad.slang",
        ROOT / "Source" / "Shaders" / "Slang" / "Particles" / "ParticleRender.slang",
    )
    for shader_path in raw_index_shader_paths:
        if not shader_path.is_file():
            fail(f"runtime shader is missing: {shader_path.relative_to(ROOT)}", failures)
            continue
        shader_source = shader_path.read_text(encoding="utf-8", errors="ignore")
        if "SV_VulkanVertexID" not in shader_source:
            fail(f"runtime shader lost raw Vulkan vertex-index semantics: {shader_path.relative_to(ROOT)}", failures)
        if re.search(r":\s*SV_(?:Vertex|Instance)ID\b", shader_source):
            fail(f"runtime shader regressed to base-relative SV_VertexID/SV_InstanceID: {shader_path.relative_to(ROOT)}", failures)

    # Per-draw bindless indexing stays explicitly non-uniform.
    for shader_path in (
        ROOT / "Source" / "Shaders" / "Slang" / "ForwardPlus" / "ClusteredForward.slang",
        ROOT / "Source" / "Shaders" / "Slang" / "Ui" / "UiQuad.slang",
    ):
        if shader_path.is_file() and "NonUniformResourceIndex" not in shader_path.read_text(encoding="utf-8", errors="ignore"):
            fail(f"bindless shader lost explicit non-uniform indexing: {shader_path.relative_to(ROOT)}", failures)

    legacy_shader_root = ROOT / "Source" / "Shaders" / "Vulkan"
    if legacy_shader_root.exists():
        fail("the legacy VulkanRenderer shader group is back under Source/Shaders/Vulkan", failures)

    for fragment in ('SwimShaderCompilerPublicHeaders', 'SwimSlangReflectionSample', 'SWIM_SLANG_REFLECTION_SAMPLE_PATH'):
        if fragment not in tests_text:
            fail(f"Slang compiler/reflection validation wiring is missing: {fragment}", failures)


def check_phase8_rhi_type_architecture(failures: list[str]) -> None:
    types_header = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "RHI" / "RhiTypes.h"
    contracts_header = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "RHI" / "RhiContracts.h"
    factory_header = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "RHI" / "RhiFactory.h"
    frame_header = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "RHI" / "RhiFrameLifetime.h"
    types_test = ROOT / "Source" / "Tests" / "Suites" / "RHI" / "RhiTypesTests.cpp"
    contracts_test = ROOT / "Source" / "Tests" / "Suites" / "RHI" / "RhiContractsTests.cpp"
    frame_test = ROOT / "Source" / "Tests" / "Suites" / "RHI" / "RhiFrameLifetimeTests.cpp"
    boundary = ROOT / "Source" / "Tests" / "HeaderBoundary" / "RhiPublicHeaders.cpp"
    for path in (types_header, contracts_header, factory_header, frame_header, types_test, contracts_test, frame_test, boundary):
        if not path.is_file():
            fail(f"backend-neutral RHI foundation is missing: {path.relative_to(ROOT)}", failures)
    if not types_header.is_file() or not contracts_header.is_file():
        return

    types_text = types_header.read_text(encoding="utf-8", errors="ignore")
    contracts_text = contracts_header.read_text(encoding="utf-8", errors="ignore")
    factory_text = factory_header.read_text(encoding="utf-8", errors="ignore") if factory_header.is_file() else ""
    frame_text = frame_header.read_text(encoding="utf-8", errors="ignore") if frame_header.is_file() else ""
    combined = types_text + contracts_text + factory_text + frame_text
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    tests_text = (ROOT / "cmake" / "Tests.cmake").read_text(encoding="utf-8", errors="ignore")

    for fragment in (
        'enum class Format',
        'enum class ResourceState',
        'enum class DescriptorType',
        'struct DescriptorBindingDesc',
        'struct DescriptorSchemaDesc',
        'struct GraphicsCapabilities',
        'DescriptorIndexing',
        'BufferDeviceAddress',
        'MeshShaders',
        'RayTracingPipeline',
    ):
        if fragment not in types_text:
            fail(f"RHI item 32 contract is missing: {fragment}", failures)

    for fragment in (
        'class GraphicsSystem',
        'class Adapter : public RhiObject',
        'class Device : public RhiObject',
        'class Queue : public RhiObject',
        'class Swapchain : public RhiObject',
        'class CommandPool : public RhiObject',
        'class CommandList : public RhiObject',
        'class Buffer : public RhiObject',
        'class Texture : public RhiObject',
        'class TextureView : public RhiObject',
        'class Sampler : public RhiObject',
        'class ShaderProgram : public RhiObject',
        'class PipelineLayout : public RhiObject',
        'class GraphicsPipeline : public RhiObject',
        'class ComputePipeline : public RhiObject',
        'class DescriptorTable : public RhiObject',
        'class Fence : public RhiObject',
        'class Timeline : public RhiObject',
        'class QueryPool : public RhiObject',
        'CreateSwapchain(Platform::Window& window',
        'virtual std::uintptr_t GetNativeHandle() const = 0',
    ):
        if fragment not in contracts_text:
            fail(f"RHI item 33 object contract is missing: {fragment}", failures)

    for fragment in (
        'ShaderProgramInterfaceDesc Interface{}',
        'ShaderProgram* Program = nullptr',
        'PipelineLayout* Layout = nullptr',
        'virtual const ShaderProgramInterface& GetInterface() const = 0',
        'CreateDescriptorTable(const DescriptorTableDesc& desc)',
    ):
        if fragment not in contracts_text:
            fail(f"shader-reflection-owned RHI binding contract is missing: {fragment}", failures)
    pipeline_layout_start = contracts_text.find('struct PipelineLayoutDesc')
    descriptor_table_start = contracts_text.find('struct DescriptorTableDesc')
    if pipeline_layout_start != -1 and descriptor_table_start != -1:
        if 'DescriptorSchemas' in contracts_text[pipeline_layout_start:descriptor_table_start]:
            fail("PipelineLayoutDesc duplicates descriptor schemas instead of deriving them from ShaderProgram reflection", failures)
    descriptor_write_start = contracts_text.find('struct DescriptorWrite')
    query_pool_start = contracts_text.find('struct QueryPoolDesc')
    if descriptor_write_start != -1 and query_pool_start != -1:
        if 'DescriptorType Type' in contracts_text[descriptor_write_start:query_pool_start]:
            fail("DescriptorWrite duplicates the reflected descriptor type", failures)

    for fragment in ('class GraphicsFactory', 'Register(GraphicsApi api', 'Create(GraphicsApi api, const GraphicsSystemDesc& desc = {}) const'):
        if fragment not in factory_text:
            fail(f"RHI runtime graphics factory contract is missing: {fragment}", failures)

    for fragment in (
        'class FrameContextRing',
        'struct FrameContext',
        'CompletionValue',
        'CreateCommandPool(desc.Queue)',
        'device.CreateTimeline(0)',
        'timeline->Wait(context.CompletionValue)',
        'signals.push_back({ timeline.get(), signalValue })',
        'retiredObjects',
        'RetireAt(std::uint64_t completionValue',
        'GetLastSubmittedPoint()',
    ):
        if fragment not in frame_text:
            fail(f"RHI item 38 frame-lifetime contract is missing: {fragment}", failures)
    if 'WaitIdle(' in frame_text or 'vkDeviceWaitIdle' in frame_text or 'vkQueueWaitIdle' in frame_text:
        fail("RHI frame lifetime regressed to idle-based retirement", failures)

    for forbidden in ('<vulkan/', 'VkFormat', 'VkImage', 'VkBuffer', 'D3D12_', 'ID3D12', 'MTL::', 'HWND'):
        if forbidden in combined:
            fail(f"generic RHI public contract leaks a backend/platform API: {forbidden}", failures)

    if 'SwimRhiPublicHeaders' not in tests_text or '\n\t\tRHI\n' not in tests_text:
        fail("RHI tests/header boundary are not part of the portable test graph", failures)



def check_phase9_vulkan_rhi_architecture(failures: list[str]) -> None:
    backend_root = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "RHI" / "Backends" / "Vulkan"
    backend_header = backend_root / "VulkanRhiBackend.h"
    backend_source = backend_root / "VulkanRhiBackend.cpp"
    dependency_file = ROOT / "cmake" / "VulkanRhiDependencies.cmake"
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")

    for path in (backend_header, backend_source, dependency_file):
        if not path.is_file():
            fail(f"Vulkan RHI item 35 file is missing: {path.relative_to(ROOT)}", failures)
    if not backend_source.is_file() or not dependency_file.is_file():
        return

    header_text = backend_header.read_text(encoding="utf-8", errors="ignore") if backend_header.is_file() else ""
    source_text = "\n".join(
        path.read_text(encoding="utf-8", errors="ignore")
        for path in sorted(backend_root.rglob("*"))
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
    )
    dependency_text = dependency_file.read_text(encoding="utf-8", errors="ignore")

    for forbidden in ('<vulkan/', '<volk.h>', '<VkBootstrap.h>', 'VkInstance', 'VkDevice', 'vkb::'):
        if forbidden in header_text:
            fail(f"Vulkan RHI public factory header leaks an implementation type/include: {forbidden}", failures)

    for fragment in (
        '#include <volk.h>',
        '#include <VkBootstrap.h>',
        'vkb::InstanceBuilder',
        'vkb::PhysicalDeviceSelector',
        'vkb::DeviceBuilder',
        'volk::volkInitializeCustom',
        'volk::volkLoadInstanceTable',
        'volk::volkLoadDeviceTable',
        '.require_api_version(1, 3, 0)',
        '.set_minimum_version(1, 3)',
        'features.synchronization2 = VK_TRUE',
        'features.dynamicRendering = VK_TRUE',
        'features.timelineSemaphore = VK_TRUE',
        'features.descriptorIndexing = VK_TRUE',
        'features.drawIndirectCount = VK_TRUE',
        'features.bufferDeviceAddress = VK_TRUE',
        'select_devices()',
        'RegisterGraphicsBackend',
    ):
        if fragment not in source_text:
            fail(f"Vulkan RHI item 35 bootstrap/feature contract is missing: {fragment}", failures)

    for fragment in (
        'GITHUB_REPOSITORY zeux/volk',
        'GIT_TAG 1.4.350',
        'GITHUB_REPOSITORY charles-lunarg/vk-bootstrap',
        'GIT_TAG v1.4.350',
        'VOLK_NAMESPACE ON',
        'VK_BOOTSTRAP_TEST OFF',
    ):
        if fragment not in dependency_text:
            fail(f"Vulkan RHI dependency pin/isolation is missing: {fragment}", failures)

    for fragment in (
        'option(SWIM_ENABLE_VULKAN_RHI',
        'file(GLOB_RECURSE SWIM_VULKAN_RHI_SOURCES',
        'set(SWIM_VULKAN_RHI_PRIVATE_LIBS',
        'set(SWIM_VULKAN_RHI_PRIVATE_DEFINITIONS',
        'target_link_libraries(SwimEngine PRIVATE ${SWIM_VULKAN_RHI_PRIVATE_LIBS})',
        'list(APPEND SWIM_ENGINE_SOURCES ${SWIM_VULKAN_RHI_SOURCES})',
        'list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Systems/Renderer/RHI/Backends/")',
    ):
        if fragment not in cmake_text:
            fail(f"Vulkan RHI target/isolation wiring is missing: {fragment}", failures)

    tests_text = (ROOT / "cmake" / "Tests.cmake").read_text(encoding="utf-8", errors="ignore")
    test_path = ROOT / "Source" / "Tests" / "Suites" / "RHIVulkan" / "VulkanFactoryTests.cpp"
    boundary_path = ROOT / "Source" / "Tests" / "HeaderBoundary" / "RhiVulkanPublicHeaders.cpp"
    if not test_path.is_file() or not boundary_path.is_file():
        fail("Vulkan RHI registration/public-header validation files are missing", failures)
    for fragment in ('SwimRhiVulkanPublicHeaders', 'SWIM_VULKAN_RHI_SUITES', '${SWIM_VULKAN_RHI_SOURCES}', '${SWIM_VULKAN_RHI_PRIVATE_LIBS}'):
        if fragment not in tests_text:
            fail(f"Vulkan RHI validation wiring is missing: {fragment}", failures)

    for fragment in (
        'Platform::Internal::AcquireVulkanLoader',
        'Platform::Internal::ReleaseVulkanLoader',
        'Platform::Internal::GetVulkanInstanceProcAddress',
        'Platform::Internal::GetVulkanInstanceExtensions',
        'Platform::Internal::GetVulkanPresentationSupport',
        'Platform::Internal::CreateVulkanSurface',
        'Platform::Internal::DestroyVulkanSurface',
        'vkGetPhysicalDeviceSurfaceSupportKHR',
        'vkb::SwapchainBuilder',
    ):
        if fragment not in source_text:
            fail(f"Vulkan RHI item 36 SDL3 WSI/surface contract is missing: {fragment}", failures)

    if 'window.GetNativeHandle()' in source_text:
        fail("Vulkan RHI surface creation bypasses SDL3 WSI through a native window handle", failures)

    vma_source = backend_root / "VulkanMemoryAllocator.cpp"
    if not vma_source.is_file():
        fail("Vulkan RHI item 37 VMA implementation unit is missing", failures)
    else:
        vma_source_text = vma_source.read_text(encoding="utf-8", errors="ignore")
        if "#define VMA_IMPLEMENTATION" not in vma_source_text or "#include <vk_mem_alloc.h>" not in vma_source_text:
            fail("Vulkan RHI item 37 VMA implementation unit is malformed", failures)

    for fragment in (
        'GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator',
        'GIT_TAG v3.4.0',
        'GPUOpen::VulkanMemoryAllocator',
    ):
        if fragment not in dependency_text and fragment not in cmake_text:
            fail(f"Vulkan RHI item 37 VMA dependency wiring is missing: {fragment}", failures)

    for fragment in (
        'VMA_STATIC_VULKAN_FUNCTIONS=0',
        'VMA_DYNAMIC_VULKAN_FUNCTIONS=1',
        'VMA_VULKAN_VERSION=1003000',
        'GPUOpen::VulkanMemoryAllocator',
    ):
        if fragment not in cmake_text:
            fail(f"Vulkan RHI item 37 VMA target isolation is missing: {fragment}", failures)

    for fragment in (
        'VmaAllocator Allocator = nullptr',
        'vmaCreateAllocator',
        'vmaDestroyAllocator',
        'vmaCreateBuffer',
        'vmaDestroyBuffer',
        'vmaCreateImage',
        'vmaDestroyImage',
        'vmaSetAllocationName',
        'VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE',
        'VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT',
        'VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT',
    ):
        if fragment not in source_text:
            fail(f"Vulkan RHI item 37 allocation contract is missing: {fragment}", failures)

    for fragment in (
        'class VulkanTimeline final',
        'VK_SEMAPHORE_TYPE_TIMELINE',
        'vkGetSemaphoreCounterValue',
        'vkWaitSemaphores',
        'vkQueueSubmit2',
        'VK_STRUCTURE_TYPE_SUBMIT_INFO_2',
        'class VulkanCommandPool final',
        'vkResetCommandPool',
        'class VulkanCommandList final',
        'VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT',
        'timeline->GetState()->DeviceState.get() != state.get()',
        'PresentationQueue',
        'vkQueueWaitIdle(state->PresentationQueue)',
    ):
        if fragment not in source_text:
            fail(f"Vulkan RHI item 38 timeline/frame-retirement contract is missing: {fragment}", failures)

    swapchain_path = backend_root / "VulkanSwapchain.cpp"
    if not swapchain_path.is_file():
        fail("Vulkan swapchain implementation is missing", failures)
    else:
        swapchain_text = swapchain_path.read_text(encoding="utf-8", errors="ignore")
        if 'vkDeviceWaitIdle' in swapchain_text:
            fail("Vulkan swapchain resize regressed to device-wide idle waits", failures)
        if 'vkQueueWaitIdle(state->PresentationQueue)' not in swapchain_text:
            fail("Vulkan swapchain resize lost the WSI-only present-completion fallback", failures)

    wsi_header = ROOT / "Source" / "Engine" / "Platform" / "Internal" / "VulkanWsi.h"
    wsi_source = ROOT / "Source" / "Engine" / "Platform" / "Internal" / "VulkanWsi.cpp"
    if not wsi_header.is_file() or not wsi_source.is_file():
        fail("Platform SDL3 Vulkan WSI bridge files are missing", failures)
    else:
        wsi_header_text = wsi_header.read_text(encoding="utf-8", errors="ignore")
        wsi_source_text = wsi_source.read_text(encoding="utf-8", errors="ignore")
        for forbidden in ('SDL_Window', 'VkSurfaceKHR', 'VkInstance', '<SDL3/', '<vulkan/'):
            if forbidden in wsi_header_text:
                fail(f"Platform Vulkan WSI bridge header leaks SDL/Vulkan implementation type: {forbidden}", failures)
        for fragment in (
            '#include <SDL3/SDL_vulkan.h>',
            'SDL_Vulkan_LoadLibrary',
            'SDL_Vulkan_UnloadLibrary',
            'SDL_Vulkan_GetVkGetInstanceProcAddr',
            'SDL_Vulkan_GetInstanceExtensions',
            'SDL_Vulkan_GetPresentationSupport',
            'SDL_Vulkan_CreateSurface',
            'SDL_Vulkan_DestroySurface',
            'WindowAccess::GetSdlWindow',
        ):
            if fragment not in wsi_source_text:
                fail(f"Platform SDL3 Vulkan WSI implementation is missing: {fragment}", failures)

    window_header = ROOT / "Source" / "Engine" / "Platform" / "Window.h"
    window_text = window_header.read_text(encoding="utf-8", errors="ignore") if window_header.is_file() else ""
    for forbidden in ('SDL_Window', 'VkSurfaceKHR', 'VkInstance', '<SDL3/', '<vulkan/'):
        if forbidden in window_text:
            fail(f"Platform::Window public contract leaks SDL/Vulkan WSI detail: {forbidden}", failures)

    generic_rhi_root = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "RHI"
    for path in generic_rhi_root.glob("*.h"):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for forbidden in ('<vulkan/', '<volk.h>', '<VkBootstrap.h>', 'VkInstance', 'VkDevice', 'vkb::'):
            if forbidden in text:
                fail(f"generic RHI header {path.relative_to(ROOT)} leaks Vulkan implementation detail: {forbidden}", failures)


    # Pipeline/draw implementations stay in focused backend units, outside generic headers.
    for relative in (
        "Pipelines/VulkanShaderProgram.cpp", "Pipelines/VulkanPipelineLayout.cpp",
        "Pipelines/VulkanGraphicsPipeline.cpp", "Commands/VulkanCommandListDraw.cpp",
        "Internal/VulkanPipelineUtils.cpp",
        "Descriptors/VulkanDescriptorLayout.cpp", "Descriptors/VulkanDescriptorTable.cpp",
        "Descriptors/VulkanDescriptorTableWrites.cpp", "Resources/VulkanSampler.cpp",
        "Commands/VulkanCommandListDescriptors.cpp",
        "Internal/VulkanSwapchainSession.cpp", "VulkanSwapchainRetirement.cpp",
        "Internal/VulkanDiagnostics.cpp", "Internal/VulkanInstanceDiagnostics.cpp",
        "Internal/VulkanAdapterInfo.cpp", "Commands/VulkanCommandListDebug.cpp",
        "Queries/VulkanQueryPool.cpp", "Commands/VulkanCommandListQueries.cpp",
        "Internal/VulkanDeviceLoss.cpp", "Internal/VulkanDeviceFault.cpp",
        "Internal/VulkanDeviceRetirement.cpp", "Internal/VulkanMemoryBudget.cpp",
        "Internal/VulkanPipelineCache.cpp", "Internal/VulkanPipelineCacheData.cpp",
    ):
        if not (ROOT / "Source/Engine/Systems/Renderer/RHI/Backends/Vulkan" / relative).is_file():
            fail(f"Vulkan graphics implementation unit is missing: {relative}", failures)
    for suite_file in ("VulkanPipelineTests.cpp", "VulkanDrawTests.cpp", "VulkanTriangleSmokeTests.cpp",
                       "VulkanDescriptorTests.cpp", "VulkanDescriptorDrawTests.cpp", "VulkanTextureSmokeTests.cpp",
                       "VulkanSwapchainTests.cpp", "VulkanWindowSmokeTests.cpp", "VulkanDiagnosticsTests.cpp",
                       "VulkanQueryTests.cpp", "VulkanTimestampSmokeTests.cpp",
                       "VulkanDeviceLossTests.cpp", "VulkanDeviceFaultTests.cpp",
                       "VulkanMemoryBudgetTests.cpp", "VulkanMemoryBudgetSmokeTests.cpp",
                       "VulkanPipelineCacheTests.cpp", "VulkanPipelineCacheDataTests.cpp",
                       "VulkanPipelineCacheConcurrencyTests.cpp", "VulkanPipelineCachePersistenceTests.cpp",
                       "VulkanPipelineCacheSmokeTests.cpp", "VulkanUploadArenaTests.cpp",
                       "VulkanUploadArenaSmokeTests.cpp", "VulkanReadbackArenaTests.cpp",
                       "VulkanReadbackArenaSmokeTests.cpp"):
        check_suite_is_compiled("RHIVulkan", suite_file, failures)
    check_suite_is_compiled("RHI", "RhiDiagnosticsTests.cpp", failures)
    check_suite_is_compiled("RHI", "RhiTimestampTests.cpp", failures)
    check_suite_is_compiled("RHI", "RhiDeviceDiagnosticsTests.cpp", failures)
    check_suite_is_compiled("RHI", "RhiMemoryBudgetTests.cpp", failures)
    check_suite_is_compiled("RHI", "RhiUploadArenaTests.cpp", failures)
    check_suite_is_compiled("RHI", "RhiReadbackArenaTests.cpp", failures)
    check_suite_is_compiled("RHI", "RhiReadbackFrameTests.cpp", failures)


def check_runtime_logging_contract(failures: list[str]) -> None:
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    dependencies_text = (ROOT / "cmake" / "Dependencies.cmake").read_text(encoding="utf-8", errors="ignore")
    main_source = (ROOT / "Source" / "main.cpp").read_text(encoding="utf-8", errors="ignore")
    log_header_path = ROOT / "Source" / "Engine" / "Logging" / "Log.h"
    log_source_path = ROOT / "Source" / "Engine" / "Logging" / "Log.cpp"

    if not log_header_path.is_file() or not log_source_path.is_file():
        fail("runtime logging implementation is missing", failures)
        return

    log_source = log_source_path.read_text(encoding="utf-8", errors="ignore")
    for fragment in (
        "GITHUB_REPOSITORY gabime/spdlog",
        "GIT_TAG v1.15.3",
        "add_library(spdlog::spdlog ALIAS SwimSpdlog)",
    ):
        if fragment not in dependencies_text:
            fail(f"Tungsten-style spdlog dependency contract is missing: {fragment}", failures)

    for fragment in (
        "spdlog::spdlog",
        "/SUBSYSTEM:CONSOLE",
    ):
        if fragment not in cmake_text:
            fail(f"release console/logging CMake contract is missing: {fragment}", failures)

    for forbidden in (
        "/SUBSYSTEM:windows",
        "/SUBSYSTEM:WINDOWS",
        "#pragma comment(linker",
    ):
        if forbidden in main_source:
            fail(f"release build can suppress the console again: {forbidden}", failures)

    for fragment in (
        "stdout_color_sink_mt",
        "basic_file_sink_mt",
        'ResolveExecutableDirectory() / "Logs"',
        '"swim_engine_log_"',
        "std::cout.rdbuf(coutBuffer.get())",
        "std::cerr.rdbuf(cerrBuffer.get())",
        "std::cerr.unsetf(std::ios::unitbuf)",
        "cerrWasUnitBuffered",
        "logger->flush_on(spdlog::level::info)",
    ):
        if fragment not in log_source:
            fail(f"runtime console/file logging contract is missing: {fragment}", failures)

    for fragment in (
        "Engine::Logging::Initialize()",
        "Engine::Logging::Shutdown()",
        "Unhandled startup/runtime exception",
    ):
        if fragment not in main_source:
            fail(f"process logging lifetime/error boundary is missing: {fragment}", failures)


def check_source_files_are_utf8(failures: list[str]) -> None:
    for source_root in (ROOT / "Source",):
        if not source_root.exists():
            continue
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES | {".glsl", ".hlsl", ".slang"}:
                continue
            try:
                source_text = path.read_text(encoding="utf-8")
            except UnicodeDecodeError as exc:
                fail(f"source file is not valid UTF-8: {path.relative_to(ROOT)} ({exc})", failures)
                continue

            for line_number, line in enumerate(source_text.splitlines(), start=1):
                if line.startswith(r"\t"):
                    fail(
                        f"source file contains serialized indentation escape at "
                        f"{path.relative_to(ROOT)}:{line_number}",
                        failures,
                    )

def check_include_case_and_sandbox_assets(failures: list[str]) -> None:
    # Windows resolves includes case-insensitively, Linux does not: every quoted include
    # of a first-party header must name the file with its exact case (SandBox.h vs
    # Sandbox.h broke the Linux build once).
    source_root = ROOT / "Source"
    names: dict[Path, set[str]] = {}

    def exact(relative: str) -> bool | None:
        current = source_root
        for part in relative.split("/"):
            if current not in names:
                if not current.is_dir():
                    return None
                names[current] = set(os.listdir(current))
            if part not in names[current]:
                lowered = {name.lower() for name in names[current]}
                return False if part.lower() in lowered else None
            current = current / part
        return True

    include = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
    for path in source_root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES | {".slang"}:
            continue
        for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
            match = include.match(line)
            if match and exact(match.group(1)) is False:
                fail(
                    f"include does not match the file's case: {path.relative_to(ROOT)}:{line_number} ({match.group(1)})",
                    failures,
                )

    # The sandbox loads this Sponza; without it only the light swarm appears.
    sponza = ROOT / "Assets" / "Models" / "Sponza" / "sponza-ktx-draco.glb"
    if (ROOT / "Assets").is_dir() and not sponza.is_file():
        fail("Assets/Models/Sponza/sponza-ktx-draco.glb is missing (the sandbox's Sponza source)", failures)

def check_retirement_boundaries(failures: list[str]) -> None:
    retired_types = re.compile(
        r"\b(?:InputManager|CommandSystem|SystemManager|EditorIpcBridge|SceneSerializer|SceneStorage|SceneSyncTracker|SceneToolingBridge"
        r"|VulkanRenderer|OpenGLRenderer|MeshPool|MaterialPool|TexturePool|FontPool|EditorCamera|CubeMapController)\b"
    )
    for path in (ROOT / "Source").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore").replace("\\", "/")
        code = re.sub(r"//[^\n]*", "", text)
        if retired_types.search(code) or re.search(r'#\s*include[^\n]*Deprecated/', text):
            fail(f"active source references a retired implementation: {path.relative_to(ROOT)}", failures)
    engine = (ROOT / "Source" / "Engine" / "SwimEngine.cpp").read_text(encoding="utf-8")
    pump = engine.find("platformSystem->PumpEvents(")
    advance = engine.find("inputSystem->AdvanceFrame();")
    fixed = engine.find("scene->FixedUpdatePhysics(")
    if engine.count("inputSystem->AdvanceFrame();") != 1 or not (0 <= pump < advance < fixed):
        fail("input snapshot must advance once after pumping events and before fixed simulation", failures)
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    for fragment in ("set(SWIM_COMMANDS_SOURCES",
                     "Source/Engine/Commands/CommandRegistry.cpp"):
        if fragment not in cmake:
            fail(f"command module ownership boundary is missing: {fragment}", failures)
    check_suite_is_compiled("Commands", "CommandRegistryTests.cpp", failures)


def check_render_graph_boundaries(failures: list[str]) -> None:
    renderer = ROOT / "Source/Engine/Systems/Renderer"
    # RenderGraph and the GPU residency layers built on it (Resources/, Geometry/)
    # share one boundary: backend-neutral RHI only, no scene/ECS or platform.
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials", "GpuMaterials", "Environment", "Lights", "ClusteredLighting", "Shadows", "ForwardPlus", "PostProcess", "Temporal", "ScreenSpace", "Particles", "Skinning", "Residency"):
        module_root = renderer / module
        if not module_root.is_dir():
            fail(f"modern renderer module is missing: {module_root.relative_to(ROOT)}", failures)
            continue
        for path in module_root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8")
            for include in re.findall(r'#\s*include\s*[<"]([^>"\n]+)', text):
                normalized = include.replace("\\", "/")
                if any(part in normalized for part in ("Backends/", "vulkan", "volk", "vk_mem_alloc", "entt", "Engine/Scene", "Engine/Platform", "Systems/Scene")):
                    fail(f"{module} leaks a backend, scene or platform dependency: {path.relative_to(ROOT)} -> {include}", failures)
    # The graph must stay below residency policy.
    for path in (renderer / "RenderGraph").rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
            r'#\s*include[^\n]*(Renderer/Geometry/|Renderer/Resources/)', path.read_text(encoding="utf-8")
        ):
            fail(f"RenderGraph must not depend on residency layers: {path.relative_to(ROOT)}", failures)
    check_suite_is_compiled("RenderGraph", "RenderGraphTransferTests.cpp", failures)
    check_suite_is_compiled("RenderResources", "GpuResourceRegistryTests.cpp", failures)
    check_suite_is_compiled("RenderGeometry", "GeometryHeapTests.cpp", failures)
    check_suite_is_compiled("RenderResidency", "AssetResidencyServiceTests.cpp", failures)
    check_suite_is_compiled("RenderScene", "GpuSceneTests.cpp", failures)
    check_suite_is_compiled("Scene/Ecs", "RenderExtractionTests.cpp", failures)
    check_suite_is_compiled("RenderVisibility", "VisibilityReferenceTests.cpp", failures)
    check_suite_is_compiled("RenderVisibility", "GpuVisibilityTests.cpp", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanIndirectDrawTests.cpp", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanGpuVisibilitySmokeTests.cpp", failures)
    check_suite_is_compiled("RenderVisibility", "OcclusionTests.cpp", failures)
    check_suite_is_compiled("RenderMaterials", "MaterialTemplateTests.cpp", failures)
    check_suite_is_compiled("RenderMaterials", "StandardPbrTests.cpp", failures)
    check_suite_is_compiled("RenderGpuMaterials", "GpuMaterialTableTests.cpp", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanStandardMaterialSmokeTests.cpp", failures)
    # Items 59/60: the GPU material table sits above Materials and the GPU layers; nothing
    # below it (and not Materials itself) may include it. The standard PBR model keeps its
    # CPU definition next to the shader.
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Renderer/GpuMaterials/', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on the GPU material table: {path.relative_to(ROOT)}", failures)
    for relative in ("Source/Engine/Systems/Renderer/Materials/StandardPbr.cpp", "Source/Shaders/Slang/Materials/StandardPbr.slang"):
        if not (ROOT / relative).is_file():
            fail(f"Standard PBR definition is missing: {relative}", failures)
    if "Renderer/GpuMaterials/*.cpp" not in (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"):
        fail("GpuMaterials sources must compile with the backend-neutral renderer source list", failures)
    # Material templates (item 58) are pure layout/data: no RHI, graph or GPU layer.
    for path in (renderer / "Materials").rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
            r'#\s*include[^\n]*(Renderer/RHI/|Renderer/RenderGraph/|Renderer/GpuScene/|Renderer/Visibility/)', path.read_text(encoding="utf-8")
        ):
            fail(f"Materials must stay a data layer: {path.relative_to(ROOT)}", failures)
    if "Renderer/Materials/*.cpp" not in (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"):
        fail("Materials sources must compile with the backend-neutral renderer source list", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanGpuOcclusionSmokeTests.cpp", failures)
    # Items 61/62: image-based lighting sits above RenderGraph and Materials. The layers
    # below never include it, it never reaches the GPU Scene, visibility, the GPU
    # material table or residency, and it keeps a CPU definition next to its shaders.
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials", "GpuMaterials"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Renderer/Environment/', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on the environment layer: {path.relative_to(ROOT)}", failures)
    for path in (renderer / "Environment").rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
            r'#\s*include[^\n]*(Renderer/GpuScene/|Renderer/Visibility/|Renderer/GpuMaterials/|Renderer/Residency/|Engine/Assets/)',
            path.read_text(encoding="utf-8"),
        ):
            fail(f"Environment must stay above RenderGraph/Materials only: {path.relative_to(ROOT)}", failures)
    for relative in ("Environment/EnvironmentMath.cpp", "Environment/EnvironmentReference.cpp", "Environment/EnvironmentBuilder.cpp"):
        if not (renderer / relative).is_file():
            fail(f"Environment unit is missing: Renderer/{relative}", failures)
    for shader in ("EnvironmentCommon", "EnvironmentSky", "EnvironmentDownsample", "EnvironmentPrefilter", "EnvironmentIrradiance",
                   "EnvironmentBrdfLut", "EnvironmentLighting"):
        if not (ROOT / f"Source/Shaders/Slang/Environment/{shader}.slang").is_file():
            fail(f"Environment shader is missing: Shaders/Slang/Environment/{shader}.slang", failures)
    if "Renderer/Environment/*.cpp" not in (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"):
        fail("Environment sources must compile with the backend-neutral renderer source list", failures)
    check_suite_is_compiled("RenderEnvironment", "EnvironmentMathTests.cpp", failures)
    check_suite_is_compiled("RenderEnvironment", "EnvironmentReferenceTests.cpp", failures)
    check_suite_is_compiled("RenderEnvironment", "EnvironmentBuilderTests.cpp", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanEnvironmentSmokeTests.cpp", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanPbrGallerySmokeTests.cpp", failures)
    check_suite_is_compiled("ShaderCompiler", "ShaderEnvironmentLayoutTests.cpp", failures)
    # Item 63: the GPU light buffer sits beside GpuMaterials (it reuses the GPU Scene's
    # record-upload helpers). Nothing below it includes it, it never reaches
    # visibility, environment or residency, and its CPU definition sits next to the shader.
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials", "GpuMaterials", "Environment"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Renderer/Lights/', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on the GPU light buffer: {path.relative_to(ROOT)}", failures)
    for path in (renderer / "Lights").rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
            r'#\s*include[^\n]*(Renderer/Visibility/|Renderer/Environment/|Renderer/GpuMaterials/|Renderer/Residency/|Engine/Assets/)',
            path.read_text(encoding="utf-8"),
        ):
            fail(f"Lights must stay beside the GPU Scene layers: {path.relative_to(ROOT)}", failures)
    for relative in ("Lights/LightMath.cpp", "Lights/GpuLightBuffer.cpp", "Lights/GpuLightRecord.h"):
        if not (renderer / relative).is_file():
            fail(f"GPU light unit is missing: Renderer/{relative}", failures)
    if not (ROOT / "Source/Shaders/Slang/Lights/GpuLightRecords.slang").is_file():
        fail("GPU light shader records (Shaders/Slang/Lights/GpuLightRecords.slang) are missing", failures)
    if "Renderer/Lights/*.cpp" not in (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"):
        fail("Lights sources must compile with the backend-neutral renderer source list", failures)
    check_suite_is_compiled("RenderLights", "LightMathTests.cpp", failures)
    check_suite_is_compiled("RenderLights", "GpuLightBufferTests.cpp", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanGpuLightSmokeTests.cpp", failures)
    check_suite_is_compiled("ShaderCompiler", "ShaderLightLayoutTests.cpp", failures)
    # Items 64/65/68: clustered light assignment sits above the GPU light buffer.
    # Nothing below it includes it, it never reaches visibility, environment,
    # materials or residency, and every GPU pass keeps its CPU definition.
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials", "GpuMaterials", "Environment", "Lights"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Renderer/ClusteredLighting/', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on clustered lighting: {path.relative_to(ROOT)}", failures)
    for path in (renderer / "ClusteredLighting").rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
            r'#\s*include[^\n]*(Renderer/Visibility/|Renderer/Environment/|Renderer/GpuMaterials/|Renderer/Residency/|Engine/Assets/)',
            path.read_text(encoding="utf-8"),
        ):
            fail(f"ClusteredLighting must depend only on the light buffer and the graph: {path.relative_to(ROOT)}", failures)
    for relative in ("ClusteredLighting/ClusterGrid.cpp", "ClusteredLighting/ClusterReference.cpp",
                     "ClusteredLighting/ClusteredLightAssigner.cpp", "ClusteredLighting/ClusterBindings.h"):
        if not (renderer / relative).is_file():
            fail(f"clustered lighting unit is missing: Renderer/{relative}", failures)
    for shader in ("ClusterGrid", "ClusterLightCull", "ClusterBounds", "ClusterAssign", "ClusterScan", "ClusterHeatmap", "ClusteredLighting"):
        if not (ROOT / f"Source/Shaders/Slang/ClusteredLighting/{shader}.slang").is_file():
            fail(f"clustered lighting shader is missing: Shaders/Slang/ClusteredLighting/{shader}.slang", failures)
    if "Renderer/ClusteredLighting/*.cpp" not in (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"):
        fail("ClusteredLighting sources must compile with the backend-neutral renderer source list", failures)
    check_suite_is_compiled("RenderClusteredLighting", "ClusterGridTests.cpp", failures)
    check_suite_is_compiled("RenderClusteredLighting", "ClusteredLightAssignerTests.cpp", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanClusteredLightingSmokeTests.cpp", failures)
    check_suite_is_compiled("ShaderCompiler", "ShaderClusterLayoutTests.cpp", failures)
    # Items 66/67/69: Clustered Forward+ is the consumer layer above visibility,
    # materials, environment, lights and clusters. Nothing below it (or residency)
    # includes it, it never reaches residency or assets, and its CPU definition,
    # shaders and tests stay in place.
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials", "GpuMaterials", "Environment", "Lights",
                   "ClusteredLighting", "Residency"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Renderer/ForwardPlus/', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on Clustered Forward+: {path.relative_to(ROOT)}", failures)
    for path in (renderer / "ForwardPlus").rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
            r'#\s*include[^\n]*(Renderer/Residency/|Engine/Assets/|Engine/IO/|Engine/Jobs/)', path.read_text(encoding="utf-8")
        ):
            fail(f"ForwardPlus must not depend on residency, assets, IO or jobs: {path.relative_to(ROOT)}", failures)
    for relative in ("ForwardPlus/ForwardPlusReference.cpp", "ForwardPlus/ForwardPlusRenderer.cpp", "ForwardPlus/ForwardPlusBindings.h",
                     "ForwardPlus/ForwardPlusRecords.h", "ForwardPlus/StandardVertex.h"):
        if not (renderer / relative).is_file():
            fail(f"Clustered Forward+ unit is missing: Renderer/{relative}", failures)
    for shader in ("ForwardPlusRecords", "ClusteredForward", "ForwardTransparentSort"):
        if not (ROOT / f"Source/Shaders/Slang/ForwardPlus/{shader}.slang").is_file():
            fail(f"Clustered Forward+ shader is missing: Shaders/Slang/ForwardPlus/{shader}.slang", failures)
    if "Renderer/ForwardPlus/*.cpp" not in (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"):
        fail("ForwardPlus sources must compile with the backend-neutral renderer source list", failures)
    if "FORWARD_TRANSPARENT=1" not in (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"):
        fail("the transparent Forward+ variant must be compiled from ClusteredForward.slang", failures)
    check_suite_is_compiled("RenderForwardPlus", "ForwardPlusReferenceTests.cpp", failures)
    check_suite_is_compiled("RenderForwardPlus", "ForwardPlusRendererTests.cpp", failures)
    check_suite_is_compiled("RenderForwardPlus", "ForwardPlusFixtureTests.cpp", failures)
    check_suite_is_compiled("RenderResidency", "StandardVertexLayoutTests.cpp", failures)
    check_suite_is_compiled("RenderClusteredLighting", "ClusterScalingTests.cpp", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanForwardPlusSmokeTests.cpp", failures)
    check_suite_is_compiled("ShaderCompiler", "ShaderForwardPlusLayoutTests.cpp", failures)
    # Items 70-72: shadows sit above visibility, GPU Scene, geometry, materials and
    # lights, and below Forward+ (which samples the atlas). The layers below never
    # include them, they never reach Forward+, clustering, environment or residency,
    # and their CPU definitions, shaders and tests stay in place.
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials", "GpuMaterials", "Environment", "Lights",
                   "ClusteredLighting", "Residency"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Renderer/Shadows/', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on shadows: {path.relative_to(ROOT)}", failures)
    for path in (renderer / "Shadows").rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
            r'#\s*include[^\n]*(Renderer/ForwardPlus/|Renderer/ClusteredLighting/|Renderer/Environment/|Renderer/Residency/|Engine/Assets/|Engine/IO/|Engine/Jobs/)',
            path.read_text(encoding="utf-8"),
        ):
            fail(f"Shadows must not depend on Forward+, clustering, environment, residency, assets, IO or jobs: {path.relative_to(ROOT)}", failures)
    for relative in ("Shadows/ShadowMath.cpp", "Shadows/ShadowAtlasAllocator.cpp", "Shadows/ShadowPlanner.cpp", "Shadows/ShadowRenderer.cpp",
                     "Shadows/ShadowRecords.h", "Shadows/ShadowBindings.h", "Shadows/ShadowGraphResources.h"):
        if not (renderer / relative).is_file():
            fail(f"shadow unit is missing: Renderer/{relative}", failures)
    for shader in ("ShadowRecords", "ShadowDepth"):
        if not (ROOT / f"Source/Shaders/Slang/Shadows/{shader}.slang").is_file():
            fail(f"shadow shader is missing: Shaders/Slang/Shadows/{shader}.slang", failures)
    if "Renderer/Shadows/*.cpp" not in (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"):
        fail("Shadows sources must compile with the backend-neutral renderer source list", failures)
    if "SHADOW_ALPHA_TEST=1" not in (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"):
        fail("the alpha-tested shadow variant must be compiled from ShadowDepth.slang", failures)
    if "Shadows/ShadowRecords.slang" not in (ROOT / "Source/Shaders/Slang/ForwardPlus/ClusteredForward.slang").read_text(encoding="utf-8"):
        fail("Clustered Forward+ must sample shadows through Shadows/ShadowRecords.slang", failures)
    for test in ("ShadowMathTests.cpp", "ShadowAtlasTests.cpp", "ShadowPlannerTests.cpp", "ShadowSamplingTests.cpp", "ShadowRendererTests.cpp"):
        check_suite_is_compiled("RenderShadows", test, failures)
    check_suite_is_compiled("RHIVulkan", "VulkanShadowSmokeTests.cpp", failures)
    check_suite_is_compiled("ShaderCompiler", "ShaderShadowLayoutTests.cpp", failures)
    check_suite_is_compiled("ShaderCompiler", "ShaderGpuAvBudgetTests.cpp", failures)
    # Items 73-74: post-processing consumes only the render graph and RHI contract.
    # No other renderer module includes it, and it includes no other renderer module.
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials", "GpuMaterials", "Environment", "Lights",
                   "ClusteredLighting", "Shadows", "ForwardPlus", "Temporal", "ScreenSpace", "Particles", "Residency"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Renderer/PostProcess/', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on post-processing: {path.relative_to(ROOT)}", failures)
    for path in (renderer / "PostProcess").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        for include in re.findall(r'#\s*include\s*[<"]([^>"\n]+)', path.read_text(encoding="utf-8")):
            if "Renderer/" in include and not re.search(r"Renderer/(PostProcess|RenderGraph|RHI)/", include):
                fail(f"PostProcess may include only PostProcess, RenderGraph and RHI headers: {path.relative_to(ROOT)} -> {include}", failures)
            if re.search(r"Engine/(Assets|IO|Jobs)/", include):
                fail(f"PostProcess must not depend on assets, IO or jobs: {path.relative_to(ROOT)} -> {include}", failures)
    for relative in ("PostProcess/PostProcessReference.cpp", "PostProcess/PostProcessor.cpp", "PostProcess/PostProcessSettings.h",
                     "PostProcess/PostProcessRecords.h", "PostProcess/PostProcessBindings.h", "PostProcess/PostProcessGraphResources.h"):
        if not (renderer / relative).is_file():
            fail(f"post-processing unit is missing: Renderer/{relative}", failures)
    for shader in ("PostProcessRecords", "PostHistogram", "PostExposure", "PostBloomDownsample", "PostBloomUpsample", "PostComposite"):
        if not (ROOT / f"Source/Shaders/Slang/PostProcess/{shader}.slang").is_file():
            fail(f"post-processing shader is missing: Shaders/Slang/PostProcess/{shader}.slang", failures)
    post_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    if "Renderer/PostProcess/*.cpp" not in post_cmake:
        fail("PostProcess sources must compile with the backend-neutral renderer source list", failures)
    if "POST_OUTPUT_HDR=1" not in post_cmake:
        fail("the HDR composite variant must be compiled from PostComposite.slang", failures)
    check_suite_is_compiled("RenderPostProcess", "PostProcessReferenceTests.cpp", failures)
    check_suite_is_compiled("RenderPostProcess", "PostProcessorTests.cpp", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanPostProcessSmokeTests.cpp", failures)
    check_suite_is_compiled("ShaderCompiler", "ShaderPostProcessLayoutTests.cpp", failures)
    # Item 75: temporal anti-aliasing consumes only the render graph and RHI contract,
    # and no other renderer module includes it (the Forward+ motion vectors it reads are
    # plain graph textures).
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials", "GpuMaterials", "Environment", "Lights",
                   "ClusteredLighting", "Shadows", "ForwardPlus", "PostProcess", "ScreenSpace", "Particles", "Residency"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Renderer/Temporal/', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on temporal anti-aliasing: {path.relative_to(ROOT)}", failures)
    for path in (renderer / "Temporal").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        for include in re.findall(r'#\s*include\s*[<"]([^>"\n]+)', path.read_text(encoding="utf-8")):
            if "Renderer/" in include and not re.search(r"Renderer/(Temporal|RenderGraph|RHI)/", include):
                fail(f"Temporal may include only Temporal, RenderGraph and RHI headers: {path.relative_to(ROOT)} -> {include}", failures)
    for relative in ("Temporal/TemporalReference.cpp", "Temporal/TemporalAntiAliasing.cpp", "Temporal/TemporalSettings.h",
                     "Temporal/TemporalRecords.h", "Temporal/TemporalBindings.h", "Temporal/TemporalGraphResources.h"):
        if not (renderer / relative).is_file():
            fail(f"temporal anti-aliasing unit is missing: Renderer/{relative}", failures)
    for shader in ("TemporalRecords", "TemporalResolve"):
        if not (ROOT / f"Source/Shaders/Slang/Temporal/{shader}.slang").is_file():
            fail(f"temporal shader is missing: Shaders/Slang/Temporal/{shader}.slang", failures)
    if "Renderer/Temporal/*.cpp" not in post_cmake:
        fail("Temporal sources must compile with the backend-neutral renderer source list", failures)
    if "SV_Target2" not in (ROOT / "Source/Shaders/Slang/ForwardPlus/ClusteredForward.slang").read_text(encoding="utf-8"):
        fail("the opaque Forward+ program must write motion vectors (SV_Target2)", failures)
    check_suite_is_compiled("RenderTemporal", "TemporalReferenceTests.cpp", failures)
    check_suite_is_compiled("RenderTemporal", "TemporalAntiAliasingTests.cpp", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanTemporalSmokeTests.cpp", failures)
    check_suite_is_compiled("ShaderCompiler", "ShaderTemporalLayoutTests.cpp", failures)
    # Item 76: screen-space AO and fog consume only the render graph and RHI contract; the
    # Forward+ normal and indirect targets they read are plain graph textures.
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials", "GpuMaterials", "Environment", "Lights",
                   "ClusteredLighting", "Shadows", "ForwardPlus", "PostProcess", "Temporal", "Particles", "Residency"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Renderer/ScreenSpace/', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on the screen-space effects: {path.relative_to(ROOT)}", failures)
    for path in (renderer / "ScreenSpace").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        for include in re.findall(r'#\s*include\s*[<"]([^>"\n]+)', path.read_text(encoding="utf-8")):
            if "Renderer/" in include and not re.search(r"Renderer/(ScreenSpace|RenderGraph|RHI)/", include):
                fail(f"ScreenSpace may include only ScreenSpace, RenderGraph and RHI headers: {path.relative_to(ROOT)} -> {include}", failures)
    for relative in ("ScreenSpace/ScreenSpaceReference.cpp", "ScreenSpace/ScreenSpaceEffects.cpp", "ScreenSpace/ScreenSpaceSettings.h",
                     "ScreenSpace/ScreenSpaceRecords.h", "ScreenSpace/ScreenSpaceBindings.h", "ScreenSpace/ScreenSpaceGraphResources.h"):
        if not (renderer / relative).is_file():
            fail(f"screen-space unit is missing: Renderer/{relative}", failures)
    for shader in ("ScreenSpaceRecords", "ScreenSpaceAo", "ScreenSpaceBlur", "ScreenSpaceComposite", "ScreenSpaceReflection"):
        if not (ROOT / f"Source/Shaders/Slang/ScreenSpace/{shader}.slang").is_file():
            fail(f"screen-space shader is missing: Shaders/Slang/ScreenSpace/{shader}.slang", failures)
    if "Renderer/ScreenSpace/*.cpp" not in post_cmake:
        fail("ScreenSpace sources must compile with the backend-neutral renderer source list", failures)
    if "SV_Target4" not in (ROOT / "Source/Shaders/Slang/ForwardPlus/ClusteredForward.slang").read_text(encoding="utf-8"):
        fail("the opaque Forward+ program must write the normal and indirect targets (SV_Target3-4)", failures)
    if "SV_Target6" not in (ROOT / "Source/Shaders/Slang/ForwardPlus/ClusteredForward.slang").read_text(encoding="utf-8"):
        fail("the opaque Forward+ program must write the reflectance and specular targets (SV_Target5-6)", failures)
    check_suite_is_compiled("RenderScreenSpace", "ScreenSpaceReferenceTests.cpp", failures)
    check_suite_is_compiled("RenderScreenSpace", "ScreenSpaceEffectsTests.cpp", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanScreenSpaceSmokeTests.cpp", failures)
    check_suite_is_compiled("ShaderCompiler", "ShaderScreenSpaceLayoutTests.cpp", failures)
    # Item 77: GPU particles consume only the render graph, the RHI contract and the
    # generational handles of Resources/; no other renderer module includes them.
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials", "GpuMaterials", "Environment", "Lights",
                   "ClusteredLighting", "Shadows", "ForwardPlus", "PostProcess", "Temporal", "ScreenSpace", "Residency"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Renderer/Particles/', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on GPU particles: {path.relative_to(ROOT)}", failures)
    for path in (renderer / "Particles").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        for include in re.findall(r'#\s*include\s*[<"]([^>"\n]+)', path.read_text(encoding="utf-8")):
            if "Renderer/" in include and not re.search(r"Renderer/(Particles|RenderGraph|RHI|Resources)/", include):
                fail(f"Particles may include only Particles, RenderGraph, RHI and Resources headers: {path.relative_to(ROOT)} -> {include}", failures)
    for relative in ("Particles/ParticleSettings.h", "Particles/ParticleRecords.h", "Particles/ParticleBindings.h",
                     "Particles/ParticleReference.cpp", "Particles/ParticleSystem.cpp", "Particles/ParticleGraphResources.h"):
        if not (renderer / relative).is_file():
            fail(f"particle unit is missing: Renderer/{relative}", failures)
    for shader in ("ParticleRecords", "ParticleSimulate", "ParticleEmit", "ParticleCompact", "ParticleFinalize", "ParticleRender"):
        if not (ROOT / f"Source/Shaders/Slang/Particles/{shader}.slang").is_file():
            fail(f"particle shader is missing: Shaders/Slang/Particles/{shader}.slang", failures)
    if "Renderer/Particles/*.cpp" not in post_cmake:
        fail("Particles sources must compile with the backend-neutral renderer source list", failures)
    check_suite_is_compiled("RenderParticles", "ParticleReferenceTests.cpp", failures)
    check_suite_is_compiled("RenderParticles", "ParticleSystemTests.cpp", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanParticleSmokeTests.cpp", failures)
    check_suite_is_compiled("ShaderCompiler", "ShaderParticleLayoutTests.cpp", failures)
    # Item 78: GPU skinning writes GeometryHeap output meshes, so it may include Geometry,
    # GpuScene's RenderBounds and Forward+'s StandardVertex layout besides the graph, the
    # RHI contract and Resources/ handles; no other renderer module includes it.
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials", "GpuMaterials", "Environment", "Lights",
                   "ClusteredLighting", "Shadows", "ForwardPlus", "PostProcess", "Temporal", "ScreenSpace", "Particles", "Residency"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Renderer/Skinning/', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on GPU skinning: {path.relative_to(ROOT)}", failures)
    for path in (renderer / "Skinning").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        for include in re.findall(r'#\s*include\s*[<"]([^>"\n]+)', path.read_text(encoding="utf-8")):
            if "Renderer/" in include and not re.search(
                r"Renderer/(Skinning|RenderGraph|RHI|Resources|Geometry)/|Renderer/GpuScene/RenderBounds\.h|Renderer/ForwardPlus/StandardVertex\.h", include
            ):
                fail(f"Skinning may include only Skinning, RenderGraph, RHI, Resources, Geometry, RenderBounds.h and StandardVertex.h: {path.relative_to(ROOT)} -> {include}", failures)
            if re.search(r"Systems/Animation/", include):
                fail(f"Skinning takes palettes, not animators: {path.relative_to(ROOT)} -> {include}", failures)
    for relative in ("Skinning/SkinningRecords.h", "Skinning/SkinningBindings.h", "Skinning/SkinningReference.cpp",
                     "Skinning/SkinningSystem.cpp", "Skinning/SkinningGraphResources.h"):
        if not (renderer / relative).is_file():
            fail(f"skinning unit is missing: Renderer/{relative}", failures)
    for shader in ("SkinningRecords", "Skinning"):
        if not (ROOT / f"Source/Shaders/Slang/Skinning/{shader}.slang").is_file():
            fail(f"skinning shader is missing: Shaders/Slang/Skinning/{shader}.slang", failures)
    if "Renderer/Skinning/*.cpp" not in post_cmake:
        fail("Skinning sources must compile with the backend-neutral renderer source list", failures)
    if "PreviousVertexOffset" not in (ROOT / "Source/Shaders/Slang/ForwardPlus/ClusteredForward.slang").read_text(encoding="utf-8"):
        fail("the opaque Forward+ program must read skinned previous positions (GpuInstanceRecord::PreviousVertexOffset)", failures)
    check_suite_is_compiled("RenderSkinning", "SkinningReferenceTests.cpp", failures)
    check_suite_is_compiled("RenderSkinning", "SkinningSystemTests.cpp", failures)
    check_suite_is_compiled("RHIVulkan", "VulkanSkinningSmokeTests.cpp", failures)
    check_suite_is_compiled("ShaderCompiler", "ShaderSkinningLayoutTests.cpp", failures)
    # Item 78: the CPU animation runtime (Systems/Animation) reads plain asset structs and
    # may use Jobs; it knows nothing about renderers, scenes, platforms or EnTT, and no
    # renderer module includes it (skinning takes palettes).
    animation = ROOT / "Source/Engine/Systems/Animation"
    if not animation.is_dir():
        fail("the animation runtime (Source/Engine/Systems/Animation) is missing", failures)
    else:
        for path in animation.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            for include in re.findall(r'#\s*include\s*[<"]([^>"\n]+)', path.read_text(encoding="utf-8")):
                if include.startswith("Engine/") and not re.match(
                    r"Engine/(Systems/Animation/|Jobs/|Assets/(AnimationClipAsset|SkeletonAsset|AssetMath)\.h)", include
                ):
                    fail(f"Animation may include only Animation, Jobs and the skeleton/clip asset structs: {path.relative_to(ROOT)} -> {include}", failures)
                if any(part in include for part in ("entt", "glm", "SDL", "vulkan")):
                    fail(f"Animation must not depend on {include}: {path.relative_to(ROOT)}", failures)
        for relative in ("AnimationMath.cpp", "Skeleton.cpp", "AnimationClip.cpp", "Animator.cpp", "SkeletonInstance.cpp", "AnimationUpdate.cpp"):
            if not (animation / relative).is_file():
                fail(f"animation unit is missing: Systems/Animation/{relative}", failures)
    for module_root in [renderer / m for m in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "ForwardPlus", "Skinning", "Particles")]:
        for path in module_root.rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Systems/Animation/', path.read_text(encoding="utf-8")
            ):
                fail(f"renderer modules must not depend on the animation runtime: {path.relative_to(ROOT)}", failures)
    if "SWIM_ANIMATION_SOURCES" not in post_cmake or "SWIM_ANIMATION_SOURCES" not in read_tests_cmake():
        fail("the animation runtime must compile into SwimTests through SWIM_ANIMATION_SOURCES", failures)
    check_suite_is_compiled("Animation", "AnimationClipTests.cpp", failures)
    check_suite_is_compiled("Animation", "AnimatorTests.cpp", failures)
    check_suite_is_compiled("AssetCompiler", "AnimationAssetTests.cpp", failures)
    # GPU visibility (culling, LOD, binning, indirect commands) sits above the GPU
    # Scene; the layers below never include it, and it never reaches residency.
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Renderer/Visibility/', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on GPU visibility: {path.relative_to(ROOT)}", failures)
    if not (renderer / "Visibility/VisibilityReference.cpp").is_file():
        fail("GPU visibility CPU reference (Visibility/VisibilityReference.cpp) is missing", failures)
    # Items 50/51: the HZB and occlusion keep a CPU definition next to the GPU path,
    # and the canonical depth convention is declared once.
    for relative in ("Visibility/HzbPyramid.cpp", "Visibility/HzbBuilder.cpp", "Visibility/DepthConvention.h"):
        if not (renderer / relative).is_file():
            fail(f"GPU occlusion unit is missing: Renderer/{relative}", failures)
    if not (ROOT / "Source/Shaders/Slang/GpuScene/HzbReduce.slang").is_file():
        fail("HZB reduction shader (Shaders/Slang/GpuScene/HzbReduce.slang) is missing", failures)
    # The GPU Scene sits above Resources and below residency/extraction: the lower
    # layers never include it, and it never reaches assets, residency or EnTT.
    for module in ("RenderGraph", "Resources", "Geometry"):
        for path in (renderer / module).rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and re.search(
                r'#\s*include[^\n]*Renderer/GpuScene/', path.read_text(encoding="utf-8")
            ):
                fail(f"{module} must not depend on the GPU Scene: {path.relative_to(ROOT)}", failures)
    # Residency (Assets/IO/Jobs aware) sits above the GPU layers; they never see it.
    for module in ("RenderGraph", "Resources", "Geometry", "GpuScene", "Visibility", "Materials", "GpuMaterials", "Environment", "Lights", "ClusteredLighting", "Shadows", "ForwardPlus", "PostProcess", "Temporal", "ScreenSpace", "Particles", "Skinning"):
        for path in (renderer / module).rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8")
            if re.search(r'#\s*include[^\n]*(Renderer/Residency/|Engine/Assets/|Engine/IO/|Engine/Jobs/)', text):
                fail(f"{module} must not depend on asset residency, assets, IO or jobs: {path.relative_to(ROOT)}", failures)
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    if "SWIM_RENDER_RESIDENCY_SOURCES" not in cmake_text or "SWIM_RENDER_RESIDENCY_SOURCES" not in read_tests_cmake():
        fail("Residency sources must stay a separate, dependency-gated source list", failures)
    if "Renderer/GpuScene/*.cpp" not in cmake_text:
        fail("GpuScene sources must compile with the backend-neutral renderer source list", failures)
    if "Renderer/Visibility/*.cpp" not in cmake_text:
        fail("Visibility sources must compile with the backend-neutral renderer source list", failures)
    # EnTT -> GPU Scene extraction stays testable: its core files use only EnTT,
    # the TransformSystem and the GPU Scene; the Transform/Scene runtime adapter
    # lives in RenderExtraction/Runtime and is compiled by SwimEngine alone.
    extraction = ROOT / "Source/Engine/Systems/Scene/RenderExtraction"
    if not (extraction / "RenderExtractor.cpp").is_file():
        fail("EnTT render extraction (Scene/RenderExtraction/RenderExtractor.cpp) is missing", failures)
    for path in extraction.glob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        for include in re.findall(r'#\s*include\s*[<"]([^>"\n]+)', path.read_text(encoding="utf-8")):
            if any(part in include for part in ("Backends/", "Components/Transform.h", "Scene/Scene.h", "Renderer/Vulkan", "PCH.h")):
                fail(f"render extraction core must not include {include}: {path.relative_to(ROOT)}", failures)
    if "SWIM_SCENE_RENDER_EXTRACTION_SOURCES" not in cmake_text or "SWIM_SCENE_RENDER_EXTRACTION_SOURCES" not in read_tests_cmake():
        fail("render extraction sources must be listed for SwimTests' EnTT suites", failures)
    for path in (renderer / "RHI").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        if re.search(r'#\s*include[^\n]*RenderGraph/', path.read_text(encoding="utf-8")):
            fail(f"RHI must not depend on higher-level RenderGraph: {path.relative_to(ROOT)}", failures)


def check_phase22_23_runtime(failures: list[str]) -> None:
    """Phase 22 (no OpenGL, no editor; state, clock, tags, behaviours) and Phase 23 (assembled runtime)."""
    for retired in (
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "OpenGL",
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Vulkan",
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core",
        ROOT / "Source" / "Engine" / "Systems" / "Editor",
        ROOT / "Source" / "Engine" / "Components" / "ObjectTag.h",
    ):
        if retired.exists():
            fail(f"retired Phase 22/23 code is back in the active tree: {retired.relative_to(ROOT)}", failures)
    for path in (ROOT / "Source").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        code = re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8", errors="ignore"))
        for forbidden in ("EngineState::Editing", "GizmoSystem", "GraphicsBackend::OpenGL"):
            if forbidden in code:
                fail(f"removed editor/OpenGL symbol returned: {forbidden} in {path.relative_to(ROOT)}", failures)

    for relative in (
        "Source/Engine/Runtime/EngineStateMachine.h",
        "Source/Engine/Runtime/SimulationClock.h",
        "Source/Engine/Runtime/UiRuntime.h",
        "Source/Engine/Components/Tags.h",
        "Source/Engine/Systems/Camera/FlyCameraController.h",
        "Source/Engine/Systems/Renderer/Runtime/FrameRenderer.h",
        "Source/Engine/Systems/Renderer/Runtime/RenderDevice.h",
        "Source/Engine/Systems/Scene/RenderExtraction/Runtime/SceneRenderBridge.h",
        "Source/Game/Scenes/Sandbox.h",
        "Source/Game/Ui/SandboxHud.h",
        "Resources/Fonts/DejaVuSans.ttf",
        "docs/EngineRuntime.md",
        "Deprecated/README.md",
    ):
        if not (ROOT / relative).is_file():
            fail(f"Phase 22/23 runtime file is missing: {relative}", failures)
    for suite, file_name in (
        ("Engine", "EngineStateMachineTests.cpp"),
        ("Engine", "SimulationClockTests.cpp"),
        ("Engine", "SceneRuntimeTests.cpp"),
        ("Engine", "UiRuntimeTests.cpp"),
        ("Game", "SandboxRuntimeTests.cpp"),
    ):
        check_suite_is_compiled(suite, file_name, failures)

    sync_script = ROOT / "scripts" / "sync-findings-doc.py"
    if sync_script.is_file():
        result = subprocess.run([sys.executable, str(sync_script), "--check"], capture_output=True, text=True)
        if result.returncode != 0:
            fail("docs/EngineRuntime.md findings differ from Source/Game/Findings.cpp (run scripts/sync-findings-doc.py)", failures)
    else:
        fail("scripts/sync-findings-doc.py is missing", failures)


def main() -> int:
    failures: list[str] = []

    check_generated_visual_studio_files(failures)
    check_vendored_dependencies(failures)
    check_required_cmake_files(failures)
    check_build_workflow(failures)
    check_machine_specific_paths(failures)
    check_legacy_library_includes(failures)
    check_tungsten_style_cmake(failures)
    check_preserved_build_contract(failures)
    check_modern_cmake_dependency_compatibility(failures)
    check_windows_compile_contract_and_warning_hygiene(failures)
    check_foundation_architecture_boundaries(failures)
    check_phase2_engine_architecture(failures)
    check_phase3_job_architecture(failures)
    check_phase20_text_dependencies(failures)
    check_phase20_ui_rendering(failures)
    check_phase20_controls_and_canvases(failures)
    check_phase3_io_architecture(failures)
    check_phase3_memory_architecture(failures)
    check_phase4_asset_architecture(failures)
    check_phase5_scene_architecture(failures)
    check_phase6_physics_architecture(failures)
    check_phase7_shader_architecture(failures)
    check_phase8_rhi_type_architecture(failures)
    check_phase9_vulkan_rhi_architecture(failures)
    check_render_graph_boundaries(failures)
    check_retirement_boundaries(failures)
    check_phase22_23_runtime(failures)
    check_runtime_logging_contract(failures)
    check_source_files_are_utf8(failures)
    check_include_case_and_sandbox_assets(failures)

    if failures:
        print("Swim Engine build-layout verification FAILED:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("Swim Engine build-layout verification passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
