/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ROBOT_WEB_H_
#define ROBOT_WEB_H_

/**
 * @brief Execute a drive command from the web page.
 *
 * The command is "<throttle> <turn> <speed>", for example "1 -1 80", and
 * runs for a short time unless it is repeated. "reset" clears latched drive
 * inhibits such as a detected collision.
 *
 * @param cmd NUL-terminated command text
 *
 * @retval 0 Success
 * @retval -EINVAL Malformed command
 */
int web_command(char *cmd);

#endif /* ROBOT_WEB_H_ */
