#include "modem_radio.h"
#include "common.h"
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nrf24l1_radio, CONFIG_KERNEL_LOG_LEVEL);

int g_tx_free;
struct k_poll_signal signal;
struct k_poll_event events[1] = {
    K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
    K_POLL_MODE_NOTIFY_ONLY,
    &signal),
};

void recv_async_cb(const struct device *dev, uint8_t *rfdata, uint16_t size)
{
    LOG_INF("[Remote]: Async Received data: %s", rfdata);
    // printk("RCV Dump: %s", rfdata);
    modem_pipe_transmit(data.uart_pipe, rfdata, size);

}

void rf_send_cb(void *, void *, void*)
{
    LOG_INF("Thread is alive!");

    for(;;)
    {
        k_poll(events, 1, K_FOREVER);
        int signaled, result;

        k_poll_signal_check(&signal, &signaled, &result);

        if (signaled && (result == 0x0)) {
            // A-OK!
            LOG_INF("Data is sent");

        } else {
            // weird error
            LOG_ERR("Ops, something wrong happenned");
        }
        g_tx_free = 1;
        k_poll_signal_reset(&signal);
        events[0].state = K_POLL_STATE_NOT_READY;
    }
}
/* TX Thread */
#define TX_STACK_SIZE   256
#define TX_THREAD_PRIO  7
K_THREAD_STACK_DEFINE(tx_thread_stack, TX_STACK_SIZE);
struct k_thread tx_thread_data;
// K_THREAD_DEFINE(tx_thread_id, TX_STACK_SIZE, rf_send_cb, NULL, NULL, NULL, TX_THREAD_PRIO, 0, 0);


int radio_init(const struct device *const pnrf_dev)
{
    int ret = 0;
    if (!device_is_ready(pnrf_dev)) {
        LOG_ERR("%s Device not ready", pnrf_dev->name);
        return -1;
    }
    LOG_INF("%s is ready to use.\n", pnrf_dev->name);
    k_poll_signal_init(&signal);
    k_tid_t my_tid = k_thread_create(&tx_thread_data, tx_thread_stack,
                                     K_THREAD_STACK_SIZEOF(tx_thread_stack),
                                     rf_send_cb,
                                     NULL, NULL, NULL,
                                     TX_THREAD_PRIO, 0, K_NO_WAIT);

    ret = rf_recv_async(pnrf_dev, &recv_async_cb);
    return ret;
}

int radio_listen(const struct device *const pnrf_dev)
{
    return rf_recv_async(pnrf_dev, &recv_async_cb);
}