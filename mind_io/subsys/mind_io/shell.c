/*
 * "mio" shell commands.
 *
 *   mio info                          card status, identity, resources, warnings
 *   mio ports                         I2C/SPI ports and their slot state
 *   mio lines                         lines with their current state
 *   mio line <label> [0|1]            read or set a line
 *   mio led <on|off>                  fault LED
 *   mio eeprom read <offset> <len>    raw card EEPROM bytes, as "DATA <hex>"
 *   mio eeprom write <offset> <hex>   raw write (used by scripts/mio_card.py)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <mind_io/mind_io.h>
#include "mio_internal.h"

#define EEPROM_SHELL_MAX 64U

static const char *const speed_names[] = {"any", "100 kHz", "400 kHz", "1 MHz"};

static const struct mio_card *card_or_error(const struct shell *sh)
{
	const struct mio_card *card = mio_card_get();
	const char *reason;

	if (card == NULL) {
		enum mio_card_status st = mio_card_status(&reason);

		shell_error(sh, "no usable card (%s): %s", mio_card_status_name(st), reason);
	}

	return card;
}

static const char *bus_name(uint8_t bus)
{
	const char *name = mio_resource_name((enum mio_resource)bus);

	return (name != NULL) ? name : "?";
}

/* I2C ports on one host bus (direct or through repeaters) share one address space */
static void warn_shared_buses(const struct shell *sh, const struct mio_card *card)
{
	for (uint8_t i = 0U; i < card->n_i2c_ports; i++) {
		const struct mio_i2c_port_desc *a = &card->i2c_ports[i];

		for (uint8_t j = i + 1U; j < card->n_i2c_ports; j++) {
			const struct mio_i2c_port_desc *b = &card->i2c_ports[j];

			if (a->bus == b->bus) {
				shell_warn(sh, "note: %s and %s share %s: device addresses must be "
					   "unique across both ports", a->label, b->label,
					   bus_name(a->bus));
			}
		}

		for (uint8_t d = 0U; d < card->n_devices; d++) {
			if (card->devices[d].bus == a->bus) {
				shell_warn(sh, "note: address 0x%02x on %s (%s) is taken by an "
					   "on-card device", card->devices[d].addr, a->label,
					   bus_name(a->bus));
			}
		}
	}
}

static int cmd_info(const struct shell *sh, size_t argc, char **argv)
{
	const struct mio_card *card;
	const char *reason;
	enum mio_card_status st = mio_card_status(&reason);

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (st != MIO_CARD_OK) {
		shell_error(sh, "card: %s (%s)", mio_card_status_name(st), reason);
		return (st == MIO_CARD_ABSENT) ? -ENODEV : -EINVAL;
	}

	card = mio_card_get();
	if (card->has_identity) {
		const struct mio_identity *id = &card->identity;

		shell_print(sh, "card: %s, rev %u.%u, serial %s, made %04u-%02u-%02u", id->name,
			    id->hw_major, id->hw_minor, id->serial, id->date / 10000U,
			    (id->date / 100U) % 100U, id->date % 100U);
	} else {
		shell_print(sh, "card: (no identity record)");
	}

	shell_fprintf(sh, SHELL_NORMAL, "resources:");
	for (int res = 0; res < MIO_RES_COUNT; res++) {
		if (mio_has_resource((enum mio_resource)res)) {
			shell_fprintf(sh, SHELL_NORMAL, " %s", mio_resource_name(res));
		}
	}
	shell_fprintf(sh, SHELL_NORMAL, "\n");

	for (uint8_t i = 0U; i < card->n_devices; i++) {
		shell_print(sh, "on-card device %u: %s @ 0x%02x on %s", card->devices[i].index,
			    (card->devices[i].type == MIO_DEV_PCA9557) ? "PCA9557" : "unknown",
			    card->devices[i].addr, bus_name(card->devices[i].bus));
	}

	shell_print(sh, "%u I2C port(s), %u SPI port(s), %u line(s)", card->n_i2c_ports,
		    card->n_spi_ports, card->n_lines);
	warn_shared_buses(sh, card);

	return 0;
}

