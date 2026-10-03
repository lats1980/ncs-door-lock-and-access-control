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
  src/disc_loop_pn5190.c
)

target_sources(app PRIVATE ${app_sources})

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
