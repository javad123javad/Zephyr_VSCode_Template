/*
 * Driver for the PCA9557 8-bit I2C GPIO expander.
 *
 * This is a distinct driver from Zephyr's in-tree "nxp,pca95xx": that
 * one targets the 16-bit PCA9535/39/55 family (paired port0/port1
 * registers at offsets 0x00-0x07). Pointing it at a PCA9557 makes its
 * boot-time init write to offset 0x06 (out of range on this part),
 * which NACKs and leaves the device stuck "not ready". The PCA9557
 * is an 8-bit, single-port part with a much shorter register map:
 *
 *   0x00  Input Port          (read-only, actual pin state)
 *   0x01  Output Port         (read/write, driven level)
 *   0x02  Polarity Inversion  (read/write, not used by this driver)
 *   0x03  Configuration       (read/write, 0 = output, 1 = input)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_pca9557

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#include <zephyr/drivers/gpio/gpio_utils.h>

#define LOG_LEVEL CONFIG_GPIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gpio_pca9557);

#define REG_INPUT_PORT  0x00
#define REG_OUTPUT_PORT 0x01
#define REG_POL_INV     0x02
#define REG_CONFIG      0x03

#define PCA9557_NPINS 8

/** Configuration data */
struct gpio_pca9557_config {
	/* gpio_driver_config needs to be first */
	struct gpio_driver_config common;
	struct i2c_dt_spec bus;
#ifdef CONFIG_GPIO_PCA9557_INTERRUPT
	struct gpio_dt_spec int_gpio;
#endif
};

/** Runtime driver data */
struct gpio_pca9557_data {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common;

	struct {
		uint8_t input;
		uint8_t output;
		uint8_t dir;
	} reg_cache;

	struct k_sem lock;

#ifdef CONFIG_GPIO_PCA9557_INTERRUPT
	/* Self-reference for interrupt handling */
	const struct device *instance;

	sys_slist_t callbacks;

	struct {
		uint8_t edge_rising;
		uint8_t edge_falling;
		uint8_t level_high;
		uint8_t level_low;
	} interrupts;

	struct gpio_callback gpio_callback;
	struct k_work interrupt_worker;
	bool interrupt_active;
#endif
};

static int pca9557_read_reg(const struct device *dev, uint8_t reg, uint8_t *value)
{
	const struct gpio_pca9557_config *config = dev->config;
	int ret;

	ret = i2c_reg_read_byte_dt(&config->bus, reg, value);
	if (ret != 0) {
		LOG_ERR("PCA9557[0x%02X]: error reading register 0x%02X (%d)",
			config->bus.addr, reg, ret);
	}

	return ret;
}

static int pca9557_write_reg(const struct device *dev, uint8_t reg, uint8_t value,
			      uint8_t *cache)
{
	const struct gpio_pca9557_config *config = dev->config;
	int ret;

	ret = i2c_reg_write_byte_dt(&config->bus, reg, value);
	if (ret != 0) {
		LOG_ERR("PCA9557[0x%02X]: error writing register 0x%02X (%d)",
			config->bus.addr, reg, ret);
		return ret;
	}

	if (cache != NULL) {
		*cache = value;
	}

	return 0;
}

/**
 * @brief Configure pin direction (and, for outputs, initial level).
 */
static int setup_pin_dir(const struct device *dev, uint32_t pin, int flags)
{
	struct gpio_pca9557_data *data = dev->data;
	uint8_t reg_dir = data->reg_cache.dir;
	uint8_t reg_out = data->reg_cache.output;
	int ret;

	/* For each pin, 0 == output, 1 == input */
	if ((flags & GPIO_OUTPUT) != 0U) {
		if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
			reg_out |= BIT(pin);
		} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
			reg_out &= ~BIT(pin);
		}

		ret = pca9557_write_reg(dev, REG_OUTPUT_PORT, reg_out, &data->reg_cache.output);
		if (ret != 0) {
			return ret;
		}
		reg_dir &= ~BIT(pin);
	} else {
		reg_dir |= BIT(pin);
	}

	return pca9557_write_reg(dev, REG_CONFIG, reg_dir, &data->reg_cache.dir);
}

static int gpio_pca9557_config(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	struct gpio_pca9557_data *data = dev->data;
	int ret;

	/* Does not support disconnected pin */
	if ((flags & (GPIO_INPUT | GPIO_OUTPUT)) == GPIO_DISCONNECTED) {
		return -ENOTSUP;
	}

	/* Fixed push-pull output stage; no open-drain option */
	if ((flags & GPIO_SINGLE_ENDED) != 0U) {
		return -ENOTSUP;
	}

	/* No pull-up/pull-down resistors on this part */
	if ((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) != 0U) {
		return -ENOTSUP;
	}

	/* Can't do I2C bus operations from an ISR */
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	k_sem_take(&data->lock, K_FOREVER);
	ret = setup_pin_dir(dev, pin, flags);
	k_sem_give(&data->lock);

	return ret;
}

