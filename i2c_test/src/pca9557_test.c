/*
 * PCA9557PW,118 8-bit GPIO expander on io_i2c @ 0x19 (address pins:
 * A0=VCC, A1=GND, A2=GND), driven by the out-of-tree "nxp,pca9557"
 * driver (see ../pca9557_driver) through Zephyr's standard gpio.h API.
 *
 * Walks a single high bit across all 8 pins. Each step is verified by
 * reading the chip's Output Port register back over I2C: reading the
 * pins themselves is not a reliable check on this part, since IO0 is an
 * open-drain output (reads low without an external pull-up) and the
 * Polarity Inversion register resets to 0xf0, inverting IO4-IO7 in the
 * Input Port register.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pca9557_test.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#define PCA9557_NODE        DT_NODELABEL(io_expander)
#define PCA9557_NPINS       8U
#define PCA9557_REG_OUTPUT  0x01
#define PCA9557_STEP_MS     100

static const struct device *const expander = DEVICE_DT_GET(PCA9557_NODE);
static const struct i2c_dt_spec expander_i2c = I2C_DT_SPEC_GET(PCA9557_NODE);

int pca9557_test_run(const struct shell *sh)
{
	uint8_t out;
	int ret;

	if (!device_is_ready(expander)) {
		shell_error(sh, "PCA9557: device not ready");
		return -ENODEV;
	}

	for (uint8_t pin = 0; pin < PCA9557_NPINS; pin++) {
		ret = gpio_pin_configure(expander, pin, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			shell_error(sh, "PCA9557: failed to configure pin %u (%d)", pin, ret);
			return ret;
		}
	}

	for (uint8_t pin = 0; pin < PCA9557_NPINS; pin++) {
		ret = gpio_port_set_masked_raw(expander, BIT_MASK(PCA9557_NPINS), BIT(pin));
		if (ret < 0) {
			shell_error(sh, "PCA9557: write failed at pin %u (%d)", pin, ret);
			break;
		}

		ret = i2c_reg_read_byte_dt(&expander_i2c, PCA9557_REG_OUTPUT, &out);
		if (ret < 0) {
			shell_error(sh, "PCA9557: output register read failed (%d)", ret);
			break;
		}

		if (out != BIT(pin)) {
			shell_error(sh, "PCA9557: pin %u: wrote 0x%02x, output register reads 0x%02x",
				    pin, (unsigned int)BIT(pin), out);
			ret = -EIO;
			break;
		}

		k_msleep(PCA9557_STEP_MS);
	}

	(void)gpio_port_clear_bits_raw(expander, BIT_MASK(PCA9557_NPINS));

	if (ret == 0) {
		shell_print(sh, "PCA9557 @ 0x%02x: walked IO0-IO7, output register verified "
			    "at each step", expander_i2c.addr);
	}

	return ret;
}
