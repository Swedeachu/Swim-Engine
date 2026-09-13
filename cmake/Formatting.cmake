# Explicit developer tools, never a prerequisite for compiling the engine.
# Default to current edits so existing untouched source need not be reformatted.
find_program(SWIM_CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format-22)
find_package(Python3 QUIET COMPONENTS Interpreter)
set(SWIM_FORMAT_BASE_REF "HEAD" CACHE STRING "Git revision used by SwimFormat/SwimFormatCheck")

if(SWIM_CLANG_FORMAT_EXECUTABLE AND Python3_Interpreter_FOUND)
	add_custom_target(SwimFormat
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/format-source.py"
			--clang-format "${SWIM_CLANG_FORMAT_EXECUTABLE}" --base-ref "${SWIM_FORMAT_BASE_REF}"
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		COMMENT "Formatting changed first-party C/C++ sources"
		VERBATIM
	)
	add_custom_target(SwimFormatCheck
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/format-source.py"
			--clang-format "${SWIM_CLANG_FORMAT_EXECUTABLE}" --base-ref "${SWIM_FORMAT_BASE_REF}" --check
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		COMMENT "Checking changed first-party C/C++ formatting and braces"
		VERBATIM
	)
	swim_set_solution_folder(SwimFormat "${SWIM_SOLUTION_FOLDER_TOOLS}")
	swim_set_solution_folder(SwimFormatCheck "${SWIM_SOLUTION_FOLDER_TOOLS}")
endif()
