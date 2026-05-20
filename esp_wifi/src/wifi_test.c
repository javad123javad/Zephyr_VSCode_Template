/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <errno.h>

#define WIFI_SSID     "Proximus-Home-E9C0"
#define WIFI_PASSWORD "wdju9h4ce23b7"

#define HTTP_HOST    "httpbin.org"
#define HTTP_IP      "18.233.255.213"
#define HTTP_PORT    80
#define HTTP_REQUEST "GET /get HTTP/1.0\r\nHost: " HTTP_HOST "\r\nConnection: close\r\n\r\n"

static K_SEM_DEFINE(net_ready, 0, 1);
static struct net_mgmt_event_callback if_cb;

static void if_event_handler(struct net_mgmt_event_callback *cb,
                             uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_IF_UP) {
		k_sem_give(&net_ready);
	}
}

static int wifi_connect(struct net_if *iface)
{
	struct wifi_connect_req_params params = {
		.ssid        = WIFI_SSID,
		.ssid_length = strlen(WIFI_SSID),
		.psk         = WIFI_PASSWORD,
		.psk_length  = strlen(WIFI_PASSWORD),
		.security    = WIFI_SECURITY_TYPE_PSK,
		.channel     = WIFI_CHANNEL_ANY,
		.band        = WIFI_FREQ_BAND_2_4_GHZ,
	};

	return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
}

static int http_get(void)
{
	int sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (sock < 0) {
		printk("socket() failed: %d\n", errno);
		return -1;
	}

	struct ifreq ifr = {0};

	strncpy(ifr.ifr_name, "wlan0", sizeof(ifr.ifr_name));
	zsock_setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));

	struct zsock_timeval timeout = {.tv_sec = 15};

	zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	zsock_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

	struct sockaddr_in addr = {0};

	addr.sin_family = AF_INET;
	addr.sin_port   = htons(HTTP_PORT);
	zsock_inet_pton(AF_INET, HTTP_IP, &addr.sin_addr);

	printk("Connecting to %s:%d...\n", HTTP_HOST, HTTP_PORT);
	if (zsock_connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printk("Connect failed: errno=%d\n", errno);
		zsock_close(sock);
		return -1;
	}
	printk("Connected!\n");

	int sent = zsock_send(sock, HTTP_REQUEST, strlen(HTTP_REQUEST), 0);

	if (sent < 0) {
		printk("Send failed: errno=%d\n", errno);
		zsock_close(sock);
		return -1;
	}
	printk("Request sent (%d bytes)\n", sent);

	printk("--- Response ---\n");
	char buf[256];
	int total = 0;
	int len;

	while ((len = zsock_recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
		buf[len] = '\0';
		printk("%s", buf);
		total += len;
	}

	printk("\n--- End (%d bytes) ---\n", total);
	zsock_close(sock);
	return 0;
}

int main(void)
{
	struct net_if *iface = net_if_get_first_wifi();

	printk("Interface name: %s\n", net_if_get_device(iface)->name);

	net_mgmt_init_event_callback(&if_cb, if_event_handler, NET_EVENT_IF_UP);
	net_mgmt_add_event_callback(&if_cb);

	if (net_if_is_up(iface)) {
		k_sem_give(&net_ready);
	} else {
		wifi_connect(iface);
	}

	printk("Waiting for network...\n");
	k_sem_take(&net_ready, K_FOREVER);
	k_sleep(K_SECONDS(2));

	return http_get();
}
