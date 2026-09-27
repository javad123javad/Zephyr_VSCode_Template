/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/net_compat.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(weather, LOG_LEVEL_INF);

#include "weather.h"
#include "weather_json.h"
#include "weather_cert/isrg_root_x1.h"

#define WEATHER_HOST_FORECAST "api.open-meteo.com"
#define WEATHER_HOST_GEOCODE  "geocoding-api.open-meteo.com"
#define WEATHER_HTTPS_PORT    "443"

/* How often the cached forecast is refreshed, and how long a failed
 * geocode/forecast attempt waits before automatically retrying (AD-4:
 * "every 15-30 minutes").
 */
#define WEATHER_POLL_PERIOD_S 900U

#define WEATHER_HTTP_TIMEOUT_MS   10000
#define WEATHER_HTTP_RECV_BUF_LEN 512U
#define WEATHER_BODY_BUF_LEN      1024U
#define WEATHER_URL_BUF_LEN       160U

struct weather_location {
    double lat;
    double lon;
    char city[WEATHER_CITY_NAME_LEN];
};

static struct k_mutex weather_lock;
static enum weather_status status = WEATHER_STATUS_NO_CITY;
static struct weather_location loc;
static bool pending_geocode;
static char pending_city[WEATHER_CITY_NAME_LEN];
static double cached_temp_c;
static int32_t cached_weather_code = -1;

/* < 0 means "never attempted yet"; forces an immediate attempt. */
static int64_t last_forecast_attempt_ms = -1;
static int64_t last_geocode_attempt_ms = -1;

static uint8_t http_recv_buf[WEATHER_HTTP_RECV_BUF_LEN];

static void save_location(void)
{
    int ret = settings_save_one("weather/loc", &loc, sizeof(loc));

    if (ret != 0) {
        LOG_ERR("Failed to save weather location: %d", ret);
    }
}

