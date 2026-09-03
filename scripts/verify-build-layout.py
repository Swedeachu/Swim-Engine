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
    "cmake/MemoryDependencies.cmake",
    "cmake/PlatformDependencies.cmake",
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
        'SWIM_SOLUTION_FOLDER_ENGINE_MODULES "Engine Modules"',
        'SWIM_SOLUTION_FOLDER_TESTS "Tests"',
        'SWIM_SOLUTION_FOLDER_EXAMPLES "Examples"',
        'SWIM_SOLUTION_FOLDER_THIRD_PARTY "Third Party"',
        "function(swim_set_solution_folder",
    ):
        if fragment not in solution_layout_text:
            fail(f"Visual Studio solution layout contract is missing: {fragment}", failures)

    for fragment in (
        'include(cmake/SolutionLayout.cmake)',
        'swim_set_solution_folder(SwimPlatform "${SWIM_SOLUTION_FOLDER_ENGINE_MODULES}")',
        'swim_set_solution_folder(SwimInput "${SWIM_SOLUTION_FOLDER_ENGINE_MODULES}")',
        'add_library(Swim::Core ALIAS SwimCore)',
        'swim_set_solution_folder(SwimCore "${SWIM_SOLUTION_FOLDER_ENGINE_MODULES}")',
        'add_executable(SwimEngineConfigTests EXCLUDE_FROM_ALL',
        'add_executable(SwimHelloWindow EXCLUDE_FROM_ALL',
        'add_executable(SwimHeadlessPlatform EXCLUDE_FROM_ALL',
        'add_library(SwimPlatformPublicHeaders OBJECT EXCLUDE_FROM_ALL',
        'add_executable(SwimInputTests EXCLUDE_FROM_ALL',
        '"${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/SDL3"',
    ):
        if fragment not in cmake_text:
            fail(f"first-party/IDE target organization is missing: {fragment}", failures)

    for fragment in (
        '"${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Draco"',
        '"${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/WebP"',
        'EXCLUDE_FROM_ALL YES',
        'swim_set_solution_folder(SwimZstd "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Zstd")',
        'swim_set_solution_folder(SwimBasis "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Basis Universal")',
        'swim_set_solution_folder(SwimGlad "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/GLAD")',
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
            '"Engine Modules", "Tests", "Third Party", "CMake"',
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
        for fragment in ("windows-build-common.ps1", "Get-SwimWindowsBuildPlan", "BuildPlan.CMakePath", "-DebugBuild:$Debug"):
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

    for fragment in (
        'if(CMAKE_GENERATOR MATCHES "^Ninja")',
        "CMAKE_SUPPRESS_REGENERATION ON CACHE BOOL",
    ):
        if fragment not in cmake_text:
            fail(
                f"Ninja automatic CMake regeneration suppression is missing: {fragment}",
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
        'set(SWIM_NLOHMANN_JSON_VERSION "3.10.4")',
        'c9ac7589260f36ea7016d4d51a6c95809803298c7caec9f55830a0214c5f9140',
        'releases/download/v${SWIM_NLOHMANN_JSON_VERSION}/json.hpp',
        'EXPECTED_HASH "SHA256=${SWIM_NLOHMANN_JSON_SHA256}"',
        'file(SHA256 "${SWIM_NLOHMANN_JSON_HEADER}" SWIM_JSON_ACTUAL_SHA256)',
        'if(FETCHCONTENT_FULLY_DISCONNECTED)',
        'add_library(SwimJson INTERFACE)',
        'add_library(nlohmann_json::nlohmann_json ALIAS SwimJson)',
        'function(swim_assert_cached_git_dependency_clean dependency_name source_dir)',
        'status --porcelain --untracked-files=all',
        "Cached dependency '${dependency_name}' is dirty",
        'swim_physx_source',
        'WEBP_BUILD_LIBWEBPMUX ON',
        'webpdemux',
        'libwebpmux',
        'add_library(Swim::WebP ALIAS SwimWebPBundle)',
    )
    for fragment in required_dependency_contract_fragments:
        if fragment not in dependency_text:
            fail(f"legacy third-party link contract is missing: {fragment}", failures)

    if 'GITHUB_REPOSITORY nlohmann/json' in dependency_text:
        fail(
            "nlohmann/json reverted to a full Git checkout; use the pinned release single-header artifact to avoid Windows path/dirty-cache failures",
            failures,
        )

    # The PhysX generator is a batch file. Running an absolute quoted path
    # through cmd.exe caused CMake/MSBuild to preserve the escape quotes and
    # Windows attempted to execute a command literally named \"C:/...bat\".
    # Invoke the local batch name from SWIM_PHYSX_ROOT instead.
    if '\\"${SWIM_PHYSX_GENERATOR}\\"' in physx_build_text:
        fail("PhysX generator still uses the broken escaped absolute-path cmd invocation", failures)

    if 'mklink /J' in physx_build_text:
        fail("PhysX reverted to a source-cache junction; use the isolated short Git worktree so builds cannot dirty CPM source", failures)

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
        'set(SWIM_NLOHMANN_JSON_VERSION "3.10.4")',
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
        ROOT / "Source" / "Engine" / "Systems" / "IO" / "InputManager.h",
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Renderer.h",
    )
    win32_contract_tokens = re.compile(r"\b(?:HWND|HINSTANCE|WPARAM|LPARAM|LRESULT|WNDPROC)\b|\bWM_[A-Z0-9_]+\b|\bVK_[A-Z0-9_]+\b")
    for path in generic_contract_files:
        text = path.read_text(encoding="utf-8", errors="ignore")
        if win32_contract_tokens.search(text):
            fail(f"generic engine contract still exposes Win32/message vocabulary: {path.relative_to(ROOT)}", failures)

    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    required_foundation_fragments = (
        "add_library(Swim::Platform ALIAS SwimPlatform)",
        "target_link_libraries(SwimPlatform PRIVATE ${SWIM_SDL3_TARGET})",
        "add_library(Swim::Input ALIAS SwimInput)",
        "target_link_libraries(SwimInput PUBLIC Swim::Platform)",
        'list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Platform/")',
        'list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Input/")',
    )
    for fragment in required_foundation_fragments:
        if fragment not in cmake_text:
            fail(f"foundation CMake boundary is missing: {fragment}", failures)

    if cmake_text.count("${SWIM_SDL3_TARGET}") != 1:
        fail("SDL3 target must be linked directly by SwimPlatform only", failures)

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

    vulkan_device_header = (
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Vulkan" / "VulkanDeviceManager.h"
    ).read_text(encoding="utf-8", errors="ignore")
    vk_platform_define = vulkan_device_header.find("VK_USE_PLATFORM_WIN32_KHR")
    vk_header_include = vulkan_device_header.find("<vulkan/vulkan.h>")
    if vk_platform_define < 0 or vk_header_include < 0 or vk_platform_define > vk_header_include:
        fail("VulkanDeviceManager.h must define VK_USE_PLATFORM_WIN32_KHR before including Vulkan headers", failures)

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
        r"target_compile_definitions\(SwimPlatform PRIVATE(?P<body>.*?)\n\)",
        cmake_text,
        re.DOTALL,
    )
    if platform_compile_definition_block is None:
        fail("SwimPlatform is missing its private Windows compile-definition block", failures)
    else:
        platform_definition_text = platform_compile_definition_block.group("body")
        for compile_definition in ("WIN32_LEAN_AND_MEAN", "NOMINMAX"):
            if f"$<$<PLATFORM_ID:Windows>:{compile_definition}>" not in platform_definition_text:
                fail(f"SwimPlatform is missing Windows compile definition: {compile_definition}", failures)

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
        ROOT / "Source" / "Tests" / "Platform" / "PublicHeaderCompile.cpp"
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
    ):
        if fragment not in config_header:
            fail(f"Phase 2 runtime configuration contract is missing: {fragment}", failures)

    for fragment in (
        'argument == "--graphics"',
        'argument == "--physics"',
        "ResolveGraphicsBackend",
        "ResolvePhysicsBackend",
    ):
        if fragment not in config_source:
            fail(f"Phase 2 command-line backend selection is missing: {fragment}", failures)

    if "SystemManager" in engine_header or "systemManager" in engine_source or "AddSystem<" in engine_source:
        fail("SwimEngine core lifecycle regressed to stringly typed SystemManager ownership", failures)

    for fragment in (
        "int SwimEngine::AwakeSystems()",
        "int SwimEngine::InitSystems()",
        "void SwimEngine::UpdateSystems(double dt)",
        "void SwimEngine::FixedUpdateSystems(unsigned int tickThisSecond)",
        "int SwimEngine::ExitSystems()",
        "Destroy consumers before the services they reference.",
        "switch (graphicsBackend)",
    ):
        if fragment not in engine_source:
            fail(f"Phase 2 explicit lifecycle/backend contract is missing: {fragment}", failures)

    if "SwimEngine::CONTEXT" in engine_source or "RenderContext" in engine_header:
        fail("compile-time renderer selection returned to SwimEngine", failures)

    for owner_type in (
        "InputManager",
        "CommandSystem",
        "SceneSystem",
        "VulkanRenderer",
        "OpenGLRenderer",
        "CameraSystem",
        "PhysicsSystem",
    ):
        if f"std::unique_ptr<{owner_type}>" not in engine_header:
            fail(f"Phase 2 core owner is not uniquely owned: {owner_type}", failures)
        if f"std::shared_ptr<{owner_type}>" in engine_header:
            fail(f"Phase 2 core owner regressed to shared ownership: {owner_type}", failures)

    camera_header = (
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Camera" / "CameraSystem.h"
    ).read_text(encoding="utf-8", errors="ignore")
    camera_source = (
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Camera" / "CameraSystem.cpp"
    ).read_text(encoding="utf-8", errors="ignore")
    if "SwimEngine::GetInstance" in camera_header or "SwimEngine::GetInstance" in camera_source:
        fail("CameraSystem regressed to global engine discovery instead of injected runtime configuration", failures)

    vulkan_index_draw = (
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Vulkan" / "VulkanIndexDraw.cpp"
    ).read_text(encoding="utf-8", errors="ignore")
    if "SwimEngine::GetInstance" in vulkan_index_draw:
        fail("VulkanIndexDraw regressed to global engine discovery instead of injected renderer/scene/camera services", failures)

    transform_source = (
        ROOT / "Source" / "Engine" / "Components" / "Transform.cpp"
    ).read_text(encoding="utf-8", errors="ignore")
    if "SwimEngine::GetInstance" in transform_source:
        fail("Transform regressed to global active-scene discovery", failures)
    if "GraphicsBackend::" in transform_source:
        fail("Transform regressed to graphics-API-specific clip-space behavior", failures)

    cubemap_controller_source = (
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Environment" / "CubeMapController.cpp"
    ).read_text(encoding="utf-8", errors="ignore")
    if "SwimEngine::GetInstance" in cubemap_controller_source:
        fail("CubeMapController regressed to global engine discovery instead of renderer-owned backend injection", failures)

    for renderer_name, relative_path in (
        ("VulkanRenderer", ("Vulkan", "VulkanRenderer.cpp")),
        ("OpenGLRenderer", ("OpenGL", "OpenGLRenderer.cpp")),
    ):
        renderer_source = (
            ROOT / "Source" / "Engine" / "Systems" / "Renderer" / relative_path[0] / relative_path[1]
        ).read_text(encoding="utf-8", errors="ignore")
        if "GetInstance()->GetSceneSystem()" in renderer_source:
            fail(f"{renderer_name} regressed to global active-scene discovery", failures)

    for fragment in (
        "add_library(SwimCore STATIC",
        "add_library(Swim::Core ALIAS SwimCore)",
        "Source/Engine/EngineConfig.cpp",
        "add_executable(SwimEngineConfigTests EXCLUDE_FROM_ALL",
    ):
        if fragment not in cmake_text:
            fail(f"Phase 2 Core CMake/test boundary is missing: {fragment}", failures)

    # Phase 2 is a whole-tree invariant now, not a handful of migrated call sites.
    runtime_locator_patterns = (
        "SwimEngine::GetInstance()",
        "MeshPool::GetInstance()",
        "TexturePool::GetInstance()",
        "MaterialPool::GetInstance()",
        "FontPool::GetInstance()",
        "EntityFactory::GetInstance()",
    )
    source_roots = (ROOT / "Source" / "Engine", ROOT / "Source" / "Game")
    for source_root in source_roots:
        if not source_root.exists():
            continue
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            for locator in runtime_locator_patterns:
                if locator in text:
                    fail(
                        f"Phase 2 runtime service locator returned in {path.relative_to(ROOT)}: {locator}",
                        failures,
                    )

    for shared_engine_pattern in (
        "std::shared_ptr<SwimEngine>",
        "std::weak_ptr<SwimEngine>",
        "enable_shared_from_this<SwimEngine>",
    ):
        if shared_engine_pattern in engine_header or shared_engine_pattern in engine_source:
            fail(f"SwimEngine lifetime regressed to process-shared ownership: {shared_engine_pattern}", failures)

    scene_system_header = (
        ROOT / "Source" / "Engine" / "Systems" / "Scene" / "SceneSystem.h"
    ).read_text(encoding="utf-8", errors="ignore")
    scene_system_source = (
        ROOT / "Source" / "Engine" / "Systems" / "Scene" / "SceneSystem.cpp"
    ).read_text(encoding="utf-8", errors="ignore")
    if "Preregister(std::shared_ptr<Scene>" in scene_system_header:
        fail("Scene preregistration regressed to process-global mutable Scene instances", failures)
    if "factory.clear()" in scene_system_source:
        fail("Scene constructor metadata is cleared by the first engine instance", failures)

    primitive_meshes_header = (
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Meshes" / "PrimitiveMeshes.h"
    ).read_text(encoding="utf-8", errors="ignore")
    if '#include "Vertex.h"' not in primitive_meshes_header:
        fail("PrimitiveMeshes.h must include Vertex.h directly instead of relying on PCH/transitive includes", failures)

    scene_header = (
        ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.h"
    ).read_text(encoding="utf-8", errors="ignore")
    scene_source = (
        ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.cpp"
    ).read_text(encoding="utf-8", errors="ignore")
    for fragment in (
        "Scene();",
        "explicit Scene(const std::string& name);",
        "~Scene() override;",
    ):
        if fragment not in scene_header:
            fail(f"Scene incomplete-type ownership boundary is missing declaration: {fragment}", failures)
    for fragment in (
        "Scene::Scene()",
        "Scene::Scene(const std::string& name)",
        "Scene::~Scene() = default;",
    ):
        if fragment not in scene_source:
            fail(f"Scene incomplete-type ownership boundary is missing out-of-line definition: {fragment}", failures)
    if "Scene() :" in scene_header or "explicit Scene(const std::string& name =" in scene_header:
        fail("Scene constructors must remain out-of-line while Scene owns forward-declared EntityFactory", failures)

    font_pool_header = (
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Font" / "FontPool.h"
    ).read_text(encoding="utf-8", errors="ignore")
    font_pool_source = (
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Font" / "FontPool.cpp"
    ).read_text(encoding="utf-8", errors="ignore")
    if "void Flush();" not in font_pool_header or "void FontPool::Flush()" not in font_pool_source:
        fail("FontPool shutdown API must declare and define Flush() consistently", failures)

    direct_service_include_checks = (
        (
            ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "OpenGL" / "OpenGLRenderer.cpp",
            '#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"',
            "OpenGLRenderer must include MaterialPool.h directly before calling MaterialPool methods",
        ),
        (
            ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Vulkan" / "VulkanRenderer.cpp",
            '#include "Engine/Platform/FileSystem.h"',
            "VulkanRenderer must include FileSystem.h directly before calling FileSystem methods",
        ),
        (
            ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.cpp",
            '#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"',
            "Scene.cpp must include MeshPool.h directly for scene-owned resource wiring",
        ),
        (
            ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.cpp",
            '#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"',
            "Scene.cpp must include MaterialPool.h directly for scene-owned resource wiring",
        ),
    )
    for path, fragment, message in direct_service_include_checks:
        text = path.read_text(encoding="utf-8", errors="ignore")
        if fragment not in text:
            fail(message, failures)

    # Behavior/gameplay translation units must not dereference services that Behavior only forward-declares
    # unless they include the concrete service header themselves. This catches MSVC/PCH-only completeness failures.
    game_service_include_rules = (
        ("renderer->", "Engine/Systems/Renderer/Renderer.h"),
        ("scene->", "Engine/Systems/Scene/Scene.h"),
        ("input->", "Engine/Systems/IO/InputManager.h"),
        ("cameraSystem->", "Engine/Systems/Renderer/Core/Camera/CameraSystem.h"),
        ("sceneSystem->", "Engine/Systems/Scene/SceneSystem.h"),
    )
    game_source_root = ROOT / "Source" / "Game"
    for path in game_source_root.rglob("*.cpp"):
        text = path.read_text(encoding="utf-8", errors="ignore").replace("\\", "/")
        for expression, include_path in game_service_include_rules:
            if expression in text and include_path not in text:
                fail(
                    f"gameplay source dereferences a forward-declared service without its concrete header: "
                    f"{path.relative_to(ROOT)}: {expression} requires {include_path}",
                    failures,
                )

    cubemap_test_header = (
        ROOT / "Source" / "Game" / "Behaviors" / "Demo" / "CubeMapControlTest.h"
    ).read_text(encoding="utf-8", errors="ignore")
    cubemap_test_source = (
        ROOT / "Source" / "Game" / "Behaviors" / "Demo" / "CubeMapControlTest.cpp"
    ).read_text(encoding="utf-8", errors="ignore")
    if "class CubeMapController;" not in cubemap_test_header:
        fail("CubeMapControlTest must explicitly declare its non-owning CubeMapController dependency", failures)
    if "std::unique_ptr<Engine::CubeMapController>&" in cubemap_test_header:
        fail("CubeMapControlTest helper must not expose renderer ownership through unique_ptr", failures)
    for fragment in (
        '#include "Engine/Systems/Renderer/Renderer.h"',
        '#include "Engine/Systems/Renderer/Core/Environment/CubeMapController.h"',
        '#include "Engine/Systems/Scene/Scene.h"',
        '#include "Engine/Systems/IO/InputManager.h"',
    ):
        if fragment not in cubemap_test_source:
            fail(f"CubeMapControlTest direct dependency include is missing: {fragment}", failures)

    include_case_checks = (
        (ROOT / "Source" / "Game" / "Behaviors" / "Demo" / "SetTextCallBack.cpp", '#include "SetTextCallBack.h"'),
        (ROOT / "Source" / "Engine" / "Systems" / "Physics" / "PhysicsWorld.h", '#include "RigidBody.h"'),
        (ROOT / "Source" / "Engine" / "Systems" / "Physics" / "Rigibody.cpp", '#include "RigidBody.h"'),
        (ROOT / "Source" / "Game" / "Testing" / "PrimitiveTest.cpp", '#include "PCH.h"'),
        (ROOT / "Source" / "Game" / "Testing" / "PrimitivePhysicsTest.cpp", '#include "PCH.h"'),
    )
    for path, fragment in include_case_checks:
        text = path.read_text(encoding="utf-8", errors="ignore")
        if fragment not in text:
            fail(f"project-local include spelling/case regressed in {path.relative_to(ROOT)}: {fragment}", failures)

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
        "add_library(SwimJobs STATIC",
        "add_library(Swim::Jobs ALIAS SwimJobs)",
        "target_link_libraries(SwimJobs PRIVATE Swim::EnkiTS Swim::Memory)",
        "add_executable(SwimJobSystemTests EXCLUDE_FROM_ALL",
        "target_link_libraries(SwimEngine PRIVATE",
        "Swim::Jobs",
    ):
        if fragment not in cmake_text:
            fail(f"Phase 3 JobSystem CMake boundary is missing: {fragment}", failures)

    if 'list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Jobs/")' not in cmake_text:
        fail("JobSystem.cpp can be compiled twice: legacy SwimEngine source glob must exclude the module root", failures)

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
        "rendererRuntimeServices.Jobs = jobSystem.get()",
        "sceneServices.Jobs = jobSystem.get()",
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
    io_test_path = ROOT / "Source" / "Tests" / "IO" / "AsyncIoServiceTests.cpp"
    if not io_header_path.is_file() or not io_source_path.is_file() or not io_test_path.is_file():
        fail("Phase 3 Async IO service/tests are missing", failures)
        return

    io_header = io_header_path.read_text(encoding="utf-8", errors="ignore")
    io_source = io_source_path.read_text(encoding="utf-8", errors="ignore")
    engine_header = (ROOT / "Source" / "Engine" / "SwimEngine.h").read_text(encoding="utf-8", errors="ignore")
    engine_source = (ROOT / "Source" / "Engine" / "SwimEngine.cpp").read_text(encoding="utf-8", errors="ignore")
    renderer_services = (
        ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "RendererRuntimeServices.h"
    ).read_text(encoding="utf-8", errors="ignore")
    scene_header = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.h").read_text(
        encoding="utf-8", errors="ignore"
    )
    scene_system_header = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "SceneSystem.h").read_text(
        encoding="utf-8", errors="ignore"
    )

    for fragment in (
        "add_library(SwimIO STATIC",
        "add_library(Swim::IO ALIAS SwimIO)",
        "PUBLIC Swim::Platform",
        "PRIVATE Swim::Jobs",
        "add_executable(SwimAsyncIoTests EXCLUDE_FROM_ALL",
        "target_link_libraries(SwimAsyncIoTests PRIVATE Swim::IO Swim::Jobs)",
        'list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/IO/")',
        "Swim::IO",
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
        "rendererRuntimeServices.IO = ioSystem.get()",
        "sceneServices.IO = ioSystem.get()",
        "ioSystem->PumpCompletions()",
        "ioSystem->Shutdown(Swim::IO::IoShutdownMode::Drain)",
    ):
        target = engine_header if "unique_ptr" in fragment else engine_source
        if fragment not in target:
            fail(f"engine-owned Async IO lifecycle/injection is missing: {fragment}", failures)

    if "Swim::IO::AsyncIoService* IO" not in renderer_services:
        fail("legacy renderer runtime services do not expose the engine-owned Async IO service", failures)
    if "Swim::IO::AsyncIoService* IO" not in scene_system_header:
        fail("SceneSystem services do not expose the engine-owned Async IO service", failures)
    if "GetIoSystem()" not in scene_header:
        fail("Scene runtime service view does not expose Async IO without global discovery", failures)

    io_shutdown_index = engine_source.find("ioSystem->Shutdown(Swim::IO::IoShutdownMode::Drain)")
    scene_exit_index = engine_source.find('exitSystem("SceneSystem", sceneSystem.get())')
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
    test_path = ROOT / "Source" / "Tests" / "Memory" / "MemoryArenaTests.cpp"

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
    renderer_services = (ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "RendererRuntimeServices.h").read_text(encoding="utf-8", errors="ignore")
    scene_system_header = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "SceneSystem.h").read_text(encoding="utf-8", errors="ignore")
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
        "add_library(SwimMemory STATIC",
        "add_library(Swim::Memory ALIAS SwimMemory)",
        "target_link_libraries(SwimMemory PRIVATE Swim::Mimalloc)",
        "SWIM_MEMORY_USE_MIMALLOC",
        'list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Memory/")',
        "target_sources(SwimEngine PRIVATE $<TARGET_OBJECTS:mimalloc-obj>)",
        "add_executable(SwimMemoryTests EXCLUDE_FROM_ALL",
        "Swim::Memory",
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
        "rendererRuntimeServices.FrameMemory = &frameArena",
        "sceneServices.FrameMemory = &frameArena",
    ):
        if fragment not in engine_source:
            fail(f"frame arena lifecycle/injection is missing: {fragment}", failures)

    if "Swim::Memory::FrameArena* FrameMemory" not in renderer_services:
        fail("legacy renderer runtime services do not expose frame memory", failures)
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
    )
    for fragment in broad_filters:
        if fragment in cmake_text:
            fail(f"legacy source exclusion is too broad and can remove unrelated implementation files: {fragment}", failures)


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
    check_phase3_io_architecture(failures)
    check_phase3_memory_architecture(failures)
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
