#ifndef __MODEM_PIPE_H_
#define __MODEM_PIPE_H_
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

int jmodem_pipe_init(const struct device *const uart_dev);
int init_modem_pipe(const struct device *const uart_dev);

#endif //__MODEM_PIPE_H_