# Swim Engine test build rules.
#
# The whole runnable test corpus is one program, `SwimTests`. Suites live under
# `Source/Tests/Suites/<group>` and register themselves through static
# initializers, so adding coverage never means adding a CMake target: drop a
# `.cpp` into the group directory whose dependencies it needs and the next
# configure picks it up.
#
# Header-boundary gates stay separate on purpose. Their whole value is that each
# one links only its own module, proving that module's public headers do not
# require anything else. Folding them into `SwimTests` (which links everything)
# would silently destroy that guarantee.

# Compiles one public-header/architecture gate as an object library.
#
#   swim_add_header_boundary(<name>
#     SOURCE <path>
#     [LINK <targets>...]
#     [INCLUDE <dirs>...]
#     [BUILD_BY_DEFAULT])
function(swim_add_header_boundary TARGET_NAME)
	cmake_parse_arguments(SWIM_BOUNDARY "BUILD_BY_DEFAULT" "SOURCE" "LINK;INCLUDE" ${ARGN})

	if(NOT SWIM_BOUNDARY_SOURCE)
		message(FATAL_ERROR "swim_add_header_boundary(${TARGET_NAME}) requires SOURCE")
	endif()

	if(SWIM_BOUNDARY_BUILD_BY_DEFAULT)
		add_library(${TARGET_NAME} OBJECT ${SWIM_BOUNDARY_SOURCE})
	else()
		add_library(${TARGET_NAME} OBJECT EXCLUDE_FROM_ALL ${SWIM_BOUNDARY_SOURCE})
	endif()

	target_include_directories(${TARGET_NAME} PRIVATE
		${CMAKE_SOURCE_DIR}/Source
		${SWIM_BOUNDARY_INCLUDE}
	)
	target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)

	if(SWIM_BOUNDARY_LINK)
		target_link_libraries(${TARGET_NAME} PRIVATE ${SWIM_BOUNDARY_LINK})
	endif()

	if(MSVC)
		target_compile_options(${TARGET_NAME} PRIVATE /utf-8 /W3 /permissive-)
	endif()

	source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SWIM_BOUNDARY_SOURCE})
	swim_set_solution_folder(${TARGET_NAME} "${SWIM_SOLUTION_FOLDER_TESTS}/Header Boundary")
endfunction()