static int cmd_ports(const struct shell *sh, size_t argc, char **argv)
{
	const struct mio_card *card = card_or_error(sh);
	char pin[12];

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (card == NULL) {
		return -ENODEV;
	}

	for (uint8_t i = 0U; i < card->n_i2c_ports; i++) {
		const struct mio_i2c_port_desc *p = &card->i2c_ports[i];

		shell_print(sh, "%-15s I2C slot %u  %s %s, %s  %s", p->label, p->slot,
			    bus_name(p->bus), (p->topology == MIO_I2C_REPEATER) ? "via repeater"
										 : "direct",
			    speed_names[p->speed],
			    (mio_port_by_label(p->label) != NULL) ? "ready" : "NOT READY");
	}

	for (uint8_t i = 0U; i < card->n_spi_ports; i++) {
		const struct mio_spi_port_desc *p = &card->spi_ports[i];

		shell_print(sh, "%-15s SPI slot %u  CS %s active-%s, max %u Hz  %s", p->label,
			    p->slot, mio_pin_name(&p->cs, pin, sizeof(pin)),
			    ((p->cs_flags & MIO_CS_ACTIVE_HIGH) != 0U) ? "high" : "low",
			    p->max_freq, (mio_port_by_label(p->label) != NULL) ? "ready" : "NOT READY");
	}

	return 0;
}

static int cmd_lines(const struct shell *sh, size_t argc, char **argv)
{
	const struct mio_card *card = card_or_error(sh);
	char pin[12];

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (card == NULL) {
		return -ENODEV;
	}

	for (uint8_t i = 0U; i < card->n_lines; i++) {
		const struct mio_line_desc *d = &card->lines[i];
		struct mio_line line = {.desc = d};
		int val = mio_line_read(&line);

		shell_print(sh, "%-15s %-7s %-6s active-%-4s %-7s %s", d->label,
			    mio_pin_name(&d->pin, pin, sizeof(pin)),
			    ((d->flags & MIO_LINE_OUTPUT) != 0U) ? "output" : "input",
			    ((d->flags & MIO_LINE_ACTIVE_LOW) != 0U) ? "low" : "high",
			    mio_role_name(d->role),
			    (val < 0) ? "error" : ((val != 0) ? "active" : "inactive"));
	}

	return 0;
}

static int cmd_line(const struct shell *sh, size_t argc, char **argv)
{
	struct mio_line line;
	int ret;

	ret = mio_line_get(argv[1], &line);
	if (ret != 0) {
		shell_error(sh, "%s: %s", argv[1],
			    (ret == -ENOENT) ? "no such line on this card" : "no usable card");
		return ret;
	}

	if (argc == 3) {
		ret = mio_line_set(&line, (strcmp(argv[2], "0") != 0) ? 1 : 0);
		if (ret != 0) {
			shell_error(sh, "%s: %s", argv[1],
				    (ret == -ENOTSUP) ? "is an input" : "set failed");
			return ret;
		}
	}

	ret = mio_line_read(&line);
	if (ret < 0) {
		shell_error(sh, "%s: read failed", argv[1]);
		return ret;
	}

	shell_print(sh, "%s: %s", argv[1], (ret != 0) ? "active" : "inactive");
	return 0;
}

static int cmd_led(const struct shell *sh, size_t argc, char **argv)
{
	bool on = (strcmp(argv[1], "on") == 0) || (strcmp(argv[1], "1") == 0);
	int ret;

	ARG_UNUSED(argc);

	ret = mio_fault_led_set(on);
	if (ret == -ENODEV) {
		shell_error(sh, "this board does not wire the fault LED");
	} else if (ret == -ENOTSUP) {
		shell_error(sh, "the card has no fault LED");
	} else if (ret != 0) {
		shell_error(sh, "fault LED: %d", ret);
	} else {
		shell_print(sh, "fault LED %s", on ? "on" : "off");
	}

	return ret;
}

