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
#include "modem_radio.h"
#include "modem_led.h"
#include "common.h"
LOG_MODULE_REGISTER(nrf24l1_radio);
/////////////////

#define TEST
///////////// USB CCM ////////////
const struct device *const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

///////////// LED ///////////////
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

//////////// RF PHY /////////////
#define DEFAULT_RADIO_NODE DT_NODELABEL(nrf24l1)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DEFAULT_RADIO_NODE),
             "No default RF radio specified in DT");

const struct device *const nrf_dev = DEVICE_DT_GET(DEFAULT_RADIO_NODE);


int main(void)
{
    int ret = 0;
    k_poll_signal_init(&signal);

    ret = led_init(&led);
    if(ret)
    {
        LOG_ERR("Failed to init LED: %d", ret);
        return ret;
    }
    ring_buf_init(&ringbuf, sizeof(ring_buffer), ring_buffer);

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

    struct k_poll_event events[1] = {
        K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
        K_POLL_MODE_NOTIFY_ONLY,
        &signal),
    };
    for(;;)
    {
        k_poll(events, 1, K_FOREVER);

        int signaled, result;

        k_poll_signal_check(&signal, &signaled, &result);

        if (signaled && (result == 0)) {
            gpio_pin_toggle_dt(&led);
            // A-OK!
        } else {
            // weird error
        }
        k_poll_signal_reset(&signal);
        events[0].state = K_POLL_STATE_NOT_READY;
        // k_sleep(K_FOREVER);

    }

}
