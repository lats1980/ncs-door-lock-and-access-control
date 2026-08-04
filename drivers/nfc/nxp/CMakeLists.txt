#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Name of the NXP NFC Reader Library folder (as extracted from the NXP
# download package), e.g. "NxpNfcRdLib". Override with
# -DNXP_NFC_CHIP_LIB=CustomName if needed.
set(NXP_NFC_CHIP_LIB "NxpNfcRdLib" CACHE STRING "NXP NFC reader library folder name")

set(NXP_NFC_PKG_DIR ${CMAKE_CURRENT_LIST_DIR}/${NXP_NFC_CHIP_LIB})
set(NXP_NFC_RDLIB_DIR ${NXP_NFC_PKG_DIR}/NxpNfcRdLib)
set(NXP_NFC_COMPS_DIR ${NXP_NFC_RDLIB_DIR}/comps)

if(CONFIG_PN5190_DRV)
	zephyr_compile_definitions(NXPBUILD__PHHAL_HW_PN5190)
	set(NXP_PHHALHW_SRC_DIR ${NXP_NFC_COMPS_DIR}/phhalHw/src/Pn5190)
elseif(CONFIG_PN5180_DRV)
	zephyr_compile_definitions(NXPBUILD__PHHAL_HW_PN5180)
	set(NXP_PHHALHW_SRC_DIR ${NXP_NFC_COMPS_DIR}/phhalHw/src/Pn5180)
else()
	message(FATAL_ERROR "NXP NFC HAL driver not selected (PN5190_DRV or PN5180_DRV)")
endif()

zephyr_compile_definitions(NXPBUILD_CUSTOMER_HEADER_INCLUDED)

if(CONFIG_NFC_NXP_HW_TARGET)
	zephyr_compile_definitions(NXPBUILD__PHHAL_HW_TARGET)
endif()

if(CONFIG_NFC_NXP_PAL_I14443P3A)
	zephyr_compile_definitions(NXPBUILD__PHPAL_I14443P3A_SW)
endif()

if(CONFIG_NFC_NXP_PAL_I14443P3B)
	zephyr_compile_definitions(NXPBUILD__PHPAL_I14443P3B_SW)
endif()

if(CONFIG_NFC_NXP_PAL_I14443P4A)
	zephyr_compile_definitions(NXPBUILD__PHPAL_I14443P4A_SW)
endif()

if(CONFIG_NFC_NXP_PAL_I14443P4)
	zephyr_compile_definitions(NXPBUILD__PHPAL_I14443P4_SW)
endif()

if(CONFIG_NFC_NXP_PAL_I14443P4MC)
	zephyr_compile_definitions(NXPBUILD__PHPAL_I14443P4MC_SW)
endif()

if(CONFIG_NFC_NXP_PAL_MIFARE)
	zephyr_compile_definitions(NXPBUILD__PHPAL_MIFARE_SW)
endif()

if(CONFIG_NFC_NXP_PAL_FELICA)
	zephyr_compile_definitions(NXPBUILD__PHPAL_FELICA_SW)
endif()

if(CONFIG_NFC_NXP_PAL_SLI15693)
	zephyr_compile_definitions(NXPBUILD__PHPAL_SLI15693_SW)
endif()

if(CONFIG_NFC_NXP_PAL_I18000P3M3)
	zephyr_compile_definitions(NXPBUILD__PHPAL_I18000P3M3_SW)
endif()

if(CONFIG_NFC_NXP_PAL_I18092MPI)
	zephyr_compile_definitions(NXPBUILD__PHPAL_I18092MPI_SW)
endif()

if(CONFIG_NFC_NXP_PAL_I18092MT)
	zephyr_compile_definitions(NXPBUILD__PHPAL_I18092MT_SW)
endif()

if(CONFIG_NFC_NXP_NFCLIB_EMVCO)
	zephyr_compile_definitions(NXPBUILD__PH_NFCLIB_EMVCO)
endif()

