/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ROBOT_WS_H_
#define ROBOT_WS_H_

#include <zephyr/net/http/server.h>

/**
 * @brief Take over a new WebSocket connection as the control session.
 *
 * Matches http_resource_websocket_cb_t. A newer connection replaces the
 * current one, so a client that reconnects is not locked out by a stale
 * session.
 *
 * @retval 0 Connection accepted
 */
int ws_control_setup(int ws_socket, struct http_request_ctx *request_ctx, void *user_data);

#endif /* ROBOT_WS_H_ */
