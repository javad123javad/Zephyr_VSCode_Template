# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_weather)

set(APP_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_include_directories(app PRIVATE ${APP_SRC_DIR})

target_sources(app PRIVATE
  ${APP_SRC_DIR}/weather_json.c
  ${APP_SRC_DIR}/weather_scene.c
  src/main.c
)
