/*
 * Test-only emulator of the PCA9557 register set: Input, Output, Polarity
 * Inversion and Configuration, with the power-on values of the real part.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mind_test_pca9557

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

#include "test_emul.h"

static uint8_t regs[4];
static uint8_t ptr;
static uint8_t ext_inputs;

struct test_pca9557_regs test_pca9557_regs(void)
{
	return (struct test_pca9557_regs){.out = regs[1], .pol = regs[2], .cfg = regs[3]};
}

void test_pca9557_set_inputs(uint8_t levels)
{
	ext_inputs = levels;
}

static uint8_t read_reg(uint8_t reg)
{
	if (reg == 0U) {
		uint8_t pins = (regs[1] & (uint8_t)~regs[3]) | (ext_inputs & regs[3]);

		return pins ^ regs[2];
	}

	return regs[reg];
}

static int pca9557_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
			    int addr)
{
	ARG_UNUSED(target);
	ARG_UNUSED(addr);

	for (int i = 0; i < num_msgs; i++) {
		struct i2c_msg *msg = &msgs[i];

		if ((msg->flags & I2C_MSG_READ) != 0U) {
			for (uint32_t j = 0U; j < msg->len; j++) {
				msg->buf[j] = read_reg(ptr);
			}
			continue;
		}

		if (msg->len == 0U) {
			continue;
		}
		ptr = msg->buf[0] & 0x03U;
		if ((msg->len >= 2U) && (ptr != 0U)) {
			regs[ptr] = msg->buf[1];
		}
	}

	return 0;
}

static const struct i2c_emul_api pca9557_api = {
	.transfer = pca9557_transfer,
};

static int pca9557_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(target);
	ARG_UNUSED(parent);

	regs[1] = 0x00U;
	regs[2] = 0xF0U;
	regs[3] = 0xFFU;
	return 0;
}

static int pca9557_dev_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

#define PCA9557_EMUL(inst)                                                                         \
	DEVICE_DT_INST_DEFINE(inst, pca9557_dev_init, NULL, NULL, NULL, POST_KERNEL,              \
			      CONFIG_I2C_INIT_PRIORITY, NULL);                                     \
	EMUL_DT_INST_DEFINE(inst, pca9557_emul_init, NULL, NULL, &pca9557_api, NULL);

DT_INST_FOREACH_STATUS_OKAY(PCA9557_EMUL)
