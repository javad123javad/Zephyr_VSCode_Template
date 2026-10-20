/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>

#include "drive.h"

LOG_MODULE_REGISTER(web, LOG_LEVEL_INF);

/*
 * A drive command from the page only runs this long. The page repeats it
 * every 150 ms while a key is held and sends "stop" on release, so this only
 * bounds how long the robot keeps moving after the connection drops. It
 * leaves room for a few delayed or lost packets.
 */
#define WEB_DRIVE_MS 700U

static const uint8_t index_html_gz[] = {
#include "index.html.gz.inc"
};

static struct http_resource_detail_static index_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_STATIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_encoding = "gzip",
		.content_type = "text/html",
	},
	.static_data = index_html_gz,
	.static_data_len = sizeof(index_html_gz),
};

/* Parse "<throttle> <turn> <speed>", for example "1 -1 80". */
static int parse_command(char *body, int32_t *throttle, int32_t *turn, int32_t *speed)
{
	long val[3];
	char *pos = body;

	for (size_t i = 0U; i < ARRAY_SIZE(val); i++) {
		char *end;

		val[i] = strtol(pos, &end, 10);
		if (end == pos) {
			return -EINVAL;
		}
		pos = end;
	}

	*throttle = (int32_t)val[0];
	*turn = (int32_t)val[1];
	*speed = (int32_t)val[2];

	return 0;
}

static int drive_handler(struct http_client_ctx *client, enum http_transaction_status status,
			 const struct http_request_ctx *request_ctx,
			 struct http_response_ctx *response_ctx, void *user_data)
{
	static char body[16];
	static size_t len;
	int32_t throttle;
	int32_t turn;
	int32_t speed;

	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if ((status == HTTP_SERVER_TRANSACTION_ABORTED) ||
	    (status == HTTP_SERVER_TRANSACTION_COMPLETE)) {
		len = 0U;
		return 0;
	}

	/* Keep one byte for the terminator. */
	if ((len + request_ctx->data_len) >= sizeof(body)) {
		len = 0U;
		return -ENOMEM;
	}

	memcpy(&body[len], request_ctx->data, request_ctx->data_len);
	len += request_ctx->data_len;

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	body[len] = '\0';

	if (parse_command(body, &throttle, &turn, &speed) == 0) {
		drive_mix(throttle, turn, speed, WEB_DRIVE_MS);
		response_ctx->status = HTTP_204_NO_CONTENT;
	} else {
		response_ctx->status = HTTP_400_BAD_REQUEST;
	}

	response_ctx->final_chunk = true;
	len = 0U;

	return 0;
}

static struct http_resource_detail_dynamic drive_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = drive_handler,
};

static uint16_t robot_http_port = CONFIG_ROBOT_HTTP_PORT;

HTTP_SERVICE_DEFINE(robot_http, NULL, &robot_http_port, CONFIG_HTTP_SERVER_MAX_CLIENTS, 4, NULL,
		    NULL, NULL);

HTTP_RESOURCE_DEFINE(index_resource, robot_http, "/", &index_resource_detail);
HTTP_RESOURCE_DEFINE(drive_resource, robot_http, "/drive", &drive_resource_detail);
