/*
 * mind_io pin backends: connector signals on host GPIOs (IO_GPIO0..3,
 * IO_SPI_CS0, IO_F/A) and pins of on-card PCA9557 expanders.
 *
 * The PCA9557 is driven through raw register access because its bus and
 * address come from the card at runtime. Two properties of the part matter:
 * its Polarity Inversion register resets to 0xf0 (inverting IO4-IO7 in the
 * Input Port register), so it is cleared at init; and IO0 is an open-drain
 * output, which needs an external pull-up to read or drive high.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <mind_io/mind_io.h>
#include "mio_internal.h"

LOG_MODULE_DECLARE(mind_io, CONFIG_MIND_IO_LOG_LEVEL);

#define CONN DT_COMPAT_GET_ANY_STATUS_OKAY(mind_din41612_io)

BUILD_ASSERT(DT_PROP_LEN_OR(CONN, io_gpios, 0) <= MIO_NUM_GPIO,
	     "io-gpios lists more GPIOs than the connector has");

#define IO_GPIO_SPEC(node, prop, idx) GPIO_DT_SPEC_GET_BY_IDX(node, prop, idx)

static const struct gpio_dt_spec io_gpios[MIO_NUM_GPIO] = {
	COND_CODE_1(DT_NODE_HAS_PROP(CONN, io_gpios),
		    (DT_FOREACH_PROP_ELEM_SEP(CONN, io_gpios, IO_GPIO_SPEC, (,))), ())
};

static const struct gpio_dt_spec spi_cs0 = GPIO_DT_SPEC_GET_OR(CONN, spi_cs0_gpios, {0});
static const struct gpio_dt_spec fault_led = GPIO_DT_SPEC_GET_OR(CONN, fault_led_gpios, {0});

/* ---- host GPIOs ---- */

static const struct gpio_dt_spec *host_spec(const struct mio_pin_ref *ref)
{
	const struct gpio_dt_spec *spec = NULL;

	if (ref->source == MIO_PIN_SPI_CS0) {
		spec = &spi_cs0;
	} else if ((ref->source == MIO_PIN_GPIO) && (ref->pin < MIO_NUM_GPIO)) {
		spec = &io_gpios[ref->pin];
	} else {
		/* not a host pin */
	}

	return ((spec != NULL) && (spec->port != NULL)) ? spec : NULL;
}

/* ---- PCA9557 expanders ---- */

#define PCA9557_REG_INPUT    0x00
#define PCA9557_REG_OUTPUT   0x01
#define PCA9557_REG_POLARITY 0x02
#define PCA9557_REG_CONFIG   0x03

struct expander {
	const struct device *bus;
	uint8_t addr;
	bool ready;
	uint8_t out;    /* Output Port register (physical levels) */
	uint8_t cfg;    /* Configuration register (1 = input) */
	uint8_t invert; /* active-low pins */
	struct k_mutex lock;
};

static struct expander expanders[MIO_MAX_ONBOARD_DEVS];

static struct expander *expander_get(const struct mio_pin_ref *ref)
{
	if ((ref->source != MIO_PIN_EXPANDER) || (ref->dev >= MIO_MAX_ONBOARD_DEVS) ||
	    (ref->pin >= MIO_EXPANDER_PINS) || !expanders[ref->dev].ready) {
		return NULL;
	}

	return &expanders[ref->dev];
}

static int expander_write(struct expander *exp, uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte(exp->bus, exp->addr, reg, val);
}

void mio_expanders_init(const struct mio_card *card)
{
	for (uint8_t i = 0U; i < card->n_devices; i++) {
		const struct mio_onboard_dev *dev = &card->devices[i];
		struct expander *exp = &expanders[dev->index];
		int ret;

		if (dev->type != MIO_DEV_PCA9557) {
			LOG_WRN("on-card device %u: unsupported type %u", dev->index, dev->type);
			continue;
		}

		exp->bus = mio_host_i2c(dev->bus);
		exp->addr = dev->addr;
		exp->out = 0x00U;
		exp->cfg = 0xFFU;
		exp->invert = 0x00U;
		k_mutex_init(&exp->lock);

		if ((exp->bus == NULL) || !device_is_ready(exp->bus)) {
			LOG_ERR("on-card PCA9557 %u: host bus not available", dev->index);
			continue;
		}

		ret = expander_write(exp, PCA9557_REG_POLARITY, 0x00U);
		if (ret == 0) {
			ret = expander_write(exp, PCA9557_REG_OUTPUT, exp->out);
		}
		if (ret == 0) {
			ret = expander_write(exp, PCA9557_REG_CONFIG, exp->cfg);
		}
		if (ret != 0) {
			LOG_ERR("on-card PCA9557 %u @ 0x%02x: no answer (%d)", dev->index,
				dev->addr, ret);
			continue;
		}

		exp->ready = true;
	}
}

