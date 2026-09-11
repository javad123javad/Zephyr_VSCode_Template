/*
 * Scans both on-board I2C buses at boot and reports any device that
 * ACKs its address. Follow up interactively from the shell with:
 *   i2c scan i2c@50005400   (i2c1 / io_cnf_i2c)
 *   i2c scan i2c@50005800   (i2c2 / io_i2c)
 * (The shell device names are the raw devicetree node names because
 * no "label" property is set on these nodes upstream.)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i2c_scan.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

struct i2c_bus {
	const struct device *dev;
	const char *name;
};

static const struct i2c_bus buses[] = {
	{ .dev = DEVICE_DT_GET(DT_NODELABEL(i2c1)), .name = "i2c1 (io_cnf_i2c)" },
	{ .dev = DEVICE_DT_GET(DT_NODELABEL(i2c2)), .name = "i2c2 (io_i2c)" },
};

/* 7-bit address range scanned by the I2C shell's "i2c scan" command */
#define I2C_SCAN_ADDR_FIRST 0x04
#define I2C_SCAN_ADDR_LAST  0x77

static void i2c_bus_scan(const struct i2c_bus *bus)
{
	uint8_t found = 0;

	if (!device_is_ready(bus->dev)) {
		printk("%s: device not ready\n", bus->name);
		return;
	}

	printk("Scanning %s ...\n", bus->name);
	printk("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

	for (uint8_t i = 0; i <= I2C_SCAN_ADDR_LAST; i += 16) {
		printk("%02x: ", i);
		for (uint8_t j = 0; j < 16; j++) {
			uint8_t addr = i + j;
			struct i2c_msg msg;
			uint8_t dummy;

			if (addr < I2C_SCAN_ADDR_FIRST || addr > I2C_SCAN_ADDR_LAST) {
				printk("   ");
				continue;
			}

			msg.buf = &dummy;
			msg.len = 0U;
			msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

			if (i2c_transfer(bus->dev, &msg, 1, addr) == 0) {
				printk("%02x ", addr);
				found++;
			} else {
				printk("-- ");
			}
		}
		printk("\n");
	}

	printk("%s: %u device(s) found\n\n", bus->name, found);
}

void i2c_scan_all(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(buses); i++) {
		i2c_bus_scan(&buses[i]);
	}

	printk("Scan done. Use the shell (\"i2c scan i2c@50005400\" / "
	       "\"i2c scan i2c@50005800\") to re-scan after connecting a device.\n");
}
