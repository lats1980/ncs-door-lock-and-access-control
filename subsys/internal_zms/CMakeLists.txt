#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_include_directories(include)
zephyr_sources(
  src/aead.cpp
  src/internal_zms.cpp
  src/key.cpp
  src/nonce.cpp
  src/storage.cpp
)

zephyr_sources_ifdef(CONFIG_DOOR_LOCK_INTERNAL_ZMS_ROLLBACK_PROTECTION src/counter.cpp)
