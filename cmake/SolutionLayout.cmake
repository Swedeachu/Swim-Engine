include_guard(GLOBAL)

# Keep generated IDE projects readable without changing the actual CMake target
# graph. FOLDER only affects IDE presentation; target names, link dependencies,
# Ninja builds, and command-line build behavior remain unchanged.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")

set(SWIM_SOLUTION_FOLDER_ENGINE_MODULES "Engine Modules")
set(SWIM_SOLUTION_FOLDER_TESTS "Tests")
set(SWIM_SOLUTION_FOLDER_EXAMPLES "Examples")
set(SWIM_SOLUTION_FOLDER_THIRD_PARTY "Third Party")

function(swim_set_solution_folder target_name folder_name)
	if(NOT TARGET "${target_name}")
		message(FATAL_ERROR "Cannot place missing target '${target_name}' in Visual Studio folder '${folder_name}'")
	endif()

	get_target_property(SWIM_SOLUTION_REAL_TARGET "${target_name}" ALIASED_TARGET)
	if(SWIM_SOLUTION_REAL_TARGET)
		set(target_name "${SWIM_SOLUTION_REAL_TARGET}")
	endif()

	set_property(TARGET "${target_name}" PROPERTY FOLDER "${folder_name}")
endfunction()