static int weather_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                 void *cb_arg)
{
    if (settings_name_steq(name, "loc", NULL) && len == sizeof(loc)) {
        read_cb(cb_arg, &loc, sizeof(loc));
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(weather, "weather", NULL, weather_settings_set, NULL, NULL);

enum weather_status weather_get_status(void)
{
    enum weather_status s;

    k_mutex_lock(&weather_lock, K_FOREVER);
    s = status;
    k_mutex_unlock(&weather_lock);

    return s;
}

bool weather_get_temperature_c(double *out)
{
    bool have_temp;

    k_mutex_lock(&weather_lock, K_FOREVER);
    have_temp = (status == WEATHER_STATUS_OK);
    *out = cached_temp_c;
    k_mutex_unlock(&weather_lock);

    return have_temp;
}

/* WMO weather codes (used by Open-Meteo's "weather_code" field), grouped
 * into short human-readable text. Ranges per
 * https://open-meteo.com/en/docs (WMO Weather interpretation codes).
 */
const char *weather_get_condition_text(void)
{
    int32_t code;

    k_mutex_lock(&weather_lock, K_FOREVER);
    code = cached_weather_code;
    k_mutex_unlock(&weather_lock);

    if (code == 0) {
        return "Clear";
    } else if (code >= 1 && code <= 3) {
        return "Partly cloudy";
    } else if (code == 45 || code == 48) {
        return "Fog";
    } else if (code >= 51 && code <= 67) {
        return "Rain";
    } else if (code >= 71 && code <= 77) {
        return "Snow";
    } else if (code >= 80 && code <= 82) {
        return "Showers";
    } else if (code >= 95 && code <= 99) {
        return "Thunderstorm";
    }

    return "Unknown";
}

const char *weather_get_city_name(void)
{
    return loc.city;
}

void weather_init(void)
{
    k_mutex_init(&weather_lock);

    settings_subsys_init();
    settings_load_subtree("weather");

    if (loc.city[0] != '\0') {
        status = WEATHER_STATUS_PENDING;
    }

    int ret = tls_credential_add(WEATHER_TLS_CA_CERT_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
                      weather_ca_certificate, sizeof(weather_ca_certificate));

    if (ret != 0) {
        LOG_ERR("Failed to register weather CA certificate: %d", ret);
    }
}

void weather_set_city(const char *name, size_t len)
{
    size_t copy_len = MIN(len, sizeof(pending_city) - 1U);

    k_mutex_lock(&weather_lock, K_FOREVER);
    memcpy(pending_city, name, copy_len);
    pending_city[copy_len] = '\0';
    pending_geocode = true;
    status = WEATHER_STATUS_PENDING;
    k_mutex_unlock(&weather_lock);

    /* Force an immediate attempt rather than waiting out whatever is
     * left of the previous retry backoff.
     */
    last_geocode_attempt_ms = -1;
}

/* Very small subset of URL encoding: Open-Meteo's geocoding API accepts
 * a "name" query parameter, and the only character common in city names
 * that isn't already URL-safe is the space. Names with punctuation or
 * non-ASCII characters aren't handled here; a full percent-encoder is
 * more machinery than this feature's first pass needs.
 */
static void url_encode_city(const char *name, char *out, size_t out_len)
{
    size_t i;

    for (i = 0U; i < out_len - 1U && name[i] != '\0'; i++) {
        out[i] = (name[i] == ' ') ? '+' : name[i];
    }
    out[i] = '\0';
}

struct http_body_ctx {
    char *buf;
    size_t buf_len;
    size_t len;
};

static int http_body_cb(struct http_response *rsp, enum http_final_call final_data,
             void *user_data)
{
    ARG_UNUSED(final_data);

    struct http_body_ctx *ctx = user_data;
    bool have_room = ctx->len + 1U < ctx->buf_len;

    if (rsp->body_frag_start != NULL && rsp->body_frag_len > 0U && have_room) {
        size_t space = ctx->buf_len - ctx->len - 1U;
        size_t copy_len = MIN(rsp->body_frag_len, space);

        memcpy(&ctx->buf[ctx->len], rsp->body_frag_start, copy_len);
        ctx->len += copy_len;
        ctx->buf[ctx->len] = '\0';
    }

    return 0;
}

static bool https_get(const char *host, const char *url, char *body_buf, size_t body_buf_len,
               size_t *out_body_len)
{
    struct zsock_addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct zsock_addrinfo *res = NULL;
    int ret = zsock_getaddrinfo(host, WEATHER_HTTPS_PORT, &hints, &res);

    if (ret != 0 || res == NULL) {
        LOG_ERR("DNS resolve failed for %s: %d", host, ret);
        return false;
    }

    int sock = zsock_socket(res->ai_family, res->ai_socktype, IPPROTO_TLS_1_2);

    if (sock < 0) {
        LOG_ERR("socket() failed: %d", -errno);
        zsock_freeaddrinfo(res);
        return false;
    }

    sec_tag_t sec_tag_list[] = {WEATHER_TLS_CA_CERT_TAG};
    bool tls_ok = zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tag_list,
                    sizeof(sec_tag_list)) == 0 &&
              zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, host, strlen(host) + 1U) == 0;

    if (!tls_ok) {
        LOG_ERR("TLS sockopt failed: %d", -errno);
        zsock_close(sock);
        zsock_freeaddrinfo(res);
        return false;
    }

    ret = zsock_connect(sock, res->ai_addr, res->ai_addrlen);
    zsock_freeaddrinfo(res);

    if (ret < 0) {
        LOG_ERR("connect() to %s failed: %d", host, -errno);
        zsock_close(sock);
        return false;
    }

    struct http_body_ctx ctx = {.buf = body_buf, .buf_len = body_buf_len};
    struct http_request req = {
        .method = HTTP_GET,
        .url = url,
        .host = host,
        .protocol = "HTTP/1.1",
        .response = http_body_cb,
        .recv_buf = http_recv_buf,
        .recv_buf_len = sizeof(http_recv_buf),
    };

    ret = http_client_req(sock, &req, WEATHER_HTTP_TIMEOUT_MS, &ctx);
    zsock_close(sock);

    if (ret < 0) {
        LOG_ERR("HTTP request to %s failed: %d", host, ret);
        return false;
    }

    *out_body_len = ctx.len;

    return true;
}