static int expander_configure(struct expander *exp, uint8_t pin, gpio_flags_t flags)
{
	uint8_t bit = BIT(pin);
	int ret;

	if ((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) != 0U) {
		LOG_WRN("PCA9557 pin %u: the part has no internal pull resistors", pin);
	}

	k_mutex_lock(&exp->lock, K_FOREVER);

	WRITE_BIT(exp->invert, pin, (flags & GPIO_ACTIVE_LOW) != 0U);

	if ((flags & GPIO_OUTPUT) != 0U) {
		bool active = (flags & GPIO_OUTPUT_INIT_HIGH) != 0U;
		bool level = active != ((exp->invert & bit) != 0U);

		WRITE_BIT(exp->out, pin, level);
		exp->cfg &= (uint8_t)~bit;
		ret = expander_write(exp, PCA9557_REG_OUTPUT, exp->out);
	} else {
		exp->cfg |= bit;
		ret = 0;
	}

	if (ret == 0) {
		ret = expander_write(exp, PCA9557_REG_CONFIG, exp->cfg);
	}

	k_mutex_unlock(&exp->lock);

	return (ret == 0) ? 0 : -EIO;
}

static int expander_set(struct expander *exp, uint8_t pin, int value)
{
	int ret;

	k_mutex_lock(&exp->lock, K_FOREVER);
	WRITE_BIT(exp->out, pin, (value != 0) != ((exp->invert & BIT(pin)) != 0U));
	ret = expander_write(exp, PCA9557_REG_OUTPUT, exp->out);
	k_mutex_unlock(&exp->lock);

	return (ret == 0) ? 0 : -EIO;
}

static int expander_get_value(struct expander *exp, uint8_t pin)
{
	uint8_t in;
	int ret;

	ret = i2c_reg_read_byte(exp->bus, exp->addr, PCA9557_REG_INPUT, &in);
	if (ret != 0) {
		return -EIO;
	}

	return (((in ^ exp->invert) & BIT(pin)) != 0U) ? 1 : 0;
}

/* ---- common pin interface ---- */

int mio_pin_configure(const struct mio_pin_ref *ref, gpio_flags_t flags)
{
	const struct gpio_dt_spec *spec = host_spec(ref);
	struct expander *exp;

	if (spec != NULL) {
		return gpio_pin_configure(spec->port, spec->pin, flags);
	}

	exp = expander_get(ref);
	if (exp != NULL) {
		return expander_configure(exp, ref->pin, flags);
	}

	return -ENODEV;
}

int mio_pin_set(const struct mio_pin_ref *ref, int value)
{
	const struct gpio_dt_spec *spec = host_spec(ref);
	struct expander *exp;

	if (spec != NULL) {
		return gpio_pin_set(spec->port, spec->pin, value);
	}

	exp = expander_get(ref);
	if (exp != NULL) {
		return expander_set(exp, ref->pin, value);
	}

	return -ENODEV;
}

int mio_pin_get(const struct mio_pin_ref *ref)
{
	const struct gpio_dt_spec *spec = host_spec(ref);
	struct expander *exp;

	if (spec != NULL) {
		return gpio_pin_get(spec->port, spec->pin);
	}

	exp = expander_get(ref);
	if (exp != NULL) {
		return expander_get_value(exp, ref->pin);
	}

	return -ENODEV;
}

const char *mio_pin_name(const struct mio_pin_ref *ref, char *buf, size_t size)
{
	switch (ref->source) {
	case MIO_PIN_SPI_CS0:
		snprintf(buf, size, "SPI_CS0");
		break;
	case MIO_PIN_GPIO:
		snprintf(buf, size, "GPIO%u", ref->pin);
		break;
	case MIO_PIN_EXPANDER:
		snprintf(buf, size, "EXP%u.%u", ref->dev, ref->pin);
		break;
	default:
		snprintf(buf, size, "?");
		break;
	}

	return buf;
}

/* ---- fault LED ---- */

bool mio_fault_led_init(void)
{
	if (fault_led.port == NULL) {
		return false;
	}

	if (!gpio_is_ready_dt(&fault_led) ||
	    (gpio_pin_configure_dt(&fault_led, GPIO_OUTPUT_INACTIVE) != 0)) {
		LOG_ERR("fault LED GPIO not usable");
		return false;
	}

	return true;
}

int mio_fault_led_write(bool on)
{
	return gpio_pin_set_dt(&fault_led, on ? 1 : 0);
}
