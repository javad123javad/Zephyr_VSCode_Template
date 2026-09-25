/*
 * Tests of the portable card format library against the golden image that
 * scripts/mio_card.py builds from tests/golden/dev_card.yaml.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>
#include <mind_io/card_format.h>

static const uint8_t golden[] = {
#include "dev_card.bin.inc"
};

static struct mio_card card;
static uint8_t buf[512];

static void reseal(uint8_t *image, size_t len_without_crc)
{
	uint32_t crc;
	size_t total = len_without_crc + MIO_CARD_CRC_SIZE;

	image[6] = (uint8_t)(total & 0xFFU);
	image[7] = (uint8_t)(total >> 8);
	crc = mio_crc32(image, len_without_crc);
	for (int i = 0; i < 4; i++) {
		image[len_without_crc + i] = (uint8_t)(crc >> (8 * i));
	}
}

static void decode_golden(void)
{
	const char *reason = NULL;

	zassert_ok(mio_card_decode(golden, sizeof(golden), &card, &reason), "%s", reason);
}

ZTEST(card_format, test_golden_fields)
{
	decode_golden();

	zassert_true(card.has_identity);
	zassert_str_equal(card.identity.name, "MINDOS-IO-DEV");
	zassert_str_equal(card.identity.serial, "DEV0001");
	zassert_equal(card.identity.hw_major, 1);
	zassert_equal(card.identity.hw_minor, 0);
	zassert_equal(card.identity.date, 20260925U);

	zassert_equal(card.resources,
		      MIO_RES_BIT(MIO_RES_I2C_CNF) | MIO_RES_BIT(MIO_RES_I2C_IO) |
			      MIO_RES_BIT(MIO_RES_SPI) | MIO_RES_BIT(MIO_RES_SPI_CS0) |
			      MIO_RES_BIT(MIO_RES_I2S) | MIO_RES_BIT(MIO_RES_USB1) |
			      MIO_RES_BIT(MIO_RES_USB2) | MIO_RES_BIT(MIO_RES_FAULT_LED) |
			      MIO_RES_BIT(MIO_RES_GPIO3));

	zassert_equal(card.n_devices, 1);
	zassert_equal(card.devices[0].type, MIO_DEV_PCA9557);
	zassert_equal(card.devices[0].bus, MIO_RES_I2C_IO);
	zassert_equal(card.devices[0].addr, 0x19);

	zassert_equal(card.n_i2c_ports, 1);
	zassert_equal(card.i2c_ports[0].slot, 0);
	zassert_equal(card.i2c_ports[0].speed, MIO_I2C_SPEED_STANDARD);
	zassert_str_equal(card.i2c_ports[0].label, "SENSOR1");

	zassert_equal(card.n_spi_ports, 1);
	zassert_equal(card.spi_ports[0].cs.source, MIO_PIN_SPI_CS0);
	zassert_equal(card.spi_ports[0].cs_flags, 0);
	zassert_equal(card.spi_ports[0].max_freq, 10000000U);
	zassert_str_equal(card.spi_ports[0].label, "CAN1");

	zassert_equal(card.n_lines, 3);
	zassert_equal(card.lines[0].pin.source, MIO_PIN_GPIO);
	zassert_equal(card.lines[0].pin.pin, 3);
	zassert_equal(card.lines[0].flags, MIO_LINE_ACTIVE_LOW | MIO_LINE_PULL_UP);
	zassert_equal(card.lines[0].role, MIO_ROLE_IRQ);
	zassert_equal(card.lines[0].port, MIO_PORT_REF(MIO_PORT_KIND_SPI, 0));
	zassert_str_equal(card.lines[0].label, "CAN1_INT");
	zassert_equal(card.lines[1].pin.source, MIO_PIN_EXPANDER);
	zassert_equal(card.lines[1].pin.pin, 1);
	zassert_equal(card.lines[1].flags, MIO_LINE_OUTPUT);
	zassert_equal(card.lines[2].flags,
		      MIO_LINE_OUTPUT | MIO_LINE_ACTIVE_LOW | MIO_LINE_INIT_ACTIVE);
}

ZTEST(card_format, test_encode_matches_golden)
{
	const char *reason = NULL;
	int len;

	decode_golden();
	len = mio_card_encode(&card, buf, sizeof(buf), &reason);
	zassert_equal(len, sizeof(golden), "%d (%s)", len, reason);
	zassert_mem_equal(buf, golden, sizeof(golden));
}

ZTEST(card_format, test_image_len_from_header)
{
	uint8_t blank[MIO_CARD_HDR_SIZE];

	zassert_equal(mio_card_image_len(golden), sizeof(golden));
	memset(blank, 0xFF, sizeof(blank));
	zassert_equal(mio_card_image_len(blank), 0);
}

ZTEST(card_format, test_encode_buffer_too_small)
{
	decode_golden();
	zassert_equal(mio_card_encode(&card, buf, sizeof(golden) - 1, NULL), -ENOSPC);
}

ZTEST(card_format, test_blank_eeprom)
{
	memset(buf, 0xFF, 64);
	zassert_equal(mio_card_decode(buf, 64, &card, NULL), -ENODATA);
}

ZTEST(card_format, test_crc_mismatch)
{
	memcpy(buf, golden, sizeof(golden));
	buf[20] ^= 0x01;
	zassert_equal(mio_card_decode(buf, sizeof(golden), &card, NULL), -EBADMSG);
}

ZTEST(card_format, test_truncated)
{
	zassert_equal(mio_card_decode(golden, 100, &card, NULL), -EBADMSG);
}

ZTEST(card_format, test_wrong_version)
{
	memcpy(buf, golden, sizeof(golden));
	buf[4] = 2;
	zassert_equal(mio_card_decode(buf, sizeof(golden), &card, NULL), -ENOTSUP);
}

ZTEST(card_format, test_unknown_record_skipped)
{
	const uint8_t vendor[] = {0xE0, 3, 1, 2, 3};
	size_t body = sizeof(golden) - MIO_CARD_CRC_SIZE;
	const char *reason = NULL;

	memcpy(buf, golden, body);
	memcpy(&buf[body], vendor, sizeof(vendor));
	reseal(buf, body + sizeof(vendor));

	zassert_ok(mio_card_decode(buf, body + sizeof(vendor) + MIO_CARD_CRC_SIZE, &card,
				   &reason), "%s", reason);
	zassert_equal(card.n_lines, 3);
}

ZTEST(card_format, test_record_past_end_rejected)
{
	size_t body = sizeof(golden) - MIO_CARD_CRC_SIZE;

	memcpy(buf, golden, body);
	buf[body] = 0xE0;
	buf[body + 1] = 200; /* claims more payload than the image holds */
	reseal(buf, body + 2);
	zassert_equal(mio_card_decode(buf, body + 2 + MIO_CARD_CRC_SIZE, &card, NULL), -EINVAL);
}

