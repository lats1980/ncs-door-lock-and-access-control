# Matter Aliro door lock application

## Dual-MCU architecture

This application runs the Matter stack and the Aliro AT Host (`aliro_at`) on one MCU.
The AT Host communicates with `aliro-access-control-app` running on a second MCU, where
the Aliro stack runs as an AT Module. Together, the two applications provide Matter and
Aliro access-control functionality.

The two MCUs can communicate over either:

- Serial (UART)
- Bluetooth Low Energy, using Nordic UART Service (NUS)

Build the peer `aliro-access-control-app` with the matching transport configuration.

## Build

Run the following commands from the repository root.

### AT Host over serial

```shell
west build -p -b nrf54l15dk/nrf54l15/cpuapp -d build_at_host_serial/ applications/matter-aliro-door-lock-app/ -- \
  -Dmatter-aliro-door-lock-app_FILE_SUFFIX=at_host \
  -DEXTRA_DTC_OVERLAY_FILE=overlay-at_uart.overlay
```

### AT Host over BLE NUS

```shell
west build -p -b nrf54l15dk/nrf54l15/cpuapp -d build_at_host_ble/ applications/matter-aliro-door-lock-app/ -- \
  -Dmatter-aliro-door-lock-app_FILE_SUFFIX=at_host \
  -DEXTRA_CONF_FILE="overlay-aliro_at_host_ble_transport.conf"
```
