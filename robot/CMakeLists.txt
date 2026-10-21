# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

# Wi-Fi credentials are kept out of version control in wifi.conf.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/wifi.conf)
  list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/wifi.conf)
endif()

# The application runs under MCUboot, which sysbuild builds and configures.
if(NOT SYSBUILD)
  message(FATAL_ERROR "Build this application with sysbuild: west build --sysbuild ...")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(robot)

target_sources(app PRIVATE
  src/collision.c
  src/drive.c
  src/health.c
  src/imu.c
  src/link.c
  src/main.c
  src/motor.c
  src/ota.c
  src/wifi.c
)
target_sources_ifdef(CONFIG_ROBOT_BATTERY app PRIVATE src/battery.c)