static int gpio_pca9557_port_get_raw(const struct device *dev, uint32_t *value)
{
	struct gpio_pca9557_data *data = dev->data;
	uint8_t buf;
	int ret;

	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	k_sem_take(&data->lock, K_FOREVER);
	ret = pca9557_read_reg(dev, REG_INPUT_PORT, &buf);
	if (ret == 0) {
		data->reg_cache.input = buf;
		*value = buf;
	}
	k_sem_give(&data->lock);

	return ret;
}

static int gpio_pca9557_port_set_masked_raw(const struct device *dev, uint32_t mask,
					     uint32_t value)
{
	struct gpio_pca9557_data *data = dev->data;
	uint8_t reg_out;
	int ret;

	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	k_sem_take(&data->lock, K_FOREVER);
	reg_out = data->reg_cache.output;
	reg_out = (reg_out & ~mask) | (mask & value);
	ret = pca9557_write_reg(dev, REG_OUTPUT_PORT, reg_out, &data->reg_cache.output);
	k_sem_give(&data->lock);

	return ret;
}

static int gpio_pca9557_port_set_bits_raw(const struct device *dev, uint32_t mask)
{
	return gpio_pca9557_port_set_masked_raw(dev, mask, mask);
}

static int gpio_pca9557_port_clear_bits_raw(const struct device *dev, uint32_t mask)
{
	return gpio_pca9557_port_set_masked_raw(dev, mask, 0);
}

static int gpio_pca9557_port_toggle_bits(const struct device *dev, uint32_t mask)
{
	struct gpio_pca9557_data *data = dev->data;
	uint8_t reg_out;
	int ret;

	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	k_sem_take(&data->lock, K_FOREVER);
	reg_out = data->reg_cache.output ^ mask;
	ret = pca9557_write_reg(dev, REG_OUTPUT_PORT, reg_out, &data->reg_cache.output);
	k_sem_give(&data->lock);

	return ret;
}

#ifdef CONFIG_GPIO_PCA9557_INTERRUPT
static void get_triggered_it(struct gpio_pca9557_data *data, uint8_t *trig_edge,
			      uint8_t *trig_level)
{
	uint8_t input_cache = data->reg_cache.input;
	uint8_t input_new, changed_pins;
	int ret;

	ret = pca9557_read_reg(data->instance, REG_INPUT_PORT, &input_new);
	if (ret != 0) {
		return;
	}
	data->reg_cache.input = input_new;

	changed_pins = input_cache ^ input_new;

	*trig_edge |= changed_pins & input_new & data->interrupts.edge_rising;
	*trig_edge |= changed_pins & input_cache & data->interrupts.edge_falling;
	*trig_level |= input_new & data->interrupts.level_high;
	*trig_level |= (uint8_t)~input_new & data->interrupts.level_low;
}

static void gpio_pca9557_interrupt_worker(struct k_work *work)
{
	struct gpio_pca9557_data *data =
		CONTAINER_OF(work, struct gpio_pca9557_data, interrupt_worker);
	uint8_t trig_edge = 0, trig_level = 0;

	k_sem_take(&data->lock, K_FOREVER);
	get_triggered_it(data, &trig_edge, &trig_level);
	k_sem_give(&data->lock);

	if ((trig_edge | trig_level) != 0) {
		gpio_fire_callbacks(&data->callbacks, data->instance, trig_edge | trig_level);
	}

	/* Emulate level triggering: keep firing while the level condition holds */
	if (trig_level != 0) {
		k_work_submit(&data->interrupt_worker);
	}
}

static void gpio_pca9557_interrupt_callback(const struct device *dev, struct gpio_callback *cb,
					     gpio_port_pins_t pins)
{
	struct gpio_pca9557_data *data = CONTAINER_OF(cb, struct gpio_pca9557_data, gpio_callback);

	ARG_UNUSED(dev);
	ARG_UNUSED(pins);

	/* Cannot do I2C bus operations from an ISR; hand off to a worker */
	k_work_submit(&data->interrupt_worker);
}

