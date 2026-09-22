# Internet Clock (Zephyr + LVGL)

ESP32-S3 DevKitC and a 4" ST7796S 480x320 color SPI TFT with an XPT2046
resistive touch panel. Wi-Fi connects at boot, time comes from SNTP
(`pool.ntp.org`), and the whole UI (clock, settings menu, time zone, Wi-Fi
scan/connect) is touch-driven.

Originally built on a classic ESP32 DevKitC, but that chip's Zephyr SPI
driver has real gaps (see "Why ESP32-S3" below) that caused persistent
display corruption no devicetree/application-level fix could work around.
Moved to ESP32-S3, which has a properly DMA-accelerated SPI path in Zephyr.
The classic-ESP32 overlay (`boards/esp32_devkitc_procpu.overlay`) is kept
for reference but is no longer the primary target.

## Hardware

- Board: ESP32-S3 DevKitC (`esp32s3_devkitc/esp32s3/procpu`)
- Display: 4" ST7796S, 480x320, SPI (`sitronix,st7796s` via Zephyr's
  `zephyr,mipi-dbi-spi` wrapper).
- Touch: XPT2046 resistive touch controller, sharing the display's SPI bus.

## Pinout

Display and touch share SPI2 (SCK/SDI/SDO); each has its own chip-select.

| Module pin         | ESP32-S3 pin | Notes                          |
|---------------------|--------------|----------------------------------|
| VCC                 | 3V3          |                                  |
| GND                 | GND          |                                  |
| SCK / T_CLK         | GPIO12       | shared SPI clock                |
| SDI (MOSI) / D_DIN  | GPIO11       | shared SPI data out              |
| SDO (MISO) / T_DO   | GPIO13       | shared SPI data in                |
| CS (display)        | GPIO42       | display chip-select               |
| T_CS (touch)        | GPIO7        | touch chip-select                 |
| DC / RS             | GPIO41       | display data/command              |
| RESET               | GPIO39       | display reset                     |
| LED (backlight)     | GPIO6        | held high at boot (always on)     |
| T_IRQ               | GPIO8        | wired, currently unused by driver |

No external pull-ups or resistors needed. Pins were chosen to avoid the S3's
strapping pins (0, 3, 45, 46), USB D+/D- (19/20), and the octal PSRAM
interconnect (33-37). DC/RESET/CS follow the pin assignment used by the
VIEWE UEDX32480035E-WB-A board (`boards/viewe/uedx32480035e_wb_a` upstream in
Zephyr), which this display's init sequence and gamma/power tuning were also
taken from — the first values that produced a clean, stable image on this
panel.

## Build & flash

From the west workspace root (`/home/javad/zephyrproject`):

```sh
export ZEPHYR_BASE=/home/javad/zephyrproject/zephyr
west build -b esp32s3_devkitc/esp32s3/procpu -d temp_workspace/build_s3 temp_workspace
west flash -d temp_workspace/build_s3 --esp-device /dev/ttyACM0
```

Console is the board's default UART0 console (bridged to the same USB port
used for flashing); no devicetree console override needed. 115200 baud.

## UI-only iteration with native_sim

For LVGL/UI changes, `native_sim` gives a desktop window with mouse-as-touch
instead of reflashing hardware every time (requires `SDL2-devel`/`libsdl2-dev`
installed on the host; the default `native_sim` target is 32-bit and needs
32-bit SDL2 headers, so this project targets the 64-bit variant instead):

```sh
west build -b native_sim/native/64 -d temp_workspace/build_native temp_workspace
./temp_workspace/build_native/zephyr/zephyr.exe
```

`src/wifi_sim.c` stands in for `src/wifi.c` on this target (picked
automatically in `CMakeLists.txt` via `CONFIG_ARCH_POSIX`): no real Wi-Fi
hardware exists on native_sim, so it fabricates a ticking clock and two dummy
scan results just to exercise the screens.

## Configuration

- `prj.conf`: `CONFIG_WIFI_CREDENTIALS_STATIC_SSID` / `_PASSWORD` — your Wi-Fi
  network (also changeable on-device via Settings > Wi-Fi Setup, which persists
  to flash).
- `src/ui.c`: `DEFAULT_TZ_OFFSET_HOURS` — default UTC offset used at boot (also
  changeable on-device via Settings > Time Zone; not persisted across reboots).

## Why ESP32-S3

On the classic ESP32, the display would render briefly then corrupt/blank,
regardless of SPI clock speed, chip-select hold settings, write chunk size,
gamma/power tuning, or whether touch was enabled. A from-scratch Arduino
port using the exact same wiring and near-identical init sequence ran
rock-solid, which pointed at Zephyr's ESP32 SPI driver rather than our
config. Reading `drivers/spi/spi_esp32_spim.c` turned up several code paths
gated on `CONFIG_SOC_SERIES_ESP32` that only apply to (or are only
implemented for) newer variants (S2/S3/C3) — e.g. MOSI/MISO idle-line
polarity setup and DMA handling — reflecting real, and in one case
register-level, differences between classic ESP32 and the newer chips.
ESP32-S3 gets the fully-supported path.

## Touch and display orientation

The `touch@1` node in `boards/esp32s3_devkitc_procpu.overlay` sets
`swapped-x-y`, `inverted-x`, and `inverted-y`: this panel's touch axes are
physically swapped and mirrored relative to the display. The `st7796s` node
sets `rgb-is-inverted` for the same reason — this panel reports RGB as BGR,
so without it colors come out wrong (e.g. blue renders as green). Both were
derived empirically for this specific panel; a different physical panel of
the same model may need different values.

## Known limitations

- Backlight is wired always-on (no brightness control).
- Timezone offset is not persisted across reboots.
