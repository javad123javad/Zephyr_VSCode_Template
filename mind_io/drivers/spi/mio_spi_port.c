/*
 * SPI port slot of a mind I/O card: an SPI controller that drives the
 * chip-select the card assigns to the slot (IO_SPI_CS0, a connector GPIO or
 * an on-card expander pin) and forwards transfers to the connector SPI bus
 * without a chip-select of their own. When the card has no SPI port in this
 * slot, init fails, so the slot and the devices attached under it are not
 * ready.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mind_io_spi_port

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <mind_io/mind_io.h>
#include "mio_internal.h"

LOG_MODULE_DECLARE(mind_io, CONFIG_MIND_IO_LOG_LEVEL);

BUILD_ASSERT(!IS_ENABLED(CONFIG_SPI_RTIO), "mind_io SPI port slots do not support SPI RTIO");

struct mio_spi_port_config {
	uint8_t slot;
};

struct mio_spi_port_data {
	const struct device *bus;
	const struct mio_spi_port_desc *desc;
	/* Stable per-port config handed to the host bus: SPI controllers compare
	 * the config and lock owner by pointer.
	 */
	struct spi_config bus_cfg;
	struct k_mutex lock;
	bool cs_asserted;
};

static int cs_set(struct mio_spi_port_data *data, bool asserted)
{
	int ret = mio_pin_set(&data->desc->cs, asserted ? 1 : 0);

	if (ret == 0) {
		data->cs_asserted = asserted;
	}

	return ret;
}

static int mio_spi_port_transceive(const struct device *dev, const struct spi_config *config,
				   const struct spi_buf_set *tx_bufs,
				   const struct spi_buf_set *rx_bufs)
{
	struct mio_spi_port_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	data->bus_cfg = *config;
	data->bus_cfg.cs = (struct spi_cs_control){0};
	if ((data->desc->max_freq != 0U) && (data->bus_cfg.frequency > data->desc->max_freq)) {
		data->bus_cfg.frequency = data->desc->max_freq;
	}

	ret = data->cs_asserted ? 0 : cs_set(data, true);
	if (ret == 0) {
		ret = spi_transceive(data->bus, &data->bus_cfg, tx_bufs, rx_bufs);
		if ((ret != 0) || ((config->operation & SPI_HOLD_ON_CS) == 0U)) {
			(void)cs_set(data, false);
		}
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static int mio_spi_port_release(const struct device *dev, const struct spi_config *config)
{
	struct mio_spi_port_data *data = dev->data;
	int ret;

	ARG_UNUSED(config);

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->cs_asserted) {
		(void)cs_set(data, false);
	}
	ret = spi_release(data->bus, &data->bus_cfg);
	k_mutex_unlock(&data->lock);

	return ret;
}

static DEVICE_API(spi, mio_spi_port_api) = {
	.transceive = mio_spi_port_transceive,
	.release = mio_spi_port_release,
};

static int mio_spi_port_init(const struct device *dev)
{
	const struct mio_spi_port_config *config = dev->config;
	struct mio_spi_port_data *data = dev->data;
	gpio_flags_t cs_flags = GPIO_OUTPUT_INACTIVE;
	int ret;

	data->desc = mio_card_spi_port(config->slot);
	if (data->desc == NULL) {
		LOG_DBG("SPI slot %u: not provided by the card", config->slot);
		return -ENODEV;
	}

	data->bus = mio_host_spi();
	if ((data->bus == NULL) || !device_is_ready(data->bus)) {
		LOG_ERR("SPI port %s: host SPI bus not available", data->desc->label);
		return -ENODEV;
	}

	k_mutex_init(&data->lock);

	if ((data->desc->cs_flags & MIO_CS_ACTIVE_HIGH) == 0U) {
		cs_flags |= GPIO_ACTIVE_LOW;
	}

	ret = mio_pin_configure(&data->desc->cs, cs_flags);
	if (ret != 0) {
		LOG_ERR("SPI port %s: chip-select not usable (%d)", data->desc->label, ret);
		return -ENODEV;
	}

	return 0;
}

#define MIO_SPI_PORT_DEFINE(inst)                                                                  \
	static const struct mio_spi_port_config mio_spi_port_config_##inst = {                     \
		.slot = DT_INST_PROP(inst, slot),                                                  \
	};                                                                                         \
	static struct mio_spi_port_data mio_spi_port_data_##inst;                                  \
	SPI_DEVICE_DT_INST_DEFINE(inst, mio_spi_port_init, NULL, &mio_spi_port_data_##inst,        \
				  &mio_spi_port_config_##inst, POST_KERNEL,                        \
				  CONFIG_MIND_IO_PORT_INIT_PRIORITY, &mio_spi_port_api);

DT_INST_FOREACH_STATUS_OKAY(MIO_SPI_PORT_DEFINE)
