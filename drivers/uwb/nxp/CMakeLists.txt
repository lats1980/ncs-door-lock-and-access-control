#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# NXP SR150 UWB IoT middleware port for Zephyr / nRF Connect SDK.
#
# The NXP package is NOT part of this repository. The user extracts it to
# ${NXP_UWB_PKG_DIR} (default: drivers/uwb/nxp/uwbiot-top). To keep the NXP
# sources untouched and still adapt them, the build works on a staged copy:
#
#   <NXP_UWB_PKG_DIR>/libs  --copy-->  <build>/nxp_uwb_stage/libs
#   nxp_uwb_platform/overlay/**        replaces same-named files in the copy
#   patches/*.patch                    are applied to the copy
#
# The Zephyr platform layer (OSAL, SPI/GPIO bus, logging) in nxp_uwb_platform/
# replaces the NXP board/OSAL/log code, which is simply not compiled. Only the
# device configuration of the package (boards/Host/Rhodes4/UWB_DeviceConfig_SR1XX.h)
# is used, through a generated <UWB_DeviceConfig.h>.

set(NXP_UWB_PKG_DIR ${CMAKE_CURRENT_LIST_DIR}/uwbiot-top CACHE PATH
	"Folder the NXP UWBIOT package was extracted to (the one that contains libs/)")

set(NXP_UWB_SUPPORTED_VERSION "v04.08.01")
set(NXP_UWB_LIBS_DIR ${NXP_UWB_PKG_DIR}/libs)
set(NXP_UWB_PLATFORM_DIR ${CMAKE_CURRENT_LIST_DIR}/nxp_uwb_platform)

# ---------------------------------------------------------------------------
# Check the NXP package
# ---------------------------------------------------------------------------
if(NOT EXISTS ${NXP_UWB_LIBS_DIR}/uwb-iot)
	message(FATAL_ERROR
		"NXP UWB IoT middleware not found: ${NXP_UWB_LIBS_DIR}/uwb-iot\n"
		"Extract the NXP package UWBIOT_SR150_${NXP_UWB_SUPPORTED_VERSION} so that "
		"${NXP_UWB_PKG_DIR}/libs exists, or pass -DNXP_UWB_PKG_DIR=<path to its uwbiot-top>. "
		"See drivers/uwb/nxp/README.md.")
endif()

file(STRINGS ${NXP_UWB_LIBS_DIR}/halimpl/inc/uwbiot_ver.h _nxp_uwb_ver_line
	REGEX "UWBIOTVER_STR_VER_STRING_NUM")
string(REGEX MATCH "v[0-9]+\\.[0-9]+\\.[0-9]+" NXP_UWB_PKG_VERSION "${_nxp_uwb_ver_line}")
if(NOT NXP_UWB_PKG_VERSION STREQUAL NXP_UWB_SUPPORTED_VERSION)
	message(FATAL_ERROR
		"NXP UWB IoT package is ${NXP_UWB_PKG_VERSION}, this port supports "
		"${NXP_UWB_SUPPORTED_VERSION} only.")
endif()

# ---------------------------------------------------------------------------
# Stage the NXP sources (copy + overlay + patches)
# ---------------------------------------------------------------------------
set(NXP_UWB_STAGE_DIR ${CMAKE_CURRENT_BINARY_DIR}/nxp_uwb_stage)
set(NXP_UWB_STAGED_LIBS ${NXP_UWB_STAGE_DIR}/libs)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/nxp_uwb_stage.cmake)
nxp_uwb_stage(
	LIBS_DIR    ${NXP_UWB_LIBS_DIR}
	OVERLAY_DIR ${NXP_UWB_PLATFORM_DIR}/overlay
	PATCH_DIR   ${CMAKE_CURRENT_LIST_DIR}/patches
	STAGE_DIR   ${NXP_UWB_STAGE_DIR}
)

# ---------------------------------------------------------------------------
# Sources: NXP middleware (from the staged copy)
# ---------------------------------------------------------------------------
zephyr_library_named(uwb_nxp)

