/*
 * mind_io core: reads the card description from the card EEPROM at boot,
 * validates it, brings up the card's lines and answers queries about it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <mind_io/mind_io.h>
#include "mio_internal.h"

LOG_MODULE_REGISTER(mind_io, CONFIG_MIND_IO_LOG_LEVEL);

#define CONN DT_COMPAT_GET_ANY_STATUS_OKAY(mind_din41612_io)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(mind_din41612_io) == 1,
	     "mind_io needs exactly one enabled mind,din41612-io connector node");
BUILD_ASSERT(CONFIG_MIND_IO_INIT_PRIORITY > CONFIG_I2C_INIT_PRIORITY,
	     "the card EEPROM is read over I2C, so mind_io must start after the I2C buses");
BUILD_ASSERT(CONFIG_MIND_IO_PORT_INIT_PRIORITY > CONFIG_MIND_IO_INIT_PRIORITY,
	     "port slots need the card description read by the core");

#define HOST_DEV_OR_NULL(prop)                                                                     \
	COND_CODE_1(DT_NODE_HAS_PROP(CONN, prop), (DEVICE_DT_GET(DT_PHANDLE(CONN, prop))), (NULL))

static const struct device *const host_i2c_cnf = DEVICE_DT_GET(DT_PHANDLE(CONN, i2c_cnf));
static const struct device *const host_i2c_io = HOST_DEV_OR_NULL(i2c_io);
static const struct device *const host_spi = HOST_DEV_OR_NULL(spi);

/* Card EEPROM writes are split at this boundary; every card EEPROM must
 * have a page size that is a multiple of it.
 */
#define CARD_EEPROM_CHUNK      16U
#define CARD_EEPROM_WRITE_MS   10U

static struct mio_card card;
static enum mio_card_status status = MIO_CARD_ABSENT;
static const char *status_reason = "not read yet";
static uint8_t image[CONFIG_MIND_IO_CARD_MAX_SIZE];
static bool fault_led_wired;

struct port_slot {
	uint8_t slot;
	const struct device *dev;
};

#define PORT_SLOT_ENTRY(node) {.slot = DT_PROP(node, slot), .dev = DEVICE_DT_GET(node)},

static const struct port_slot i2c_slots[] = {
#ifdef CONFIG_MIND_IO_I2C_PORT
	DT_FOREACH_STATUS_OKAY(mind_io_i2c_port, PORT_SLOT_ENTRY)
#endif
};

static const struct port_slot spi_slots[] = {
#ifdef CONFIG_MIND_IO_SPI_PORT
	DT_FOREACH_STATUS_OKAY(mind_io_spi_port, PORT_SLOT_ENTRY)
#endif
};

const struct device *mio_host_i2c(uint8_t bus_res)
{
	if (bus_res == MIO_RES_I2C_CNF) {
		return host_i2c_cnf;
	}
	if (bus_res == MIO_RES_I2C_IO) {
		return host_i2c_io;
	}

	return NULL;
}

const struct device *mio_host_spi(void)
{
	return host_spi;
}

enum mio_card_status mio_card_status(const char **reason)
{
	if (reason != NULL) {
		*reason = (status == MIO_CARD_OK) ? NULL : status_reason;
	}

	return status;
}

const char *mio_card_status_name(enum mio_card_status st)
{
	switch (st) {
	case MIO_CARD_OK:
		return "ok";
	case MIO_CARD_ABSENT:
		return "absent";
	case MIO_CARD_BLANK:
		return "blank";
	case MIO_CARD_INVALID:
		return "invalid";
	default:
		return "unknown";
	}
}

const struct mio_card *mio_card_get(void)
{
	return (status == MIO_CARD_OK) ? &card : NULL;
}

bool mio_has_resource(enum mio_resource res)
{
	return (status == MIO_CARD_OK) && ((card.resources & MIO_RES_BIT(res)) != 0U);
}

const struct mio_i2c_port_desc *mio_card_i2c_port(uint8_t slot)
{
	if (status != MIO_CARD_OK) {
		return NULL;
	}

	for (uint8_t i = 0U; i < card.n_i2c_ports; i++) {
		if (card.i2c_ports[i].slot == slot) {
			return &card.i2c_ports[i];
		}
	}

	return NULL;
}

const struct mio_spi_port_desc *mio_card_spi_port(uint8_t slot)
{
	if (status != MIO_CARD_OK) {
		return NULL;
	}

	for (uint8_t i = 0U; i < card.n_spi_ports; i++) {
		if (card.spi_ports[i].slot == slot) {
			return &card.spi_ports[i];
		}
	}

	return NULL;
}

static const struct device *slot_device(const struct port_slot *slots, size_t n, uint8_t slot)
{
	for (size_t i = 0U; i < n; i++) {
		if ((slots[i].slot == slot) && device_is_ready(slots[i].dev)) {
			return slots[i].dev;
		}
	}

	return NULL;
}

