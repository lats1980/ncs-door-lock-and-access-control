#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(door-lock-app)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../cmake)

if(CONFIG_NCS_ALIRO AND NOT CONFIG_ALIRO_ON_EXTERNAL_MCU)
add_subdirectory(src/aliro)
endif()
add_subdirectory_ifdef(CONFIG_CHIP src/matter)
add_subdirectory_ifdef(CONFIG_DOOR_LOCK_DFU_BLE_SMP src/dfu_smp)
add_subdirectory_ifdef(CONFIG_DOOR_LOCK_BLE_NUS src/bt_nus)
if(CONFIG_ALIRO_AT_MODULE OR CONFIG_ALIRO_AT_HOST)
add_subdirectory(src/at_command)
endif()

file(GLOB app_sources CONFIGURE_DEPENDS src/*.cpp)
target_sources(app PRIVATE ${app_sources})
target_include_directories(app PRIVATE src)
