/*
 * MCP2515 SPI CAN controller on io_spi (8MHz oscillator, INT on PB3,
 * CS via spi2's hardware NSS pin). Defaults to internal loopback mode
 * so the controller and SPI link are exercised even without a second
 * node on the bus; a test frame is sent every second and anything
 * received is printed. Disable CAN_TEST_LOOPBACK once you have a
 * second CAN node to talk to.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "can_test.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>

#define CAN_TEST_LOOPBACK    1 /* no second CAN node needed on the bus */
#define CAN_TEST_ID           0x123
#define CAN_TEST_PERIOD_TICKS 2 /* 2 * 500ms = 1s */

static const struct device *can_dev = DEVICE_DT_GET(DT_NODELABEL(mcp2515));

CAN_MSGQ_DEFINE(can_rx_msgq, 4);

static bool can_ready;

void can_test_init(void)
{
	const struct can_filter filter = {
		.id = CAN_TEST_ID,
		.mask = CAN_STD_ID_MASK,
	};
	int ret;

	if (!device_is_ready(can_dev)) {
		printk("MCP2515: device not ready\n");
		return;
	}

	if (CAN_TEST_LOOPBACK) {
		ret = can_set_mode(can_dev, CAN_MODE_LOOPBACK);
		if (ret < 0) {
			printk("MCP2515: failed to set loopback mode (%d)\n", ret);
			return;
		}
	}

	ret = can_start(can_dev);
	if (ret < 0) {
		printk("MCP2515: failed to start (%d)\n", ret);
		return;
	}

	ret = can_add_rx_filter_msgq(can_dev, &can_rx_msgq, &filter);
	if (ret < 0) {
		printk("MCP2515: failed to add rx filter (%d)\n", ret);
		return;
	}

	printk("MCP2515 @ 500 kbit/s: %s, sending id 0x%x every %ds\n",
	       CAN_TEST_LOOPBACK ? "loopback mode" : "normal mode",
	       CAN_TEST_ID, CAN_TEST_PERIOD_TICKS / 2);
	can_ready = true;
}

void can_test_step(unsigned int tick)
{
	struct can_frame frame;

	if (!can_ready) {
		return;
	}

	if (tick % CAN_TEST_PERIOD_TICKS == 0) {
		static uint8_t counter;
		int ret;

		frame = (struct can_frame){
			.id = CAN_TEST_ID,
			.dlc = 1,
			.data = { counter++ },
		};

		ret = can_send(can_dev, &frame, K_MSEC(100), NULL, NULL);
		if (ret < 0) {
			printk("MCP2515: send failed (%d)\n", ret);
		}
	}

	while (k_msgq_get(&can_rx_msgq, &frame, K_NO_WAIT) == 0) {
		printk("MCP2515: rx id=0x%x dlc=%u data[0]=0x%02x\n",
		       frame.id, frame.dlc, frame.data[0]);
	}
}