const struct device *mio_port_by_label(const char *label)
{
	if ((status != MIO_CARD_OK) || (label == NULL)) {
		return NULL;
	}

	for (uint8_t i = 0U; i < card.n_i2c_ports; i++) {
		if (strncmp(card.i2c_ports[i].label, label, MIO_LABEL_LEN) == 0) {
			return slot_device(i2c_slots, ARRAY_SIZE(i2c_slots), card.i2c_ports[i].slot);
		}
	}

	for (uint8_t i = 0U; i < card.n_spi_ports; i++) {
		if (strncmp(card.spi_ports[i].label, label, MIO_LABEL_LEN) == 0) {
			return slot_device(spi_slots, ARRAY_SIZE(spi_slots), card.spi_ports[i].slot);
		}
	}

	return NULL;
}

/* ---- raw card EEPROM access ---- */

int mio_card_eeprom_read(off_t offset, uint8_t *buf, size_t len)
{
	uint8_t addr[2];

	if ((offset < 0) || ((offset + len) > 0x10000U)) {
		return -EINVAL;
	}

	addr[0] = (uint8_t)(offset >> 8);
	addr[1] = (uint8_t)(offset & 0xFF);

	return i2c_write_read(host_i2c_cnf, MIO_CARD_EEPROM_ADDR, addr, sizeof(addr), buf, len);
}

static int wait_write_done(void)
{
	uint8_t probe[2] = {0U, 0U};

	/* The EEPROM does not acknowledge while its internal write cycle runs */
	for (unsigned int ms = 0U; ms <= CARD_EEPROM_WRITE_MS; ms++) {
		if (i2c_write(host_i2c_cnf, probe, sizeof(probe), MIO_CARD_EEPROM_ADDR) == 0) {
			return 0;
		}
		k_msleep(1);
	}

	return -EIO;
}

int mio_card_eeprom_write(off_t offset, const uint8_t *buf, size_t len)
{
	uint8_t chunk[2U + CARD_EEPROM_CHUNK];
	int ret;

	if ((offset < 0) || ((offset + len) > 0x10000U)) {
		return -EINVAL;
	}

	while (len > 0U) {
		size_t n = MIN(len, CARD_EEPROM_CHUNK - ((size_t)offset % CARD_EEPROM_CHUNK));

		chunk[0] = (uint8_t)(offset >> 8);
		chunk[1] = (uint8_t)(offset & 0xFF);
		memcpy(&chunk[2], buf, n);

		ret = i2c_write(host_i2c_cnf, chunk, 2U + n, MIO_CARD_EEPROM_ADDR);
		if (ret != 0) {
			return -EIO;
		}

		ret = wait_write_done();
		if (ret != 0) {
			return ret;
		}

		offset += (off_t)n;
		buf += n;
		len -= n;
	}

	return 0;
}

/* ---- boot ---- */

static void load_card(void)
{
	size_t len;
	int ret;

	if (!device_is_ready(host_i2c_cnf)) {
		status = MIO_CARD_ABSENT;
		status_reason = "I2C_CNF host bus not ready";
		return;
	}

	ret = mio_card_eeprom_read(0, image, MIO_CARD_HDR_SIZE);
	if (ret != 0) {
		status = MIO_CARD_ABSENT;
		status_reason = "no answer from the card EEPROM on I2C_CNF";
		return;
	}

	len = mio_card_image_len(image);
	if (len == 0U) {
		status = MIO_CARD_BLANK;
		status_reason = "card EEPROM holds no card description";
		return;
	}
	if (len > sizeof(image)) {
		status = MIO_CARD_INVALID;
		status_reason = "card image larger than CONFIG_MIND_IO_CARD_MAX_SIZE";
		return;
	}

	ret = mio_card_eeprom_read(MIO_CARD_HDR_SIZE, &image[MIO_CARD_HDR_SIZE],
				   len - MIO_CARD_HDR_SIZE);
	if (ret != 0) {
		status = MIO_CARD_ABSENT;
		status_reason = "card EEPROM read failed";
		return;
	}

	ret = mio_card_decode(image, len, &card, &status_reason);
	if (ret == -ENODATA) {
		status = MIO_CARD_BLANK;
	} else if (ret != 0) {
		status = MIO_CARD_INVALID;
	} else {
		status = MIO_CARD_OK;
		status_reason = NULL;
	}
}

int mio_fault_led_set(bool on)
{
	if (!fault_led_wired) {
		return -ENODEV;
	}
	if ((status == MIO_CARD_OK) && !mio_has_resource(MIO_RES_FAULT_LED)) {
		return -ENOTSUP;
	}

	return mio_fault_led_write(on);
}

static int mind_io_init(void)
{
	fault_led_wired = mio_fault_led_init();

	load_card();

	if (status == MIO_CARD_OK) {
		LOG_INF("card '%s' rev %u.%u serial '%s': %u I2C / %u SPI ports, %u lines",
			card.has_identity ? card.identity.name : "?", card.identity.hw_major,
			card.identity.hw_minor, card.has_identity ? card.identity.serial : "?",
			card.n_i2c_ports, card.n_spi_ports, card.n_lines);
		mio_expanders_init(&card);
		mio_lines_init(&card);
	} else if (status == MIO_CARD_ABSENT) {
		LOG_WRN("no I/O card: %s", status_reason);
	} else {
		LOG_ERR("I/O card %s: %s", mio_card_status_name(status), status_reason);
		if (fault_led_wired) {
			(void)mio_fault_led_write(true);
		}
	}

	return 0;
}

SYS_INIT(mind_io_init, POST_KERNEL, CONFIG_MIND_IO_INIT_PRIORITY);
