/*
 * BME280 environmental sensor on io_i2c @ 0x76 (SDO -> GND). Uses the
 * standard "bosch,bme280" devicetree binding and Zephyr's sensor API.
 * One sample is taken and checked against the sensor's specified
 * operating range, which catches a stuck or unconfigured sensor
 * returning zeros or garbage.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bme280_test.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

static const struct device *const bme280 = DEVICE_DT_GET(DT_NODELABEL(bme280));

int bme280_test_run(const struct shell *sh)
{
	struct sensor_value temp, press, humidity;
	int ret;

	if (!device_is_ready(bme280)) {
		shell_error(sh, "BME280: device not ready");
		return -ENODEV;
	}

	ret = sensor_sample_fetch(bme280);
	if (ret < 0) {
		shell_error(sh, "BME280: sample fetch failed (%d)", ret);
		return ret;
	}

	(void)sensor_channel_get(bme280, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	(void)sensor_channel_get(bme280, SENSOR_CHAN_PRESS, &press);
	(void)sensor_channel_get(bme280, SENSOR_CHAN_HUMIDITY, &humidity);

	shell_print(sh, "BME280: temp=%d.%06d C, press=%d.%06d kPa, humidity=%d.%06d %%RH",
		    temp.val1, temp.val2, press.val1, press.val2, humidity.val1, humidity.val2);

	/* Operating range from the datasheet: -40..85 C, 30..110 kPa, 0..100 %RH */
	if ((temp.val1 < -40) || (temp.val1 > 85) ||
	    (press.val1 < 30) || (press.val1 > 110) ||
	    (humidity.val1 < 0) || (humidity.val1 > 100)) {
		shell_error(sh, "BME280: reading outside the sensor's operating range");
		return -ERANGE;
	}

	return 0;
}
