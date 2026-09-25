/*
 * mind_io integration tests on native_sim: card read at boot, lines on
 * connector GPIOs and on the on-card PCA9557, fault LED, raw card EEPROM
 * access, I2C/SPI port slots and the shell commands.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#include <mind_io/mind_io.h>
#include "test_emul.h"

static const struct device *const gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *const i2c_port0 = DEVICE_DT_GET(DT_NODELABEL(mio_i2c_port0));
static const struct device *const spi_port0 = DEVICE_DT_GET(DT_NODELABEL(mio_spi_port0));

/* gpio0 pins as wired in app.overlay */
#define PIN_GPIO0     0
#define PIN_GPIO3     3
#define PIN_SPI_CS0   4
#define PIN_FAULT_LED 5

static const struct shell *sh;

static const char *run(const char *cmd, int expected_ret)
{
	size_t size;
	int ret;

	shell_backend_dummy_clear_output(sh);
	ret = shell_execute_cmd(sh, cmd);
	zassert_equal(ret, expected_ret, "'%s' returned %d", cmd, ret);

	return shell_backend_dummy_get_output(sh, &size);
}

static void *shell_setup(void)
{
	sh = shell_backend_dummy_get_ptr();
	WAIT_FOR(shell_ready(sh), 20000, k_msleep(1));
	zassert_true(shell_ready(sh), "dummy shell backend not ready");
	return NULL;
}

#ifdef CONFIG_TEST_CARD_IMAGE_GOLDEN

static const struct device *const i2c_port1 = DEVICE_DT_GET(DT_NODELABEL(mio_i2c_port1));
static const struct device *const spi_port1 = DEVICE_DT_GET(DT_NODELABEL(mio_spi_port1));

/* ---- card read at boot ---- */

ZTEST(mio_card, test_status_ok)
{
	const char *reason = "unset";
	const struct mio_card *card;

	zassert_equal(mio_card_status(&reason), MIO_CARD_OK);
	zassert_is_null(reason);

	card = mio_card_get();
	zassert_not_null(card);
	zassert_str_equal(card->identity.name, "MINDOS-IO-DEV");
	zassert_true(mio_has_resource(MIO_RES_SPI));
	zassert_true(mio_has_resource(MIO_RES_GPIO3));
	zassert_false(mio_has_resource(MIO_RES_GPIO0));
	zassert_false(mio_has_resource(MIO_RES_RS485));
	zassert_not_null(mio_card_i2c_port(0));
	zassert_is_null(mio_card_i2c_port(1));
}

ZTEST(mio_card, test_undeclared_gpio_untouched)
{
	gpio_flags_t flags = 0xFFFFFFFFU;

	zassert_ok(gpio_emul_flags_get(gpio0, PIN_GPIO0, &flags));
	zassert_equal(flags & (GPIO_INPUT | GPIO_OUTPUT), 0, "GPIO0 must stay unconfigured");
}

ZTEST(mio_card, test_fault_led)
{
	zassert_equal(gpio_emul_output_get(gpio0, PIN_FAULT_LED), 0, "off with a valid card");
	zassert_ok(mio_fault_led_set(true));
	zassert_equal(gpio_emul_output_get(gpio0, PIN_FAULT_LED), 1);
	zassert_ok(mio_fault_led_set(false));
	zassert_equal(gpio_emul_output_get(gpio0, PIN_FAULT_LED), 0);
}

/* ---- lines ---- */

ZTEST(mio_card, test_gpio_input_line)
{
	struct mio_line irq;
	gpio_flags_t flags;

	zassert_ok(mio_line_get("CAN1_INT", &irq));

	zassert_ok(gpio_emul_flags_get(gpio0, PIN_GPIO3, &flags));
	zassert_true((flags & GPIO_INPUT) != 0U);
	zassert_true((flags & GPIO_PULL_UP) != 0U);

	/* active-low: a low level reads as active */
	zassert_ok(gpio_emul_input_set(gpio0, PIN_GPIO3, 0));
	zassert_equal(mio_line_read(&irq), 1);
	zassert_ok(gpio_emul_input_set(gpio0, PIN_GPIO3, 1));
	zassert_equal(mio_line_read(&irq), 0);

	zassert_equal(mio_line_set(&irq, 1), -ENOTSUP, "inputs can't be set");
}

