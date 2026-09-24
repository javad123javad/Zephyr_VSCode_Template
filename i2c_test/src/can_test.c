/*
 * MCP2515 SPI CAN controller on io_spi (8MHz oscillator, INT on PB3,
 * CS via spi2's hardware NSS pin). Runs in internal loopback mode, so
 * the controller, its SPI link and its interrupt line are exercised
 * without a second node on the bus: one frame is sent and must be
 * received back. Set CAN_TEST_LOOPBACK to 0 once a second CAN node is
 * connected to answer on CAN_TEST_ID.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "can_test.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>

#define CAN_TEST_LOOPBACK   1
#define CAN_TEST_ID         0x123
#define CAN_TEST_TIMEOUT_MS 100

static const struct device *const can_dev = DEVICE_DT_GET(DT_NODELABEL(mcp2515));

CAN_MSGQ_DEFINE(can_rx_msgq, 4);

static bool can_started;

static int can_setup(const struct shell *sh)
{
	const struct can_filter filter = {
		.id = CAN_TEST_ID,
		.mask = CAN_STD_ID_MASK,
	};
	int ret;

	if (can_started) {
		return 0;
	}

	if (CAN_TEST_LOOPBACK) {
		ret = can_set_mode(can_dev, CAN_MODE_LOOPBACK);
		if (ret < 0) {
			shell_error(sh, "MCP2515: failed to set loopback mode (%d)", ret);
			return ret;
		}
	}

	ret = can_start(can_dev);
	if (ret < 0) {
		shell_error(sh, "MCP2515: failed to start (%d)", ret);
		return ret;
	}

	ret = can_add_rx_filter_msgq(can_dev, &can_rx_msgq, &filter);
	if (ret < 0) {
		shell_error(sh, "MCP2515: failed to add rx filter (%d)", ret);
		(void)can_stop(can_dev);
		return ret;
	}

	can_started = true;
	return 0;
}

int can_test_run(const struct shell *sh)
{
	static uint8_t counter;
	struct can_frame tx;
	struct can_frame rx;
	int ret;

	if (!device_is_ready(can_dev)) {
		shell_error(sh, "MCP2515: device not ready");
		return -ENODEV;
	}

	ret = can_setup(sh);
	if (ret < 0) {
		return ret;
	}

	k_msgq_purge(&can_rx_msgq);

	tx = (struct can_frame){
		.id = CAN_TEST_ID,
		.dlc = 1,
		.data = { counter++ },
	};

	ret = can_send(can_dev, &tx, K_MSEC(CAN_TEST_TIMEOUT_MS), NULL, NULL);
	if (ret < 0) {
		shell_error(sh, "MCP2515: send failed (%d)", ret);
		return ret;
	}

	ret = k_msgq_get(&can_rx_msgq, &rx, K_MSEC(CAN_TEST_TIMEOUT_MS));
	if (ret < 0) {
		shell_error(sh, "MCP2515: no frame received within %d ms", CAN_TEST_TIMEOUT_MS);
		return -ETIMEDOUT;
	}

	if ((rx.id != tx.id) || (rx.dlc != tx.dlc) || (rx.data[0] != tx.data[0])) {
		shell_error(sh, "MCP2515: sent id=0x%x data[0]=0x%02x, received id=0x%x "
			    "dlc=%u data[0]=0x%02x", tx.id, tx.data[0], rx.id, rx.dlc, rx.data[0]);
		return -EIO;
	}

	shell_print(sh, "MCP2515 @ 500 kbit/s (%s): sent and received id=0x%x data[0]=0x%02x",
		    CAN_TEST_LOOPBACK ? "loopback" : "normal", rx.id, rx.data[0]);
	return 0;
}