static int gpio_pca9557_pin_interrupt_configure(const struct device *dev, gpio_pin_t pin,
						 enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	const struct gpio_pca9557_config *config = dev->config;
	struct gpio_pca9557_data *data = dev->data;
	bool enabled, edge, level, active;
	int ret = 0;

	if (BIT(pin) > config->common.port_pin_mask) {
		return -EINVAL;
	}

	if ((mode != GPIO_INT_MODE_DISABLED) &&
	    (BIT(pin) & data->reg_cache.dir) != BIT(pin)) {
		LOG_ERR("PCA9557[0x%02X]: output pin cannot trigger interrupt",
			config->bus.addr);
		return -ENOTSUP;
	}

	k_sem_take(&data->lock, K_FOREVER);

	enabled = (mode & GPIO_INT_MODE_DISABLED) == 0U;
	edge = mode == GPIO_INT_MODE_EDGE;
	level = mode == GPIO_INT_MODE_LEVEL;
	WRITE_BIT(data->interrupts.edge_rising, pin,
		  enabled && edge && ((trig & GPIO_INT_TRIG_HIGH) == GPIO_INT_TRIG_HIGH));
	WRITE_BIT(data->interrupts.edge_falling, pin,
		  enabled && edge && ((trig & GPIO_INT_TRIG_LOW) == GPIO_INT_TRIG_LOW));
	WRITE_BIT(data->interrupts.level_high, pin,
		  enabled && level && ((trig & GPIO_INT_TRIG_HIGH) == GPIO_INT_TRIG_HIGH));
	WRITE_BIT(data->interrupts.level_low, pin,
		  enabled && level && ((trig & GPIO_INT_TRIG_LOW) == GPIO_INT_TRIG_LOW));

	active = (data->interrupts.edge_rising || data->interrupts.edge_falling ||
		  data->interrupts.level_high || data->interrupts.level_low);

	if (active != data->interrupt_active) {
		ret = gpio_pin_interrupt_configure_dt(
			&config->int_gpio,
			active ? GPIO_INT_EDGE_TO_ACTIVE : GPIO_INT_MODE_DISABLED);
		if (ret != 0) {
			LOG_ERR("PCA9557[0x%02X]: failed to configure interrupt pin %d (%d)",
				config->bus.addr, config->int_gpio.pin, ret);
			goto done;
		}
		data->interrupt_active = active;

		if (active) {
			/* Reset any already-pending signal on INT */
			uint8_t dummy;

			pca9557_read_reg(dev, REG_INPUT_PORT, &dummy);
			data->reg_cache.input = dummy;
		}
	}

done:
	k_sem_give(&data->lock);
	return ret;
}

static int gpio_pca9557_manage_callback(const struct device *dev, struct gpio_callback *callback,
					 bool set)
{
	struct gpio_pca9557_data *data = dev->data;

	k_sem_take(&data->lock, K_FOREVER);
	gpio_manage_callback(&data->callbacks, callback, set);
	k_sem_give(&data->lock);

	return 0;
}
#endif /* CONFIG_GPIO_PCA9557_INTERRUPT */

static DEVICE_API(gpio, gpio_pca9557_api) = {
	.pin_configure = gpio_pca9557_config,
	.port_get_raw = gpio_pca9557_port_get_raw,
	.port_set_masked_raw = gpio_pca9557_port_set_masked_raw,
	.port_set_bits_raw = gpio_pca9557_port_set_bits_raw,
	.port_clear_bits_raw = gpio_pca9557_port_clear_bits_raw,
	.port_toggle_bits = gpio_pca9557_port_toggle_bits,
#ifdef CONFIG_GPIO_PCA9557_INTERRUPT
	.pin_interrupt_configure = gpio_pca9557_pin_interrupt_configure,
	.manage_callback = gpio_pca9557_manage_callback,
#endif
};

static int gpio_pca9557_init(const struct device *dev)
{
	const struct gpio_pca9557_config *config = dev->config;
	struct gpio_pca9557_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->bus.bus)) {
		return -ENODEV;
	}

	k_sem_init(&data->lock, 1, 1);

	/* All pins as inputs by default (chip's own power-on-reset state) */
	ret = pca9557_write_reg(dev, REG_CONFIG, 0xFF, &data->reg_cache.dir);
	if (ret != 0) {
		return ret;
	}

#ifdef CONFIG_GPIO_PCA9557_INTERRUPT
	data->instance = dev;
	k_work_init(&data->interrupt_worker, gpio_pca9557_interrupt_worker);

	if (config->int_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->int_gpio)) {
			LOG_ERR("PCA9557[0x%02X]: interrupt GPIO not ready", config->bus.addr);
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
		if (ret != 0) {
			LOG_ERR("PCA9557[0x%02X]: failed to configure interrupt pin %d (%d)",
				config->bus.addr, config->int_gpio.pin, ret);
			return ret;
		}

		gpio_init_callback(&data->gpio_callback, gpio_pca9557_interrupt_callback,
				    BIT(config->int_gpio.pin));
		ret = gpio_add_callback(config->int_gpio.port, &data->gpio_callback);
		if (ret != 0) {
			LOG_ERR("PCA9557[0x%02X]: failed to add interrupt callback (%d)",
				config->bus.addr, ret);
			return ret;
		}
	}
#endif

	return 0;
}

#define GPIO_PCA9557_INIT(inst)						\
	static const struct gpio_pca9557_config gpio_pca9557_##inst##_cfg = {	\
		.common = GPIO_COMMON_CONFIG_FROM_DT_INST(inst),		\
		.bus = I2C_DT_SPEC_INST_GET(inst),				\
		IF_ENABLED(CONFIG_GPIO_PCA9557_INTERRUPT,			\
		(.int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, interrupt_gpios, {}),)) \
	};									\
										\
	static struct gpio_pca9557_data gpio_pca9557_##inst##_data = {	\
		.reg_cache.input = 0x00,					\
		.reg_cache.output = 0xFF,					\
		.reg_cache.dir = 0xFF,						\
	};									\
										\
	DEVICE_DT_INST_DEFINE(inst, gpio_pca9557_init, NULL,			\
			       &gpio_pca9557_##inst##_data,			\
			       &gpio_pca9557_##inst##_cfg,			\
			       POST_KERNEL, CONFIG_GPIO_PCA9557_INIT_PRIORITY,	\
			       &gpio_pca9557_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_PCA9557_INIT)
