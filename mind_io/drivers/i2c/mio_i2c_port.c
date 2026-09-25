/*
 * I2C port slot of a mind I/O card: an I2C controller that forwards every
 * transfer to the host bus the card assigns to the slot. When the card has
 * no I2C port in this slot, init fails, so the slot and the devices attached
 * under it are not ready.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mind_io_i2c_port

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

#include <mind_io/mind_io.h>
#include "mio_internal.h"

LOG_MODULE_DECLARE(mind_io, CONFIG_MIND_IO_LOG_LEVEL);

struct mio_i2c_port_config {
	uint8_t slot;
};

struct mio_i2c_port_data {
	const struct device *bus;
};

static int mio_i2c_port_configure(const struct device *dev, uint32_t dev_config)
{
	const struct mio_i2c_port_data *data = dev->data;

	return i2c_configure(data->bus, dev_config);
}

static int mio_i2c_port_get_config(const struct device *dev, uint32_t *dev_config)
{
	const struct mio_i2c_port_data *data = dev->data;

	return i2c_get_config(data->bus, dev_config);
}

static int mio_i2c_port_transfer(const struct device *dev, struct i2c_msg *msgs, uint8_t num_msgs,
				 uint16_t addr)
{
	const struct mio_i2c_port_data *data = dev->data;

	return i2c_transfer(data->bus, msgs, num_msgs, addr);
}

static DEVICE_API(i2c, mio_i2c_port_api) = {
	.configure = mio_i2c_port_configure,
	.get_config = mio_i2c_port_get_config,
	.transfer = mio_i2c_port_transfer,
#ifdef CONFIG_I2C_RTIO
	.iodev_submit = i2c_iodev_submit_fallback,
#endif
};

/* Slows the shared host bus down if the port can't run at its current speed */
static void apply_speed_limit(const struct device *bus, const struct mio_i2c_port_desc *desc)
{
	uint32_t cfg;

	if ((desc->speed == MIO_I2C_SPEED_ANY) || (i2c_get_config(bus, &cfg) != 0)) {
		return;
	}

	/* MIO_I2C_SPEED_* use the same values as I2C_SPEED_* */
	if (I2C_SPEED_GET(cfg) > desc->speed) {
		cfg = (cfg & ~I2C_SPEED_MASK) | I2C_SPEED_SET(desc->speed);
		if (i2c_configure(bus, cfg) != 0) {
			LOG_WRN("I2C port %s: could not lower the bus speed", desc->label);
		}
	}
}

static int mio_i2c_port_init(const struct device *dev)
{
	const struct mio_i2c_port_config *config = dev->config;
	struct mio_i2c_port_data *data = dev->data;
	const struct mio_i2c_port_desc *desc = mio_card_i2c_port(config->slot);

	if (desc == NULL) {
		LOG_DBG("I2C slot %u: not provided by the card", config->slot);
		return -ENODEV;
	}

	data->bus = mio_host_i2c(desc->bus);
	if ((data->bus == NULL) || !device_is_ready(data->bus)) {
		LOG_ERR("I2C port %s: host bus not available", desc->label);
		return -ENODEV;
	}

	apply_speed_limit(data->bus, desc);

	return 0;
}

#define MIO_I2C_PORT_DEFINE(inst)                                                                  \
	static const struct mio_i2c_port_config mio_i2c_port_config_##inst = {                     \
		.slot = DT_INST_PROP(inst, slot),                                                  \
	};                                                                                         \
	static struct mio_i2c_port_data mio_i2c_port_data_##inst;                                  \
	I2C_DEVICE_DT_INST_DEFINE(inst, mio_i2c_port_init, NULL, &mio_i2c_port_data_##inst,        \
				  &mio_i2c_port_config_##inst, POST_KERNEL,                        \
				  CONFIG_MIND_IO_PORT_INIT_PRIORITY, &mio_i2c_port_api);

DT_INST_FOREACH_STATUS_OKAY(MIO_I2C_PORT_DEFINE)
