#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_include_directories(.)

add_subdirectory_ifdef(CONFIG_ST25R500_DRV st25r500)
add_subdirectory_ifdef(CONFIG_ST25R200_DRV st25r200)

if(CONFIG_ST25R3911_DRV OR CONFIG_ST25R3916_DRV OR CONFIG_ST25R3916B_DRV) 
add_subdirectory(st25r391x)
endif()
