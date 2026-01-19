/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/rf/rf.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(nrf24l1_radio);


#define DEFAULT_RADIO_NODE DT_NODELABEL(nrf24l1)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DEFAULT_RADIO_NODE),
             "No default LoRa radio specified in DT");

int main(void)
{
    const struct device *const nrf_dev = DEVICE_DT_GET(DEFAULT_RADIO_NODE);
    printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

    if (!device_is_ready(nrf_dev)) {
        LOG_ERR("%s Device not ready", nrf_dev->name);
        return 0;
    }
    LOG_INF("%s is ready to use.\n", nrf_dev->name);

    return 0;
}
