/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>

#include "ota.h"

LOG_MODULE_REGISTER(ota, LOG_LEVEL_INF);

void ota_confirm(void)
{
	int err;

	if (boot_is_img_confirmed()) {
		return;
	}

	err = boot_write_img_confirmed();
	if (err != 0) {
		LOG_ERR("Confirming the new firmware failed (%d)", err);
		return;
	}

	LOG_INF("New firmware confirmed");
}
