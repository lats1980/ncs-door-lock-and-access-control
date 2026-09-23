#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_ALIRO_AT_MODULE OR CONFIG_ALIRO_AT_HOST)

zephyr_include_directories(include)

# at_host.h needs aliro/types.h even when NCS_ALIRO=n (lib/aliro not added then)
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../lib/aliro/include)

zephyr_library()

zephyr_library_sources(src/at_command.cpp)

zephyr_library_sources_ifdef(CONFIG_ALIRO_AT_HOST src/at_host.cpp)

zephyr_library_sources_ifdef(CONFIG_ALIRO_AT_TRANSPORT_SERIAL src/at_transport_serial.cpp)

if(CONFIG_ALIRO_AT_TRANSPORT_BLE AND NOT CONFIG_CHIP)
  zephyr_library_sources(src/at_transport_ble_module.cpp)
endif()

if(CONFIG_ALIRO_AT_TRANSPORT_BLE AND CONFIG_CHIP)
  zephyr_library_sources(src/at_transport_ble_host.cpp)
endif()

if(CONFIG_ALIRO_AT_MODULE)
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/at_module.cpp)
endif()

endif()