ZTEST(mio_card, test_expander_lines)
{
	struct mio_line relay;
	struct mio_line led;
	struct test_pca9557_regs regs = test_pca9557_regs();

	/* Polarity cleared, IO1/IO2 outputs; RELAY1 inactive (low), LED1 active
	 * and active-low (low).
	 */
	zassert_equal(regs.pol, 0x00);
	zassert_equal(regs.cfg, 0xF9);
	zassert_equal(regs.out & 0x06, 0x00);

	zassert_ok(mio_line_get("RELAY1", &relay));
	zassert_ok(mio_line_get("LED1", &led));

	zassert_ok(mio_line_set(&relay, 1));
	zassert_ok(mio_line_set(&led, 0));
	regs = test_pca9557_regs();
	zassert_equal(regs.out & 0x06, 0x06);
	zassert_equal(mio_line_read(&relay), 1);
	zassert_equal(mio_line_read(&led), 0);

	zassert_ok(mio_line_set(&relay, 0));
	zassert_equal(test_pca9557_regs().out & 0x02, 0x00);
}

ZTEST(mio_card, test_unknown_line)
{
	struct mio_line line;

	zassert_equal(mio_line_get("NOPE", &line), -ENOENT);
}

/* ---- raw card EEPROM access ---- */

ZTEST(mio_card, test_eeprom_raw_access)
{
	uint8_t data[20];
	uint8_t readback[20];

	zassert_ok(mio_card_eeprom_read(0, readback, 4));
	zassert_mem_equal(readback, MIO_CARD_MAGIC, 4);

	for (size_t i = 0; i < sizeof(data); i++) {
		data[i] = (uint8_t)(0xA0 + i);
	}

	/* crosses a 16-byte chunk boundary */
	zassert_ok(mio_card_eeprom_write(0x100A, data, sizeof(data)));
	zassert_mem_equal(&test_card_eeprom_mem()[0x100A], data, sizeof(data));
	zassert_ok(mio_card_eeprom_read(0x100A, readback, sizeof(readback)));
	zassert_mem_equal(readback, data, sizeof(data));
}

ZTEST_SUITE(mio_card, NULL, NULL, NULL, NULL, NULL);

/* ---- port slots ---- */

ZTEST(mio_ports, test_slot_readiness)
{
	zassert_true(device_is_ready(i2c_port0));
	zassert_false(device_is_ready(i2c_port1), "the card has no I2C slot 1");
	zassert_true(device_is_ready(spi_port0));
	zassert_false(device_is_ready(spi_port1), "the card has no SPI slot 1");
}

ZTEST(mio_ports, test_port_by_label)
{
	zassert_equal(mio_port_by_label("SENSOR1"), i2c_port0);
	zassert_equal(mio_port_by_label("CAN1"), spi_port0);
	zassert_is_null(mio_port_by_label("CAN1_INT"), "lines are not ports");
	zassert_is_null(mio_port_by_label("NOPE"));
}

ZTEST(mio_ports, test_i2c_forwarding)
{
	uint8_t val;

	/* The PCA9557 sits on the host I2C_IO bus; the port reaches it */
	zassert_ok(i2c_reg_read_byte(i2c_port0, 0x19, 0x03, &val));
	zassert_equal(val, test_pca9557_regs().cfg);
	zassert_ok(i2c_reg_read_byte(i2c_port0, 0x19, 0x02, &val));
	zassert_equal(val, 0x00);
}

static int spi_xfer(uint32_t freq, uint16_t extra_op, uint8_t *tx, uint8_t *rx, size_t len)
{
	struct spi_config cfg = {
		.frequency = freq,
		.operation = SPI_WORD_SET(8) | SPI_OP_MODE_CONTROLLER | extra_op,
	};
	struct spi_buf tx_buf = {.buf = tx, .len = len};
	struct spi_buf rx_buf = {.buf = rx, .len = len};
	struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
	struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

	return spi_transceive(spi_port0, &cfg, &tx_set, &rx_set);
}

ZTEST(mio_ports, test_spi_chip_select_and_forwarding)
{
	uint8_t tx[3] = {0xA5, 0x5A, 0x01};
	uint8_t rx[3] = {0};
	struct test_spi_echo_stats stats;

	/* active-low CS0 idles high */
	zassert_equal(gpio_emul_output_get(gpio0, PIN_SPI_CS0), 1);

	zassert_ok(spi_xfer(1000000, 0, tx, rx, sizeof(tx)));
	zassert_mem_equal(rx, tx, sizeof(tx));

	stats = test_spi_echo_stats();
	zassert_equal(stats.cs_level, 0, "CS0 asserted (low) during the transfer");
	zassert_equal(stats.last_freq, 1000000U);
	zassert_equal(gpio_emul_output_get(gpio0, PIN_SPI_CS0), 1, "CS0 released after");
}

ZTEST(mio_ports, test_spi_max_frequency)
{
	uint8_t tx[1] = {0};
	uint8_t rx[1];

	zassert_ok(spi_xfer(20000000, 0, tx, rx, sizeof(tx)));
	zassert_equal(test_spi_echo_stats().last_freq, 10000000U, "clamped to the card's limit");
}

