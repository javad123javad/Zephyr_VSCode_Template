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
#include <zephyr/shell/shell.h>

#define EEPROM_NODE DT_NODELABEL(board_eeprom)
#define PAGE_SIZE   DT_PROP(EEPROM_NODE, pagesize)
#define DUMP_LEN    64U

static const struct device *const eeprom = DEVICE_DT_GET(EEPROM_NODE);

static void dump(const struct shell *sh, off_t offset, const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i += 16U) {
		shell_hexdump_line(sh, (unsigned int)(offset + i), &buf[i], MIN(len - i, 16U));
	}
}

static int write_verify(const struct shell *sh, off_t offset, const uint8_t *data,
			size_t len, const char *what)
{
	uint8_t readback[PAGE_SIZE];
	int err;

	err = eeprom_write(eeprom, offset, data, len);
	if (err != 0) {
		shell_error(sh, "EEPROM: %s write failed (%d)%s", what, err,
			    (err == -EIO) ? " - is the WC (write control) pin held high?" : "");
		return err;
	}

	err = eeprom_read(eeprom, offset, readback, len);
	if (err != 0) {
		shell_error(sh, "EEPROM: %s read-back failed (%d)", what, err);
		return err;
	}

	if (memcmp(data, readback, len) != 0) {
		shell_error(sh, "EEPROM: %s read-back mismatch", what);
		dump(sh, offset, readback, len);
		return -EIO;
	}

	return 0;
}

int eeprom_test_run(const struct shell *sh)
{
	uint8_t head[DUMP_LEN];
	uint8_t saved[PAGE_SIZE];
	uint8_t pattern[PAGE_SIZE];
	size_t size;
	off_t last_page;
	int test_err;
	int restore_err;
	int err;

	if (!device_is_ready(eeprom)) {
		shell_error(sh, "EEPROM: M24C64 @ 0x%02x not ready", DT_REG_ADDR(EEPROM_NODE));
		return -ENODEV;
	}

	size = eeprom_get_size(eeprom);
	last_page = (off_t)(size - PAGE_SIZE);
	shell_print(sh, "EEPROM: M24C64 @ 0x%02x, %u bytes, %u-byte pages",
		    DT_REG_ADDR(EEPROM_NODE), (unsigned int)size, (unsigned int)PAGE_SIZE);

	err = eeprom_read(eeprom, 0, head, sizeof(head));
	if (err != 0) {
		shell_error(sh, "EEPROM: read failed (%d)", err);
		return err;
	}
	shell_print(sh, "EEPROM: first %u bytes (board configuration area):", DUMP_LEN);
	dump(sh, 0, head, sizeof(head));

	err = eeprom_read(eeprom, last_page, saved, sizeof(saved));
	if (err != 0) {
		shell_error(sh, "EEPROM: read of last page failed (%d)", err);
		return err;
	}

	/* Complement of the saved bytes, so every bit is flipped by the test */
	for (size_t i = 0; i < sizeof(pattern); i++) {
		pattern[i] = (uint8_t)~saved[i];
	}

	test_err = write_verify(sh, last_page, pattern, sizeof(pattern), "test pattern");

	/* Always restore, even if the pattern check failed half-way */
	restore_err = write_verify(sh, last_page, saved, sizeof(saved), "restore");
	if (restore_err != 0) {
		shell_error(sh, "EEPROM: WARNING: last page @ 0x%04lx may not hold its original "
			    "contents; they were:", (unsigned long)last_page);
		dump(sh, last_page, saved, sizeof(saved));
		return restore_err;
	}

	if (test_err != 0) {
		return test_err;
	}

	shell_print(sh, "EEPROM: write/read-back on last page @ 0x%04lx OK "
		    "(original contents restored)", (unsigned long)last_page);
	return 0;
}