/* Validation: start from the golden card, break one rule, expect -EINVAL */

static void expect_invalid(const char *what)
{
	const char *reason = NULL;

	zassert_equal(mio_card_encode(&card, buf, sizeof(buf), &reason), -EINVAL, "%s", what);
	zassert_not_null(reason);
}

ZTEST(card_format, test_line_on_chip_select_rejected)
{
	decode_golden();
	card.lines[0].pin = card.spi_ports[0].cs;
	expect_invalid("line on SPI_CS0");

	decode_golden();
	card.resources |= MIO_RES_BIT(MIO_RES_GPIO1);
	card.spi_ports[0].cs = (struct mio_pin_ref){.source = MIO_PIN_GPIO, .pin = 1};
	card.lines[0].pin = card.spi_ports[0].cs;
	expect_invalid("line on a GPIO chip-select");
}

ZTEST(card_format, test_duplicate_label_rejected)
{
	decode_golden();
	strcpy(card.lines[1].label, "SENSOR1");
	expect_invalid("duplicate label");
}

ZTEST(card_format, test_label_too_long_rejected)
{
	decode_golden();
	memset(card.i2c_ports[0].label, 'A', MIO_LABEL_LEN);
	expect_invalid("label without terminator");
}

ZTEST(card_format, test_nonexistent_gpio_rejected)
{
	decode_golden();
	card.lines[0].pin.pin = MIO_NUM_GPIO;
	expect_invalid("GPIO out of range");
}

ZTEST(card_format, test_undeclared_gpio_rejected)
{
	decode_golden();
	card.resources &= ~MIO_RES_BIT(MIO_RES_GPIO3);
	expect_invalid("GPIO3 not in RESOURCES");
}

ZTEST(card_format, test_expander_without_device_rejected)
{
	decode_golden();
	card.n_devices = 0;
	expect_invalid("expander line without device");
}

ZTEST(card_format, test_port_reference_must_exist)
{
	decode_golden();
	card.lines[0].port = MIO_PORT_REF(MIO_PORT_KIND_SPI, 3);
	expect_invalid("reference to a missing port");
}

ZTEST(card_format, test_device_on_eeprom_address_rejected)
{
	decode_golden();
	card.devices[0].bus = MIO_RES_I2C_CNF;
	card.devices[0].addr = MIO_CARD_EEPROM_ADDR;
	expect_invalid("device on the card EEPROM address");
}

ZTEST(card_format, test_crc32_reference)
{
	/* Standard check value of CRC-32/ISO-HDLC (zlib) */
	zassert_equal(mio_crc32((const uint8_t *)"123456789", 9), 0xCBF43926U);
}

ZTEST_SUITE(card_format, NULL, NULL, NULL, NULL, NULL);
