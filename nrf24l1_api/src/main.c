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

LOG_MODULE_REGISTER(nrf24l1_radio);
/////////////////
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/logging/log.h>
// LOG_MODULE_REGISTER(cdc_acm_echo, LOG_LEVEL_INF);

const struct device *const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

#define RING_BUF_SIZE 1024
uint8_t ring_buffer[RING_BUF_SIZE];

struct ring_buf ringbuf;

static bool rx_throttled;

static inline void print_baudrate(const struct device *dev)
{
    uint32_t baudrate;
    int ret;

    ret = uart_line_ctrl_get(dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
    if (ret) {
        LOG_WRN("Failed to get baudrate, ret code %d", ret);
    } else {
        LOG_INF("Baudrate %u", baudrate);
    }
}

static void interrupt_handler(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
        if (!rx_throttled && uart_irq_rx_ready(dev)) {
            int recv_len, rb_len;
            uint8_t buffer[64];
            size_t len = MIN(ring_buf_space_get(&ringbuf),
                             sizeof(buffer));

            if (len == 0) {
                /* Throttle because ring buffer is full */
                uart_irq_rx_disable(dev);
                rx_throttled = true;
                continue;
            }

            recv_len = uart_fifo_read(dev, buffer, len);
            if (recv_len < 0) {
                LOG_ERR("Failed to read UART FIFO");
                recv_len = 0;
            };

            rb_len = ring_buf_put(&ringbuf, buffer, recv_len);
            if (rb_len < recv_len) {
                LOG_ERR("Drop %u bytes", recv_len - rb_len);
            }

            LOG_DBG("tty fifo -> ringbuf %d bytes", rb_len);
            if (rb_len) {
                uart_irq_tx_enable(dev);
            }
        }

        if (uart_irq_tx_ready(dev)) {
            uint8_t buffer[64];
            int rb_len, send_len;

            rb_len = ring_buf_get(&ringbuf, buffer, sizeof(buffer));
            if (!rb_len) {
                LOG_DBG("Ring buffer empty, disable TX IRQ");
                uart_irq_tx_disable(dev);
                continue;
            }

            if (rx_throttled) {
                uart_irq_rx_enable(dev);
                rx_throttled = false;
            }

            send_len = uart_fifo_fill(dev, buffer, rb_len);
            if (send_len < rb_len) {
                LOG_ERR("Drop %d bytes", rb_len - send_len);
            }

            LOG_DBG("ringbuf -> tty fifo %d bytes", send_len);
        }
    }
}
////////////////////////////
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED3_NODE DT_ALIAS(led3)


static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
// static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
// static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(LED3_NODE, gpios);


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
    LOG_INF("Thread is alive!");

    for(;;)
    {
        k_poll(events,1, K_FOREVER);
        int signaled, result;

        k_poll_signal_check(&signal, &signaled, &result);

        if (signaled && (result == 0x0)) {
            // A-OK!
            // LOG_INF("Data is sent");
        } else {
            // weird error
            // LOG_ERR("Ops, something wrong happenned");
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

void recv_async_cb(const struct device *dev, uint8_t *data, uint16_t size)
{
    // LOG_INF("[Remote]: Async Received data: %s", data);
}
int main(void)
{
    const struct device *const nrf_dev = DEVICE_DT_GET(DEFAULT_RADIO_NODE);
    int ret = 0;
    if (!gpio_is_ready_dt(&led) /*&& !gpio_is_ready_dt(&led1)&& !gpio_is_ready_dt(&led3)*/) {
        LOG_INF("LED PRobelm");
        return 0;
    }
    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    // ret |= gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
    // ret |= gpio_pin_configure_dt(&led3, GPIO_OUTPUT_ACTIVE);

    if (ret < 0) {
        LOG_INF("LED configure Probelem");
        return 0;
    }
    gpio_pin_set_dt(&led, GPIO_OUTPUT_HIGH);
    // gpio_pin_set_dt(&led1, GPIO_OUTPUT_HIGH);
    // gpio_pin_set_dt(&led3, GPIO_OUTPUT_HIGH);

    //usb
    /////////////////
    if (!device_is_ready(nrf_dev)) {
        LOG_ERR("%s Device not ready", nrf_dev->name);
        return 0;
    }
    LOG_INF("%s is ready to use.\n", nrf_dev->name);
    k_poll_signal_init(&signal);

///////////////

    // if (!device_is_ready(uart_dev)) {
    //     LOG_ERR("CDC ACM device not ready");
    //     return 0;
    // }

    // ret = usb_enable(NULL);
    // if (ret != 0) {
    //     LOG_ERR("Failed to enable USB");
    //     return 0;
    // }

    // ring_buf_init(&ringbuf, sizeof(ring_buffer), ring_buffer);

    // LOG_INF("Wait for DTR");

    // while (true) {
    //     uint32_t dtr = 0U;

    //     uart_line_ctrl_get(uart_dev, UART_LINE_CTRL_DTR, &dtr);
    //     if (dtr) {
    //         break;
    //     }

    //     k_sleep(K_MSEC(100));
    // }

    // LOG_INF("DTR set");

    // /* They are optional, we use them to test the interrupt endpoint */
    // ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DCD, 1);
    // if (ret) {
    //     LOG_WRN("Failed to set DCD, ret code %d", ret);
    // }

    // ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DSR, 1);
    // if (ret) {
    //     LOG_WRN("Failed to set DSR, ret code %d", ret);
    // }

    // /* Wait 100ms for the host to do all settings */
    // k_msleep(100);

    // print_baudrate(uart_dev);

    // uart_irq_callback_set(uart_dev, interrupt_handler);
    // /* Enable rx interrupts */
    // uart_irq_rx_enable(uart_dev);
    /////////////
    const char msg[] = "Hello!\r\n";
    ret = rf_recv_async(nrf_dev, &recv_async_cb);
    LOG_INF("Async read ret: %d", ret);
    for(;;)
    {

        ret =  rf_send_async(nrf_dev, (uint8_t*)msg, strlen(msg), &signal);
        if(ret)
        {
            LOG_ERR("Failed to send data over RF Link! err: %d", ret);
        }
        k_msleep(100);
    }

}
