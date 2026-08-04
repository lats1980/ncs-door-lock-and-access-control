# NXP Discovery Loop Sample

This sample demonstrates the NXP NFC Reader Library **Discovery Loop** (`phacDiscLoop`) on nRF Connect SDK / Zephyr. It is a port of the NXP example `NfcrdlibEx1_DiscoveryLoop` and uses the [NXP NFC driver](../../nfc/nxp/README.md).

The application runs in **poll mode**, continuously scanning for NFC tags (Type A and Type B). When a tag is detected, it is activated and tag information is printed to the console log.

## Overview

After initialization (`nxp_nfc_init`, `phNfcLib_Init`), the sample starts an IRQ monitor thread and enters an infinite discovery loop. For each iteration it:

1. Optionally applies an NFC profile and configures LPCD (see Kconfig below).
2. Runs `phacDiscLoop_Run()` to detect and activate tags.
3. Handles multi-technology and multi-card scenarios.
4. Turns the RF field off and waits before the next poll cycle.

Chip-specific logic (IRQ handling and LPCD calibration) lives in `src/disc_loop_pn5190.c` or `src/disc_loop_pn5180.c`, selected at build time by the NXP driver Kconfig.

## Requirements

- [nRF54L15 DK](https://docs.nordicsemi.com/bundle/ncs-latest/page/zephyr/boards/nordic/nrf54l15dk/doc/index.html)
- PNEV5190B
- NXP NFC Reader Library 07.16 installed under `drivers/nfc/nxp/NxpNfcRdLib/` (see [NXP driver README](../../nfc/nxp/README.md))

## Pin mapping (nRF54L15 DK ↔ PN5190 EB)

Wire the nRF54L15 DK to the PNEV5190B host interface as shown below. Pin assignments are defined in `boards/nrf54l15dk_nrf54l15_cpuapp.overlay`.

The PNEV5190B is **independently powered**. Use its own power supply; do not connect VDDIO from the nRF54L15 DK. Connect GND between the boards for a common ground reference.

| nRF54L15 DK | PN5190 EB (PNEV5190B) |
|-------------|------------------------|
| P1.13       | SPI_CLK (SCK)          |
| P1.12       | SPI_MISO (MISO)        |
| P1.11       | SPI_MOSI (MOSI)        |
| P2.08       | SPI_NSS (NSS)          |
| P0.04       | IRQ                    |
| P2.06       | RESET (RST)            |
| GND         | GND                    |

> **Note:** The board overlay disables buttons and LEDs that share pins with the NFC interface (P0.04 IRQ and P1.13 SCK).

## Building and running

From the repository root:

```bash
west build -p -b nrf54l15dk/nrf54l15/cpuapp drivers/samples/nxp_discovery_loop
west flash
```

Connect a serial terminal and present an NFC tag near the antenna. Log output shows detected technology type and tag details.

## Configuration

Sample-specific options are defined in `Kconfig` under **NXP discovery loop sample**:

| Kconfig | Default | Description |
|---------|---------|-------------|
| `NCS_NXP_DISCOVERY_LOOP_DISC_CONFIG` | `y` | Apply the NFC discovery loop profile via `disc_loop_apply_profile()` instead of using Reader Library defaults. |
| `NCS_NXP_DISCOVERY_LOOP_LPCD` | `y` | Enable Low Power Card Detection (LPCD) in the discovery loop to reduce power while waiting for a tag. |
| `NCS_NXP_DISCOVERY_LOOP_SAMPLE_LOG_LEVEL` | `INF` | Log level for this sample application. |

NXP driver options (HAL chip selection, PAL components, and so on) are configured through the NXP driver Kconfig (`CONFIG_NFC_DRIVER_NXP`, `CONFIG_PN5190_DRV`, etc.). See `drivers/nfc/nxp/Kconfig`.

To disable a sample option, set it in `prj.conf`:

```
CONFIG_NCS_NXP_DISCOVERY_LOOP_DISC_CONFIG=n
CONFIG_NCS_NXP_DISCOVERY_LOOP_LPCD=n
```

## Source layout

| File | Description |
|------|-------------|
| `src/main.c` | Entry point, discovery loop state machine, tag processing |
| `src/disc_loop_config.c` | Discovery loop profile configuration |
| `src/disc_loop_pn5190.c` | PN5190 IRQ handler and LPCD setup |
| `src/disc_loop_pn5180.c` | PN5180 IRQ handler and LPCD setup |
| `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | Devicetree overlay for SPI and GPIO pins |
