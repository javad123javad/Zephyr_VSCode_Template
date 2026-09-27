# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(esp32_lgvl)

target_include_directories(app PRIVATE src src/ui)

target_sources(app PRIVATE
  src/backlight.c
  src/main.c
  src/ui.c
  src/ui/ui_clock.c
  src/ui/ui_common.c
  src/ui/ui_home.c
  src/ui/ui_local.c
  src/ui/ui_menu.c
  src/ui/ui_nav.c
  src/ui/ui_network.c
  src/ui/ui_password.c
  src/ui/ui_placeholder.c
  src/ui/ui_settings.c
  src/ui/ui_touch_cal.c
  src/weather_json.c
)

if(CONFIG_ARCH_POSIX)
  # native_sim/QEMU-less host build for fast LVGL UI iteration: no real
  # Wi-Fi/network stack exists there, so use the fake stand-ins instead.
  target_sources(app PRIVATE src/wifi_sim.c src/weather_sim.c)
else()
  target_sources(app PRIVATE src/wifi.c src/weather.c)

  set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

  generate_inc_file_for_target(
    app
    src/weather_cert/isrg_root_x1.der
    ${gen_dir}/isrg_root_x1.der.inc
  )
endif()