static bool fetch_geocode(const char *city_name)
{
    char encoded_name[WEATHER_CITY_NAME_LEN];
    char url[WEATHER_URL_BUF_LEN];
    static char body[WEATHER_BODY_BUF_LEN];
    size_t body_len;

    url_encode_city(city_name, encoded_name, sizeof(encoded_name));
    snprintf(url, sizeof(url), "/v1/search?name=%s&count=1&language=en&format=json",
         encoded_name);

    if (!https_get(WEATHER_HOST_GEOCODE, url, body, sizeof(body), &body_len)) {
        return false;
    }

    double lat;
    double lon;
    char city[WEATHER_CITY_NAME_LEN];

    if (!weather_parse_geocode_json(body, body_len, &lat, &lon, city, sizeof(city))) {
        LOG_ERR("Geocoding response parse failed for \"%s\"", city_name);
        return false;
    }

    k_mutex_lock(&weather_lock, K_FOREVER);
    loc.lat = lat;
    loc.lon = lon;
    memcpy(loc.city, city, sizeof(loc.city));
    k_mutex_unlock(&weather_lock);

    LOG_INF("Geocoded \"%s\" -> %s (%.4f, %.4f)", city_name, city, lat, lon);

    return true;
}

static bool fetch_forecast(double lat, double lon)
{
    char url[WEATHER_URL_BUF_LEN];
    static char body[WEATHER_BODY_BUF_LEN];
    size_t body_len;

    snprintf(url, sizeof(url),
         "/v1/forecast?latitude=%.4f&longitude=%.4f"
         "&current=temperature_2m,weather_code&temperature_unit=celsius",
         lat, lon);

    if (!https_get(WEATHER_HOST_FORECAST, url, body, sizeof(body), &body_len)) {
        return false;
    }

    double temp_c;
    int32_t weather_code;

    if (!weather_parse_forecast_json(body, body_len, &temp_c, &weather_code)) {
        LOG_ERR("Forecast response parse failed");
        return false;
    }

    k_mutex_lock(&weather_lock, K_FOREVER);
    cached_temp_c = temp_c;
    cached_weather_code = weather_code;
    k_mutex_unlock(&weather_lock);

    LOG_INF("Forecast: %.1fC, code=%d", temp_c, weather_code);

    return true;
}

static bool due(int64_t last_attempt_ms, int64_t now_ms)
{
    return last_attempt_ms < 0 ||
           (now_ms - last_attempt_ms) >= (int64_t)WEATHER_POLL_PERIOD_S * MSEC_PER_SEC;
}

void weather_poll(void)
{
    int64_t now_ms = k_uptime_get();
    bool do_geocode;
    char city_copy[WEATHER_CITY_NAME_LEN];

    k_mutex_lock(&weather_lock, K_FOREVER);
    do_geocode = pending_geocode && due(last_geocode_attempt_ms, now_ms);
    if (do_geocode) {
        memcpy(city_copy, pending_city, sizeof(city_copy));
    }
    k_mutex_unlock(&weather_lock);

    if (do_geocode) {
        last_geocode_attempt_ms = now_ms;

        if (fetch_geocode(city_copy)) {
            k_mutex_lock(&weather_lock, K_FOREVER);
            pending_geocode = false;
            status = WEATHER_STATUS_OK;
            k_mutex_unlock(&weather_lock);

            save_location();

            /* Fetch the forecast for the newly-resolved location
             * on the very next poll instead of waiting out a
             * full period.
             */
            last_forecast_attempt_ms = -1;
        } else {
            k_mutex_lock(&weather_lock, K_FOREVER);
            status = WEATHER_STATUS_ERROR;
            k_mutex_unlock(&weather_lock);
        }
        return;
    }

    bool have_location;
    double lat;
    double lon;

    k_mutex_lock(&weather_lock, K_FOREVER);
    have_location = (loc.city[0] != '\0') && !pending_geocode;
    lat = loc.lat;
    lon = loc.lon;
    k_mutex_unlock(&weather_lock);

    if (!have_location || !due(last_forecast_attempt_ms, now_ms)) {
        return;
    }

    last_forecast_attempt_ms = now_ms;

    bool ok = fetch_forecast(lat, lon);

    k_mutex_lock(&weather_lock, K_FOREVER);
    status = ok ? WEATHER_STATUS_OK : WEATHER_STATUS_ERROR;
    k_mutex_unlock(&weather_lock);
}
