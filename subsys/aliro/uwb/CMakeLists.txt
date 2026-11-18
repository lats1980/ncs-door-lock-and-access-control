#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_include_directories(.)

zephyr_library_sources(uwb.cpp)

# The generic UltraWideBand facade (uwb.cpp) is always built. The backend that
# provides uwb_impl.h and its sources is selected by the DOOR_LOCK_ALIRO_UWB_IMPL
# choice:
#  - STUB (default): build the in-tree implementation from stub_impl/.
#  - SR150:          build the NXP SR150 implementation from sr150_impl/.
#  - EXTERNAL:       build no in-tree implementation; an out-of-tree
#                    module provides uwb_impl.h and its sources through the
#                    external-provider seam.
if(CONFIG_DOOR_LOCK_ALIRO_UWB_IMPL_STUB)
  add_subdirectory(stub_impl)
elseif(CONFIG_DOOR_LOCK_ALIRO_UWB_IMPL_SR150)
  add_subdirectory(sr150_impl)
elseif(CONFIG_DOOR_LOCK_ALIRO_UWB_IMPL_EXTERNAL)
  # Intentionally left empty: the implementation is provided out-of-tree.
endif()
