/*
 * M24C64-R board-configuration EEPROM on io_cnf_i2c, driven by Zephyr's
 * in-tree "atmel,at24" EEPROM driver (ST's M24xxx family uses the same
 * programming model).
 *
 * The EEPROM holds real board settings, so the test never leaves
 * anything changed: the start of the memory is only read and dumped,
 * and the write test uses the last page, which is saved first and
 * written back afterwards. Only a power loss during the few
 * milliseconds of the test could leave that page modified.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eeprom_test.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>

#define EEPROM_NODE DT_NODELABEL(board_eeprom)
#define PAGE_SIZE   DT_PROP(EEPROM_NODE, pagesize)
#define DUMP_LEN    64U

static const struct device *const eeprom = DEVICE_DT_GET(EEPROM_NODE);

static void dump(off_t offset, const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if ((i % 16U) == 0U) {
			printk("  %04lx:", (unsigned long)(offset + i));
		}
		printk(" %02x", buf[i]);
		if ((i % 16U) == 15U) {
			printk("\n");
		}
	}
}

static bool write_verify(off_t offset, const uint8_t *data, size_t len, const char *what)
{
	uint8_t readback[PAGE_SIZE];
	int err;

	err = eeprom_write(eeprom, offset, data, len);
	if (err != 0) {
		printk("EEPROM: %s write failed (%d)%s\n", what, err,
		       (err == -EIO) ? " - is the WC (write control) pin held high?" : "");
		return false;
	}

	err = eeprom_read(eeprom, offset, readback, len);
	if (err != 0) {
		printk("EEPROM: %s read-back failed (%d)\n", what, err);
		return false;
	}

	if (memcmp(data, readback, len) != 0) {
		printk("EEPROM: %s read-back mismatch\n", what);
		dump(offset, readback, len);
		return false;
	}

	return true;
}

void eeprom_test_run(void)
{
	uint8_t head[DUMP_LEN];
	uint8_t saved[PAGE_SIZE];
	uint8_t pattern[PAGE_SIZE];
	size_t size;
	off_t last_page;
	bool ok;
	bool restored;
	int err;

	if (!device_is_ready(eeprom)) {
		printk("EEPROM: M24C64 @ 0x%02x not ready\n", DT_REG_ADDR(EEPROM_NODE));
		return;
	}

	size = eeprom_get_size(eeprom);
	last_page = (off_t)(size - PAGE_SIZE);
	printk("EEPROM: M24C64 @ 0x%02x, %u bytes, %u-byte pages\n",
	       DT_REG_ADDR(EEPROM_NODE), (unsigned int)size, (unsigned int)PAGE_SIZE);

	err = eeprom_read(eeprom, 0, head, sizeof(head));
	if (err != 0) {
		printk("EEPROM: read failed (%d)\n", err);
		return;
	}
	printk("EEPROM: first %u bytes (board configuration area):\n", DUMP_LEN);
	dump(0, head, sizeof(head));

	err = eeprom_read(eeprom, last_page, saved, sizeof(saved));
	if (err != 0) {
		printk("EEPROM: read of last page failed (%d)\n", err);
		return;
	}

	/* Complement of the saved bytes, so every bit is flipped by the test */
	for (size_t i = 0; i < sizeof(pattern); i++) {
		pattern[i] = (uint8_t)~saved[i];
	}

	ok = write_verify(last_page, pattern, sizeof(pattern), "test pattern");

	/* Always restore, even if the pattern check failed half-way */
	restored = write_verify(last_page, saved, sizeof(saved), "restore");
	if (!restored) {
		printk("EEPROM: WARNING: last page @ 0x%04lx may not hold its original "
		       "contents; they were:\n", (unsigned long)last_page);
		dump(last_page, saved, sizeof(saved));
	}

	printk("EEPROM: write/read-back on last page @ 0x%04lx %s%s\n",
	       (unsigned long)last_page, (ok && restored) ? "OK" : "FAILED",
	       restored ? " (original contents restored)" : "");
}
