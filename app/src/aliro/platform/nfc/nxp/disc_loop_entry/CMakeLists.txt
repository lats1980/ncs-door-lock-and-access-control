	#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#



zephyr_include_directories(.)

# Core NxpNfcRdLib transport implementation
zephyr_library_sources(disc_loop_config.c)
if(CONFIG_PN5190_DRV)
  zephyr_library_sources(disc_loop_pn5190.c)
elseif(CONFIG_PN5180_DRV)
  zephyr_library_sources(disc_loop_pn5180.c)
else()
  message(FATAL_ERROR "NXP NFC HAL driver not selected (PN5190_DRV or PN5180_DRV)")
endif()
zephyr_library_sources(disc_loop_entry.c)
