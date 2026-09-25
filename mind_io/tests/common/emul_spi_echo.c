/*
 * Test-only SPI emulator: echoes the transmitted bytes and records the bus
 * frequency and the physical IO_SPI_CS0 level seen during each transfer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mind_test_spi_echo

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/spi_emul.h>

#include "test_emul.h"

#define CONN DT_COMPAT_GET_ANY_STATUS_OKAY(mind_din41612_io)

static const struct gpio_dt_spec cs0 = GPIO_DT_SPEC_GET(CONN, spi_cs0_gpios);
static struct test_spi_echo_stats stats;

struct test_spi_echo_stats test_spi_echo_stats(void)
{
	return stats;
}

static int spi_echo_io(const struct emul *target, const struct spi_config *config,
		       const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs)
{
	ARG_UNUSED(target);

	stats.transfers++;
	stats.last_freq = config->frequency;
	stats.cs_level = gpio_emul_output_get(cs0.port, cs0.pin);

	if ((tx_bufs != NULL) && (rx_bufs != NULL) && (tx_bufs->count > 0U) &&
	    (rx_bufs->count > 0U)) {
		const struct spi_buf *tx = &tx_bufs->buffers[0];
		const struct spi_buf *rx = &rx_bufs->buffers[0];

		if ((tx->buf != NULL) && (rx->buf != NULL)) {
			memcpy(rx->buf, tx->buf, MIN(tx->len, rx->len));
		}
	}

	return 0;
}

static const struct spi_emul_api spi_echo_api = {
	.io = spi_echo_io,
};

static int spi_echo_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(target);
	ARG_UNUSED(parent);
	return 0;
}

static int spi_echo_dev_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

#define SPI_ECHO_EMUL(inst)                                                                        \
	DEVICE_DT_INST_DEFINE(inst, spi_echo_dev_init, NULL, NULL, NULL, POST_KERNEL,             \
			      CONFIG_SPI_INIT_PRIORITY, NULL);                                     \
	EMUL_DT_INST_DEFINE(inst, spi_echo_emul_init, NULL, NULL, &spi_echo_api, NULL);

DT_INST_FOREACH_STATUS_OKAY(SPI_ECHO_EMUL)
