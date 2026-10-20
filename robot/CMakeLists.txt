# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

# Wi-Fi credentials are kept out of version control in wifi.conf.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/wifi.conf)
  list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/wifi.conf)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(robot)

target_sources(app PRIVATE
  src/drive.c
  src/main.c
  src/motor.c
  src/web.c
  src/wifi.c
)

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(NAME http_resource_desc_robot_http
  KVMA RAM_REGION GROUP RODATA_REGION
)

generate_inc_file_for_target(app
  src/web/index.html
  ${ZEPHYR_BINARY_DIR}/include/generated/index.html.gz.inc
  --gzip
)