# nxp_nfc_platform/include must precede NXP RTOS/phOsal/inc so phOsal.h wrapper
# and phOsal_Config.h shadow the NXP Confidential headers.
zephyr_include_directories(
	${CMAKE_CURRENT_LIST_DIR}/nxp_nfc_platform/include
	${NXP_NFC_RDLIB_DIR}/types
	${NXP_NFC_RDLIB_DIR}/intfs
	${NXP_NFC_PKG_DIR}/RTOS/phOsal/inc
	${NXP_NFC_PKG_DIR}/Platform/DAL/inc
)

if(CONFIG_NFC_NXP_DEBUG_HELPERS)
	zephyr_include_directories(
		${CMAKE_CURRENT_LIST_DIR}/nxp_nfc_debug/include
	)
endif()

file(GLOB nxp_nfc_rdlib_sources CONFIGURE_DEPENDS
	${CMAKE_CURRENT_LIST_DIR}/nxp_nfc_platform/*.c
	${NXP_PHHALHW_SRC_DIR}/*.c
	${NXP_NFC_COMPS_DIR}/phNfcLib/src/*.c
	${NXP_NFC_COMPS_DIR}/phKeyStore/src/Sw/*.c
	${NXP_NFC_COMPS_DIR}/phKeyStore/src/*.c
	${NXP_NFC_COMPS_DIR}/phCryptoSym/src/*.c
	${NXP_NFC_COMPS_DIR}/phCryptoSym/src/Sw/*.c
	${NXP_NFC_COMPS_DIR}/phCryptoRng/src/*.c
	${NXP_NFC_COMPS_DIR}/phCryptoRng/src/Sw/*.c
	${NXP_NFC_COMPS_DIR}/phTools/src/*.c
	${NXP_NFC_COMPS_DIR}/phalT1T/src/Sw/*.c
	${NXP_NFC_COMPS_DIR}/phalTop/src/Sw/*.c
	${NXP_NFC_COMPS_DIR}/phacDiscLoop/src/*.c
	${NXP_NFC_COMPS_DIR}/phacDiscLoop/src/Sw/*.c
	${NXP_NFC_COMPS_DIR}/phlnLlcp/src/Sw/*.c
)

if(CONFIG_NFC_NXP_PAL_I14443P3A)
	file(GLOB nxp_pal_i14443p3a_sources CONFIGURE_DEPENDS
		${NXP_NFC_COMPS_DIR}/phpalI14443p3a/src/Sw/*.c
	)
	list(APPEND nxp_nfc_rdlib_sources ${nxp_pal_i14443p3a_sources})
endif()

if(CONFIG_NFC_NXP_PAL_I14443P3B)
	file(GLOB nxp_pal_i14443p3b_sources CONFIGURE_DEPENDS
		${NXP_NFC_COMPS_DIR}/phpalI14443p3b/src/Sw/*.c
	)
	list(APPEND nxp_nfc_rdlib_sources ${nxp_pal_i14443p3b_sources})
endif()

if(CONFIG_NFC_NXP_PAL_I14443P4A)
	file(GLOB nxp_pal_i14443p4a_sources CONFIGURE_DEPENDS
		${NXP_NFC_COMPS_DIR}/phpalI14443p4a/src/Sw/*.c
	)
	list(APPEND nxp_nfc_rdlib_sources ${nxp_pal_i14443p4a_sources})
endif()

if(CONFIG_NFC_NXP_PAL_I14443P4)
	file(GLOB nxp_pal_i14443p4_sources CONFIGURE_DEPENDS
		${NXP_NFC_COMPS_DIR}/phpalI14443p4/src/Sw/*.c
	)
	list(APPEND nxp_nfc_rdlib_sources ${nxp_pal_i14443p4_sources})
endif()

if(CONFIG_NFC_NXP_PAL_I14443P4MC)
	file(GLOB nxp_pal_i14443p4mc_sources CONFIGURE_DEPENDS
		${NXP_NFC_COMPS_DIR}/phpalI14443p4mC/src/Sw/*.c
		${NXP_NFC_COMPS_DIR}/phceT4T/src/Sw/*.c
	)
	list(APPEND nxp_nfc_rdlib_sources ${nxp_pal_i14443p4mc_sources})
endif()

if(CONFIG_NFC_NXP_PAL_MIFARE)
	file(GLOB nxp_pal_mifare_sources CONFIGURE_DEPENDS
		${NXP_NFC_COMPS_DIR}/phpalMifare/src/Sw/*.c
	)
	list(APPEND nxp_nfc_rdlib_sources ${nxp_pal_mifare_sources})

	file(GLOB nxp_al_mifare_sources CONFIGURE_DEPENDS
		${NXP_NFC_COMPS_DIR}/phTMIUtils/src/*.c
		${NXP_NFC_COMPS_DIR}/phalMfc/src/Sw/*.c
		${NXP_NFC_COMPS_DIR}/phalMful/src/Sw/*.c
		${NXP_NFC_COMPS_DIR}/phalMfdf/src/Sw/*.c
		${NXP_NFC_COMPS_DIR}/phalMfdfEVx/src/Sw/*.c
		${NXP_NFC_COMPS_DIR}/phalMfNtag42XDna/src/Sw/*.c
		${NXP_NFC_COMPS_DIR}/phalVca/src/Sw/*.c
		${NXP_NFC_COMPS_DIR}/phalVca/src/*.c
		${NXP_NFC_COMPS_DIR}/phalMfdfLight/src/Sw/*.c
		${NXP_NFC_COMPS_DIR}/phalMfpEVx/src/Sw/*.c
	)
	list(APPEND nxp_nfc_rdlib_sources ${nxp_al_mifare_sources})
endif()

if(CONFIG_NFC_NXP_PAL_FELICA)
	file(GLOB nxp_pal_felica_sources CONFIGURE_DEPENDS
		${NXP_NFC_COMPS_DIR}/phpalFelica/src/Sw/*.c
		${NXP_NFC_COMPS_DIR}/phalFelica/src/Sw/*.c
	)
	list(APPEND nxp_nfc_rdlib_sources ${nxp_pal_felica_sources})
endif()

if(CONFIG_NFC_NXP_PAL_SLI15693)
	file(GLOB nxp_pal_sli15693_sources CONFIGURE_DEPENDS
		${NXP_NFC_COMPS_DIR}/phpalSli15693/src/Sw/*.c
		${NXP_NFC_COMPS_DIR}/phalICode/src/Sw/*.c
	)
	list(APPEND nxp_nfc_rdlib_sources ${nxp_pal_sli15693_sources})
endif()

if(CONFIG_NFC_NXP_PAL_I18000P3M3)
	file(GLOB nxp_pal_i18000p3m3_sources CONFIGURE_DEPENDS
		${NXP_NFC_COMPS_DIR}/phpalI18000p3m3/src/Sw/*.c
		${NXP_NFC_COMPS_DIR}/phalI18000p3m3/src/Sw/*.c
	)
	list(APPEND nxp_nfc_rdlib_sources ${nxp_pal_i18000p3m3_sources})
endif()

if(CONFIG_NFC_NXP_PAL_I18092MPI)
	file(GLOB nxp_pal_i18092mpi_sources CONFIGURE_DEPENDS
		${NXP_NFC_COMPS_DIR}/phpalI18092mPI/src/Sw/*.c
	)
	list(APPEND nxp_nfc_rdlib_sources ${nxp_pal_i18092mpi_sources})
endif()

if(CONFIG_NFC_NXP_PAL_I18092MT)
	file(GLOB nxp_pal_i18092mt_sources CONFIGURE_DEPENDS
		${NXP_NFC_COMPS_DIR}/phpalI18092mT/src/Sw/*.c
	)
	list(APPEND nxp_nfc_rdlib_sources ${nxp_pal_i18092mt_sources})
endif()

if(CONFIG_NFC_NXP_DEBUG_HELPERS)
	list(APPEND nxp_nfc_rdlib_sources
		${CMAKE_CURRENT_LIST_DIR}/nxp_nfc_debug/nxp_nfc_debug.c
	)
endif()

zephyr_library_named(nxp_nfc_rdlib)
zephyr_library_compile_options(-Wno-double-promotion)
zephyr_library_sources(${nxp_nfc_rdlib_sources})
