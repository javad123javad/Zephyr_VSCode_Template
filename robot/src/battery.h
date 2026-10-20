/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ROBOT_BATTERY_H_
#define ROBOT_BATTERY_H_

#include <stdbool.h>
#include <stdint.h>

enum battery_id {
	BATTERY_MOTOR,
	BATTERY_BOARD,
	BATTERY_COUNT,
};

struct battery_state {
	/** Filtered pack voltage in millivolts. */
	uint32_t mv;
	/** Below the warning level: speed is capped. */
	bool low;
	/** Below the cutoff: driving is inhibited. */
	bool empty;
};

#if defined(CONFIG_ROBOT_BATTERY)

/**
 * @brief Start periodic battery measurement.
 *
 * @retval 0 Started
 * @retval -ENODEV ADC not ready
 * @retval -ENOTSUP Battery monitoring disabled (CONFIG_ROBOT_BATTERY=n)
 */
int battery_start(void);

/**
 * @brief Get the latest state of one battery.
 *
 * @param id Battery to read
 * @param state Filled with the latest state
 *
 * @retval 0 Success
 * @retval -ENODATA No measurement yet, or monitoring disabled
 */
int battery_get(enum battery_id id, struct battery_state *state);

#else

#include <errno.h>

static inline int battery_start(void)
{
	return -ENOTSUP;
}

static inline int battery_get(enum battery_id id, struct battery_state *state)
{
	(void)id;
	(void)state;

	return -ENODATA;
}

#endif /* CONFIG_ROBOT_BATTERY */

#endif /* ROBOT_BATTERY_H_ */
