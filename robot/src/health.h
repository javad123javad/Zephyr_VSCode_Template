/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ROBOT_HEALTH_H_
#define ROBOT_HEALTH_H_

#include <stdbool.h>

/** Threads whose progress keeps the watchdog fed. */
enum health_source {
	HEALTH_LINK,
	HEALTH_IMU,
	HEALTH_COUNT,
};

/**
 * @brief Report that a watched thread made progress.
 *
 * Call this from the thread's main loop. Safe from any context.
 *
 * @param src The reporting thread
 */
void health_beat(enum health_source src);

/**
 * @brief Watch a thread from now on, or stop watching it.
 *
 * @param src The thread
 * @param on True to require its heartbeat
 */
void health_watch(enum health_source src, bool on);

#endif /* ROBOT_HEALTH_H_ */
