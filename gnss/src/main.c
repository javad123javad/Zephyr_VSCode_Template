/*
 * Copyright (c) 2023 Trackunit Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/logging/log.h>
#include "common.h"
#include "modem_gnss.h"
#include "modem_pipe.h"
LOG_MODULE_REGISTER(ZBoard);
/* GPS Serial port */
const struct device *const gps_uart_dev = DEVICE_DT_GET(DT_NODELABEL(gps_usart));
////////////
#define DEFAULT_RADIO_NODE DT_NODELABEL(nrf24l1)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DEFAULT_RADIO_NODE),
             "No default RF radio specified in DT");
const struct device *const nrf_dev = DEVICE_DT_GET(DEFAULT_RADIO_NODE);
/* Regulators */
#define GPS_REGULATOR   DT_NODELABEL(reg_gps_3p3v)
#define RF_REGULATOR    DT_NODELABEL(reg_radio_3p3v)

const struct device *gps_reg = DEVICE_DT_GET(GPS_REGULATOR);
const struct device *rf_reg = DEVICE_DT_GET(RF_REGULATOR);

int main(void)
{
    int ret = 0;
    /* Enable regulators */
    ret = regulator_enable(gps_reg);
    ret |= regulator_enable(rf_reg);

    k_msleep(1000);

// Config radio PHY


    ret = gnss_init();
#if 1

    k_poll_signal_init(&signal);
    ring_buf_init(&ringbuf, sizeof(ring_buffer), ring_buffer);

    ret = radio_init(nrf_dev);
    if(ret)
    {
        LOG_ERR("Failed to init Radio PHY: %d", ret);
        // return ret;
    }
#endif
    struct k_poll_event events[1] = {
        [0]=K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
                                     K_POLL_MODE_NOTIFY_ONLY,
                                     &signal),
    };

    ret = jmodem_pipe_init(gps_uart_dev);
    if(ret)
    {
        LOG_ERR("Unable to init pip: %d", ret);
    }

    for(;;)
    {
        k_poll(events, 1, K_FOREVER);

        int signaled, result;

        k_poll_signal_check(&signal, &signaled, &result);

        if (signaled && (result == 0)) {
            // gpio_pin_toggle_dt(&led);
            // A-OK!
        } else {
            // weird error
        }
        k_poll_signal_reset(&signal);
        events[0].state = K_POLL_STATE_NOT_READY;

    }

    return 0;
}
