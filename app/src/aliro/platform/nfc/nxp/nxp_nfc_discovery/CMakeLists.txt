	#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#



zephyr_include_directories(.)

zephyr_library_sources(nxp_nfc_discovery_config.c)
if(CONFIG_PN5190_DRV)
  zephyr_library_sources(nxp_pn5190_platform.c)
elseif(CONFIG_PN5180_DRV)
  zephyr_library_sources(nxp_pn5180_platform.c)
else()
  message(FATAL_ERROR "NXP NFC HAL driver not selected (PN5190_DRV or PN5180_DRV)")
endif()
zephyr_library_sources(nxp_nfc_discovery.c)
