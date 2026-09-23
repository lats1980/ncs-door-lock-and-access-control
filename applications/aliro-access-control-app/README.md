# Aliro access-control application

## Dual-MCU architecture

This application runs the Aliro stack as an AT Module on one MCU. It communicates with
`matter-aliro-door-lock-app` running on a second MCU, where the Matter stack and Aliro
AT Host (`aliro_at`) run. Together, the two applications provide Matter and Aliro
access-control functionality.

The two MCUs can communicate over either:

- Serial (UART)
- Bluetooth Low Energy, using Nordic UART Service (NUS)

Build the peer `matter-aliro-door-lock-app` with the matching transport configuration.

## Build

Run the following commands from the repository root.

### AT Module over serial

```shell
west build -p -b nrf54l15dk/nrf54l15/cpuapp -d build_at_module_serial/ applications/aliro-access-control-app/ -- \
  -Daliro-access-control-app_FILE_SUFFIX=at_module \
  -DEXTRA_DTC_OVERLAY_FILE=overlay-at_uart.overlay
```

### AT Module over BLE NUS

```shell
west build -p -b nrf54l15dk/nrf54l15/cpuapp applications/aliro-access-control-app/ -d build_at_module_ble/ -- \
  -Daliro-access-control-app_FILE_SUFFIX=at_module \
  -DEXTRA_CONF_FILE="overlay-aliro_at_module_ble_transport.conf"
```