ZTEST(mio_ports, test_spi_hold_on_cs)
{
	uint8_t tx[1] = {0};
	uint8_t rx[1];
	struct spi_config cfg = {.operation = SPI_WORD_SET(8) | SPI_OP_MODE_CONTROLLER};

	zassert_ok(spi_xfer(1000000, SPI_HOLD_ON_CS, tx, rx, sizeof(tx)));
	zassert_equal(gpio_emul_output_get(gpio0, PIN_SPI_CS0), 0, "CS0 held");
	zassert_ok(spi_release(spi_port0, &cfg));
	zassert_equal(gpio_emul_output_get(gpio0, PIN_SPI_CS0), 1, "CS0 released");
}

ZTEST_SUITE(mio_ports, NULL, NULL, NULL, NULL, NULL);

/* ---- shell ---- */

ZTEST(mio_shell, test_info)
{
	const char *out = run("mio info", 0);

	zassert_not_null(strstr(out, "MINDOS-IO-DEV"), "%s", out);
	zassert_not_null(strstr(out, " I2C_IO"), "%s", out);
	zassert_not_null(strstr(out, "address 0x19 on SENSOR1"), "%s", out);
}

ZTEST(mio_shell, test_ports_and_lines)
{
	const char *out = run("mio ports", 0);

	zassert_not_null(strstr(out, "SENSOR1"), "%s", out);
	zassert_not_null(strstr(out, "CAN1"), "%s", out);
	zassert_is_null(strstr(out, "NOT READY"), "%s", out);

	out = run("mio lines", 0);
	zassert_not_null(strstr(out, "RELAY1"), "%s", out);
	zassert_not_null(strstr(out, "EXP0.1"), "%s", out);
}

ZTEST(mio_shell, test_line_command)
{
	zassert_not_null(strstr(run("mio line RELAY1 1", 0), "RELAY1: active"));
	zassert_equal(test_pca9557_regs().out & 0x02, 0x02);
	zassert_not_null(strstr(run("mio line RELAY1 0", 0), "RELAY1: inactive"));
	run("mio line CAN1_INT 1", -ENOTSUP);
	run("mio line NOPE", -ENOENT);
}

ZTEST(mio_shell, test_eeprom_commands)
{
	zassert_not_null(strstr(run("mio eeprom read 0 4", 0), "DATA 4d494f43"));
	run("mio eeprom write 0x1800 00112233", 0);
	zassert_mem_equal(&test_card_eeprom_mem()[0x1800], "\x00\x11\x22\x33", 4);
	zassert_not_null(strstr(run("mio eeprom read 0x1800 4", 0), "DATA 00112233"));
	run("mio eeprom write 0x1800 0011223", -EINVAL);
	run("mio eeprom read 0 65", -EINVAL);
}

ZTEST_SUITE(mio_shell, NULL, shell_setup, NULL, NULL, NULL);

#else /* no valid card: blank or absent */

ZTEST(mio_no_card, test_status)
{
	const char *reason = NULL;
	enum mio_card_status expected =
		IS_ENABLED(CONFIG_TEST_CARD_ABSENT) ? MIO_CARD_ABSENT : MIO_CARD_BLANK;

	zassert_equal(mio_card_status(&reason), expected);
	zassert_not_null(reason);
	zassert_is_null(mio_card_get());
	zassert_false(mio_has_resource(MIO_RES_I2C_CNF));
}

ZTEST(mio_no_card, test_everything_unavailable)
{
	struct mio_line line;

	zassert_equal(mio_line_get("RELAY1", &line), -ENODEV);
	zassert_is_null(mio_port_by_label("SENSOR1"));
	zassert_false(device_is_ready(i2c_port0));
	zassert_false(device_is_ready(spi_port0));
}

ZTEST(mio_no_card, test_fault_led)
{
	/* A blank card lights its fault LED; an absent card has no LED to light */
	zassert_equal(gpio_emul_output_get(gpio0, PIN_FAULT_LED),
		      IS_ENABLED(CONFIG_TEST_CARD_ABSENT) ? 0 : 1);
}

ZTEST(mio_no_card, test_info_reports_status)
{
	const char *out = run("mio info",
			      IS_ENABLED(CONFIG_TEST_CARD_ABSENT) ? -ENODEV : -EINVAL);

	zassert_not_null(strstr(out, IS_ENABLED(CONFIG_TEST_CARD_ABSENT) ? "absent" : "blank"),
			 "%s", out);
}

ZTEST_SUITE(mio_no_card, NULL, shell_setup, NULL, NULL, NULL);

#endif /* CONFIG_TEST_CARD_IMAGE_GOLDEN */
