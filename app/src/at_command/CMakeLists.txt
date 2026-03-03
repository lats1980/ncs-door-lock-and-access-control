#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_include_directories(.)

if(CONFIG_ALIRO_AT_MODULE OR CONFIG_ALIRO_AT_HOST)
  target_sources(app PRIVATE at_command.cpp)
endif()

target_sources_ifdef(CONFIG_ALIRO_AT_MODULE app PRIVATE
  at_module.cpp
)

target_sources_ifdef(CONFIG_ALIRO_AT_HOST app PRIVATE
  at_host.cpp
)

target_sources_ifdef(CONFIG_ALIRO_AT_TRANSPORT_SERIAL app PRIVATE
  at_transport_serial.cpp
)

target_sources_ifdef(CONFIG_ALIRO_AT_TRANSPORT_BLE app PRIVATE
  at_transport_ble.cpp
)