# Collects every suite source under the given `Source/Tests/Suites` directories.
function(swim_collect_test_suite_sources OUT_VARIABLE)
	set(COLLECTED "")
	foreach(SUITE_DIRECTORY ${ARGN})
		file(GLOB_RECURSE GROUP_SOURCES
			${CMAKE_SOURCE_DIR}/Source/Tests/Suites/${SUITE_DIRECTORY}/*.cpp
			${CMAKE_SOURCE_DIR}/Source/Tests/Suites/${SUITE_DIRECTORY}/*.h
		)
		list(APPEND COLLECTED ${GROUP_SOURCES})
	endforeach()
	set(${OUT_VARIABLE} ${COLLECTED} PARENT_SCOPE)
endfunction()

# Defines the single `SwimTests` program plus the header-boundary gates.
#
# Call this once, from whichever point in the configure has the widest set of
# available dependency targets. Suite groups whose dependencies are absent in the
# current configuration are simply not compiled, so a Linux foundation build gets
# the foundation suites while a full Windows build gets everything.
function(swim_configure_tests)
	if(TARGET SwimTests)
		return()
	endif()

	# --- Header-boundary gates ---------------------------------------------

	swim_add_header_boundary(SwimPlatformPublicHeaders
		SOURCE Source/Tests/HeaderBoundary/PlatformPublicHeaders.cpp)

	swim_add_header_boundary(SwimIoPublicHeaders
		SOURCE Source/Tests/HeaderBoundary/IoPublicHeaders.cpp)

	swim_add_header_boundary(SwimAssetPublicHeaders
		SOURCE Source/Tests/HeaderBoundary/AssetsPublicHeaders.cpp)

	swim_add_header_boundary(SwimAssetCompilerPublicHeaders
		SOURCE Source/Tests/HeaderBoundary/AssetCompilerPublicHeaders.cpp)

	swim_add_header_boundary(SwimShaderCompilerPublicHeaders
		SOURCE Source/Tests/HeaderBoundary/ShaderCompilerPublicHeaders.cpp)

	# Swim::Rhi and Swim::RhiVulkan no longer exist as targets (their sources
	# compile directly into whichever real binary needs them -- see
	# CMakeLists.txt's "Engine module source discovery"). Neither gate needs a
	# LINK any more: both public headers only ever needed the default
	# ${CMAKE_SOURCE_DIR}/Source include dir that swim_add_header_boundary()
	# already sets, and an OBJECT library like this one never actually links,
	# so RhiVulkanPublicHeaders.cpp taking the address of
	# Swim::RhiVulkan::CreateGraphicsSystem only needs the declaration.
	swim_add_header_boundary(SwimRhiPublicHeaders
		SOURCE Source/Tests/HeaderBoundary/RhiPublicHeaders.cpp)

	if(SWIM_VULKAN_RHI_AVAILABLE)
		swim_add_header_boundary(SwimRhiVulkanPublicHeaders
			SOURCE Source/Tests/HeaderBoundary/RhiVulkanPublicHeaders.cpp
			BUILD_BY_DEFAULT)
	endif()

	if(NOT SWIM_OFFLINE_DEPENDENCY_STUBS)
		# Swim::Physics is also gone; its one real dependency was PUBLIC
		# glm::glm (the generic physics headers use glm types directly), so
		# that is what these gates link now instead of the module target.
		swim_add_header_boundary(SwimPhysicsPublicHeaders
			SOURCE Source/Tests/HeaderBoundary/PhysicsPublicHeaders.cpp
			LINK glm::glm)

		# The backend contract gate builds by default: it is the cheapest possible
		# guard against a physics backend leaking back into generic test code.
		swim_add_header_boundary(SwimPhysicsBackendContractCompile
			SOURCE Source/Tests/HeaderBoundary/PhysicsBackendContractCompile.cpp
			LINK glm::glm
			BUILD_BY_DEFAULT)
		target_sources(SwimPhysicsBackendContractCompile PRIVATE Source/Tests/Framework/Test.cpp)
	endif()

	# --- The single test program -------------------------------------------

	set(SWIM_TEST_FRAMEWORK_SOURCES
		${CMAKE_SOURCE_DIR}/Source/Tests/Framework/Test.h
		${CMAKE_SOURCE_DIR}/Source/Tests/Framework/Test.cpp
		${CMAKE_SOURCE_DIR}/Source/Tests/Framework/TestRunner.h
		${CMAKE_SOURCE_DIR}/Source/Tests/Framework/TestRunner.cpp
		${CMAKE_SOURCE_DIR}/Source/Tests/Framework/TestMain.cpp
	)

	# Foundation suites that remain fully buildable even when dependency stubs are
	# active. Offline mode deliberately has no SDL headers, so Platform/Input/IO
	# suites join the same single SwimTests program only in dependency-enabled builds.
	swim_collect_test_suite_sources(SWIM_TEST_SUITE_SOURCES
		Core
		Memory
		Jobs
		Assets
		Physics/Generic
		Scene/Headless
		RHI
		Commands
	)

	set(SWIM_TEST_FIXTURE_SOURCES "")
	set(SWIM_TEST_LINK_LIBRARIES "")

	# SwimCore/Commands/Memory/Jobs/Assets/Rhi are no longer separate targets
	# (see CMakeLists.txt's "Engine module source discovery"), so SwimTests
	# compiles their sources directly instead of linking module libraries.
	# SWIM_TEST_MODULE_SOURCES is a straight union of disjoint source lists,
	# never a target -- the one place duplication could happen is Assets,
	# which SwimAssetCompiler also embeds: when that target exists, SwimTests
	# gets Assets through linking it instead of compiling Assets a second time
	# into the same binary.
	set(SWIM_TEST_MODULE_SOURCES
		${SWIM_CORE_SOURCES}
		${SWIM_COMMANDS_SOURCES}
		${SWIM_MEMORY_SOURCES}
		${SWIM_JOB_SOURCES}
	)
	list(APPEND SWIM_TEST_LINK_LIBRARIES Swim::Mimalloc Swim::EnkiTS)

	if(NOT TARGET SwimAssetCompiler)
		list(APPEND SWIM_TEST_MODULE_SOURCES ${SWIM_ASSET_SOURCES})
	endif()

	if(NOT SWIM_OFFLINE_DEPENDENCY_STUBS)
		swim_collect_test_suite_sources(SWIM_PLATFORM_FOUNDATION_SUITES
			IO
			Input
		)
		list(APPEND SWIM_TEST_SUITE_SOURCES ${SWIM_PLATFORM_FOUNDATION_SUITES})
		list(APPEND SWIM_TEST_MODULE_SOURCES
			${SWIM_IO_SOURCES}
			${SWIM_PLATFORM_SOURCES}
			${SWIM_INPUT_SOURCES}
			${SWIM_PHYSICS_SOURCES}
		)
		list(APPEND SWIM_TEST_LINK_LIBRARIES ${SWIM_SDL3_TARGET} glm::glm)
	endif()

	find_package(Threads REQUIRED)
	list(APPEND SWIM_TEST_LINK_LIBRARIES Threads::Threads)

	if(TARGET SwimAssetCompiler)
		swim_collect_test_suite_sources(SWIM_ASSET_COMPILER_SUITES AssetCompiler)
		list(APPEND SWIM_TEST_SUITE_SOURCES ${SWIM_ASSET_COMPILER_SUITES})
		list(APPEND SWIM_TEST_FIXTURE_SOURCES ${CMAKE_SOURCE_DIR}/Source/Tests/Fixtures/DracoTriangleFixture.h)
		list(APPEND SWIM_TEST_LINK_LIBRARIES Swim::AssetCompiler Swim::AssetCompilerDraco)
	endif()

	if(TARGET SwimShaderCompiler)
		swim_collect_test_suite_sources(SWIM_SHADER_COMPILER_SUITES ShaderCompiler)
		list(APPEND SWIM_TEST_SUITE_SOURCES ${SWIM_SHADER_COMPILER_SUITES})
		list(APPEND SWIM_TEST_LINK_LIBRARIES Swim::ShaderCompiler)
	endif()

	# Scene/ECS suites consume EnTT and the legacy renderer-facing headers, which
	# only exist once the full runtime dependency surface is configured.
	if(TARGET EnTT::EnTT)
		swim_collect_test_suite_sources(SWIM_SCENE_ECS_SUITES Scene/Ecs)
		list(APPEND SWIM_TEST_SUITE_SOURCES ${SWIM_SCENE_ECS_SUITES})
		list(APPEND SWIM_TEST_LINK_LIBRARIES EnTT::EnTT glm::glm)
	endif()

	if(SWIM_VULKAN_RHI_AVAILABLE)
		swim_collect_test_suite_sources(SWIM_VULKAN_RHI_SUITES RHIVulkan)
		list(APPEND SWIM_TEST_FIXTURE_SOURCES
			${CMAKE_SOURCE_DIR}/Source/Tests/Fixtures/VulkanCommandCapture.h
			${CMAKE_SOURCE_DIR}/Source/Tests/Fixtures/VulkanPipelineCapture.h
			${CMAKE_SOURCE_DIR}/Source/Tests/Fixtures/VulkanDescriptorCapture.h
		)
		list(APPEND SWIM_TEST_SUITE_SOURCES ${SWIM_VULKAN_RHI_SUITES})
		list(APPEND SWIM_TEST_MODULE_SOURCES ${SWIM_VULKAN_RHI_SOURCES})
		# Backend dispatch-spy suites inspect native commands without a GPU.
		# These dependencies remain private to the test executable.
		list(APPEND SWIM_TEST_LINK_LIBRARIES ${SWIM_VULKAN_RHI_PRIVATE_LIBS})
	endif()

	if(SWIM_PHYSX_BACKEND_AVAILABLE)
		swim_collect_test_suite_sources(SWIM_PHYSX_SUITES Physics/PhysX)
		list(APPEND SWIM_TEST_SUITE_SOURCES ${SWIM_PHYSX_SUITES})
		list(APPEND SWIM_TEST_FIXTURE_SOURCES ${CMAKE_SOURCE_DIR}/Source/Tests/Fixtures/PhysicsBackendContract.h)
		list(APPEND SWIM_TEST_MODULE_SOURCES ${SWIM_PHYSX_BACKEND_SOURCES})
		list(APPEND SWIM_TEST_LINK_LIBRARIES Swim::PhysX)
		if(TARGET SwimPhysXBuild)
			list(APPEND SWIM_TEST_EXTRA_DEPENDENCIES SwimPhysXBuild)
		endif()
	endif()

	if(SWIM_JOLT_BACKEND_AVAILABLE)
		swim_collect_test_suite_sources(SWIM_JOLT_SUITES Physics/Jolt)
		list(APPEND SWIM_TEST_SUITE_SOURCES ${SWIM_JOLT_SUITES})
		list(APPEND SWIM_TEST_FIXTURE_SOURCES ${CMAKE_SOURCE_DIR}/Source/Tests/Fixtures/PhysicsBackendContract.h)
		list(APPEND SWIM_TEST_MODULE_SOURCES ${SWIM_JOLT_BACKEND_SOURCES})
		list(APPEND SWIM_TEST_LINK_LIBRARIES Swim::Jolt)
	endif()

	list(REMOVE_DUPLICATES SWIM_TEST_FIXTURE_SOURCES)

	add_executable(SwimTests EXCLUDE_FROM_ALL
		${SWIM_TEST_FRAMEWORK_SOURCES}
		${SWIM_TEST_FIXTURE_SOURCES}
		${SWIM_TEST_SUITE_SOURCES}
		${SWIM_TEST_MODULE_SOURCES}
	)
	add_executable(Swim::Tests ALIAS SwimTests)

	if(SWIM_TEST_EXTRA_DEPENDENCIES)
		add_dependencies(SwimTests ${SWIM_TEST_EXTRA_DEPENDENCIES})
	endif()

	# Fixtures are header-only; keep them visible in the IDE without compiling.
	if(SWIM_TEST_FIXTURE_SOURCES)
		set_source_files_properties(${SWIM_TEST_FIXTURE_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)
	endif()

	target_include_directories(SwimTests PRIVATE ${CMAKE_SOURCE_DIR}/Source)
	target_compile_features(SwimTests PRIVATE cxx_std_20)
	target_link_libraries(SwimTests PRIVATE ${SWIM_TEST_LINK_LIBRARIES})

	# Compile definitions that used to be PRIVATE to a module's own retired
	# target now apply directly to SwimTests, since those modules' sources
	# compile as part of it.
	target_compile_definitions(SwimTests PRIVATE
		$<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
		$<$<PLATFORM_ID:Windows>:NOMINMAX>
		SWIM_MEMORY_USE_MIMALLOC=$<BOOL:${SWIM_MEMORY_USE_MIMALLOC}>
		SWIM_JOBS_USE_ENKITS=$<BOOL:${SWIM_JOBS_USE_ENKITS}>
	)
	if(SWIM_VULKAN_RHI_AVAILABLE)
		target_compile_definitions(SwimTests PRIVATE ${SWIM_VULKAN_RHI_PRIVATE_DEFINITIONS})
	endif()

	if(TARGET SwimRhiSmokeShaders)
		add_dependencies(SwimTests SwimRhiSmokeShaders)
		target_compile_definitions(SwimTests PRIVATE
			SWIM_RHI_TRIANGLE_SPIRV_PATH="${SwimRhiTriangle_SPIRV}"
			SWIM_RHI_TEXTURE_SPIRV_PATH="${SwimRhiTexture_SPIRV}"
			SWIM_RHI_TEXTURE_REFLECTION_PATH="${SwimRhiTexture_REFLECTION}"
		)
	endif()

	if(TARGET SwimSlangReflectionSample)
		add_dependencies(SwimTests SwimSlangReflectionSample)
		target_compile_definitions(SwimTests PRIVATE
			SWIM_SLANG_REFLECTION_SAMPLE_PATH="${SWIM_SLANG_REFLECTION_SAMPLE}"
			SWIM_SLANG_SPIRV_SAMPLE_PATH="${SWIM_SLANG_SPIRV_SAMPLE}"
		)
	endif()

	if(MSVC)
		# /Zc:preprocessor: MSVC's legacy (non-conforming) preprocessor can
		# mis-scan macro-argument boundaries around raw string literals that
		# contain literal '(' / ')' characters (e.g. a SWIM_CHECK(...) whose
		# argument is a raw string like R"(...)..."), silently truncating the
		# argument and leaking the rest of the literal as unrelated tokens.
		# The conforming preprocessor Microsoft ships behind this flag does
		# not have that gap; test code is exactly where raw strings-as-macro-
		# arguments come up, so this target opts in explicitly.
		target_compile_options(SwimTests PRIVATE /MP /utf-8 /W3 /permissive- /Zc:preprocessor /external:anglebrackets /external:W0)
		if(SWIM_WARNINGS_AS_ERRORS)
			target_compile_options(SwimTests PRIVATE /WX)
		endif()
	else()
		target_compile_options(SwimTests PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
		if(SWIM_WARNINGS_AS_ERRORS)
			target_compile_options(SwimTests PRIVATE -Werror)
		endif()
	endif()

	source_group(TREE ${CMAKE_SOURCE_DIR} FILES
		${SWIM_TEST_FRAMEWORK_SOURCES}
		${SWIM_TEST_FIXTURE_SOURCES}
		${SWIM_TEST_SUITE_SOURCES}
		${SWIM_TEST_MODULE_SOURCES}
	)
	swim_set_solution_folder(SwimTests "${SWIM_SOLUTION_FOLDER_TESTS}")

	set_property(TARGET SwimTests PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

	list(LENGTH SWIM_TEST_SUITE_SOURCES SWIM_TEST_SUITE_SOURCE_COUNT)
	message(STATUS "Swim tests: SwimTests compiles ${SWIM_TEST_SUITE_SOURCE_COUNT} suite source file(s)")
endfunction()
