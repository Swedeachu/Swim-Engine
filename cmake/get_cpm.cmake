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
		STATUS SWIM_CPM_DOWNLOAD_STATUS
		TLS_VERIFY ON
	)

	list(GET SWIM_CPM_DOWNLOAD_STATUS 0 SWIM_CPM_DOWNLOAD_CODE)
	list(GET SWIM_CPM_DOWNLOAD_STATUS 1 SWIM_CPM_DOWNLOAD_MESSAGE)
	if(NOT SWIM_CPM_DOWNLOAD_CODE EQUAL 0)
		file(REMOVE ${CPM_DOWNLOAD_LOCATION})
		message(FATAL_ERROR
			"Failed to download CPM.cmake v${CPM_DOWNLOAD_VERSION}: ${SWIM_CPM_DOWNLOAD_MESSAGE}. "
			"Clean builds require network access; soft builds use the existing .cache/cpm contents only."
		)
	endif()
endfunction()

if(NOT EXISTS ${CPM_DOWNLOAD_LOCATION})
	if(FETCHCONTENT_FULLY_DISCONNECTED)
		message(FATAL_ERROR
			"CPM.cmake is not cached at ${CPM_DOWNLOAD_LOCATION}. "
			"Soft builds never download dependencies; run the clean-build script once with network access."
		)
	endif()
	swim_download_cpm()
else()
	# Resume the download if it previously failed and left an empty file behind.
	file(READ ${CPM_DOWNLOAD_LOCATION} SWIM_CPM_CHECK)
	if("${SWIM_CPM_CHECK}" STREQUAL "")
		if(FETCHCONTENT_FULLY_DISCONNECTED)
			message(FATAL_ERROR
				"Cached CPM.cmake is empty at ${CPM_DOWNLOAD_LOCATION}. "
				"Soft builds never download dependencies; run the clean-build script once with network access."
			)
		endif()
		swim_download_cpm()
	endif()
endif()

include(${CPM_DOWNLOAD_LOCATION})
