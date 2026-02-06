#ifndef __MODEM_LED_H_
#define __MODEM_LED_H_
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

int led_init(const struct gpio_dt_spec *const pled_dev);

#endif //__MODEM_LED_H_