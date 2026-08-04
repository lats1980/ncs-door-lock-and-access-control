#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ncs-nxp-discovery-loop-sample)

set(app_sources
  src/main.c
  src/disc_loop_config.c
)

if(CONFIG_PN5190_DRV)
  list(APPEND app_sources src/disc_loop_pn5190.c)
elseif(CONFIG_PN5180_DRV)
  list(APPEND app_sources src/disc_loop_pn5180.c)
else()
  message(FATAL_ERROR "NXP NFC HAL driver not selected (PN5190_DRV or PN5180_DRV)")
endif()

target_sources(app PRIVATE ${app_sources})

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
