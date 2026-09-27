/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "weather_json.h"

ZTEST_SUITE(weather_json, NULL, NULL, NULL, NULL, NULL);

ZTEST(weather_json, test_forecast_valid)
{
	char buf[] = "{\"latitude\":51.5,\"longitude\":-0.1,"
		     "\"current\":{\"time\":\"2024-01-01T00:00\","
		     "\"temperature_2m\":8.3,\"weather_code\":3}}";
	double temp_c = 0.0;
	int32_t code = -1;

	zassert_true(weather_parse_forecast_json(buf, strlen(buf), &temp_c, &code),
		     "a well-formed forecast response must parse");
	zassert_within(temp_c, 8.3, 0.001, "temperature must match the response");
	zassert_equal(code, 3, "weather code must match the response");
}

ZTEST(weather_json, test_forecast_missing_current)
{
	char buf[] = "{\"latitude\":51.5,\"longitude\":-0.1}";
	double temp_c = 0.0;
	int32_t code = -1;

	zassert_false(weather_parse_forecast_json(buf, strlen(buf), &temp_c, &code),
		      "a response missing \"current\" entirely must fail closed");
}

ZTEST(weather_json, test_forecast_malformed_json)
{
	char buf[] = "{\"latitude\":51.5, this is not valid json";
	double temp_c = 0.0;
	int32_t code = -1;

	zassert_false(weather_parse_forecast_json(buf, strlen(buf), &temp_c, &code),
		      "malformed JSON must fail closed, not crash or return garbage");
}

ZTEST(weather_json, test_geocode_valid)
{
	char buf[] = "{\"results\":[{\"id\":123,\"name\":\"London\","
		     "\"latitude\":51.5,\"longitude\":-0.1,"
		     "\"country\":\"United Kingdom\"}]}";
	double lat = 0.0;
	double lon = 0.0;
	char city[32];

	zassert_true(weather_parse_geocode_json(buf, strlen(buf), &lat, &lon, city, sizeof(city)),
		     "a well-formed geocoding response must parse");
	zassert_within(lat, 51.5, 0.001, "latitude must match the response");
	zassert_within(lon, -0.1, 0.001, "longitude must match the response");
	zassert_str_equal(city, "London", "city name must match the response");
}

ZTEST(weather_json, test_geocode_no_results)
{
	char buf[] = "{\"results\":[]}";
	double lat = 0.0;
	double lon = 0.0;
	char city[32];

	zassert_false(weather_parse_geocode_json(buf, strlen(buf), &lat, &lon, city, sizeof(city)),
		      "an empty results array (city not found) must fail closed");
}

ZTEST(weather_json, test_geocode_malformed_json)
{
	char buf[] = "not json at all";
	double lat = 0.0;
	double lon = 0.0;
	char city[32];

	zassert_false(weather_parse_geocode_json(buf, strlen(buf), &lat, &lon, city, sizeof(city)),
		      "malformed JSON must fail closed, not crash or return garbage");
}
