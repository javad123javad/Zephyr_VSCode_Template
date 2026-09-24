# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Out-of-tree PCA9557 GPIO expander driver (see pca9557_driver/README
# for why this needed its own driver instead of Zephyr's in-tree
# "nxp,pca95xx").
set(ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/pca9557_driver)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(i2c_test)

# The default mindos_n6 variant executes in place from external flash and
# is only bootable when chainloaded by MCUboot, which sysbuild provides.
if(CONFIG_XIP AND NOT CONFIG_BOOTLOADER_MCUBOOT)
	message(FATAL_ERROR
		"This board variant runs from external flash (XIP) and must be "
		"built with --sysbuild so MCUboot is included, or use the /sb variant "
		"for a RAM-loaded image. See README.rst.")
endif()

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
	src/lan_test.c
	src/axisram_test.c
	src/eeprom_test.c
)

# CONFIG_CODE_DATA_RELOCATION to AXISRAM3 was tried here and reverted:
# it hangs at boot on this board. See the README section "AXISRAM3
# extra SRAM" for why - short version: relocating an *initialized*
# section (e.g. i2s_test.c's const sine table, or any .data) needs an
# early boot-time copy into AXISRAM3, and that copy (arch_data_copy(),
# called before any Kconfig SYS_INIT level) runs before the
# stm32n6_axisram driver enables AXISRAM3's clock at PRE_KERNEL_2 -
# writing to a not-yet-clocked SRAM bank hangs the bus before anything
# can even print. axisram_test.c's own buffer works fine specifically
# because it's uninitialized (no early copy needed) and is only
# touched at runtime from main(), well after PRE_KERNEL_2.
