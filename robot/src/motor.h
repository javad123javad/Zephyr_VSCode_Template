/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ROBOT_MOTOR_H_
#define ROBOT_MOTOR_H_

#include <stdint.h>

enum motor_id {
	MOTOR_1,
	MOTOR_2,
	MOTOR_3,
	MOTOR_4,
	MOTOR_COUNT,
};

/** Full speed in either direction. */
#define MOTOR_SPEED_MAX 100

/**
 * @brief Check the PWM channels of all motors, leaving every motor stopped.
 *
 * A motor whose channels are not usable is marked unavailable; the others
 * keep working.
 *
 * @retval 0 All motors ready
 * @retval -ENODEV At least one motor is unavailable
 */
int motor_init(void);

/**
 * @brief Drive one motor.
 *
 * @param id Motor to drive
 * @param speed Percentage of full power, -MOTOR_SPEED_MAX (reverse) to
 *              MOTOR_SPEED_MAX (forward); 0 lets it coast
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid motor or speed
 * @retval -ENODEV Motor unavailable
 * @retval -EIO PWM update failed
 */
int motor_set(enum motor_id id, int32_t speed);

/**
 * @brief Stop all motors.
 */
void motor_stop_all(void);

#endif /* ROBOT_MOTOR_H_ */
