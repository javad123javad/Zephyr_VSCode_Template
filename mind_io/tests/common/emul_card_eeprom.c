/*
 * Test-only emulator of a 16-bit addressed card EEPROM (M24C64-like, 8 KiB).
 * Preloaded with the golden card image unless CONFIG_TEST_CARD_IMAGE_GOLDEN
 * is disabled, in which case it reads as erased (all 0xff).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mind_test_card_eeprom

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

#include "test_emul.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static const uint8_t golden[] = {
#include "dev_card.bin.inc"
};

static uint8_t mem[TEST_CARD_EEPROM_SIZE];
static uint16_t ptr;

uint8_t *test_card_eeprom_mem(void)
{
	return mem;
}

static int card_eeprom_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
				int addr)
{
	ARG_UNUSED(target);
	ARG_UNUSED(addr);

	for (int i = 0; i < num_msgs; i++) {
		struct i2c_msg *msg = &msgs[i];
		uint32_t j = 0U;

		if ((msg->flags & I2C_MSG_READ) != 0U) {
			for (; j < msg->len; j++) {
				msg->buf[j] = mem[ptr];
				ptr = (ptr + 1U) % TEST_CARD_EEPROM_SIZE;
			}
			continue;
		}

		if (i == 0) {
			if (msg->len == 0U) {
				continue; /* address probe */
			}
			if (msg->len < 2U) {
				return -EIO;
			}
			ptr = (uint16_t)(((msg->buf[0] << 8) | msg->buf[1]) % TEST_CARD_EEPROM_SIZE);
			j = 2U;
		}

		for (; j < msg->len; j++) {
			mem[ptr] = msg->buf[j];
			ptr = (ptr + 1U) % TEST_CARD_EEPROM_SIZE;
		}
	}

	return 0;
}

static const struct i2c_emul_api card_eeprom_api = {
	.transfer = card_eeprom_transfer,
};

static int card_eeprom_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(target);
	ARG_UNUSED(parent);

	memset(mem, 0xFF, sizeof(mem));
	if (IS_ENABLED(CONFIG_TEST_CARD_IMAGE_GOLDEN)) {
		memcpy(mem, golden, sizeof(golden));
	}

	return 0;
}

static int card_eeprom_dev_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

#define CARD_EEPROM_EMUL(inst)                                                                     \
	DEVICE_DT_INST_DEFINE(inst, card_eeprom_dev_init, NULL, NULL, NULL, POST_KERNEL,          \
			      CONFIG_I2C_INIT_PRIORITY, NULL);                                     \
	EMUL_DT_INST_DEFINE(inst, card_eeprom_emul_init, NULL, NULL, &card_eeprom_api, NULL);

DT_INST_FOREACH_STATUS_OKAY(CARD_EEPROM_EMUL)

#else /* no card EEPROM node: the "absent card" test build */

uint8_t *test_card_eeprom_mem(void)
{
	return NULL;
}

#endif
