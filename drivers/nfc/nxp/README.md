# NXP NFC Driver

This driver integrates NXP PN series NFC reader ICs (PN5190 and PN5180) with nRF Connect SDK / Zephyr by porting the NXP NFC Reader Library platform layers to Zephyr.

It is compatible with **NXP NFC Reader Library 07.16** (`NxpNfcRdLib_07.16.00`).

## Architecture

The driver is split into a Zephyr platform port and the NXP NFC Reader Library. The platform port implements the interfaces expected by NXP's library without modifying NXP confidential sources.

```
drivers/nfc/nxp/
├── CMakeLists.txt          # Build integration and NXP component selection
├── Kconfig                 # Driver and PAL/HAL feature options
├── nxp_nfc_platform/       # Zephyr platform port (DAL, BAL, OSAL, init)
├── nxp_nfc_debug/          # Optional debug print helpers
└── NxpNfcRdLib/            # NXP NFC Reader Library (see setup below)
```

### Platform port (`nxp_nfc_platform`)

| Component | File | Role |
|-----------|------|------|
| **DAL** (Device Abstraction Layer) | `phDriver_nRF.c` | GPIO (reset, IRQ), timers, and interrupt handling |
| **BAL** (Bus Abstraction Layer) | `phbalReg_ZephyrSpi.c` | SPI communication with the PN5190/PN5180 via Zephyr SPI |
| **OSAL** (OS Abstraction Layer) | `phOsal_Zephyr.c` | Events, semaphores, and timing using Zephyr kernel primitives |
| **Platform init** | `nxp_nfc.c` | Library initialization and IRQ monitor thread |
| **Board config** | `Board_nRF.h` | Pin definitions mapped from devicetree (`nxp,pn5190` node) |
| **Build config** | `ph_NxpBuild_App.h` | Application-level NXP Reader Library component selection |

The OSAL headers in `nxp_nfc_platform/include` are placed ahead of the NXP library include paths so the Zephyr wrappers shadow the stock NXP headers without changing NXP source files.

### NXP NFC Reader Library (`NxpNfcRdLib`)

The library provides the protocol stack on top of the platform port:

- **HAL** (`phhalHw`) — hardware abstraction for PN5190 or PN5180
- **PAL** — protocol abstraction (ISO 14443, FeliCa, ISO 15693, MIFARE, and others)
- **AL / Activities** — higher-level components such as Discovery Loop (`phacDiscLoop`) and NFC Library API (`phNfcLib`)

Which HAL and PAL components are compiled in is controlled by Kconfig options (for example `PN5190_DRV`, `NFC_NXP_PAL_I14443P3A`).

## NXP NFC Reader Library setup

The NXP NFC Reader Library is **not** included in this repository. You must obtain it from NXP and place it locally before building.

1. Download **NXP NFC Reader Library 07.16** from the [NXP website](https://www.nxp.com/applications/technologies/security/industrial-security/nfc-reader-library-software-support-for-nfc-frontend-solutions:NFC-READER-LIBRARY).
2. Extract the archive into the `NxpNfcRdLib` directory under this folder:

   ```
   drivers/nfc/nxp/NxpNfcRdLib/
   ├── NxpNfcRdLib/
   ├── Platform/
   ├── RTOS/
   └── ...
   ```

   After extraction, CMake expects the library root at `drivers/nfc/nxp/NxpNfcRdLib/NxpNfcRdLib/`.

3. If your download package uses a different top-level folder name, override it at build time:

   ```bash
   west build ... -- -DNXP_NFC_CHIP_LIB=YourFolderName
   ```

> **Note:** Only NXP NFC Reader Library **07.16** is supported by this driver. Other versions may require changes to the platform port or build configuration.
