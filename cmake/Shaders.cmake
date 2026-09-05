include_guard(GLOBAL)

function(swim_configure_shaders target)
	if(SWIM_OFFLINE_DEPENDENCY_STUBS)
		return()
	endif()

	if(NOT SWIM_BUILD_SHADER_COMPILER OR NOT SWIM_SLANG_AVAILABLE OR NOT TARGET SwimSlangCompiler)
		message(FATAL_ERROR
			"First-party shaders require the pinned Slang compiler. Enable SWIM_BUILD_SHADER_COMPILER."
		)
	endif()

	include(cmake/SlangShaders.cmake)

	set(SWIM_RUNTIME_SHADER_ROOT "${CMAKE_CURRENT_BINARY_DIR}/Generated/Shaders/Runtime")
	set(SWIM_RUNTIME_SHADER_OUTPUTS "")
	set(SWIM_RUNTIME_SHADER_REFLECTION "")

	set(SWIM_VULKAN_SHADER_GROUPS
		"VertexShaders"
		"FragmentShaders"
		"ComputeShaders"
	)

	foreach(SWIM_SHADER_GROUP IN LISTS SWIM_VULKAN_SHADER_GROUPS)
		file(GLOB SWIM_GROUP_SHADERS
			"${CMAKE_SOURCE_DIR}/Source/Shaders/Vulkan/${SWIM_SHADER_GROUP}/*.slang"
		)
		foreach(SWIM_SHADER IN LISTS SWIM_GROUP_SHADERS)
			get_filename_component(SWIM_SHADER_NAME "${SWIM_SHADER}" NAME_WE)
			string(MAKE_C_IDENTIFIER "Vulkan_${SWIM_SHADER_GROUP}_${SWIM_SHADER_NAME}" SWIM_SHADER_TARGET_NAME)
			swim_add_slang_program(${SWIM_SHADER_TARGET_NAME}
				SOURCE "${SWIM_SHADER}"
				OUTPUT_DIRECTORY "${SWIM_RUNTIME_SHADER_ROOT}/${SWIM_SHADER_GROUP}"
				OUTPUT_NAME "${SWIM_SHADER_NAME}"
				TARGET spirv
				PROFILE spirv_1_5
			)
			list(APPEND SWIM_RUNTIME_SHADER_OUTPUTS "${${SWIM_SHADER_TARGET_NAME}_OUTPUT}")
			list(APPEND SWIM_RUNTIME_SHADER_REFLECTION "${${SWIM_SHADER_TARGET_NAME}_REFLECTION}")
		endforeach()
	endforeach()

	file(GLOB SWIM_OPENGL_SHADERS
		"${CMAKE_SOURCE_DIR}/Source/Shaders/OpenGL/*.slang"
	)
	foreach(SWIM_SHADER IN LISTS SWIM_OPENGL_SHADERS)
		get_filename_component(SWIM_SHADER_NAME "${SWIM_SHADER}" NAME_WE)
		string(MAKE_C_IDENTIFIER "OpenGL_${SWIM_SHADER_NAME}" SWIM_SHADER_TARGET_NAME)
		# Unlike SPIR-V, a GLSL target emits one kernel per entry point, so slangc
		# requires the entry point to be named before its -o path. Every legacy
		# OpenGL shader is a single-entry module using the conventional name.
		swim_add_slang_program(${SWIM_SHADER_TARGET_NAME}
			SOURCE "${SWIM_SHADER}"
			OUTPUT_DIRECTORY "${SWIM_RUNTIME_SHADER_ROOT}/OpenGL"
			OUTPUT_NAME "${SWIM_SHADER_NAME}"
			TARGET glsl
			PROFILE glsl_460
			ENTRY_POINT main
		)
		list(APPEND SWIM_RUNTIME_SHADER_OUTPUTS "${${SWIM_SHADER_TARGET_NAME}_OUTPUT}")
		list(APPEND SWIM_RUNTIME_SHADER_REFLECTION "${${SWIM_SHADER_TARGET_NAME}_REFLECTION}")
	endforeach()

	if(NOT TARGET SwimShaderArtifacts)
		add_custom_target(SwimShaderArtifacts
			DEPENDS ${SWIM_RUNTIME_SHADER_OUTPUTS} ${SWIM_RUNTIME_SHADER_REFLECTION}
			SOURCES ${SWIM_SHADER_SOURCES}
		)
		swim_set_solution_folder(SwimShaderArtifacts "${SWIM_SOLUTION_FOLDER_TOOLS}")
	endif()

	add_dependencies(${target} SwimShaderArtifacts)
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_directory
			"${SWIM_RUNTIME_SHADER_ROOT}"
			"$<TARGET_FILE_DIR:${target}>/Shaders"
		VERBATIM
	)
endfunction()
