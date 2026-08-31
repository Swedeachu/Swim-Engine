# Downloads CPM.cmake (https://github.com/cpm-cmake/CPM.cmake) into the build
# tree on first configure so it never has to be vendored in the repo.

set(CPM_DOWNLOAD_VERSION 0.40.8)

if(CPM_SOURCE_CACHE)
	set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
	set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
	set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

get_filename_component(CPM_DOWNLOAD_LOCATION ${CPM_DOWNLOAD_LOCATION} ABSOLUTE)

function(swim_download_cpm)
	message(STATUS "Downloading CPM.cmake v${CPM_DOWNLOAD_VERSION} to ${CPM_DOWNLOAD_LOCATION}")
	file(DOWNLOAD
		https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
		${CPM_DOWNLOAD_LOCATION}
	)
endfunction()

if(NOT EXISTS ${CPM_DOWNLOAD_LOCATION})
	swim_download_cpm()
else()
	# Resume the download if it previously failed and left an empty file behind.
	file(READ ${CPM_DOWNLOAD_LOCATION} SWIM_CPM_CHECK)
	if("${SWIM_CPM_CHECK}" STREQUAL "")
		swim_download_cpm()
	endif()
endif()

include(${CPM_DOWNLOAD_LOCATION})
