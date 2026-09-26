include_guard(GLOBAL)

# The engine's runtime shader set (Phase 23). Every program the FrameRenderer loads
# is compiled once by swim_add_slang_program (CMakeLists.txt, next to the native
# smokes that validate the same programs) and staged here under its runtime name:
#
#   ${CMAKE_BINARY_DIR}/Generated/Shaders/RuntimeSet/<Name>.spv
#   ${CMAKE_BINARY_DIR}/Generated/Shaders/RuntimeSet/<Name>.reflection.json
#
# SwimEngine deploys the directory as <exe dir>/Shaders/Runtime (ShaderLibrary's
# default root); SwimTests reads it through SWIM_RUNTIME_SHADER_DIR. The retired
# Vulkan/OpenGL legacy shader groups are archived under Deprecated/Shaders.
set(SWIM_RUNTIME_SHADER_PROGRAMS
	"Present=SwimRuntimePresent"
	"SkyBackground=SwimRuntimeSkyBackground"
	"GpuVisibility=SwimGpuVisibility"
	"HzbReduce=SwimHzbReduce"
	"ClusterLightCull=SwimClusterLightCull"
	"ClusterBounds=SwimClusterBounds"
	"ClusterAssign=SwimClusterAssign"
	"ClusterScan=SwimClusterScan"
	"ClusterHeatmap=SwimClusterHeatmap"
	"ForwardOpaque=SwimForwardOpaque"
	"ForwardTransparent=SwimForwardTransparent"
	"ForwardDepth=SwimForwardDepth"
	"ForwardOpaquePrepassed=SwimForwardOpaquePrepassed"
	"ForwardTransparentSort=SwimForwardTransparentSort"
	"ShadowDepth=SwimShadowDepth"
	"ShadowMasked=SwimShadowMasked"
	"EnvironmentSky=SwimEnvironmentSky"
	"EnvironmentDownsample=SwimEnvironmentDownsample"
	"EnvironmentPrefilter=SwimEnvironmentPrefilter"
	"EnvironmentIrradiance=SwimEnvironmentIrradiance"
	"EnvironmentBrdfLut=SwimEnvironmentBrdfLut"
	"PostHistogram=SwimPostHistogram"
	"PostExposure=SwimPostExposure"
	"PostBloomDownsample=SwimPostBloomDownsample"
	"PostBloomUpsample=SwimPostBloomUpsample"
	"PostComposite=SwimPostComposite"
	"PostCompositeHdr=SwimPostCompositeHdr"
	"TemporalResolve=SwimTemporalResolve"
	"ScreenSpaceAo=SwimScreenSpaceAo"
	"ScreenSpaceBlur=SwimScreenSpaceBlur"
	"ScreenSpaceComposite=SwimScreenSpaceComposite"
	"ScreenSpaceReflection=SwimScreenSpaceReflection"
	"ParticleSimulate=SwimParticleSimulate"
	"ParticleEmit=SwimParticleEmit"
	"ParticleCompact=SwimParticleCompact"
	"ParticleFinalize=SwimParticleFinalize"
	"ParticleRender=SwimParticleRender"
	"Skinning=SwimSkinning"
	"UiQuad=SwimUiQuad"
)

# Defines SwimRuntimeShaders (the staged set) once.
function(swim_define_runtime_shader_set)
	if(TARGET SwimRuntimeShaders OR SWIM_OFFLINE_DEPENDENCY_STUBS)
		return()
	endif()
	if(NOT DEFINED SwimForwardOpaque_SPIRV)
		message(FATAL_ERROR
			"The runtime shader set needs the Slang programs defined with SWIM_ENABLE_VULKAN_RHI and SWIM_BUILD_SHADER_COMPILER."
		)
	endif()

	# Programs added with swim_add_runtime_shader (SlangShaders.cmake) join the set.
	get_property(SWIM_EXTRA_RUNTIME_SHADERS GLOBAL PROPERTY SWIM_EXTRA_RUNTIME_SHADERS)
	list(APPEND SWIM_RUNTIME_SHADER_PROGRAMS ${SWIM_EXTRA_RUNTIME_SHADERS})

	set(SWIM_RUNTIME_SHADER_ROOT "${CMAKE_BINARY_DIR}/Generated/Shaders/RuntimeSet")
	set(SWIM_RUNTIME_SHADER_OUTPUTS "")
	set(SWIM_RUNTIME_SHADER_SOURCES "")
	foreach(SWIM_ENTRY IN LISTS SWIM_RUNTIME_SHADER_PROGRAMS)
		string(REPLACE "=" ";" SWIM_PAIR "${SWIM_ENTRY}")
		list(GET SWIM_PAIR 0 SWIM_RUNTIME_NAME)
		list(GET SWIM_PAIR 1 SWIM_PROGRAM)
		if(NOT DEFINED ${SWIM_PROGRAM}_SPIRV OR NOT DEFINED ${SWIM_PROGRAM}_REFLECTION)
			message(FATAL_ERROR "Runtime shader ${SWIM_RUNTIME_NAME}: program ${SWIM_PROGRAM} is not defined")
		endif()
		set(SWIM_SPV "${SWIM_RUNTIME_SHADER_ROOT}/${SWIM_RUNTIME_NAME}.spv")
		set(SWIM_JSON "${SWIM_RUNTIME_SHADER_ROOT}/${SWIM_RUNTIME_NAME}.reflection.json")
		add_custom_command(
			OUTPUT "${SWIM_SPV}" "${SWIM_JSON}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${SWIM_RUNTIME_SHADER_ROOT}"
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${${SWIM_PROGRAM}_SPIRV}" "${SWIM_SPV}"
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${${SWIM_PROGRAM}_REFLECTION}" "${SWIM_JSON}"
			DEPENDS "${${SWIM_PROGRAM}_SPIRV}" "${${SWIM_PROGRAM}_REFLECTION}"
			COMMENT "Staging runtime shader ${SWIM_RUNTIME_NAME}"
			VERBATIM
		)
		list(APPEND SWIM_RUNTIME_SHADER_OUTPUTS "${SWIM_SPV}" "${SWIM_JSON}")
	endforeach()

	add_custom_target(SwimRuntimeShaders DEPENDS ${SWIM_RUNTIME_SHADER_OUTPUTS})
	swim_set_solution_folder(SwimRuntimeShaders "${SWIM_SOLUTION_FOLDER_TOOLS}")
	set(SWIM_RUNTIME_SHADER_DIR "${SWIM_RUNTIME_SHADER_ROOT}" CACHE INTERNAL "Staged runtime shader set" FORCE)
endfunction()

function(swim_configure_shaders target)
	if(SWIM_OFFLINE_DEPENDENCY_STUBS)
		return()
	endif()

	if(NOT SWIM_BUILD_SHADER_COMPILER OR NOT SWIM_SLANG_AVAILABLE OR NOT TARGET SwimSlangCompiler)
		message(FATAL_ERROR
			"First-party shaders require the pinned Slang compiler. Enable SWIM_BUILD_SHADER_COMPILER."
		)
	endif()

	swim_define_runtime_shader_set()
	add_dependencies(${target} SwimRuntimeShaders)
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_directory
			"${SWIM_RUNTIME_SHADER_DIR}"
			"$<TARGET_FILE_DIR:${target}>/Shaders/Runtime"
		VERBATIM
	)
endfunction()
