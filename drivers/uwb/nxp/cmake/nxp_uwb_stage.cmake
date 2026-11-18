#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# nxp_uwb_stage(LIBS_DIR <dir> OVERLAY_DIR <dir> PATCH_DIR <dir> STAGE_DIR <dir>)
#
# Creates <STAGE_DIR>/libs from the NXP "libs" tree:
#   1. copy the NXP tree
#   2. copy OVERLAY_DIR over it (the overlay mirrors the layout of libs/)
#   3. apply PATCH_DIR/*.patch (paths relative to <STAGE_DIR>, i.e. a/libs/...)
#
# The user's NXP package is only read. The staging is skipped when nothing it
# depends on changed, so file timestamps (and therefore incremental builds)
# stay stable across CMake re-runs.

function(nxp_uwb_stage)
	cmake_parse_arguments(ARG "" "LIBS_DIR;OVERLAY_DIR;PATCH_DIR;STAGE_DIR" "" ${ARGN})

	# Re-run CMake when files are added/removed (not allowed in script mode)
	set(_cd "")
	if(CMAKE_ROLE STREQUAL "PROJECT")
		set(_cd CONFIGURE_DEPENDS)
	endif()

	file(GLOB_RECURSE _libs_files ${_cd} ${ARG_LIBS_DIR}/*)
	file(GLOB_RECURSE _overlay_files ${_cd} ${ARG_OVERLAY_DIR}/*)
	file(GLOB _patch_files ${_cd} ${ARG_PATCH_DIR}/*.patch)
	list(SORT _patch_files)

	set(_state "")
	foreach(_f ${_libs_files} ${_overlay_files} ${_patch_files} ${CMAKE_CURRENT_FUNCTION_LIST_FILE})
		file(SHA1 ${_f} _h)
		string(APPEND _state "${_f}=${_h}\n")
	endforeach()
	string(SHA1 _state_hash "${_state}")

	set(_stamp ${ARG_STAGE_DIR}/.stamp)
	set(_old_hash "")
	if(EXISTS ${_stamp})
		file(READ ${_stamp} _old_hash)
	endif()

	if(_old_hash STREQUAL _state_hash AND EXISTS ${ARG_STAGE_DIR}/libs)
		return()
	endif()

	message(STATUS "NXP UWB: staging middleware sources in ${ARG_STAGE_DIR}")
	file(REMOVE_RECURSE ${ARG_STAGE_DIR})
	file(MAKE_DIRECTORY ${ARG_STAGE_DIR})

	file(COPY ${ARG_LIBS_DIR} DESTINATION ${ARG_STAGE_DIR})
	if(_overlay_files)
		file(COPY ${ARG_OVERLAY_DIR}/ DESTINATION ${ARG_STAGE_DIR}/libs)
	endif()

	if(_patch_files)
		find_program(NXP_UWB_PATCH_EXECUTABLE patch)
		find_program(NXP_UWB_GIT_EXECUTABLE git)
	endif()
	foreach(_p ${_patch_files})
		get_filename_component(_pname ${_p} NAME)
		if(NXP_UWB_PATCH_EXECUTABLE)
			execute_process(
				COMMAND ${NXP_UWB_PATCH_EXECUTABLE} -p1 --forward --batch -i ${_p}
				WORKING_DIRECTORY ${ARG_STAGE_DIR}
				RESULT_VARIABLE _rc
				OUTPUT_VARIABLE _out
				ERROR_VARIABLE _err)
		elseif(NXP_UWB_GIT_EXECUTABLE)
			execute_process(
				COMMAND ${NXP_UWB_GIT_EXECUTABLE} apply -p1 ${_p}
				WORKING_DIRECTORY ${ARG_STAGE_DIR}
				RESULT_VARIABLE _rc
				OUTPUT_VARIABLE _out
				ERROR_VARIABLE _err)
		else()
			message(FATAL_ERROR "NXP UWB: need 'patch' or 'git' in PATH to apply ${_pname}")
		endif()
		if(NOT _rc EQUAL 0)
			file(REMOVE_RECURSE ${ARG_STAGE_DIR})
			message(FATAL_ERROR
				"NXP UWB: failed to apply ${_pname} to the NXP package "
				"(is it really UWBIOT ${NXP_UWB_SUPPORTED_VERSION}?)\n${_out}${_err}")
		endif()
	endforeach()

	file(WRITE ${_stamp} "${_state_hash}")
endfunction()
