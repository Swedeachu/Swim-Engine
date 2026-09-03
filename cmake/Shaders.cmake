function(swim_configure_shaders target)
	if(SWIM_OFFLINE_DEPENDENCY_STUBS)
		return()
	endif()

	find_program(SWIM_DXC_EXECUTABLE
		NAMES dxc dxc.exe
		HINTS
			"$ENV{VULKAN_SDK}/Bin"
			"$ENV{VULKAN_SDK}/Bin32"
	)

	if(NOT SWIM_DXC_EXECUTABLE)
		message(FATAL_ERROR
			"DXC was not found. Install the Vulkan SDK with DXC support or put dxc.exe on PATH."
		)
	endif()

	file(GLOB_RECURSE SWIM_VERTEX_SHADERS
		"${CMAKE_SOURCE_DIR}/Source/Shaders/Vulkan/VertexShaders/*.hlsl"
	)
	file(GLOB_RECURSE SWIM_FRAGMENT_SHADERS
		"${CMAKE_SOURCE_DIR}/Source/Shaders/Vulkan/FragmentShaders/*.hlsl"
	)
	file(GLOB_RECURSE SWIM_COMPUTE_SHADERS
		"${CMAKE_SOURCE_DIR}/Source/Shaders/Vulkan/ComputeShaders/*.hlsl"
	)

	foreach(SWIM_SHADER IN LISTS SWIM_VERTEX_SHADERS)
		get_filename_component(SWIM_SHADER_NAME "${SWIM_SHADER}" NAME_WE)
		add_custom_command(TARGET ${target} PRE_BUILD
			COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/Shaders/VertexShaders"
			COMMAND "${SWIM_DXC_EXECUTABLE}" -T vs_6_0 -E main -spirv -fspv-target-env=vulkan1.2
				-Fo "$<TARGET_FILE_DIR:${target}>/Shaders/VertexShaders/${SWIM_SHADER_NAME}.spv" "${SWIM_SHADER}"
			VERBATIM
		)
	endforeach()

	foreach(SWIM_SHADER IN LISTS SWIM_FRAGMENT_SHADERS)
		get_filename_component(SWIM_SHADER_NAME "${SWIM_SHADER}" NAME_WE)
		add_custom_command(TARGET ${target} PRE_BUILD
			COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/Shaders/FragmentShaders"
			COMMAND "${SWIM_DXC_EXECUTABLE}" -T ps_6_0 -E main -spirv -fspv-target-env=vulkan1.2
				-Fo "$<TARGET_FILE_DIR:${target}>/Shaders/FragmentShaders/${SWIM_SHADER_NAME}.spv" "${SWIM_SHADER}"
			VERBATIM
		)
	endforeach()

	foreach(SWIM_SHADER IN LISTS SWIM_COMPUTE_SHADERS)
		get_filename_component(SWIM_SHADER_NAME "${SWIM_SHADER}" NAME_WE)
		add_custom_command(TARGET ${target} PRE_BUILD
			COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/Shaders/ComputeShaders"
			COMMAND "${SWIM_DXC_EXECUTABLE}" -T cs_6_0 -E main -spirv -fspv-target-env=vulkan1.2
				-Fo "$<TARGET_FILE_DIR:${target}>/Shaders/ComputeShaders/${SWIM_SHADER_NAME}.spv" "${SWIM_SHADER}"
			VERBATIM
		)
	endforeach()
endfunction()
