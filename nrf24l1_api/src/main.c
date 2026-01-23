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
struct k_poll_signal signal;
struct k_poll_event events[1] = {
    K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
    K_POLL_MODE_NOTIFY_ONLY,
    &signal),
};
void rf_send_cb(void *, void *, void*)
{
    for(;;)
    {
        k_poll(events,1, K_FOREVER);
        int signaled, result;

        k_poll_signal_check(&signal, &signaled, &result);

        if (signaled && (result == 0x0)) {
            // A-OK!
            LOG_INF("Data is sent");
        } else {
            // weird error
            LOG_ERR("Ops, something wrong happenned");
        }
        k_poll_signal_reset(&signal);
        events[0].state = K_POLL_STATE_NOT_READY;
    }
}
/* TX Thread */
#define TX_STACK_SIZE   500
#define TX_THREAD_PRIO  4
K_THREAD_STACK_DEFINE(tx_thread_stack, TX_STACK_SIZE);
struct k_thread tx_thread_data;
K_THREAD_DEFINE(tx_thread_id, TX_STACK_SIZE, rf_send_cb, NULL, NULL, NULL, TX_THREAD_PRIO, 0, 0);

int main(void)
{
    const struct device *const nrf_dev = DEVICE_DT_GET(DEFAULT_RADIO_NODE);
    int ret = 0;

    if (!device_is_ready(nrf_dev)) {
        LOG_ERR("%s Device not ready", nrf_dev->name);
        return 0;
    }
    LOG_INF("%s is ready to use.\n", nrf_dev->name);
    k_poll_signal_init(&signal);


    const char msg[] = "Hello!\r\n";

    for(;;)
    {

        ret =  rf_send_async(nrf_dev, (uint8_t*)msg, strlen(msg), &signal);
        if(ret)
        {
            LOG_ERR("Failed to send data over RF Link! err: %d", ret);
        }
        k_msleep(5000);
    }

    return 0;
}
