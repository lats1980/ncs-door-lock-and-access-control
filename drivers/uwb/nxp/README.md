# NXP UWB Driver (SR150)

Zephyr / nRF Connect SDK port of the **NXP UWB IoT middleware** (UCI core and UWB
API) for the NXP SR150 UWB transceiver, connected over SPI.

It is compatible with **UWBIOT_SR150 v04.08.01** (`uwbiot-top`). Other versions are
rejected at configure time.

The NXP UWB middleware is **not** included in this repository. You must obtain it from NXP and place it locally before building.

## Setup

1. Download the NXP package (for the Murata Type 2BP module:
   `UWBIOT_SR150_v04.08.01_MCUx.zip`) and extract it so that this layout exists:

   ```
   drivers/uwb/nxp/uwbiot-top/
   ├── libs/            <- the middleware, compiled
   ├── firmware_images/ <- SR150 firmware (image source)
   ├── boards/Host/Rhodes4/UWB_DeviceConfig_SR1XX.h <- device configuration (included)
   └── ...
   ```

   `uwbiot-top/` is git-ignored. To keep the package elsewhere:

   ```bash
   west build ... -- -DNXP_UWB_PKG_DIR=/path/to/uwbiot-top
   ```

2. Add an `nxp,sr150` node to the board devicetree (see
   `dts/bindings/nxp,sr150.yaml`) and enable the driver:

   ```
   CONFIG_UWB_NXP=y
   ```

   The Aliro UWB backend `CONFIG_DOOR_LOCK_ALIRO_UWB_IMPL_SR150` selects it.

Working examples are `drivers/samples/demo_ranging_controller` and `demo_ranging_controlee`.

## Architecture

```
drivers/uwb/nxp/
├── CMakeLists.txt        # source list, include paths, firmware image path
├── Kconfig
├── cmake/nxp_uwb_stage.cmake
├── patches/              # patches applied to the staged NXP sources
├── nxp_uwb_platform/     # the Zephyr port
│   ├── osal/             # phOsalUwb_zephyr.c   threads, queues, semaphores, timers
│   ├── bus/              # sr150.c, uwb_bus_*.c  SPI / GPIO (devicetree)
│   ├── log/              # nxLog.c               NXP logging -> Zephyr logging
│   ├── include/          # uwb_board.h, driver_config.h, UWB_DeviceConfig.h.in, ...
│   ├── overlay/          # files that replace same-named files of the NXP package
│   └── stubs/            # weak SE stubs
└── uwbiot-top/           # NXP package (user provided)
```

