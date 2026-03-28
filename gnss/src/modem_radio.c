#include "modem_radio.h"
#include "common.h"
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(ZBoard, CONFIG_KERNEL_LOG_LEVEL);


struct k_poll_signal signal;


void recv_async_cb(const struct device *dev, uint8_t *rfdata, uint16_t size)
{
    LOG_INF("[Remote]: Async Received data: %s", rfdata);
    // modem_pipe_transmit(data.uart_pipe, rfdata, size);

}

void rf_send_cb(void *nrf_dev, void *, void*)
{
    LOG_INF("Thread is alive!");
    char buffer[RF_SPI_BUF_SIZE] = {0};
    const struct device *const pnrf_dev =(const struct device *const )nrf_dev;
    for(;;)
    {
        int rb_len = ring_buf_get(&ringbuf, buffer, sizeof(buffer));

        if(rb_len)
        {
            rf_send_async(pnrf_dev, buffer, rb_len, &signal);
        }
        else {
            radio_listen(pnrf_dev);

        }
        // k_msleep(1000);

    }
}

/* TX Thread */
#define TX_STACK_SIZE   1024
#define TX_THREAD_PRIO  6
K_THREAD_STACK_DEFINE(tx_thread_stack, TX_STACK_SIZE);

struct k_thread tx_thread_data;


int radio_init(const struct device *const pnrf_dev)
{
    int ret = 0;
    if (!device_is_ready(pnrf_dev)) {
        LOG_ERR("%s Device not ready", pnrf_dev->name);
        return -1;
    }
    LOG_INF("%s is ready to use.\n", pnrf_dev->name);
    k_tid_t tx_tid = k_thread_create(&tx_thread_data, tx_thread_stack,
                                     K_THREAD_STACK_SIZEOF(tx_thread_stack),
                                     rf_send_cb,
                                     (void*)pnrf_dev, NULL, NULL,
                                     TX_THREAD_PRIO, 0, K_NO_WAIT);
    UNUSED(tx_tid);
    ret = rf_recv_async(pnrf_dev, &recv_async_cb);
    return ret;
}

inline int radio_listen(const struct device *const pnrf_dev)
{
    return rf_recv_async(pnrf_dev, &recv_async_cb);
}