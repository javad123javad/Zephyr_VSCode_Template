# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Out-of-tree PCA9557 GPIO expander driver (see pca9557_driver/README
# for why this needed its own driver instead of Zephyr's in-tree
# "nxp,pca95xx").
set(ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/pca9557_driver)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(i2c_test)

# Pulls in Zephyr's shared USB device stack bring-up helper
# (sample_usbd_init_device() etc.), used by usb_device_test.c. See
# samples/subsys/usb/common/ - it's normally sample-only, but reusing
# it here is far less error-prone than re-deriving USB descriptor
# setup from scratch for a bring-up test.
include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

target_sources(app PRIVATE
	src/main.c
	src/i2c_scan.c
	src/pca9557_test.c
	src/bme280_test.c
	src/can_test.c
	src/i2s_test.c
	src/usb_power_test.c
	src/usb_device_test.c
)
