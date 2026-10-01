include_guard(GLOBAL)

# Check actual target sources, including generator expressions, after every target
# has been created. Archived history cannot silently enter a target through a glob.
function(swim_check_archive_directory directory)
	get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
	foreach(target IN LISTS targets)
		get_target_property(sources "${target}" SOURCES)
		foreach(source IN LISTS sources)
			string(REPLACE "\\" "/" normalized "${source}")
			string(TOLOWER "${normalized}" normalized_lower)
			string(TOLOWER "${CMAKE_SOURCE_DIR}/Deprecated/" archive_root)
			string(FIND "${normalized_lower}" "${archive_root}" archive_index)
			if(archive_index GREATER_EQUAL 0 OR normalized_lower MATCHES "(^|[:>,/])deprecated/")
				message(FATAL_ERROR "Target ${target} references archived history: ${source}")
			endif()
		endforeach()
	endforeach()
	get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
	foreach(child IN LISTS children)
		swim_check_archive_directory("${child}")
	endforeach()
endfunction()

function(swim_check_archive_boundary)
	swim_check_archive_directory("${CMAKE_SOURCE_DIR}")
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL swim_check_archive_boundary)
