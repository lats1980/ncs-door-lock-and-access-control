#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# SR150 UWB ranging demo (Controlee / Responder) on the NXP middleware port in
# drivers/uwb/nxp (Zephyr module ncs-door-lock-and-access-control).
# The NXP package must be extracted to drivers/uwb/nxp/uwbiot-top, see
# drivers/uwb/nxp/README.md.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(demo_ranging_controlee)

target_sources(app PRIVATE src/main.c)
