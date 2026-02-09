#ifndef __COMMON_H_
#define __COMMON_H_

#include <stdint.h>
#include <string.h>
#include <zephyr/modem/backend/uart.h>
#include <zephyr/modem/pipe.h>
#define UART_RX_BUF_SIZE    64
#define UART_TX_BUF_SIZE    64
#define CONSOLE_RX_BUF_SIZE 64
#define RF_SPI_BUF_SIZE     32
#define RING_BUF_SIZE       2048

struct modem_data {
    struct {
        uint8_t uart_rx[UART_RX_BUF_SIZE];
        uint8_t uart_tx[UART_TX_BUF_SIZE];
    } buffers;

    struct modem_backend_uart uart_backend;
    struct modem_pipe *uart_pipe;
};

struct msgq_data_item_t {
    uint8_t buf[CONSOLE_RX_BUF_SIZE];
    size_t len;
};


extern struct modem_data data;
extern struct ring_buf ringbuf;
extern uint8_t ring_buffer[RING_BUF_SIZE];

#endif //__COMMON_H_