static int parse_offset_len(const struct shell *sh, const char *off_s, const char *len_s,
			    off_t *offset, size_t *len)
{
	char *end;
	unsigned long v;

	v = strtoul(off_s, &end, 0);
	if ((*end != '\0') || (v > 0xFFFFUL)) {
		shell_error(sh, "invalid offset '%s'", off_s);
		return -EINVAL;
	}
	*offset = (off_t)v;

	if (len_s != NULL) {
		v = strtoul(len_s, &end, 0);
		if ((*end != '\0') || (v == 0UL) || (v > EEPROM_SHELL_MAX)) {
			shell_error(sh, "invalid length '%s' (1..%u)", len_s, EEPROM_SHELL_MAX);
			return -EINVAL;
		}
		*len = (size_t)v;
	}

	return 0;
}

static int cmd_eeprom_read(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t buf[EEPROM_SHELL_MAX];
	char hex[(2U * EEPROM_SHELL_MAX) + 1U];
	off_t offset;
	size_t len = 0U;
	int ret;

	ARG_UNUSED(argc);

	ret = parse_offset_len(sh, argv[1], argv[2], &offset, &len);
	if (ret != 0) {
		return ret;
	}

	ret = mio_card_eeprom_read(offset, buf, len);
	if (ret != 0) {
		shell_error(sh, "card EEPROM read failed (%d)", ret);
		return ret;
	}

	bin2hex(buf, len, hex, sizeof(hex));
	shell_print(sh, "DATA %s", hex);
	return 0;
}

static int cmd_eeprom_write(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t buf[EEPROM_SHELL_MAX];
	size_t hexlen = strlen(argv[2]);
	off_t offset;
	size_t len;
	int ret;

	ARG_UNUSED(argc);

	ret = parse_offset_len(sh, argv[1], NULL, &offset, &len);
	if (ret != 0) {
		return ret;
	}

	if ((hexlen == 0U) || ((hexlen % 2U) != 0U) || (hexlen > (2U * EEPROM_SHELL_MAX))) {
		shell_error(sh, "data must be 1..%u bytes as an even number of hex digits",
			    EEPROM_SHELL_MAX);
		return -EINVAL;
	}

	len = hex2bin(argv[2], hexlen, buf, sizeof(buf));
	if (len != (hexlen / 2U)) {
		shell_error(sh, "invalid hex data");
		return -EINVAL;
	}

	ret = mio_card_eeprom_write(offset, buf, len);
	if (ret != 0) {
		shell_error(sh, "card EEPROM write failed (%d) - write-protected (WC high)?", ret);
		return ret;
	}

	shell_print(sh, "wrote %u bytes at 0x%04x (applies after reset)", (unsigned int)len,
		    (unsigned int)offset);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(mio_eeprom_cmds,
	SHELL_CMD_ARG(read, NULL, "<offset> <len>: raw card EEPROM bytes", cmd_eeprom_read, 3, 0),
	SHELL_CMD_ARG(write, NULL, "<offset> <hex>: raw write, up to 64 bytes", cmd_eeprom_write,
		      3, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(mio_cmds,
	SHELL_CMD_ARG(info, NULL, "Card status, identity and resources", cmd_info, 1, 0),
	SHELL_CMD_ARG(ports, NULL, "The card's I2C and SPI ports", cmd_ports, 1, 0),
	SHELL_CMD_ARG(lines, NULL, "The card's lines and their state", cmd_lines, 1, 0),
	SHELL_CMD_ARG(line, NULL, "<label> [0|1]: read or set a line", cmd_line, 2, 1),
	SHELL_CMD_ARG(led, NULL, "<on|off>: fault LED", cmd_led, 2, 0),
	SHELL_CMD(eeprom, &mio_eeprom_cmds, "Raw card EEPROM access", NULL),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(mio, &mio_cmds, "mind_io I/O card", NULL);
