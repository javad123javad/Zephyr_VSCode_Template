/* main.c - Application main entry point */

/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bps.h>   /* ← our service, same as hrs.h in peripheral_hr */

/* -----------------------------------------------------------------------
 * Advertising data
 * --------------------------------------------------------------------- */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL,
    BT_UUID_16_ENCODE(BT_UUID_BPS_VAL)),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
    sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* -----------------------------------------------------------------------
 * State machine
 * --------------------------------------------------------------------- */
enum {
    STATE_CONNECTED,
    STATE_DISCONNECTED,

    STATE_BITS,
};

static ATOMIC_DEFINE(state, STATE_BITS);

/* -----------------------------------------------------------------------
 * Connection callbacks
 * --------------------------------------------------------------------- */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed, err 0x%02x %s\n", err,
               bt_hci_err_to_str(err));
    } else {
        printk("Connected\n");
        (void)atomic_set_bit(state, STATE_CONNECTED);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected, reason 0x%02x %s\n", reason,
           bt_hci_err_to_str(reason));

    (void)atomic_set_bit(state, STATE_DISCONNECTED);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected    = connected,
    .disconnected = disconnected,
};

/* -----------------------------------------------------------------------
 * BPS service callbacks
 * --------------------------------------------------------------------- */
static void bps_ind_changed(bool enabled)
{
    printk("BPS indication status changed: %s\n",
           enabled ? "enabled" : "disabled");
}

static struct bt_bps_cb bps_cb = {
    .ind_changed = bps_ind_changed,
};

/* -----------------------------------------------------------------------
 * Auth callbacks
 * --------------------------------------------------------------------- */
static void auth_cancel(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
    .cancel = auth_cancel,
};

/* -----------------------------------------------------------------------
 * Simulated BP readings
 * --------------------------------------------------------------------- */
static void bps_notify(void)
{
    static uint8_t idx;
    static const struct {
        uint16_t systolic;
        uint16_t diastolic;
        uint16_t map;
        uint16_t pulse;
    } bp_table[] = {
        { 120, 80,  93, 72 },
        { 122, 81,  95, 74 },
        { 118, 79,  92, 70 },
        { 135, 88, 104, 80 },
        { 115, 75,  88, 68 },
    };

    uint8_t i = idx % ARRAY_SIZE(bp_table);

    printk("BP: %u/%u mmHg  MAP %u  Pulse %u bpm\n",
           bp_table[i].systolic, bp_table[i].diastolic,
           bp_table[i].map,      bp_table[i].pulse);

    struct bt_bps_measurement meas = {
        .systolic           = bt_bps_sfloat_encode(bp_table[i].systolic, 0),
        .diastolic          = bt_bps_sfloat_encode(bp_table[i].diastolic, 0),
        .map                = bt_bps_sfloat_encode(bp_table[i].map, 0),
        .pulse_rate         = bt_bps_sfloat_encode(bp_table[i].pulse, 0),
        .pulse_rate_present = true,
        .unit_kpa           = false,
    };

    int err = bt_bps_indicate(&meas);

    if (err && err != -EACCES) {
        printk("BPS indicate error %d\n", err);
    }

    idx++;
}

#if defined(CONFIG_GPIO)
#define LED0_NODE DT_ALIAS(led0)
#if DT_NODE_HAS_STATUS_OKAY(LED0_NODE)
#include <zephyr/drivers/gpio.h>
#define HAS_LED     1
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#define BLINK_ONOFF K_MSEC(500)

static struct k_work_delayable blink_work;
static bool led_is_on;

static void blink_timeout(struct k_work *work)
{
    led_is_on = !led_is_on;
    gpio_pin_set(led.port, led.pin, (int)led_is_on);
    k_work_schedule(&blink_work, BLINK_ONOFF);
}

static int blink_setup(void)
{
    int err;

    printk("Checking LED device...");
    if (!gpio_is_ready_dt(&led)) {
        printk("failed.\n");
        return -EIO;
    }
    printk("done.\n");

    printk("Configuring GPIO pin...");
    err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (err) {
        printk("failed.\n");
        return -EIO;
    }
    printk("done.\n");

    k_work_init_delayable(&blink_work, blink_timeout);
    return 0;
}

static void blink_start(void)
{
    printk("Start blinking LED...\n");
    led_is_on = false;
    gpio_pin_set(led.port, led.pin, (int)led_is_on);
    k_work_schedule(&blink_work, BLINK_ONOFF);
}

static void blink_stop(void)
{
    struct k_work_sync work_sync;

    printk("Stop blinking LED.\n");
    k_work_cancel_delayable_sync(&blink_work, &work_sync);
    led_is_on = true;
    gpio_pin_set(led.port, led.pin, (int)led_is_on);
}
#endif /* LED0_NODE */
#endif /* CONFIG_GPIO */

/* -----------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */
int main(void)
{
    int err;

    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    printk("Bluetooth initialized\n");

    bt_conn_auth_cb_register(&auth_cb_display);

    if (bt_bps_cb_register(&bps_cb)) {
        printk("Failed to register BPS callbacks\n");
        return 0;
    }   /* ← same pattern as bt_hrs_cb_register() */

    printk("Starting Legacy Advertising (connectable and scannable)\n");
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
                          sd, ARRAY_SIZE(sd));
    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return 0;
    }



    printk("Advertising successfully started\n");

#if defined(HAS_LED)
    err = blink_setup();
    if (err) {
        return 0;
    }
    blink_start();
#endif /* HAS_LED */
    bool is_connected = false;
    while (1) {
        k_sleep(K_SECONDS(3));


        if (atomic_test_and_clear_bit(state, STATE_CONNECTED)) {
            is_connected = true;
#if defined(HAS_LED)
            blink_stop();
#endif /* HAS_LED */
        } else if (atomic_test_and_clear_bit(state, STATE_DISCONNECTED)) {
            printk("Starting Legacy Advertising (connectable and scannable)\n");
            err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad,
                                  ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
            is_connected = false;
            if (err) {
                printk("Advertising failed to start (err %d)\n", err);
                return 0;
            }


#if defined(HAS_LED)
            blink_start();
#endif /* HAS_LED */
        }
        if(is_connected)
        {
            bps_notify();

        }
    }

    return 0;
}