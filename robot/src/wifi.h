/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ROBOT_WIFI_H_
#define ROBOT_WIFI_H_

/**
 * @brief Join the configured Wi-Fi network and keep reconnecting.
 *
 * @retval 0 Connection started
 * @retval -ENODEV No Wi-Fi station interface
 * @retval -EINVAL No SSID configured
 */
int wifi_start(void);

/**
 * @brief Get the signal strength of the station link.
 *
 * @return RSSI in dBm, or 0 when not connected
 */
int wifi_rssi(void);

#endif /* ROBOT_WIFI_H_ */
