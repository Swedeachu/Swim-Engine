# Audio backend. miniaudio is private to Source/Engine/Systems/Audio (one translation
# unit compiles its implementation); engine headers never include it.

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	set(SWIM_AUDIO_AVAILABLE OFF)
	return()
endif()

CPMAddPackage(
	NAME miniaudio_source
	GITHUB_REPOSITORY mackron/miniaudio
	GIT_TAG 0.11.26
	DOWNLOAD_ONLY YES
	UPDATE_DISCONNECTED YES
)

if(NOT EXISTS "${miniaudio_source_SOURCE_DIR}/miniaudio.h")
	message(FATAL_ERROR "miniaudio 0.11.26 did not provide miniaudio.h")
endif()

add_library(SwimMiniaudio INTERFACE)
add_library(Swim::Miniaudio ALIAS SwimMiniaudio)
target_include_directories(SwimMiniaudio SYSTEM INTERFACE "${miniaudio_source_SOURCE_DIR}")
# Playback only: no capture-side encoders or the node-graph examples.
target_compile_definitions(SwimMiniaudio INTERFACE MA_NO_ENCODING MA_NO_GENERATION)
set(SWIM_AUDIO_AVAILABLE ON)
