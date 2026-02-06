#ifndef __MODEM_RADIO_H_
#define __MODEM_RADIO_H_
#include <zephyr/kernel.h>
#include <zephyr/drivers/rf/rf.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>

int radio_init(const struct device *const pnrf_dev);
int radio_listen(const struct device *const pnrf_dev);
#endif //__MODEM_RADIO_H_