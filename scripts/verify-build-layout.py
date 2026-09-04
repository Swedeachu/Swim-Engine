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
    "fastgltf",
    "glad",
    "glm",
    "json",
    "meshoptimizer",
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
        'add_executable(SwimHelloWindow EXCLUDE_FROM_ALL',
        'add_executable(SwimHeadlessPlatform EXCLUDE_FROM_ALL',
        '"${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/SDL3"',
    ):
        if fragment not in cmake_text:
            fail(f"first-party/IDE target organization is missing: {fragment}", failures)

    for fragment in (
        'swim_set_solution_folder(SwimZstd "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Zstd")',
        'swim_set_solution_folder(SwimBasisTranscoder "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/Basis Universal")',
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
    math_dependency_text = (ROOT / "cmake" / "MathDependencies.cmake").read_text(encoding="utf-8", errors="ignore")
    dependency_contract_text = dependency_text + "\n" + math_dependency_text

    required_root_fragments = (
        'set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")',
        '$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:CXX>>:_ITERATOR_DEBUG_LEVEL=0>',
        '$<$<CONFIG:Debug>:_SWIM_DEBUG>',
        '$<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>',
        '$<$<CONFIG:Debug>:/U_DEBUG>',
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
        'GIT_TAG v1.4.9',
        'GIT_TAG v1_60_snapshot_final',
        'GIT_TAG v2.0.8',
    )
    for fragment in pins:
        if fragment not in dependency_contract_text:
            fail(f"dependency pin is missing: {fragment}", failures)


def check_modern_cmake_dependency_compatibility(failures: list[str]) -> None:
    dependency_text = (ROOT / "cmake" / "Dependencies.cmake").read_text(encoding="utf-8", errors="ignore")

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
        fail("Scene constructors must remain out-of-line while Scene owns forward-declared scene subsystems", failures)

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
        '#include "Engine/Systems/Renderer/Core/Environment/CubeMapController.h"',
        '#include "Engine/Systems/Scene/Scene.h"',
        '#include "Engine/Systems/IO/InputManager.h"',
    ):
        if fragment not in cubemap_test_source:
            fail(f"CubeMapControlTest direct dependency include is missing: {fragment}", failures)

    if '#include "Engine/Systems/Renderer/Renderer.h"' in cubemap_test_source or "renderer->" in cubemap_test_source:
        fail("CubeMapControlTest regained a direct Renderer dependency; use the optional CubeMapController scene service", failures)

    include_case_checks = (
        (ROOT / "Source" / "Game" / "Behaviors" / "Demo" / "SetTextCallBack.cpp", '#include "SetTextCallBack.h"'),
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
        "sceneServices.Core.Jobs = jobSystem.get()",
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
        "sceneServices.Core.IO = ioSystem.get()",
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
        "sceneServices.Core.FrameMemory = &frameArena",
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
    renderer_services = (ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "RendererRuntimeServices.h").read_text(encoding="utf-8", errors="ignore")
    scene_header = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "Scene.h").read_text(encoding="utf-8", errors="ignore")
    scene_system_header = (ROOT / "Source" / "Engine" / "Systems" / "Scene" / "SceneSystem.h").read_text(encoding="utf-8", errors="ignore")

    for fragment in (
        "add_library(SwimAssets STATIC",
        "add_library(Swim::Assets ALIAS SwimAssets)",
        "add_executable(SwimHeadlessCoreAssets EXCLUDE_FROM_ALL",
        'list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Assets/")',
        "Swim::Assets",
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
        "rendererRuntimeServices.Assets = assetSystem.get()",
        "sceneServices.Core.Assets = assetSystem.get()",
        "assetSystem->Shutdown()",
    ):
        if fragment not in engine_source:
            fail(f"engine-owned AssetSystem lifecycle/injection is missing: {fragment}", failures)

    if "Swim::Assets::AssetSystem* Assets" not in renderer_services:
        fail("legacy renderer runtime services do not expose the engine-owned AssetSystem", failures)
    if "Swim::Assets::AssetSystem* Assets" not in scene_system_header:
        fail("SceneSystem services do not expose the engine-owned AssetSystem", failures)
    if "GetAssetSystem()" not in scene_header:
        fail("Scene runtime service view does not expose AssetSystem without global discovery", failures)

    legacy_material = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Material" / "MaterialData.h"
    legacy_binding = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Material" / "LegacyRenderBinding.h"
    legacy_material_pool = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Material" / "MaterialPool.cpp"
    legacy_material_pool_header = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Material" / "MaterialPool.h"
    legacy_mesh = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Meshes" / "Mesh.h"
    legacy_mesh_pool_header = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Meshes" / "MeshPool.h"
    legacy_mesh_pool = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Meshes" / "MeshPool.cpp"
    legacy_texture = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Textures" / "Texture2D.cpp"
    legacy_texture_header = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Textures" / "Texture2D.h"
    legacy_texture_pool = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Textures" / "TexturePool.cpp"
    legacy_texture_pool_header = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Textures" / "TexturePool.h"
    material_component = ROOT / "Source" / "Engine" / "Components" / "Material.h"
    vulkan_index_draw = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Vulkan" / "VulkanIndexDraw.cpp"

    if not all(path.is_file() for path in (legacy_material, legacy_binding, legacy_material_pool, legacy_material_pool_header, legacy_mesh, legacy_mesh_pool_header, legacy_mesh_pool, legacy_texture, legacy_texture_header, legacy_texture_pool, legacy_texture_pool_header, material_component, vulkan_index_draw)):
        fail("Phase 4 legacy mesh/material ownership migration seam is incomplete", failures)
        return

    legacy_material_text = legacy_material.read_text(encoding="utf-8", errors="ignore")
    legacy_binding_text = legacy_binding.read_text(encoding="utf-8", errors="ignore")
    legacy_material_pool_text = legacy_material_pool.read_text(encoding="utf-8", errors="ignore")
    legacy_material_pool_header_text = legacy_material_pool_header.read_text(encoding="utf-8", errors="ignore")
    legacy_mesh_text = legacy_mesh.read_text(encoding="utf-8", errors="ignore")
    legacy_mesh_pool_header_text = legacy_mesh_pool_header.read_text(encoding="utf-8", errors="ignore")
    legacy_mesh_pool_text = legacy_mesh_pool.read_text(encoding="utf-8", errors="ignore")
    legacy_texture_text = legacy_texture.read_text(encoding="utf-8", errors="ignore")
    legacy_texture_header_text = legacy_texture_header.read_text(encoding="utf-8", errors="ignore")
    legacy_texture_pool_text = legacy_texture_pool.read_text(encoding="utf-8", errors="ignore")
    legacy_texture_pool_header_text = legacy_texture_pool_header.read_text(encoding="utf-8", errors="ignore")
    material_component_text = material_component.read_text(encoding="utf-8", errors="ignore")
    vulkan_index_draw_text = vulkan_index_draw.read_text(encoding="utf-8", errors="ignore")

    for fragment in ("MeshBufferData", "std::shared_ptr<Mesh>", "VkBuffer", "GLuint"):
        if fragment in legacy_material_text:
            fail(f"legacy MaterialData regained geometry/backend ownership: {fragment}", failures)

    for fragment in ("std::shared_ptr<Mesh> mesh", "std::shared_ptr<MeshBufferData> meshBufferData", "std::shared_ptr<MaterialData> material"):
        if fragment not in legacy_binding_text:
            fail(f"legacy draw compatibility binding no longer keeps mesh/material residency separate: {fragment}", failures)

    for fragment in ("MeshBufferData", "VkBuffer", "GLuint", "meshBufferData"):
        if fragment in legacy_mesh_text:
            fail(f"legacy CPU Mesh regained renderer residency/backend state: {fragment}", failures)

    for fragment in ("GetMeshBufferData", "RequestMeshResidency", "meshResidency", "ComputeLegacyMeshContentHash", "meshContentIndex"):
        if fragment not in legacy_mesh_pool_text and fragment not in legacy_mesh_pool_header_text:
            fail(f"legacy mesh residency/content-hash migration seam is missing: {fragment}", failures)

    request_mesh_residency_index = legacy_mesh_pool_text.find("MeshPool::RequestMeshResidency")
    mesh_upload_index = legacy_mesh_pool_text.find("GenerateBuffersAndAABB")
    if request_mesh_residency_index == -1 or mesh_upload_index == -1 or mesh_upload_index < request_mesh_residency_index:
        fail("legacy MeshPool registration regained an implicit renderer upload instead of explicit RequestMeshResidency", failures)
    if "meshes->RequestMeshResidency(mesh)" not in legacy_material_pool_text:
        fail("MaterialPool must explicitly request mesh renderer residency when creating a legacy draw binding", failures)
    if "Meshes->RequestMeshResidency(m)" not in vulkan_index_draw_text:
        fail("Vulkan glyph compatibility path must explicitly request mesh renderer residency", failures)

    for fragment in ("ComputeLegacyTextureContentHash", "textureContentIndex"):
        if fragment not in legacy_texture_pool_text and fragment not in legacy_texture_pool_header_text:
            fail(f"legacy texture content-hash migration seam is missing: {fragment}", failures)

    for fragment in (
        "Texture2D(const std::string& filePath",
        "Texture2D(uint32_t width, uint32_t height",
        "MakeResident(TextureRuntimeContext residencyContext)",
        "friend class TexturePool",
    ):
        if fragment not in legacy_texture_header_text and fragment not in legacy_texture_text:
            fail(f"legacy Texture2D CPU-construction/explicit-residency boundary is missing: {fragment}", failures)
    if "Texture2D(TextureRuntimeContext" in legacy_texture_header_text or "Texture2D::Texture2D(TextureRuntimeContext" in legacy_texture_text:
        fail("Texture2D construction regained renderer-context coupling", failures)
    if "Generate();" in legacy_texture_text:
        fail("Texture2D constructor path regained implicit renderer upload", failures)
    for fragment in (
        "RequestTextureResidency(const std::shared_ptr<Texture2D>& texture)",
        "RequestTextureResidencyLocked",
        "texture->MakeResident(runtimeContext)",
    ):
        if fragment not in legacy_texture_pool_text and fragment not in legacy_texture_pool_header_text:
            fail(f"legacy TexturePool explicit renderer-residency request is missing: {fragment}", failures)
    if "std::make_shared<Texture2D>(runtimeContext" in legacy_texture_pool_text:
        fail("TexturePool regained renderer-coupled Texture2D construction", failures)

    for fragment in (
        "Swim::Assets::AssetSystem& assets",
        "LoadAndRegisterCompositeMaterial(const std::string& sourcePath)",
        "assets->Find<Swim::Assets::ModelAsset>",
        "ComputeDependencyRevisionHash",
        "compositeMaterialRevisions",
        "assets->Resolve(node.Mesh)",
        "GetOrCreateTextureFromAsset",
    ):
        if fragment not in legacy_material_pool_text and fragment not in legacy_material_pool_header_text:
            fail(f"cooked ModelAsset -> legacy renderer residency seam is missing: {fragment}", failures)

    for fragment in (
        "AssetHandle<Swim::Assets::TextureAsset>",
        "GetOrCreateTextureFromAsset",
        "assets.GetStatus(handle)",
        "assets.Resolve(handle)",
        "TranscodeBasisKtx2",
    ):
        if fragment not in legacy_texture_pool_text and fragment not in legacy_texture_pool_header_text:
            fail(f"cooked TextureAsset -> legacy renderer residency seam is missing: {fragment}", failures)

    for stale_fragment in ("tinygltf", "tiny_gltf", "WebPDecode", "draco::"):
        if stale_fragment in legacy_material_pool_text or stale_fragment in legacy_material_pool_header_text:
            fail(f"runtime MaterialPool regained source-import dependency: {stale_fragment}", failures)

    for fragment in (
        "catch (const std::exception& error)",
        "Failed to resolve cooked model for",
        "return cached;",
    ):
        if fragment not in legacy_material_pool_text:
            fail(f"legacy cooked-model compatibility residency can terminate startup again: {fragment}", failures)

    retired_tinygltf_exclusion = 'list(REMOVE_ITEM SWIM_ENGINE_SOURCES "${CMAKE_SOURCE_DIR}/Source/Engine/ThirdParty/TinyGltfImplementation.cpp")'
    if retired_tinygltf_exclusion not in cmake_text:
        fail("legacy runtime source glob can resurrect a stale TinyGltfImplementation.cpp after overlay updates", failures)

    stb_image_implementation = ROOT / "Source" / "Engine" / "ThirdParty" / "StbImageImplementation.cpp"
    if not stb_image_implementation.is_file() or "STB_IMAGE_IMPLEMENTATION" not in stb_image_implementation.read_text(encoding="utf-8", errors="ignore"):
        fail("runtime loose-image compatibility lost its explicit stb_image implementation after tinygltf removal", failures)
    for implementation_source in (
        "Source/Engine/ThirdParty/StbImageImplementation.cpp",
        "Source/Engine/ThirdParty/StbImageResizeImplementation.cpp",
    ):
        if implementation_source not in cmake_text:
            fail(f"third-party implementation TU is missing explicit PCH exclusion: {implementation_source}", failures)
    if "PROPERTIES SKIP_PRECOMPILE_HEADERS ON" not in cmake_text:
        fail("third-party implementation TUs lost their PCH exclusion", failures)

    for stale_link in ("tinygltf::tinygltf", "Swim::WebP"):
        if stale_link in cmake_text:
            fail(f"legacy runtime target still links source-import dependency: {stale_link}", failures)

    dependency_text = (ROOT / "cmake" / "Dependencies.cmake").read_text(encoding="utf-8", errors="ignore")
    for stale_package in ("NAME draco_source", "NAME webp_source", "NAME tinygltf_source"):
        if stale_package in dependency_text:
            fail(f"obsolete runtime source-import package remains in Dependencies.cmake: {stale_package}", failures)

    if "std::memcmp" in legacy_mesh_pool_text or "std::memcmp" in legacy_texture_pool_text:
        fail("legacy renderer pools regressed to O(N) raw-byte deduplication scans", failures)

    if "std::shared_ptr<LegacyRenderBinding> binding" not in material_component_text:
        fail("scene Material component does not use the geometry/material compatibility binding", failures)

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
        "texture=ktx2-or-rgba8-mips-v3",
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
    engine_source = (ROOT / "Source" / "Engine" / "SwimEngine.cpp").read_text(encoding="utf-8", errors="ignore")
    sandbox_header = (ROOT / "Source" / "Game" / "Scenes" / "SandBox.h").read_text(encoding="utf-8", errors="ignore")
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

    for fragment in (
        'scenes->RegisterSceneType<Game::SandBox>("SandBox")',
        'scenes->SetStartupScene("SandBox")',
    ):
        if fragment not in main_source:
            fail(f"application-owned scene registration/startup selection is missing: {fragment}", failures)

    scene_system_construction = "sceneSystem = std::make_unique<SceneSystem>();"
    create_begin = engine_source.find("void SwimEngine::Create()")
    create_end = engine_source.find("EngineConfigParseResult SwimEngine::ParseStartingEngineArgs", create_begin)
    init_begin = engine_source.find("int SwimEngine::Init()")
    init_end = engine_source.find("int SwimEngine::AwakeSystems()", init_begin)
    if engine_source.count(scene_system_construction) != 1:
        fail("SceneSystem must have exactly one SwimEngine-owned construction site", failures)
    elif not (0 <= create_begin < engine_source.find(scene_system_construction, create_begin, create_end) < create_end):
        fail("SceneSystem must be constructed in SwimEngine::Create so pre-Start scene registration is valid", failures)
    if 0 <= init_begin < init_end and scene_system_construction in engine_source[init_begin:init_end]:
        fail("SwimEngine::Init must not recreate SceneSystem and discard pre-Start SceneCatalog registrations", failures)
    if "Engine/Systems/Scene/SceneSystem.h" not in engine_source:
        fail("SwimEngine.cpp must explicitly include SceneSystem.h for its owned SceneSystem construction", failures)

    if "DEFINE_SCENE" in sandbox_header or "REGISTER_SCENE" in sandbox_header:
        fail("SandBox scene regained static scene-registration macros", failures)
    if "GetSceneSystem()->SetScene(name" in sandbox_source:
        fail("SandBox::Awake regained self-selection as the active scene", failures)

    check_suite_is_compiled("Scene/Headless", "SceneCatalogTests.cpp", failures)

    for forbidden in (
        "VulkanRenderer*",
        "OpenGLRenderer*",
        "Renderer* renderer",
        "GetVulkanRenderer()",
        "GetOpenGLRenderer()",
        "GetRenderer() const",
        "SetVulkanRenderer(",
        "SetOpenGLRenderer(",
    ):
        if forbidden in scene_header or forbidden in scene_source:
            fail(f"Phase 5 Scene regained a renderer/backend pointer dependency: {forbidden}", failures)

    if "Renderer* renderer" in behavior_header or "scene->GetRenderer()" in behavior_source:
        fail("Behavior base regained cached renderer ownership/discovery", failures)
    if "CubeMapController* CubeMap" not in scene_system_header or "SetCubeMapController(services.Presentation.CubeMap)" not in scene_system_source:
        fail("backend-neutral optional cubemap presentation service is missing from Scene injection", failures)
    if "void SetCubeMapController(CubeMapController* cubeMap)" not in scene_system_header:
        fail("SceneSystem cannot late-bind renderer-owned cubemap presentation state", failures)

    renderer_awake_position = engine_source.find("result = GetRenderer().Awake()")
    cubemap_bind_position = engine_source.find("sceneSystem->SetCubeMapController(GetRenderer().GetCubeMapController().get())")
    scene_awake_position = engine_source.find("result = sceneSystem->Awake()", renderer_awake_position)
    if not (0 <= renderer_awake_position < cubemap_bind_position < scene_awake_position):
        fail("cubemap presentation service must be bound after Renderer::Awake and before SceneSystem::Awake", failures)
    if "sceneServices.Presentation.CubeMap = renderer.GetCubeMapController().get()" in engine_source:
        fail("SwimEngine snapshots the cubemap controller before Renderer::Awake creates it", failures)

    for fragment in (
        "struct SceneCoreServices",
        "struct ScenePresentationServices",
        "struct SceneToolServices",
        "return Core.IsValid();",
        "return Presentation.IsAvailable();",
    ):
        if fragment not in scene_system_header:
            fail(f"Phase 5 core/presentation/tool scene-service split is missing: {fragment}", failures)

    core_block = scene_system_header.split("struct SceneCoreServices", 1)[1].split("struct ScenePresentationServices", 1)[0]
    for forbidden in (
        "InputManager*",
        "CameraSystem*",
        "CubeMapController*",
        "MeshPool*",
        "TexturePool*",
        "MaterialPool*",
        "FontPool*",
        "CommandSystem*",
    ):
        if forbidden in core_block:
            fail(f"headless Scene core services regained a presentation/tool dependency: {forbidden}", failures)

    for fragment in (
        "bool HasPresentationServices() const",
        "if (HasPresentationServices())",
        "if (sceneDebugDraw)",
        "if (inputManager)",
        "if (!inputMgr)",
        "Scene::ScreenPointToRay requires presentation camera/input services.",
    ):
        if fragment not in scene_header and fragment not in scene_source:
            fail(f"Phase 5 headless Scene lifecycle guard is missing: {fragment}", failures)

    for forbidden in (
        "sceneServices.Input =",
        "sceneServices.Camera =",
        "sceneServices.Meshes =",
        "sceneServices.Textures =",
        "sceneServices.Materials =",
        "sceneServices.Fonts =",
    ):
        engine_source = (ROOT / "Source" / "Engine" / "SwimEngine.cpp").read_text(encoding="utf-8", errors="ignore")
        if forbidden in engine_source:
            fail(f"Scene services regressed to a flat mandatory presentation profile: {forbidden}", failures)


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
        "activeScene->UpdatePhysics(*physicsSystem, dt)",
        "activeScene->FixedUpdatePhysics(*physicsSystem)",
        "GetRenderer().SetRenderScene(activeScene)",
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
        "SetCommandDispatcher",
        "SetEditorMessageSender",
        "DispatchCommand(std::string_view command) const",
        "SendEditorMessage(const std::string& message",
    ):
        if fragment not in scene_header and fragment not in scene_system_source:
            fail(f"isolated Scene tool callback seam is missing: {fragment}", failures)


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

    frustum_header_path = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Camera" / "Frustum.h"
    frustum_test_path = ROOT / "Source" / "Tests" / "Suites" / "Scene" / "Ecs" / "FrustumTests.cpp"
    opengl_renderer_header = (ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "OpenGL" / "OpenGLRenderer.h").read_text(encoding="utf-8", errors="ignore")
    opengl_renderer_source = (ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "OpenGL" / "OpenGLRenderer.cpp").read_text(encoding="utf-8", errors="ignore")
    vulkan_index_header = (ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Vulkan" / "VulkanIndexDraw.h").read_text(encoding="utf-8", errors="ignore")
    vulkan_index_source = (ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Vulkan" / "VulkanIndexDraw.cpp").read_text(encoding="utf-8", errors="ignore")
    scene_bvh_source = (scene_root / "SubSceneSystems" / "SceneBVH.cpp").read_text(encoding="utf-8", errors="ignore")

    if not frustum_header_path.is_file() or not frustum_test_path.is_file():
        fail("Phase 5 per-view Frustum implementation/tests are missing", failures)
    else:
        frustum_header = frustum_header_path.read_text(encoding="utf-8", errors="ignore")
        for forbidden in (
            "inline static const Frustum& Get()",
            "static void SetCameraMatrices",
            "static Frustum cachedFrustum",
            "static uint64_t revision",
            "Frustum::cachedFrustum",
            "Frustum::revision",
            "Frustum::cameraMovedThisFrame",
        ):
            if forbidden in frustum_header:
                fail(f"Phase 5 Frustum regained process-global view state: {forbidden}", failures)

        for fragment in (
            "void Update(const glm::mat4& view, const glm::mat4& proj)",
            "std::uint64_t GetRevision() const",
            "bool DidCameraMoveThisFrame() const",
            "std::uint64_t revision = 1",
            "bool cameraMovedThisFrame = true",
        ):
            if fragment not in frustum_header:
                fail(f"Phase 5 per-view Frustum contract is missing: {fragment}", failures)

    if '#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"' not in opengl_renderer_header:
        fail("OpenGL renderer owns concrete Frustum state without including the Frustum contract explicitly", failures)
    if "Frustum viewFrustum" not in opengl_renderer_header or "viewFrustum.Update(view, proj)" not in opengl_renderer_source:
        fail("OpenGL renderer does not own/update explicit per-view Frustum state", failures)
    if "Frustum viewFrustum" not in vulkan_index_header or "viewFrustum.Update(camera->GetViewMatrix(), camera->GetProjectionMatrix())" not in vulkan_index_source:
        fail("Vulkan draw traversal does not own/update explicit per-view Frustum state", failures)
    if '#include "Engine/Components/Transform.h"' not in vulkan_index_header:
        fail("VulkanIndexDraw uses TransformSpace enumerators/defaults without including the TransformSpace definition", failures)
    if "enum class TransformSpace;" in vulkan_index_header:
        fail("VulkanIndexDraw must not replace the TransformSpace definition with a forward declaration while using enum values", failures)
    if "frustum.GetRevision()" not in scene_bvh_source:
        fail("SceneBVH does not consume the supplied Frustum revision", failures)

    for path_text, label in (
        (opengl_renderer_source, "OpenGL renderer"),
        (vulkan_index_source, "Vulkan draw traversal"),
        (scene_bvh_source, "SceneBVH"),
    ):
        for forbidden in ("Frustum::Get()", "Frustum::SetCameraMatrices", "Frustum::GetRevision()", "Frustum::DidCameraMoveThisFrame()"):
            if forbidden in path_text:
                fail(f"{label} regained static/global Frustum access: {forbidden}", failures)

    check_suite_is_compiled("Scene/Ecs", "FrustumTests.cpp", failures)

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
        'RegisterBehaviorType<Game::Spin>("Spin")',
        'RegisterBehaviorType<Game::SimpleMovement>("SimpleMovement")',
        'RegisterBehaviorType<Game::BallShooter>("BallShooter")',
    ):
        if fragment not in scene_system_header and fragment not in main_source:
            fail(f"explicit behavior registration seam is missing: {fragment}", failures)
    check_suite_is_compiled("Scene/Ecs", "BehaviorRegistryTests.cpp", failures)

    # Durable entity IDs + serializer/storage/tooling split.
    serialization_root = scene_root / "Serialization"
    required_serialization_files = (
        "SerializedEntityId.h",
        "EntityIdentityMap.h",
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
            fail(f"Phase 5 persistence/tooling module is missing: {file_name}", failures)

    legacy_serialized_manager = scene_root / "SubSceneSystems" / "SerializedSceneManager.h"
    if legacy_serialized_manager.exists() or (scene_root / "SubSceneSystems" / "SerializedSceneManager.cpp").exists():
        fail("monolithic SerializedSceneManager returned after serializer/storage/tooling split", failures)

    if (serialization_root / "SceneSerializer.cpp").is_file():
        serializer_source = (serialization_root / "SceneSerializer.cpp").read_text(encoding="utf-8", errors="ignore")
        for forbidden in ("WM_COPYDATA", "GetExecutableDirectory", "std::ofstream", "MaterialPool"):
            if forbidden in serializer_source:
                fail(f"SceneSerializer regained transport/storage/legacy-pool policy: {forbidden}", failures)
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
                fail(f"stable-ID/AssetId scene serialization contract is missing: {fragment}", failures)

    if (serialization_root / "SceneStorage.cpp").is_file():
        storage_source = (serialization_root / "SceneStorage.cpp").read_text(encoding="utf-8", errors="ignore")
        for forbidden in ("scene sync:", "scene load:", "SendEditorMessage", "CommandSystem"):
            if forbidden in storage_source:
                fail(f"SceneStorage regained editor/tool transport policy: {forbidden}", failures)

    if (serialization_root / "SceneToolingBridge.h").is_file():
        tooling_header = (serialization_root / "SceneToolingBridge.h").read_text(encoding="utf-8", errors="ignore")
        for forbidden in ("SceneSerializer", "nlohmann", "filesystem", "FileSystem"):
            if forbidden in tooling_header:
                fail(f"SceneToolingBridge owns serialization/storage policy: {forbidden}", failures)

    for fragment in (
        "EntityIdentityMap entityIdentities",
        "CreateEntityWithSerializedId(SerializedEntityId id)",
        "FindEntityBySerializedId(SerializedEntityId id) const",
        "std::unique_ptr<SceneSerializer> sceneSerializer",
        "std::unique_ptr<SceneStorage> sceneStorage",
        "std::unique_ptr<SceneToolingBridge> sceneToolingBridge",
        "std::unique_ptr<SceneSyncTracker> sceneSyncTracker",
    ):
        if fragment not in scene_header:
            fail(f"Scene durable persistence ownership seam is missing: {fragment}", failures)

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
    if "cmd.Register<std::uint64_t" not in scene_system_source:
        fail("editor scene commands do not consume durable 64-bit SerializedEntityId values", failures)
    for path in scene_root.rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and "to_integral" in path.read_text(encoding="utf-8", errors="ignore"):
            fail(f"raw EnTT integral identity leaked back into scene persistence/tooling: {path.relative_to(ROOT)}", failures)
    check_suite_is_compiled("Scene/Ecs", "EntityIdentityMapTests.cpp", failures)

    # Canonical backend-neutral camera/clip-space contract.
    render_conventions_path = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "RenderConventions.h"
    camera_header_path = ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Core" / "Camera" / "CameraSystem.h"
    if not render_conventions_path.is_file() or not camera_header_path.is_file():
        fail("canonical render/camera convention headers are missing", failures)
    else:
        render_conventions = render_conventions_path.read_text(encoding="utf-8", errors="ignore")
        camera_header_text = camera_header_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "CanonicalWorldHandedness = WorldHandedness::RightHanded",
            "CanonicalClipSpaceDepthRange = ClipSpaceDepthRange::ZeroToOne",
            "CanonicalClipSpaceYAxis = ClipSpaceYAxis::Up",
            "CanonicalUiCoordinateOrigin = UiCoordinateOrigin::BottomLeft",
        ):
            if fragment not in render_conventions:
                fail(f"canonical render convention is missing: {fragment}", failures)
        if "glm::perspectiveRH_ZO" not in camera_header_text:
            fail("Camera projection is not explicitly right-handed with 0..1 depth", failures)
        for forbidden in ("GraphicsBackend", "projMatrix[1][1] *= -1"):
            if forbidden in camera_header_text:
                fail(f"Camera math regained backend-specific behavior: {forbidden}", failures)

    opengl_source = (ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "OpenGL" / "OpenGLRenderer.cpp").read_text(encoding="utf-8", errors="ignore")
    vulkan_source = (ROOT / "Source" / "Engine" / "Systems" / "Renderer" / "Vulkan" / "VulkanRenderer.cpp").read_text(encoding="utf-8", errors="ignore")
    if "glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)" not in opengl_source:
        fail("legacy OpenGL backend does not adapt to canonical 0..1 clip depth", failures)
    if "-(float)extent.height" not in vulkan_source:
        fail("Vulkan backend does not adapt canonical +Y camera space through viewport orientation", failures)
    for forbidden in ("Presentation.ClipDepth", "SetClipSpaceDepthRange", "GetClipSpaceDepthRange", "MinusOneToOne"):
        if forbidden in scene_header or forbidden in scene_system_header or forbidden in scene_system_source or forbidden in engine_source:
            fail(f"scene/camera convention regained backend-dependent clip-depth policy: {forbidden}", failures)
    check_suite_is_compiled("Scene/Ecs", "RenderConventionsTests.cpp", failures)




def check_phase6_physics_architecture(failures: list[str]) -> None:
    physics_root = ROOT / "Source" / "Engine" / "Systems" / "Physics"
    physx_root = physics_root / "Backends" / "PhysX"
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
        scene_bridge_root / "ScenePhysicsBridge.h",
        scene_bridge_root / "ScenePhysicsBridge.cpp",
        test_root / "Suites" / "Physics" / "Generic" / "PhysicsHandleTests.cpp",
        test_root / "Suites" / "Physics" / "PhysX" / "PhysXBackendTests.cpp",
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
            if physx_root in path.parents:
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
        physx_text = physx_world_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "PxSimulationEventCallback", "SwimSimulationFilterShader", "LayerQueryFilter",
            "PhysXWorldBackend::Raycast", "PhysXWorldBackend::Sweep", "PhysXWorldBackend::Overlap",
            "CollisionEvent", "TriggerEvent", "pendingDestroy",
            "if (!actor->attachShape(**shapePtr))", "ReleaseActor(actor)",
            "ResolveBody(const physx::PxActor* actor)", "actor->is<physx::PxRigidActor>()",
        ):
            if fragment not in physx_text:
                fail(f"PhysX backend parity implementation is missing: {fragment}", failures)

    contract_path = test_root / "Fixtures" / "PhysicsBackendContract.h"
    if contract_path.is_file():
        contract_text = contract_path.read_text(encoding="utf-8", errors="ignore")
        # The word "PhysX" may legitimately appear in the usage comment, so the
        # backend-neutrality check targets implementation identifiers only.
        for forbidden in ("physx::", "PxPhysicsAPI", "JPH::", "Jolt/", "CreatePhysXBackend"):
            if forbidden in contract_text:
                fail(f"shared physics backend contract test is backend-specific: {forbidden}", failures)
        for fragment in (
            "RunPhysicsWorldLifecycleContract", "RunPhysicsSceneQueryContract",
            "RunPhysicsSimulationContract", "RunPhysicsTriggerContract",
            "Raycast", "Sweep", "Overlap", "CollisionEvent",
            "TriggerEvent", "SetKinematicTarget", "AddForce", "IsBodyValid",
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

    cmake_path = ROOT / "CMakeLists.txt"
    if cmake_path.is_file():
        cmake_text = cmake_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "add_library(SwimPhysics STATIC", "add_library(Swim::Physics ALIAS SwimPhysics)",
            "add_library(SwimPhysicsPhysX STATIC", "add_library(Swim::PhysicsPhysX ALIAS SwimPhysicsPhysX)",
            "target_link_libraries(SwimPhysicsPhysX PUBLIC Swim::Physics PRIVATE Swim::PhysX)",
            'list(FILTER SWIM_PHYSICS_SOURCES EXCLUDE REGEX "/Source/Engine/Systems/Physics/Backends/")',
            'list(FILTER SWIM_ENGINE_SOURCES EXCLUDE REGEX "/Source/Engine/Systems/Physics/")',
            "include(cmake/MathDependencies.cmake)",
            "include(cmake/Tests.cmake)",
        ):
            if fragment not in cmake_text:
                fail(f"Phase 6 physics target boundary is missing from CMake: {fragment}", failures)

        swim_physics_link = re.search(r"target_link_libraries\(SwimPhysics\s+([^\)]*)\)", cmake_text, re.DOTALL)
        if not swim_physics_link:
            fail("SwimPhysics target has no explicit dependency boundary", failures)
        else:
            link_text = swim_physics_link.group(1)
            for forbidden in ("Swim::PhysX", "PhysX", "Jolt", "EnTT::EnTT"):
                if forbidden in link_text:
                    fail(f"Swim::Physics links backend/scene implementation dependency directly: {forbidden}", failures)

        engine_links = re.findall(r"target_link_libraries\(SwimEngine\s+([^\)]*)\)", cmake_text, re.DOTALL)
        if not engine_links:
            fail("legacy runtime has no explicit target_link_libraries dependency declaration", failures)
        else:
            link_text = "\n".join(engine_links)
            for fragment in ("Swim::Physics", "Swim::PhysicsPhysX"):
                if fragment not in link_text:
                    fail(f"legacy runtime is missing explicit physics target dependency: {fragment}", failures)
            if "Swim::PhysX" in link_text:
                fail("legacy runtime bypasses Swim::PhysicsPhysX and links raw PhysX", failures)

    cmake_path = ROOT / "CMakeLists.txt"
    if cmake_path.is_file():
        cmake_text = cmake_path.read_text(encoding="utf-8", errors="ignore")
        legacy_return = cmake_text.find("if(NOT SWIM_BUILD_LEGACY_ENGINE)")
        generic_physics = cmake_text.find("# Generic physics is part of the cross-platform foundation")
        if legacy_return < 0 or generic_physics < 0 or generic_physics > legacy_return:
            fail("generic Swim::Physics is not compiled before the non-Windows legacy-runtime return", failures)

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
        ):
            if fragment not in tests_cmake_text:
                fail(f"Phase 6 physics test boundary is missing from cmake/Tests.cmake: {fragment}", failures)

    math_dependencies_path = ROOT / "cmake" / "MathDependencies.cmake"
    if math_dependencies_path.is_file():
        math_text = math_dependencies_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in ("GITHUB_REPOSITORY g-truc/glm", "GIT_TAG 1.0.0", "add_library(glm::glm ALIAS SwimGlm)"):
            if fragment not in math_text:
                fail(f"cross-platform GLM foundation dependency is missing: {fragment}", failures)

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

    windows_helper_path = ROOT / "scripts" / "windows-build-common.ps1"
    if windows_helper_path.is_file():
        helper_text = windows_helper_path.read_text(encoding="utf-8", errors="ignore")
        for fragment in (
            "Invoke-SwimWindowsTestSuite", "SwimTests",
            "SwimPhysicsPublicHeaders", "SwimPhysicsBackendContractCompile",
        ):
            if fragment not in helper_text:
                fail(f"Windows physics validation gate is missing: {fragment}", failures)

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
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES | {".glsl", ".hlsl"}:
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
    check_phase4_asset_architecture(failures)
    check_phase5_scene_architecture(failures)
    check_phase6_physics_architecture(failures)
    check_runtime_logging_contract(failures)
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
