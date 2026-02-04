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
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/logging/log.h>
#include "modem_pipe.h"
#include "common.h"
LOG_MODULE_REGISTER(nrf24l1_radio);
/////////////////
char usb_spi_msgq_buffer[4 * sizeof(struct msgq_data_item_t)];

struct k_msgq usb_spi_msgq;

/////////////
const struct device *const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

////////////////////////////
#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define DEFAULT_RADIO_NODE DT_NODELABEL(nrf24l1)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DEFAULT_RADIO_NODE),
             "No default RF radio specified in DT");

const struct device *const nrf_dev = DEVICE_DT_GET(DEFAULT_RADIO_NODE);

struct k_poll_signal signal;
struct k_poll_event events[1] = {
    K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
    K_POLL_MODE_NOTIFY_ONLY,
    &signal),
};
void rf_send_cb(void *, void *, void*)
{
    LOG_INF("Thread is alive!");

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
#define TX_STACK_SIZE   256
#define TX_THREAD_PRIO  4
K_THREAD_STACK_DEFINE(tx_thread_stack, TX_STACK_SIZE);
struct k_thread tx_thread_data;
K_THREAD_DEFINE(tx_thread_id, TX_STACK_SIZE, rf_send_cb, NULL, NULL, NULL, TX_THREAD_PRIO, 0, 0);

void recv_async_cb(const struct device *dev, uint8_t *rfdata, uint16_t size)
{
    LOG_INF("[Remote]: Async Received data: %s", rfdata);
    modem_pipe_transmit(data.uart_pipe, rfdata, size);


}
int radio_init(const struct device *const pnrf_dev)
{
    int ret = 0;
    if (!device_is_ready(pnrf_dev)) {
        LOG_ERR("%s Device not ready", pnrf_dev->name);
        return -1;
    }
    LOG_INF("%s is ready to use.\n", pnrf_dev->name);
    k_poll_signal_init(&signal);

    ret = rf_recv_async(pnrf_dev, &recv_async_cb);
    return ret;
}
int led_init(const struct gpio_dt_spec *const pled_dev)
{
    int ret = 0;
    if (!gpio_is_ready_dt(pled_dev)) {
        LOG_INF("LED init problem");
        return 0;
    }
    ret = gpio_pin_configure_dt(pled_dev, GPIO_OUTPUT_ACTIVE);

    if (ret < 0) {
        LOG_INF("LED configure Probelem");
        return 0;
    }
    gpio_pin_set_dt(pled_dev, GPIO_OUTPUT_HIGH);

    return ret;
}
int main(void)
{
    int ret = 0;
    k_msgq_init(&usb_spi_msgq, usb_spi_msgq_buffer, sizeof(struct msgq_data_item_t), 10);

    ret = led_init(&led);
    if(ret)
    {
        LOG_ERR("Failed to init LED: %d", ret);
        return ret;
    }
    // Config radio PHY
    ret = radio_init(nrf_dev);
    if(ret)
    {
        LOG_ERR("Failed to init Radio PHY: %d", ret);
        return ret;
    }
    // Configure pipe
    ret = jmodem_pipe_init(uart_dev);
    if(ret)
    {
        LOG_ERR("Unable to init pip: %d", ret);
    }
    struct msgq_data_item_t Rxdata;

    for(;;)
    {

        k_msgq_get(&usb_spi_msgq, &Rxdata, K_FOREVER);

        LOG_INF("[SEND OVER SPI]: %s", Rxdata.buf);

        ret =  rf_send_async(nrf_dev, (uint8_t*)Rxdata.buf, Rxdata.len, &signal);

        if(ret)
        {
            LOG_ERR("Failed to send data over RF Link! err: %d", ret);
        }
    }

}
