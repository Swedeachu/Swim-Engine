# Cross-platform math dependency used by foundation/runtime-neutral modules.
# Keep GLM available before the legacy runtime gate so Linux builds compile
# generic physics contracts instead of skipping that code entirely.

if(TARGET glm::glm)
	return()
endif()

if(SWIM_OFFLINE_DEPENDENCY_STUBS)
	add_library(SwimGlm INTERFACE)
	add_library(glm::glm ALIAS SwimGlm)
	return()
endif()

CPMAddPackage(
	NAME glm_source
	GITHUB_REPOSITORY g-truc/glm
	GIT_TAG 1.0.0
	DOWNLOAD_ONLY YES
	UPDATE_DISCONNECTED YES
)

add_library(SwimGlm INTERFACE)
target_include_directories(SwimGlm SYSTEM INTERFACE
	"${glm_source_SOURCE_DIR}"
)
add_library(glm::glm ALIAS SwimGlm)
