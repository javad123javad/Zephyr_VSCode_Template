/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zephyr/kernel.h"
#include <zephyr/net/socket.h>

#define  SERVER_IP "8.8.8.8"
#define SERVER_PORT 53
#define HTTP_HOST     "httpbin.org"
#define HTTP_IP       "18.233.255.213"   /* httpbin.org */
#define HTTP_PORT     80
#define HTTP_REQUEST  "GET /get HTTP/1.0\r\nHost: " HTTP_HOST "\r\nConnection: close\r\n\r\n"


int sample_socket()
{
        int sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) {
                printk("socket() failed: %d\n", errno);
                return -1;
        }
        k_msleep(1000);
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(SERVER_PORT);
        zsock_inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

        printk("Connecting to %s:%d\n", SERVER_IP, SERVER_PORT);

        int ret = zsock_connect(sock, (struct sockaddr *)&addr, sizeof(addr));
        if (ret < 0) {
                printk("FAILED: errno=%d\n", errno);
                zsock_close(sock);
                return -1;
        }

        printk("SUCCESS! TCP connection works!\n");
}

int http_get(){
        /* Create socket bound to wlan0 */
        int sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) {
                printk("socket() failed: %d\n", errno);
                return -1;
        }

        struct ifreq ifr = {0};
        strncpy(ifr.ifr_name, "wlan0", sizeof(ifr.ifr_name));
        zsock_setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));

        struct zsock_timeval timeout = { .tv_sec = 15 };
        zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        zsock_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        /* Connect */
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

        /* Send HTTP GET */
        int sent = zsock_send(sock, HTTP_REQUEST, strlen(HTTP_REQUEST), 0);
        if (sent < 0) {
                printk("Send failed: errno=%d\n", errno);
                zsock_close(sock);
                return -1;
        }
        printk("Request sent (%d bytes)\n", sent);

        /* Read response */
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
        k_msleep(15000);
        int idx = 5;
        while(idx > 0){
                //       idx--;
                //sample_socket();
                http_get();
                k_msleep(1000);

        }
        return 0;
}
