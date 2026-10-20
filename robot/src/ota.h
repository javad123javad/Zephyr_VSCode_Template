/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ROBOT_OTA_H_
#define ROBOT_OTA_H_

/**
 * @brief Mark the running firmware as good.
 *
 * Called once the robot is reachable over the network. An update that never
 * gets this far is not confirmed, and MCUboot reverts to the previous image
 * on the next reset.
 */
void ota_confirm(void);

#endif /* ROBOT_OTA_H_ */
