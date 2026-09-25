# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mind_io_test)

set(common ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_sources(app PRIVATE
  src/main.c
  ${common}/emul_card_eeprom.c
  ${common}/emul_pca9557.c
  ${common}/emul_spi_echo.c
)
target_include_directories(app PRIVATE ${common})

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
generate_inc_file_for_target(app ${CMAKE_CURRENT_SOURCE_DIR}/../golden/dev_card.bin
			     ${gen_dir}/dev_card.bin.inc)