# Paths below are relative to the staged copy of the NXP "libs" folder.
set(NXP_UWB_SOURCES
	# HAL
	halimpl/hal/phNxpUciHal.c
	halimpl/hal/phNxpUciHal_ext.c
	# TML
	halimpl/tml/phTmlUwb.c
	halimpl/tml/phTmlUwb_transport.c
	# SPI transport
	halimpl/transport/SPI/SR1XX/uwb_uwbs_tml_interface.c
	halimpl/transport/SPI/common/phNxpUwb_Common.c
	# firmware download
	halimpl/fwd/SR1XX/phNxpUciHal_fwd.c
	halimpl/fwdl_provider/uwb_fwdl_ext.c
	halimpl/fwdl_provider/uwb_fwdl_ram.c
	# utils
	halimpl/utils/phNxpUciHal_utils.c
	halimpl/utils/phNxpUwbConfig.c
	halimpl/utils/utlv.c
	# UCI core
	uci-core/src/uwb_main.c
	uci-core/src/uwb_ucif.c
	uci-core/src/uwa_dm_api.c
	uci-core/src/uwa_sys_main.c
	uci-core/src/uwa_dm_main.c
	uci-core/src/uwa_dm_act.c
	uci-core/src/uwb_task.c
	uci-core/src/uci_hmsgs.c
	# UWB API
	uwb-iot/uwb_core/adaptation/UwbAdaptation.c
	uwb-iot/uwb_api/Api/UwbApi.c
	uwb-iot/uwb_api/Api/UwbApi_Internal.c
	uwb-iot/uwb_api/Api/UwbApi_Utility.c
	uwb-iot/uwb_api/Api/SR1XX/AppConfigParams.c
	uwb-iot/uwb_api/Api/SR1XX/UwbApi_Proprietary.c
	uwb-iot/uwb_api/Api/SR1XX/UwbApi_Proprietary_Internal.c
	uwb-iot/uwb_api/PrintUtility/PrintUtility.c
)
list(TRANSFORM NXP_UWB_SOURCES PREPEND ${NXP_UWB_STAGED_LIBS}/)
zephyr_library_sources(${NXP_UWB_SOURCES})

# ---------------------------------------------------------------------------
# Sources: Zephyr platform layer (replaces NXP boards/, osal/ and log/)
# ---------------------------------------------------------------------------
set(NXP_UWB_PLATFORM_SOURCES
	osal/phOsalUwb_zephyr.c
	log/nxLog.c
	bus/sr150.c
	bus/uwb_bus_interface.c
	bus/uwb_bus_io.c
	stubs/se_stubs.c
)
list(TRANSFORM NXP_UWB_PLATFORM_SOURCES PREPEND ${NXP_UWB_PLATFORM_DIR}/)
zephyr_library_sources(${NXP_UWB_PLATFORM_SOURCES})

# ---------------------------------------------------------------------------
# Include paths
# ---------------------------------------------------------------------------
# Board configuration (uwb_board.h, driver_config.h, ...). An application can
# provide its own copy with -DNXP_UWB_BOARD_CONFIG_DIR; the folder is searched first.
set(NXP_UWB_BOARD_CONFIG_DIR "" CACHE PATH
	"Optional folder searched before nxp_uwb_platform/include (board specific UWB_DeviceConfig.h, ...)")
if(NXP_UWB_BOARD_CONFIG_DIR)
	zephyr_include_directories(${NXP_UWB_BOARD_CONFIG_DIR})
endif()

# Device configuration (antenna delay / PDoA calibration, core config): used from
# the NXP package as it is, so that no copy of it has to be kept in this repository.
# <UWB_DeviceConfig.h> is generated from UWB_DeviceConfig.h.in and includes it.
set(NXP_UWB_DEVICE_CONFIG ${NXP_UWB_PKG_DIR}/boards/Host/Rhodes4/UWB_DeviceConfig_SR1XX.h)
if(NOT EXISTS ${NXP_UWB_DEVICE_CONFIG})
	message(FATAL_ERROR "NXP device configuration not found: ${NXP_UWB_DEVICE_CONFIG}")
endif()
set(NXP_UWB_GENERATED_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/generated/include)
configure_file(${NXP_UWB_PLATFORM_DIR}/include/UWB_DeviceConfig.h.in
	${NXP_UWB_GENERATED_INCLUDE}/UWB_DeviceConfig.h @ONLY)

set(NXP_UWB_NXP_INCLUDES
	halimpl/inc
	halimpl/fwd/SR1XX
	halimpl/transport/SPI
	uci-core/inc
	uci-core/inc/SR1XX
	uwb-iot/uwb_core/include
	uwb-iot/uwb_api/Api
	uwb-iot/uwb_api/Api/SR1XX
	uwb-iot/uwb_api/Api/SR1XX/SR150
	uwb-iot/uwb_api/PrintUtility
)
list(TRANSFORM NXP_UWB_NXP_INCLUDES PREPEND ${NXP_UWB_STAGED_LIBS}/)

zephyr_include_directories(
	${NXP_UWB_PLATFORM_DIR}/include
	${NXP_UWB_GENERATED_INCLUDE}
	${NXP_UWB_PLATFORM_DIR}/bus
	${NXP_UWB_NXP_INCLUDES}
)

# Command timeouts (uwb_target.h only defines them when not set)
zephyr_compile_definitions(
	UWB_CMD_CMPL_TIMEOUT=${CONFIG_UWB_NXP_CMD_CMPL_TIMEOUT_MS}
	UWB_CMD_RETRY_TIMEOUT=${CONFIG_UWB_NXP_CMD_RETRY_TIMEOUT_MS}
)

# ---------------------------------------------------------------------------
# SR150 firmware image
# ---------------------------------------------------------------------------
# The image comes from the NXP package (chosen by UWBIOT_SR1XX_FW_* in uwb_iot_ftr_default.h)
zephyr_include_directories(
	${NXP_UWB_PKG_DIR}/firmware_images/SR1XX
	${NXP_UWB_PKG_DIR}/firmware_images/SR1XX/Prod_ROW
)
