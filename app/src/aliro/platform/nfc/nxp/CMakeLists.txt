#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

add_subdirectory(nxp_nfc_discovery)

zephyr_include_directories(.)

# Core NxpNfcRdLib transport implementation
zephyr_library_sources(nfc_transport_NxpNfcRdLib.cpp)
