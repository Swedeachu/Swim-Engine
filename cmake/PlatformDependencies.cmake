# Cross-platform dependencies owned by the Platform layer.

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	add_library(SwimSDL3 INTERFACE)
	add_library(SDL3::SDL3 ALIAS SwimSDL3)
	set(SWIM_SDL3_TARGET SDL3::SDL3)
	return()
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

# Swim::Platform owns SDL strictly as the host/window/input implementation.
# Keep the subsystems needed for video, events, gamepads and haptics, but do not
# compile SDL's unrelated renderer/GPU/audio/camera/UI facilities into the engine.
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)

CPMAddPackage(
	NAME sdl3_source
	GITHUB_REPOSITORY libsdl-org/SDL
	GIT_TAG release-3.4.14
	EXCLUDE_FROM_ALL YES
	UPDATE_DISCONNECTED YES
)

if(TARGET SDL3::SDL3-static)
	set(SWIM_SDL3_TARGET SDL3::SDL3-static)
elseif(TARGET SDL3::SDL3)
	set(SWIM_SDL3_TARGET SDL3::SDL3)
elseif(TARGET SDL3-static)
	set(SWIM_SDL3_TARGET SDL3-static)
else()
	message(FATAL_ERROR "SDL3 3.4.14 did not provide an expected CMake target")
endif()
