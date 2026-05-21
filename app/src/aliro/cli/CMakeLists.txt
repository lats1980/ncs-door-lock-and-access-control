#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_library_sources(
  info.cpp
  shell.c
  shell_private.cpp
)

zephyr_library_sources_ifdef(CONFIG_BT btaddr.cpp)
zephyr_library_sources_ifdef(CONFIG_DOOR_LOCK_EXPEDITED_FAST_PHASE kpersistent.cpp)
if (NOT CONFIG_CHIP)
zephyr_library_sources(
  factory_reset.cpp
  provisioning.cpp
  reader.cpp
)
endif()

zephyr_include_directories(.)
