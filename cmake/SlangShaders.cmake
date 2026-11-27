include_guard(GLOBAL)

# Adds one deterministic Slang compilation unit. Entry-point/stage intent stays
# in the shader through [shader(...)] attributes unless ENTRY_POINT is supplied
# for a source that deliberately exposes multiple compile roots.
#
# Outputs are returned through <name>_OUTPUT, <name>_REFLECTION and
# <name>_DEPFILE. SPIR-V programs also expose <name>_SPIRV for compatibility.
function(swim_add_slang_program name)
	cmake_parse_arguments(SWIM_SLANG_PROGRAM
		"REQUIRED"
		"SOURCE;OUTPUT_DIRECTORY;OUTPUT_NAME;TARGET;PROFILE;ENTRY_POINT;DEPFILE_DIRECTORY;RUNTIME_NAME"
		"INCLUDE_DIRECTORIES;DEFINES"
		${ARGN}
	)

	if(NOT SWIM_SLANG_PROGRAM_SOURCE)
		message(FATAL_ERROR "swim_add_slang_program(${name}) requires SOURCE")
	endif()
	if(NOT TARGET SwimSlangCompiler)
		message(FATAL_ERROR "swim_add_slang_program(${name}) requires the pinned Slang compiler SDK")
	endif()

	if(NOT IS_ABSOLUTE "${SWIM_SLANG_PROGRAM_SOURCE}")
		set(SWIM_SLANG_PROGRAM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${SWIM_SLANG_PROGRAM_SOURCE}")
	endif()
	if(NOT EXISTS "${SWIM_SLANG_PROGRAM_SOURCE}")
		message(FATAL_ERROR "Slang source does not exist: ${SWIM_SLANG_PROGRAM_SOURCE}")
	endif()

	if(NOT SWIM_SLANG_PROGRAM_OUTPUT_DIRECTORY)
		set(SWIM_SLANG_PROGRAM_OUTPUT_DIRECTORY
			"${CMAKE_CURRENT_BINARY_DIR}/Generated/Shaders/${name}"
		)
	endif()
	if(NOT SWIM_SLANG_PROGRAM_OUTPUT_NAME)
		set(SWIM_SLANG_PROGRAM_OUTPUT_NAME "${name}")
	endif()
	if(NOT SWIM_SLANG_PROGRAM_TARGET)
		set(SWIM_SLANG_PROGRAM_TARGET "spirv")
	endif()

	if(SWIM_SLANG_PROGRAM_TARGET STREQUAL "spirv")
		if(NOT SWIM_SLANG_PROGRAM_PROFILE)
			set(SWIM_SLANG_PROGRAM_PROFILE "spirv_1_5")
		endif()
		set(SWIM_SLANG_PROGRAM_EXTENSION ".spv")

		# Deliberately no -fspv-reflect. It embeds SPV_GOOGLE_user_type /
		# SPV_GOOGLE_hlsl_functionality1 decorations, which make every
		# vkCreateShaderModule call a validation error unless the matching
		# VK_GOOGLE_* device extensions are enabled. Swim reads reflection from
		# the -reflection-json sidecar instead, which is byte-identical either
		# way, so the decorations buy nothing and cost validity.
		#
		# Slang otherwise rewrites every SPIR-V entry point to `main`. The RHI
		# deliberately carries source/reflection entry-point names through its
		# public shader artifacts, so preserve those names in OpEntryPoint too.
		# Sampled images are constrained by numeric class, not storage format:
		# a Texture2D<uint> array may contain both R8_UINT and R32_UINT views.
		# Do not let Slang infer R32ui for that array. Writable images retain
		# their explicit [format(...)] qualifiers, required by RHI reflection.
		set(SWIM_SLANG_PROGRAM_TARGET_ARGS -fvk-use-entrypoint-name -default-image-format-unknown)
		if(SWIM_SHADER_DEBUG_INFO)
			# Source-level debug info (NonSemantic.Shader.DebugInfo, sources embedded) for GPU
			# profilers, at the default optimisation level, so the measured code is the release code.
			list(APPEND SWIM_SLANG_PROGRAM_TARGET_ARGS -g2 -debug-info-include-source)
		endif()
	elseif(SWIM_SLANG_PROGRAM_TARGET STREQUAL "glsl")
		if(NOT SWIM_SLANG_PROGRAM_PROFILE)
			set(SWIM_SLANG_PROGRAM_PROFILE "glsl_460")
		endif()
		set(SWIM_SLANG_PROGRAM_EXTENSION ".glsl")
		set(SWIM_SLANG_PROGRAM_TARGET_ARGS "")
	else()
		message(FATAL_ERROR
			"swim_add_slang_program(${name}) does not support target '${SWIM_SLANG_PROGRAM_TARGET}'"
		)
	endif()

	set(SWIM_SLANG_PROGRAM_OUTPUT
		"${SWIM_SLANG_PROGRAM_OUTPUT_DIRECTORY}/${SWIM_SLANG_PROGRAM_OUTPUT_NAME}${SWIM_SLANG_PROGRAM_EXTENSION}")
	set(SWIM_SLANG_PROGRAM_REFLECTION
		"${SWIM_SLANG_PROGRAM_OUTPUT_DIRECTORY}/${SWIM_SLANG_PROGRAM_OUTPUT_NAME}.reflection.json")
	# Depfiles are build metadata, not shader artifacts. Keeping them out of the
	# output directory means a caller can deploy that directory wholesale without
	# shipping build scaffolding beside the executable.
	if(NOT SWIM_SLANG_PROGRAM_DEPFILE_DIRECTORY)
		set(SWIM_SLANG_PROGRAM_DEPFILE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Generated/ShaderDeps")
	endif()
	set(SWIM_SLANG_PROGRAM_DEPFILE
		"${SWIM_SLANG_PROGRAM_DEPFILE_DIRECTORY}/${name}.d")

	set(SWIM_SLANG_PROGRAM_INCLUDE_ARGS "")
	foreach(SWIM_INCLUDE_DIRECTORY IN LISTS SWIM_SLANG_PROGRAM_INCLUDE_DIRECTORIES)
		if(NOT IS_ABSOLUTE "${SWIM_INCLUDE_DIRECTORY}")
			set(SWIM_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${SWIM_INCLUDE_DIRECTORY}")
		endif()
		list(APPEND SWIM_SLANG_PROGRAM_INCLUDE_ARGS -I "${SWIM_INCLUDE_DIRECTORY}")
	endforeach()

	set(SWIM_SLANG_PROGRAM_DEFINE_ARGS "")
	foreach(SWIM_DEFINE IN LISTS SWIM_SLANG_PROGRAM_DEFINES)
		list(APPEND SWIM_SLANG_PROGRAM_DEFINE_ARGS "-D${SWIM_DEFINE}")
	endforeach()

	set(SWIM_SLANG_PROGRAM_ENTRY_ARGS "")
	if(SWIM_SLANG_PROGRAM_ENTRY_POINT)
		list(APPEND SWIM_SLANG_PROGRAM_ENTRY_ARGS -entry "${SWIM_SLANG_PROGRAM_ENTRY_POINT}")
	endif()

	add_custom_command(
		OUTPUT
			"${SWIM_SLANG_PROGRAM_OUTPUT}"
			"${SWIM_SLANG_PROGRAM_REFLECTION}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${SWIM_SLANG_PROGRAM_OUTPUT_DIRECTORY}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${SWIM_SLANG_PROGRAM_DEPFILE_DIRECTORY}"
		# Keep shader-visible matrix memory identical to the retired DXC path. Slang
		# defaults to row-major matrix memory, while DXC/HLSL defaulted to column-major;
		# changing this silently transposes CPU-uploaded CameraUBO/model matrices.
		COMMAND "$<TARGET_FILE:SwimSlangCompiler>"
			"${SWIM_SLANG_PROGRAM_SOURCE}"
			-lang slang
			-target "${SWIM_SLANG_PROGRAM_TARGET}"
			-profile "${SWIM_SLANG_PROGRAM_PROFILE}"
			-matrix-layout-column-major
			-preserve-params
			${SWIM_SLANG_PROGRAM_TARGET_ARGS}
			${SWIM_SLANG_PROGRAM_ENTRY_ARGS}
			${SWIM_SLANG_PROGRAM_INCLUDE_ARGS}
			${SWIM_SLANG_PROGRAM_DEFINE_ARGS}
			-reflection-json "${SWIM_SLANG_PROGRAM_REFLECTION}"
			-depfile "${SWIM_SLANG_PROGRAM_DEPFILE}"
			-o "${SWIM_SLANG_PROGRAM_OUTPUT}"
		DEPENDS "${SWIM_SLANG_PROGRAM_SOURCE}" SwimSlangCompiler
		DEPFILE "${SWIM_SLANG_PROGRAM_DEPFILE}"
		COMMENT "Compiling Slang program ${name} -> ${SWIM_SLANG_PROGRAM_TARGET} + reflection"
		VERBATIM
		COMMAND_EXPAND_LISTS
	)

	# The same program as DXIL for the Direct3D 12 backend: <name>.dxil (one container per
	# entry point) and <name>.dxil.reflection.json (its registers, matched to the SPIR-V
	# bindings by parameter name at load).
	if(SWIM_SLANG_DXC_DIR AND SWIM_SLANG_PROGRAM_TARGET STREQUAL "spirv")
		set(SWIM_SLANG_PROGRAM_DXIL "${SWIM_SLANG_PROGRAM_OUTPUT_DIRECTORY}/${SWIM_SLANG_PROGRAM_OUTPUT_NAME}.dxil")
		set(SWIM_SLANG_PROGRAM_DXIL_REFLECTION "${SWIM_SLANG_PROGRAM_OUTPUT_DIRECTORY}/${SWIM_SLANG_PROGRAM_OUTPUT_NAME}.dxil.reflection.json")
		set(SWIM_SLANG_PROGRAM_DXIL_DEPFILE "${SWIM_SLANG_PROGRAM_DEPFILE_DIRECTORY}/${name}.dxil.d")
		set(SWIM_SLANG_PROGRAM_INCLUDE_TEXT "")
		foreach(SWIM_INCLUDE_DIRECTORY IN LISTS SWIM_SLANG_PROGRAM_INCLUDE_DIRECTORIES)
			if(NOT IS_ABSOLUTE "${SWIM_INCLUDE_DIRECTORY}")
				set(SWIM_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${SWIM_INCLUDE_DIRECTORY}")
			endif()
			list(APPEND SWIM_SLANG_PROGRAM_INCLUDE_TEXT "${SWIM_INCLUDE_DIRECTORY}")
		endforeach()
		string(REPLACE ";" "|" SWIM_SLANG_PROGRAM_INCLUDE_TEXT "${SWIM_SLANG_PROGRAM_INCLUDE_TEXT}")
		string(REPLACE ";" "|" SWIM_SLANG_PROGRAM_DEFINE_TEXT "${SWIM_SLANG_PROGRAM_DEFINES}")
		add_custom_command(
			OUTPUT
				"${SWIM_SLANG_PROGRAM_DXIL}"
				"${SWIM_SLANG_PROGRAM_DXIL_REFLECTION}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${SWIM_SLANG_PROGRAM_OUTPUT_DIRECTORY}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${SWIM_SLANG_PROGRAM_DEPFILE_DIRECTORY}"
			COMMAND "${CMAKE_COMMAND}"
				"-DSLANGC=$<TARGET_FILE:SwimSlangCompiler>"
				"-DSOURCE=${SWIM_SLANG_PROGRAM_SOURCE}"
				"-DOUTPUT=${SWIM_SLANG_PROGRAM_DXIL}"
				"-DREFLECTION=${SWIM_SLANG_PROGRAM_DXIL_REFLECTION}"
				"-DDEPFILE=${SWIM_SLANG_PROGRAM_DXIL_DEPFILE}"
				"-DDXC_DIR=${SWIM_SLANG_DXC_DIR}"
				"-DPROFILE=${SWIM_SLANG_DXIL_PROFILE}"
				"-DENTRY=${SWIM_SLANG_PROGRAM_ENTRY_POINT}"
				"-DINCLUDES=${SWIM_SLANG_PROGRAM_INCLUDE_TEXT}"
				"-DDEFINES=${SWIM_SLANG_PROGRAM_DEFINE_TEXT}"
				-P "${SWIM_ENGINE_SOURCE_DIR}/cmake/SlangDxil.cmake"
			DEPENDS "${SWIM_SLANG_PROGRAM_SOURCE}" SwimSlangCompiler "${SWIM_ENGINE_SOURCE_DIR}/cmake/SlangDxil.cmake"
			DEPFILE "${SWIM_SLANG_PROGRAM_DXIL_DEPFILE}"
			COMMENT "Compiling Slang program ${name} -> dxil + reflection"
			VERBATIM
		)
		set(${name}_DXIL "${SWIM_SLANG_PROGRAM_DXIL}" PARENT_SCOPE)
		set(${name}_DXIL_REFLECTION "${SWIM_SLANG_PROGRAM_DXIL_REFLECTION}" PARENT_SCOPE)
		set_property(GLOBAL APPEND PROPERTY SWIM_SLANG_DXIL_OUTPUTS "${SWIM_SLANG_PROGRAM_DXIL}" "${SWIM_SLANG_PROGRAM_DXIL_REFLECTION}")
	endif()

	if(SWIM_SLANG_PROGRAM_RUNTIME_NAME)
		swim_register_runtime_shader("${SWIM_SLANG_PROGRAM_RUNTIME_NAME}" "${name}" "${SWIM_SLANG_PROGRAM_REQUIRED}")
	endif()

	set(${name}_OUTPUT "${SWIM_SLANG_PROGRAM_OUTPUT}" PARENT_SCOPE)
	set(${name}_REFLECTION "${SWIM_SLANG_PROGRAM_REFLECTION}" PARENT_SCOPE)
	set(${name}_DEPFILE "${SWIM_SLANG_PROGRAM_DEPFILE}" PARENT_SCOPE)
	if(SWIM_SLANG_PROGRAM_TARGET STREQUAL "spirv")
		set(${name}_SPIRV "${SWIM_SLANG_PROGRAM_OUTPUT}" PARENT_SCOPE)
	endif()
endfunction()


# Shared registry for engine and gameplay shader artifacts. A name is an identifier,
# not a filesystem path; duplicates fail at configure time instead of racing copies.
function(swim_register_runtime_shader name program required)
	if(NOT name MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
		message(FATAL_ERROR "Invalid runtime shader name: ${name}")
	endif()
	if(TARGET SwimRuntimeShaders)
		message(FATAL_ERROR "Register runtime shaders before defining/deploying SwimRuntimeShaders")
	endif()
	get_property(names GLOBAL PROPERTY SWIM_RUNTIME_SHADER_NAMES)
	if(name IN_LIST names)
		message(FATAL_ERROR "Duplicate runtime shader name: ${name}")
	endif()
	set_property(GLOBAL APPEND PROPERTY SWIM_RUNTIME_SHADER_NAMES "${name}")
	set_property(GLOBAL APPEND PROPERTY SWIM_RUNTIME_SHADER_PROGRAMS "${name}=${program}")
	if(required)
		set_property(GLOBAL APPEND PROPERTY SWIM_REQUIRED_RUNTIME_SHADERS "${name}")
	endif()
endfunction()

# The source manifest of every runtime shader, for the development shader hot reload: one
# line per program, tab separated: name, source, entry point, include directories and
# defines (both '|' separated), the build's depfile. Written at the end of the configure
# step (so programs an application adds after the engine are included) to
# SWIM_RUNTIME_SHADER_SOURCE_MANIFEST; DevPaths.cfg points the engine at it.
set(SWIM_RUNTIME_SHADER_SOURCE_MANIFEST "${CMAKE_BINARY_DIR}/Generated/Shaders/RuntimeShaderSources.txt"
	CACHE INTERNAL "Runtime shader source manifest")

function(swim_write_runtime_shader_sources)
	get_property(lines GLOBAL PROPERTY SWIM_RUNTIME_SHADER_SOURCE_LINES)
	set(content "")
	foreach(line IN LISTS lines)
		string(APPEND content "${line}\n")
	endforeach()
	set(path "${SWIM_RUNTIME_SHADER_SOURCE_MANIFEST}")
	set(previous "")
	if(EXISTS "${path}")
		file(READ "${path}" previous)
	endif()
	if(NOT previous STREQUAL content)
		file(WRITE "${path}" "${content}")
	endif()
endfunction()

function(swim_record_runtime_shader_source name source entry includes defines depfile)
	if(NOT IS_ABSOLUTE "${source}")
		set(source "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
	endif()
	set(include_list "${SWIM_ENGINE_SOURCE_DIR}/Source/Shaders/Slang")
	foreach(directory IN LISTS includes)
		if(NOT IS_ABSOLUTE "${directory}")
			set(directory "${CMAKE_CURRENT_SOURCE_DIR}/${directory}")
		endif()
		list(APPEND include_list "${directory}")
	endforeach()
	string(REPLACE ";" "|" include_text "${include_list}")
	string(REPLACE ";" "|" define_text "${defines}")
	set_property(GLOBAL APPEND PROPERTY SWIM_RUNTIME_SHADER_SOURCE_LINES
		"${name}\t${source}\t${entry}\t${include_text}\t${define_text}\t${depfile}")
	get_property(scheduled GLOBAL PROPERTY SWIM_RUNTIME_SHADER_SOURCES_SCHEDULED)
	if(NOT scheduled)
		set_property(GLOBAL PROPERTY SWIM_RUNTIME_SHADER_SOURCES_SCHEDULED TRUE)
		cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL swim_write_runtime_shader_sources)
	endif()
endfunction()

# Gameplay/features: one declaration compiles, registers and deploys both artifacts.
# REQUIRED is opt-in; optional feature programs do not gate the base renderer.
# An application that adds the engine with add_subdirectory calls it afterwards from
# its own CMakeLists (SOURCE relative to that directory): the engine's catalog is
# already generated then, so the program is staged into the same runtime set, deployed
# with it, and found by ShaderLibrary by name (<Name>.spv beside the built-ins).
function(swim_add_runtime_shader name)
	cmake_parse_arguments(SWIM_RUNTIME_SHADER "REQUIRED" "SOURCE;ENTRY_POINT" "DEFINES;INCLUDE_DIRECTORIES" ${ARGN})
	if(NOT SWIM_RUNTIME_SHADER_SOURCE)
		message(FATAL_ERROR "swim_add_runtime_shader(${name}) needs SOURCE")
	endif()
	set(program "Swim${name}")
	if(TARGET SwimRuntimeShaders)
		if(NOT name MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
			message(FATAL_ERROR "Invalid runtime shader name: ${name}")
		endif()
		get_property(names GLOBAL PROPERTY SWIM_RUNTIME_SHADER_NAMES)
		get_property(late GLOBAL PROPERTY SWIM_LATE_RUNTIME_SHADER_NAMES)
		if(name IN_LIST names OR name IN_LIST late)
			message(FATAL_ERROR "Duplicate runtime shader name: ${name}")
		endif()
		set_property(GLOBAL APPEND PROPERTY SWIM_LATE_RUNTIME_SHADER_NAMES "${name}")
		swim_add_slang_program(${program}
			SOURCE "${SWIM_RUNTIME_SHADER_SOURCE}"
			PROFILE spirv_1_5
			ENTRY_POINT "${SWIM_RUNTIME_SHADER_ENTRY_POINT}"
			INCLUDE_DIRECTORIES "${SWIM_ENGINE_SOURCE_DIR}/Source/Shaders/Slang" ${SWIM_RUNTIME_SHADER_INCLUDE_DIRECTORIES}
			DEFINES ${SWIM_RUNTIME_SHADER_DEFINES}
		)
		if(DEFINED SWIM_RUNTIME_SHADER_DIR AND NOT SWIM_OFFLINE_DEPENDENCY_STUBS)
			set(staged_spv "${SWIM_RUNTIME_SHADER_DIR}/${name}.spv")
			set(staged_json "${SWIM_RUNTIME_SHADER_DIR}/${name}.reflection.json")
			set(staged "${staged_spv}" "${staged_json}")
			set(sources "${${program}_SPIRV}" "${${program}_REFLECTION}")
			if(DEFINED ${program}_DXIL)
				list(APPEND staged "${SWIM_RUNTIME_SHADER_DIR}/${name}.dxil" "${SWIM_RUNTIME_SHADER_DIR}/${name}.dxil.reflection.json")
				list(APPEND sources "${${program}_DXIL}" "${${program}_DXIL_REFLECTION}")
			endif()
			set(copies "")
			foreach(source destination IN ZIP_LISTS sources staged)
				list(APPEND copies COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${source}" "${destination}")
			endforeach()
			add_custom_command(
				OUTPUT ${staged}
				COMMAND "${CMAKE_COMMAND}" -E make_directory "${SWIM_RUNTIME_SHADER_DIR}"
				${copies}
				DEPENDS ${sources}
				COMMENT "Staging runtime shader ${name}"
				VERBATIM
			)
			add_custom_target(${program}Stage DEPENDS ${staged})
			swim_set_solution_folder(${program}Stage "${SWIM_SOLUTION_FOLDER_TOOLS}")
			add_dependencies(SwimRuntimeShaders ${program}Stage)
		endif()
		swim_record_runtime_shader_source("${name}" "${SWIM_RUNTIME_SHADER_SOURCE}" "${SWIM_RUNTIME_SHADER_ENTRY_POINT}"
			"${SWIM_RUNTIME_SHADER_INCLUDE_DIRECTORIES}" "${SWIM_RUNTIME_SHADER_DEFINES}" "${${program}_DEPFILE}")
		set(${program}_SPIRV "${${program}_SPIRV}" PARENT_SCOPE)
		set(${program}_REFLECTION "${${program}_REFLECTION}" PARENT_SCOPE)
		return()
	endif()
	set(required "")
	if(SWIM_RUNTIME_SHADER_REQUIRED)
		set(required REQUIRED)
	endif()
	swim_add_slang_program(${program}
		SOURCE "${SWIM_RUNTIME_SHADER_SOURCE}"
		RUNTIME_NAME "${name}"
		${required}
		PROFILE spirv_1_5
		ENTRY_POINT "${SWIM_RUNTIME_SHADER_ENTRY_POINT}"
		INCLUDE_DIRECTORIES "${SWIM_ENGINE_SOURCE_DIR}/Source/Shaders/Slang" ${SWIM_RUNTIME_SHADER_INCLUDE_DIRECTORIES}
		DEFINES ${SWIM_RUNTIME_SHADER_DEFINES}
	)
	swim_record_runtime_shader_source("${name}" "${SWIM_RUNTIME_SHADER_SOURCE}" "${SWIM_RUNTIME_SHADER_ENTRY_POINT}"
		"${SWIM_RUNTIME_SHADER_INCLUDE_DIRECTORIES}" "${SWIM_RUNTIME_SHADER_DEFINES}" "${${program}_DEPFILE}")
	set(${program}_SPIRV "${${program}_SPIRV}" PARENT_SCOPE)
	set(${program}_REFLECTION "${${program}_REFLECTION}" PARENT_SCOPE)
	set_property(GLOBAL APPEND PROPERTY SWIM_EXTRA_RUNTIME_SHADER_FILES "${${program}_SPIRV}" "${${program}_REFLECTION}")
	if(DEFINED ${program}_DXIL)
		set(${program}_DXIL "${${program}_DXIL}" PARENT_SCOPE)
		set(${program}_DXIL_REFLECTION "${${program}_DXIL_REFLECTION}" PARENT_SCOPE)
		set_property(GLOBAL APPEND PROPERTY SWIM_EXTRA_RUNTIME_SHADER_FILES "${${program}_DXIL}" "${${program}_DXIL_REFLECTION}")
	endif()
endfunction()
