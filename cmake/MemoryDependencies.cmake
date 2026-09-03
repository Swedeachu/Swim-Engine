# General-purpose allocator dependency. mimalloc is isolated behind Swim::Memory
# for explicit arena backing, while the final engine executable also links the
# upstream override object so ordinary malloc/free/new/delete use mimalloc.

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	add_library(SwimMimalloc INTERFACE)
	add_library(Swim::Mimalloc ALIAS SwimMimalloc)
	set(SWIM_MEMORY_USE_MIMALLOC OFF)
	return()
endif()

set(MI_OVERRIDE ON CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT ON CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_WIN_REDIRECT OFF CACHE BOOL "" FORCE)
set(MI_OPT_ARCH OFF CACHE BOOL "" FORCE)

set(SWIM_SAVED_MEMORY_CMAKE_FOLDER "${CMAKE_FOLDER}")
set(CMAKE_FOLDER "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/mimalloc")
CPMAddPackage(
	NAME mimalloc_source
	GITHUB_REPOSITORY microsoft/mimalloc
	GIT_TAG v3.4.5
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)
set(CMAKE_FOLDER "${SWIM_SAVED_MEMORY_CMAKE_FOLDER}")
unset(SWIM_SAVED_MEMORY_CMAKE_FOLDER)

if(NOT TARGET mimalloc-static OR NOT TARGET mimalloc-obj)
	message(FATAL_ERROR "mimalloc v3.4.5 did not provide the expected static/object targets")
endif()

add_library(SwimMimalloc INTERFACE)
target_link_libraries(SwimMimalloc INTERFACE mimalloc-static)
add_library(Swim::Mimalloc ALIAS SwimMimalloc)
set(SWIM_MEMORY_USE_MIMALLOC ON)

swim_set_solution_folder(mimalloc-static "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/mimalloc")
swim_set_solution_folder(mimalloc-obj "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/mimalloc")
if(TARGET mimalloc-obj-target)
	swim_set_solution_folder(mimalloc-obj-target "${SWIM_SOLUTION_FOLDER_THIRD_PARTY}/mimalloc")
endif()
