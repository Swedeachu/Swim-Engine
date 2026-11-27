# Compiles one Slang program to DXIL (cmake -P). Register reflection comes from the HLSL
# target (the D3D register layout, no DXC library link); every entry point is then compiled
# to its own DXIL container, and the containers are concatenated into OUTPUT (each carries
# its own size). Separate compiles keep the program's register layout: global parameters are
# laid out for the whole module.
#
# -DSLANGC= -DSOURCE= -DOUTPUT= -DREFLECTION= -DDEPFILE= -DDXC_DIR= -DPROFILE=
# -DENTRY= (optional) -DINCLUDES= -DDEFINES= ('|' separated)
foreach(variable SLANGC SOURCE OUTPUT REFLECTION DEPFILE DXC_DIR PROFILE)
	if(NOT ${variable})
		message(FATAL_ERROR "SlangDxil.cmake needs -D${variable}")
	endif()
endforeach()

set(args "${SOURCE}" -lang slang -profile "${PROFILE}" -matrix-layout-column-major -preserve-params -DSWIM_TARGET_DXIL=1)
string(REPLACE "|" ";" includes "${INCLUDES}")
foreach(directory IN LISTS includes)
	list(APPEND args -I "${directory}")
endforeach()
string(REPLACE "|" ";" defines "${DEFINES}")
foreach(define IN LISTS defines)
	list(APPEND args "-D${define}")
endforeach()
# Before shader model 6.8 the draw base comes from root constants (DrawIndex.slang).
string(REGEX MATCH "[0-9]+_[0-9]+$" model "${PROFILE}")
if(model VERSION_LESS "6_8")
	list(APPEND args -DSWIM_DRAW_BASE_ROOT_CONSTANTS=1)
endif()

set(entry_args "")
if(ENTRY)
	set(entry_args -entry "${ENTRY}")
endif()

execute_process(
	COMMAND "${SLANGC}" ${args} -target hlsl ${entry_args} -reflection-json "${REFLECTION}"
	RESULT_VARIABLE result
	OUTPUT_VARIABLE output
	ERROR_VARIABLE output
)
if(result)
	message(FATAL_ERROR "Slang DXIL reflection failed for ${SOURCE}:\n${output}")
endif()

file(READ "${REFLECTION}" reflection)

# Structured buffers whose element count the shaders read (GetDimensions), found in the HLSL
# text: the backend binds those through views that carry the size. A buffer passed to a
# function is matched by element type, so every global buffer of that type is listed.
string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_]*\\.GetDimensions\\(" calls "${output}")
set(queried_types "")
foreach(call IN LISTS calls)
	string(REGEX REPLACE "\\.GetDimensions\\($" "" name "${call}")
	if(output MATCHES "StructuredBuffer<([^;(){},]*) > ${name}[ ,)]")
		list(APPEND queried_types "${CMAKE_MATCH_1}")
	endif()
endforeach()
set(size_queries "")
if(queried_types)
	string(REGEX MATCHALL "StructuredBuffer<[^;(){},]* > [A-Za-z0-9_]+ : register\\([tu][0-9]+(, space[0-9]+)?\\)" declarations "${output}")
	foreach(declaration IN LISTS declarations)
		string(REGEX MATCH "StructuredBuffer<([^;(){},]*) > [A-Za-z0-9_]+ : register\\(([tu])([0-9]+)(, space([0-9]+))?\\)" _ "${declaration}")
		set(type "${CMAKE_MATCH_1}")
		set(class shaderResource)
		if(CMAKE_MATCH_2 STREQUAL "u")
			set(class unorderedAccess)
		endif()
		set(register_space 0)
		if(CMAKE_MATCH_5)
			set(register_space ${CMAKE_MATCH_5})
		endif()
		if(type IN_LIST queried_types)
			list(APPEND size_queries "{\"class\":\"${class}\",\"index\":${CMAKE_MATCH_3},\"space\":${register_space}}")
		endif()
	endforeach()
endif()
if(size_queries)
	list(REMOVE_DUPLICATES size_queries)
	list(JOIN size_queries "," size_queries)
	string(JSON reflection SET "${reflection}" nativeSizeQueries "[${size_queries}]")
	file(WRITE "${REFLECTION}" "${reflection}")
endif()

string(JSON count LENGTH "${reflection}" entryPoints)
if(count EQUAL 0)
	message(FATAL_ERROR "${SOURCE} has no entry points")
endif()
math(EXPR last "${count} - 1")
set(parts "")
foreach(index RANGE ${last})
	string(JSON name GET "${reflection}" entryPoints ${index} name)
	set(part "${OUTPUT}.${index}.part")
	set(depfile_args "")
	if(index EQUAL 0)
		set(depfile_args -depfile "${DEPFILE}.part")
	endif()
	execute_process(
		COMMAND "${SLANGC}" ${args} -target dxil -dxc-path "${DXC_DIR}" -entry "${name}" ${depfile_args} -o "${part}"
		RESULT_VARIABLE result
		OUTPUT_VARIABLE output
		ERROR_VARIABLE output
	)
	if(result)
		file(REMOVE ${parts} "${part}")
		message(FATAL_ERROR "Slang DXIL compile failed for ${SOURCE} (${name}):\n${output}")
	endif()
	list(APPEND parts "${part}")
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}" -E cat ${parts} OUTPUT_FILE "${OUTPUT}" RESULT_VARIABLE result)
file(REMOVE ${parts})
if(result)
	message(FATAL_ERROR "Cannot write ${OUTPUT}")
endif()

# The first compile's dependencies, retargeted at the bundle.
file(READ "${DEPFILE}.part" dependencies)
file(REMOVE "${DEPFILE}.part")
string(FIND "${dependencies}" ": " colon)
if(colon LESS 0)
	message(FATAL_ERROR "Unexpected Slang depfile for ${SOURCE}")
endif()
string(SUBSTRING "${dependencies}" ${colon} -1 dependencies)
string(REPLACE " " "\\ " target "${OUTPUT}")
string(REPLACE ":" "\\:" target "${target}")
file(WRITE "${DEPFILE}" "${target}${dependencies}")
