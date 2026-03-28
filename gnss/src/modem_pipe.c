#include "common.h"
#include "modem_pipe.h"
#include <zephyr/usb/usb_device.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ZBoardModem);


uint8_t ring_buffer[RING_BUF_SIZE];

struct ring_buf ringbuf;
/* Buffers for modem backend */

struct modem_data data;

/********* EVENT HANDLER *********/

/* Callback when modem pipe receives data */
static void modem_pipe_event_handler(struct modem_pipe *pipe, enum modem_pipe_event event,
                                     void *user_data)
{
    uint8_t buf[CONSOLE_RX_BUF_SIZE] = {0};
    int ret;
    int rb_len;

    switch (event) {
    case MODEM_PIPE_EVENT_RECEIVE_READY:
        /* Read from modem pipe and send directly to console */
        do {
            ret = modem_pipe_receive(pipe, buf, sizeof(buf));

            if (ret > 0) {
                rb_len = ring_buf_put(&ringbuf, buf, ret);
                if (rb_len < ret) {
                    LOG_ERR("Drop %u bytes", ret - rb_len);
                }
                // printk("wrote\r\n");



            }
        } while (ret > 0);

        break;

    case MODEM_PIPE_EVENT_TRANSMIT_IDLE:
    {

        /* Can send more data if available */
    }
    break;

    default:
        break;
    }
}


/***
 * Init usb modem pipe
 */
int jmodem_pipe_init(const struct device *const uart_dev)
{
    int ret = 0;

    if(!uart_dev)
        return -EINVAL;

    /* Verify modem device is ready */
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("Modem device not ready");
        return -ENODEV;
    }


    init_modem_pipe(uart_dev);
    return ret;

}

int init_modem_pipe(const struct device *const usb_uart_dev)
{
    int ret;

    const struct modem_backend_uart_config uart_backend_config = {
        .uart = usb_uart_dev,
        .receive_buf = data.buffers.uart_rx,
        .receive_buf_size = sizeof(data.buffers.uart_rx),
        .transmit_buf = data.buffers.uart_tx,
        .transmit_buf_size = sizeof(data.buffers.uart_tx),
    };

    data.uart_pipe = modem_backend_uart_init(&data.uart_backend, &uart_backend_config);
    if (data.uart_pipe == NULL) {
        LOG_ERR("Failed to initialize modem backend");
        return -1;
    }

    modem_pipe_attach(data.uart_pipe, modem_pipe_event_handler, &data);

    ret = modem_pipe_open(data.uart_pipe, K_MSEC(100));
    if (ret < 0) {
        LOG_ERR("Failed to open modem pipe");
        return ret;
    }

    LOG_INF("Modem pipe initialized and opened");
    return 0;
}
