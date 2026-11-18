# demo_ranging_controller (Controller / Initiator)

SR150 Static STS two-way ranging (DS-TWR, unicast) built on the NXP middleware port in
`drivers/uwb/nxp`. Short MAC address of this device `0x1111`, of the peer `0x2222`.
Run it together with `demo_ranging_controlee` on a second board.

## Prerequisites

* Extract the NXP package (UWBIOT_SR150 v04.08.01) to `drivers/uwb/nxp/uwbiot-top`,
  see `drivers/uwb/nxp/README.md`.
* An nRF54L15 DK and an SR150 module wired to the pins listed in
  `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` (SPIM21: SCK P1.13, MOSI P1.11,
  MISO P1.12, CS P2.04, IRQ P0.00, EN P2.02, WAKEUP P2.00). Button 0 shares P1.13 with
  SCK and is disabled in the overlay.

## Build and run

```bash
west build -p -b nrf54l15dk/nrf54l15/cpuapp
west flash
```

The distances are printed to the log (RTT). The SR150 firmware is the one of the NXP
package. The antenna configuration (AoA mode, one antenna pair) is set in
`src/main.c`.