NXP's own `boards/`, `osal/` (FreeRTOS, POSIX) and `log/` code is not compiled;
`nxp_uwb_platform/` provides it instead. The only file used from NXP's `boards/` is the
device configuration, see [Board configuration](#board-configuration).

### Staging

NXP headers include their siblings with `#include "..."`, which the compiler resolves
in the including file's own folder first, so a header cannot be replaced just by
putting another folder earlier in the include path. The build therefore works on a
copy of `uwbiot-top/libs` in `<build>/.../nxp_uwb_stage/`:

1. copy `libs/`
2. copy `nxp_uwb_platform/overlay/` over it (same relative paths as in `libs/`); it
   only holds two small headers that replace the SE051W secure element ones
   (`SE_Wrapper.h`, `StateMachine.h`), see `stubs/se_stubs.c`
3. apply `patches/*.patch`

The copy is only recreated when the NXP package, the overlay or a patch changes.

### Patches

Three patches are needed:

* `0001-zephyr-os-support.patch`: adds `UWBIOT_OS_ZEPHYR` as an OS flavour (feature
  header, OSAL types), enables the CCC feature used by Aliro and guards `__DEPRECATED`,
  which the C library defines too.
* `0002-hal-wait-dev-status.patch`: `phNxpUciHal_uwbDeviceInit()` waits on a semaphore
  that is given for every INIT *and* READY device status notification, so a repeated
  INIT ended the wait for READY early and init failed (`device status is failed 0`).
  The patch waits until the wanted state was reported.
* `0003-fix-ccc-capability-parse-pointer.patch`: `parseCapabilityInfo()` passed
  `&capsInfoData[index]` together with `&index` to `parseCapabilityCCCParams()`, which
  indexes the buffer with `*index` itself, so the offset was applied twice and CCC
  capabilities were read from the wrong position. The patch passes the base pointer.

Everything else NXP expects from the platform is handled in `nxp_uwb_platform/`, for
example:

| NXP assumption | Handled in |
|---|---|
| UWB task runs up to its first blocking call inside `StartUwbTask()` (highest FreeRTOS priority) | `osal/phOsalUwb_zephyr.c`: such threads start at the highest priority and are lowered once they block |
| IRQ wait of 10 s is longer than the retry window | `bus/uwb_bus_io.c`, `CONFIG_UWB_NXP_IRQ_WAIT_MAX_MS` (5 s, also the boot IRQ wait) |
| Board context given to `uwb_bus_init()` | `include/uwb_bus_board.h`: empty, there is one SR150 and one IRQ semaphore (`bus/uwb_bus_io.c`) |
| SPI transfers of at most 255 bytes (nRF52840 port) | `bus/uwb_bus_interface.c`, `CONFIG_UWB_NXP_SPI_SPLIT_SIZE` |
| Command timeouts | `CONFIG_UWB_NXP_CMD_*_TIMEOUT_MS` (`-D` over `uwb_target.h`) |
| Thread priorities | `CONFIG_UWB_NXP_THREAD_PRIORITY`, one priority for all threads |

Add further `.patch` files (`patch -p1` format, paths `a/libs/...`) to `patches/` if a
change cannot be done in the platform layer.

## Logging

All NXP log macros end up in `nLog()` / `nLog_au8()` (`log/nxLog.c`), which forward to the
Zephyr logging subsystem as the module `nxp_uwb_log`. `CONFIG_UWB_NXP_LOG_LEVEL` sets the
level of the middleware, `CONFIG_UWB_NXP_PLATFORM_LOG_LEVEL` the one of the Zephyr
platform layer (OSAL, SPI/GPIO bus). Messages above the level are not formatted.
`CONFIG_UWB_NXP_LOG_LINE_SIZE` is the size of one log line (the UCI frames are logged as
hex, 3 characters per byte).

## SR150 firmware

The image shipped with the NXP package is used, as selected by `UWBIOT_SR1XX_FW_*`
(ROW production mainline, FW 46.45.02 in v04.08.01). It is downloaded from host RAM
over SPI.

Use a firmware that matches the middleware version.

## Board configuration

`nxp_uwb_platform/include/uwb_board.h` and `driver_config.h` replace the NXP board
files. A product can supply its own copies with `-DNXP_UWB_BOARD_CONFIG_DIR=<dir>`; the
folder is searched first.

The device configuration (core configuration, antenna definitions, antenna delay and
PDoA calibration) is the one of the NXP package,
`boards/Host/Rhodes4/UWB_DeviceConfig_SR1XX.h`, used as it is: `<UWB_DeviceConfig.h>` is
generated at configure time and only includes that file, so no copy of the NXP file is
kept in this repository. To use other values, put your own `UWB_DeviceConfig.h` into
the `NXP_UWB_BOARD_CONFIG_DIR` folder.

## Hardware dependencies

The port is not tied to a board. What depends on the hardware:

* Devicetree node `nxp,sr150`: SPI, `irq-gpios`, `wakeup-gpios`, `en-gpios` and the
  optional `en-uwb-power-gpios` (supply enable, no-op when absent).
* Antenna layout: the application sets `ANTENNAE_CONFIGURATION_RX` per session, see
  `drivers/samples/demo_ranging_*/src/main.c` and `subsys/aliro/uwb/sr150_impl`.
* Antenna delay and PDoA calibration (`UWB_DeviceConfig.h`) and board variant
  (`driver_config.h`): NXP's Rhodes reference values.

## Notes

* The heap needs room for the NXP buffers (the demo uses
  `CONFIG_HEAP_MEM_POOL_SIZE=16384`).
