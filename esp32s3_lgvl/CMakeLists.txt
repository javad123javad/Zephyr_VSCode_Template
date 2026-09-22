# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(esp32_lgvl)

target_sources(app PRIVATE src/main.c src/ui.c)

if(CONFIG_ARCH_POSIX)
  # native_sim/QEMU-less host build for fast LVGL UI iteration: no real
  # Wi-Fi hardware exists there, so use the fake stand-in instead.
  target_sources(app PRIVATE src/wifi_sim.c)
else()
  target_sources(app PRIVATE src/wifi.c)
endif()
