/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>

#include "ui_common.h"
#include "weather_scenes.h"

/* Each *.rgb565 file under weather_scenes/ (raw RGB565 pixel data, no
 * header - see tools/gen_weather_scenes.py) is converted to one of these
 * byte arrays at build time by generate_inc_file_for_target() in
 * CMakeLists.txt, the same mechanism already used for the TLS CA
 * certificate in weather_cert/isrg_root_x1.h. Letting the compiler pack
 * the header's bitfields via normal designated-initializer syntax avoids
 * hand-packing them (and getting bit order/endianness wrong) in Python.
 */
#define WEATHER_SCENE_STRIDE (UI_SCREEN_WIDTH * 2)

static const uint8_t scene_clear_map[] = {
#include "scene_clear.rgb565.inc"
};
static const uint8_t scene_partly_cloudy_map[] = {
#include "scene_partly_cloudy.rgb565.inc"
};
static const uint8_t scene_fog_map[] = {
#include "scene_fog.rgb565.inc"
};
static const uint8_t scene_rain_map[] = {
#include "scene_rain.rgb565.inc"
};
static const uint8_t scene_snow_map[] = {
#include "scene_snow.rgb565.inc"
};
static const uint8_t scene_showers_map[] = {
#include "scene_showers.rgb565.inc"
};
static const uint8_t scene_thunderstorm_map[] = {
#include "scene_thunderstorm.rgb565.inc"
};
static const uint8_t scene_unknown_map[] = {
#include "scene_unknown.rgb565.inc"
};

#define SCENE_IMAGE(map_array) \
    { \
        .header = { \
            .magic = LV_IMAGE_HEADER_MAGIC, \
            .cf = LV_COLOR_FORMAT_RGB565, \
            .w = UI_SCREEN_WIDTH, \
            .h = UI_SCREEN_HEIGHT, \
            .stride = WEATHER_SCENE_STRIDE, \
        }, \
        .data_size = sizeof(map_array), \
        .data = map_array, \
    }

static const lv_image_dsc_t scene_images[] = {
    [WEATHER_SCENE_CLEAR] = SCENE_IMAGE(scene_clear_map),
    [WEATHER_SCENE_PARTLY_CLOUDY] = SCENE_IMAGE(scene_partly_cloudy_map),
    [WEATHER_SCENE_FOG] = SCENE_IMAGE(scene_fog_map),
    [WEATHER_SCENE_RAIN] = SCENE_IMAGE(scene_rain_map),
    [WEATHER_SCENE_SNOW] = SCENE_IMAGE(scene_snow_map),
    [WEATHER_SCENE_SHOWERS] = SCENE_IMAGE(scene_showers_map),
    [WEATHER_SCENE_THUNDERSTORM] = SCENE_IMAGE(scene_thunderstorm_map),
    [WEATHER_SCENE_UNKNOWN] = SCENE_IMAGE(scene_unknown_map),
};

const lv_image_dsc_t *weather_scene_image(enum weather_scene scene)
{
    if ((size_t)scene >= ARRAY_SIZE(scene_images)) {
        scene = WEATHER_SCENE_UNKNOWN;
    }

    return &scene_images[scene];
}
