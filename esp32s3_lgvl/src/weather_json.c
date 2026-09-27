/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/sys/util.h>

#include "weather_json.h"

/* Only one geocoding result is ever requested (count=1 in the API URL),
 * so the array only needs room for one.
 */
#define GEOCODE_MAX_RESULTS 1U

struct forecast_current {
	const char *time;
	double temperature_2m;
	int32_t weather_code;
};

struct forecast_response {
	double latitude;
	double longitude;
	struct forecast_current current;
};

static const struct json_obj_descr forecast_current_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct forecast_current, time, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct forecast_current, temperature_2m, JSON_TOK_DOUBLE_FP),
	JSON_OBJ_DESCR_PRIM(struct forecast_current, weather_code, JSON_TOK_NUMBER),
};

static const struct json_obj_descr forecast_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct forecast_response, latitude, JSON_TOK_DOUBLE_FP),
	JSON_OBJ_DESCR_PRIM(struct forecast_response, longitude, JSON_TOK_DOUBLE_FP),
	JSON_OBJ_DESCR_OBJECT(struct forecast_response, current, forecast_current_descr),
};

/* Index of "current" within forecast_descr: the only field this app
 * actually requires (latitude/longitude at the top level are redundant
 * with what was already sent in the request and aren't used).
 */
#define FORECAST_CURRENT_BIT BIT(2)

struct geocode_result {
	const char *name;
	double latitude;
	double longitude;
};

struct geocode_response {
	struct geocode_result results[GEOCODE_MAX_RESULTS];
	size_t num_results;
};

static const struct json_obj_descr geocode_result_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct geocode_result, name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct geocode_result, latitude, JSON_TOK_DOUBLE_FP),
	JSON_OBJ_DESCR_PRIM(struct geocode_result, longitude, JSON_TOK_DOUBLE_FP),
};

static const struct json_obj_descr geocode_descr[] = {
	JSON_OBJ_DESCR_OBJ_ARRAY(struct geocode_response, results, GEOCODE_MAX_RESULTS,
				 num_results, geocode_result_descr,
				 ARRAY_SIZE(geocode_result_descr)),
};

bool weather_parse_forecast_json(char *buf, size_t len, double *out_temp_c,
				  int32_t *out_weather_code)
{
	/* json_obj_parse() only writes fields actually present in the
	 * document; a field silently omitted from a nested object isn't
	 * distinguishable from "parsed successfully" via the returned
	 * bitmap alone (that bitmap only covers top-level descriptors), so
	 * pre-fill with sentinels no real API response would produce and
	 * check them explicitly below.
	 */
	struct forecast_response resp = {
		.current = {
			.temperature_2m = NAN,
			.weather_code = -1,
		},
	};

	int64_t ret = json_obj_parse(buf, len, forecast_descr, ARRAY_SIZE(forecast_descr), &resp);

	if (ret < 0) {
		return false;
	}

	if ((ret & FORECAST_CURRENT_BIT) == 0) {
		return false;
	}

	if (isnan(resp.current.temperature_2m) || resp.current.weather_code < 0) {
		return false;
	}

	*out_temp_c = resp.current.temperature_2m;
	*out_weather_code = resp.current.weather_code;

	return true;
}

bool weather_parse_geocode_json(char *buf, size_t len, double *out_lat, double *out_lon,
				 char *out_city, size_t city_buf_len)
{
	struct geocode_response resp = {
		.results[0] = {
			.latitude = NAN,
			.longitude = NAN,
		},
	};

	int64_t ret = json_obj_parse(buf, len, geocode_descr, ARRAY_SIZE(geocode_descr), &resp);

	if (ret < 0) {
		return false;
	}

	if (resp.num_results == 0U) {
		return false;
	}

	const struct geocode_result *first = &resp.results[0];

	if (first->name == NULL || isnan(first->latitude) || isnan(first->longitude)) {
		return false;
	}

	*out_lat = first->latitude;
	*out_lon = first->longitude;

	size_t name_len = strlen(first->name);
	size_t copy_len = MIN(name_len, city_buf_len - 1U);

	memcpy(out_city, first->name, copy_len);
	out_city[copy_len] = '\0';

	return true;
}
