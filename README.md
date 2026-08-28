# nRF Door Lock and Access Control Add-on

In combination with the nRF Connect SDK, the nRF Door Lock and Access Control Add-on provides a complete reference for building Aliro- and Matter-compatible locks and access control readers for both residential and commercial applications. The reference integrates multiple wireless technologies - including Bluetooth Low Energy (BLE), Ultra-Wideband (UWB), NFC, Thread, and Wi-Fi - allowing developers to choose the appropriate technology for their specific use case.

Aliro standardizes the interaction that lets a phone or wearable act as a digital key at an opening. Matter specifies how connected products communicate for command-and-control use cases like remotely locking or unlocking a door, checking lock status, user provisioning or integrating with home automation systems. The reference can support Aliro alone, Matter alone, or both protocols, depending on the specific use case and product requirements.

## Getting started

To get started with the nRF Door Lock and Access Control Add-on, follow the [documentation](https://docs.nordicsemi.com/bundle/door_lock_and_access_control_1.0.0/page/index.html).

## AT Bridge (Aliro and Matter dual-MCU)

The AT command module in `app/src/at_command` implements a bridge between two MCUs in a split architecture:

- **AT Module** — runs on the Aliro MCU. It exposes Aliro reader functionality as AT commands and executes requests from the AT Host (lock/unlock, credential management, reader provisioning, and related Aliro operations).
- **AT Host** — runs on the Matter MCU together with the Matter stack. It translates Matter Door Lock cluster Aliro attributes and commands into AT commands sent to the AT Module.

Both sides share the same transport abstraction (`at_transport.h`): received data is passed to `at_process()`, and outbound data uses the registered transport send callback.

### Transports

#### Serial transport (UART)

Default transport (`CONFIG_ALIRO_AT_TRANSPORT_SERIAL`). The AT UART is bound through the devicetree chosen node `aliro,at-uart` (see `overlay-at_uart.overlay`, which assigns `uart30` on nRF54L15 DK).

On nRF54L15 DK, `uart30` uses **P0.00 (TX)** and **P0.01 (RX)**.

**Board Configurator**

Disable **VCOM0** in the [nRF Board Configurator](https://docs.nordicsemi.com/r/bundle/nrf-connect-for-desktop/page/board-configurator-app) on both DKs so that the onboard debugger VCOM does not use the AT UART pins.

**Wiring between AT Module and AT Host DKs**

Connect the two boards with a crossover UART link and a common ground:

| AT Module (Aliro MCU) | AT Host (Matter MCU) |
|-----------------------|----------------------|
| TX (P0.00)            | RX (P0.01)           |
| RX (P0.01)            | TX (P0.00)           |
| GND                   | GND                  |

**Build commands** (run from the `app/` directory):

AT Module over serial:

```shell
west build -p -b nrf54l15dk/nrf54l15/cpuapp -d build_at_module/ -- \
  -DEXTRA_CONF_FILE=overlay-aliro_at_module.conf \
  -DEXTRA_DTC_OVERLAY_FILE=overlay-at_uart.overlay \
  -DFILE_SUFFIX=at_module
```

AT Host over serial:

```shell
west build -p -b nrf54l15dk/nrf54l15/cpuapp -d build_at_host/ -- \
  -DEXTRA_CONF_FILE=overlay-aliro_at_host.conf \
  -DEXTRA_DTC_OVERLAY_FILE=overlay-at_uart.overlay \
  -DFILE_SUFFIX=at_host \
  -DSNIPPET="matter"
```

#### NUS transport (Bluetooth LE)

Wireless transport over the Nordic UART Service (NUS). The AT Module acts as a BLE **peripheral** (NUS server); the AT Host acts as a BLE **central** (NUS client) alongside Matter commissioning.

**Build commands** (run from the `app/` directory):

AT Module over NUS:

```shell
west build -p -b nrf54l15dk/nrf54l15/cpuapp -d build_at_module/ -- \
  -DEXTRA_CONF_FILE="overlay-aliro_at_module.conf;overlay-aliro_at_transport_ble_peripheral.conf" \
  -Dapp_SNIPPET=bt_nus \
  -DFILE_SUFFIX=at_module
```

AT Host over NUS:

```shell
west build -p -b nrf54l15dk/nrf54l15/cpuapp -d build_at_host/ -- \
  -DEXTRA_CONF_FILE="overlay-aliro_at_host.conf;overlay-aliro_at_transport_ble_central.conf" \
  -DSNIPPET="matter" \
  -DFILE_SUFFIX=at_host
```

## NXP PN5190 NFC reader (AT Module)

The **AT Module** build uses the NXP **PN5190** as the default NFC reader frontend (instead of the STM RFAL-based reader). This is selected in `overlay-aliro_at_module.conf` (`CONFIG_NFC_DRIVER_NXP=y`, `CONFIG_NFC_DRIVER_STM=n`) and wired through the board overlay `boards/nrf54l15dk_nrf54l15_cpuapp_at_module.overlay` (applied when building with `-DFILE_SUFFIX=at_module`).

The PN5190 connects to the nRF54L15 DK over **SPI21**. Pin assignments match the [NXP discovery loop sample](drivers/samples/nxp_discovery_loop/README.md) and the [NXP NFC driver](drivers/nfc/nxp/README.md).

**Requirements**

- [nRF54L15 DK](https://docs.nordicsemi.com/bundle/ncs-latest/page/zephyr/boards/nordic/nrf54l15dk/doc/index.html)
- [PNEV5190B](https://www.nxp.com/products/rfid-nfc/nfc-hf/nfc-readers/nfc-reader-frontend-solution:PN5190) evaluation board
- NXP NFC Reader Library under `drivers/nfc/nxp/NxpNfcRdLib/` (see the NXP driver README)

**Pin mapping (nRF54L15 DK ↔ PNEV5190B)**

The PNEV5190B is **independently powered**. Use its own power supply; do not connect VDDIO from the nRF54L15 DK. Connect GND between the boards for a common ground reference.

The AT Module board overlay disables buttons and LEDs that share pins with the NFC interface (for example P0.04 IRQ and P1.13 SCK).

| nRF54L15 DK | PN5190 EB (PNEV5190B) |
|-------------|------------------------|
| P1.13       | SPI_CLK (SCK)          |
| P1.12       | SPI_MISO (MISO)        |
| P1.11       | SPI_MOSI (MOSI)        |
| P2.08       | SPI_NSS (NSS)          |
| P0.04       | IRQ                    |
| P2.06       | RESET (RST)            |
| GND         | GND                    |

## License

Source code included within this repository is licensed under the [LicenseRef-Nordic-5-Clause](LICENSE).

## Support

To ask questions about the reference or to get technical support, please refer to [Nordic DevZone](https://devzone.nordicsemi.com).
