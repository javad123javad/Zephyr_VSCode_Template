/*
 * BME280 environmental sensor on io_i2c @ 0x76 (SDO -> GND). Uses the
 * standard "bosch,bme280" devicetree binding and Zephyr's sensor API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bme280_test.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

static const struct device *bme280 = DEVICE_DT_GET(DT_NODELABEL(bme280));

void bme280_test_init(void)
{
	if (!device_is_ready(bme280)) {
		printk("BME280: device not ready\n");
	} else {
		printk("BME280 @ 0x76: sampling every 2s\n");
	}
}

void bme280_test_sample(void)
{
	struct sensor_value temp, press, humidity;
	int ret;

	if (!device_is_ready(bme280)) {
		return;
	}

	ret = sensor_sample_fetch(bme280);
	if (ret < 0) {
		printk("BME280: sample fetch failed (%d)\n", ret);
		return;
	}

	sensor_channel_get(bme280, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	sensor_channel_get(bme280, SENSOR_CHAN_PRESS, &press);
	sensor_channel_get(bme280, SENSOR_CHAN_HUMIDITY, &humidity);

	printk("BME280: temp=%d.%06d C, press=%d.%06d kPa, humidity=%d.%06d %%RH\n",
	       temp.val1, temp.val2,
	       press.val1, press.val2,
	       humidity.val1, humidity.val2);
}
