/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_WEATHER_JSON_H_
#define APP_WEATHER_JSON_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Parses an Open-Meteo forecast response body, e.g.:
 * {"latitude":51.5,"longitude":-0.1,
 *  "current":{"time":"...","temperature_2m":8.3,"weather_code":3}}
 *
 * buf is mutated in place (the JSON library null-terminates strings where
 * it finds them), so it must not be a string literal. Returns false if the
 * document is malformed or missing the fields this app actually needs.
 */
bool weather_parse_forecast_json(char *buf, size_t len, double *out_temp_c,
                  int32_t *out_weather_code);

/* Parses an Open-Meteo geocoding response body, e.g.:
 * {"results":[{"name":"London","latitude":51.5,"longitude":-0.1}]}
 *
 * Only the first result is used (the caller is expected to request
 * count=1 from the API). buf is mutated in place, same as above. Returns
 * false if the document is malformed, or if no city was found (an empty
 * "results" array is valid JSON but not a usable result).
 */
bool weather_parse_geocode_json(char *buf, size_t len, double *out_lat, double *out_lon,
                 char *out_city, size_t city_buf_len);

#endif /* APP_WEATHER_JSON_H_ */
