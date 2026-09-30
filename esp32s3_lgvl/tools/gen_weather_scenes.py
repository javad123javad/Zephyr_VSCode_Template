#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Regenerates Home's illustrated weather-scene backgrounds.

Draws the same shapes/coordinates the LVGL-primitive version of this
scene used (before it was replaced by pre-rendered bitmaps - see AD-7 in
docs/ARCHITECTURE.md and the commit that added this script), rasterizes
each of the 8 weather_scene compositions to raw RGB565 pixel data, and
writes each as a binary blob under src/ui/weather_scenes/. Those blobs are
converted to C byte arrays at build time (generate_inc_file_for_target()
in CMakeLists.txt, the same mechanism already used for the TLS CA
certificate), so this script and its Pillow dependency are needed only
when *editing* the art, never for a normal `west build`.

Usage: python3 tools/gen_weather_scenes.py
Requires: pip install pillow
"""
import struct
from pathlib import Path

from PIL import Image, ImageDraw

SCREEN_W = 480
SCREEN_H = 320

OUT_DIR = Path(__file__).resolve().parent.parent / "src" / "ui" / "weather_scenes"

# Mirrors ui_palette.c's scene_sky_colors[] exactly.
SKY_COLORS = {
    "clear": (0x1E88E5, 0x64B5F6),
    "partly_cloudy": (0x4A6FA5, 0x8FA8C7),
    "fog": (0x8B95A1, 0xC7CDD3),
    "rain": (0x37474F, 0x546E7A),
    "snow": (0x6E7F8D, 0xE3EEF5),
    "showers": (0x455A64, 0x78909C),
    "thunderstorm": (0x1A1A2E, 0x38304A),
    "unknown": (0x121212, 0x1E1E1E),
}

GOLD = (0xFF, 0xD5, 0x4F)
CLOUD_LIGHT = (0xFF, 0xFF, 0xFF)
CLOUD_GREY = (0xCF, 0xD8, 0xDC)
CLOUD_DARK = (0x78, 0x90, 0x9C)
CLOUD_STORM = (0x37, 0x47, 0x4F)
CLOUD_STORM_2 = (0x45, 0x5A, 0x64)
RAIN_COLOR = (0xB3, 0xE5, 0xFC)
LIGHTNING_COLOR = (0xFF, 0xF1, 0x76)
SNOW_WHITE = (0xFF, 0xFF, 0xFF)

OPA_20, OPA_30, OPA_40, OPA_50, OPA_70, OPA_80, OPA_90, OPA_COVER = (
    51, 76, 102, 127, 178, 204, 229, 255,
)

RAIN_X = [90, 140, 190, 240, 290, 340, 390]
SNOW_X = [70, 110, 150, 190, 230, 270, 310, 350, 390]


def hex_to_rgb(value):
    return (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF


def draw_gradient(base, top_hex, bottom_hex):
    top = hex_to_rgb(top_hex)
    bottom = hex_to_rgb(bottom_hex)
    draw = ImageDraw.Draw(base)
    for y in range(SCREEN_H):
        t = y / (SCREEN_H - 1)
        row = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        draw.line([(0, y), (SCREEN_W - 1, y)], fill=row)


def blob(base, w, h, x, y, color, opa):
    """One filled rounded-rect/circle blended at `opa` over `base` -
    matches lv_obj_create() + LV_RADIUS_CIRCLE + bg_opa in ui_home.c's
    add_blob(): square -> circle, non-square -> a "stadium" (radius
    clamped to half the shorter side), not a squashed ellipse.
    """
    layer = Image.new("RGBA", base.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    radius = min(w, h) // 2
    draw.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=radius,
                            fill=(*color, opa))
    base.alpha_composite(layer)


def cloud(base, x, y, w, color, opa):
    """Mirrors add_cloud(): four puff circles plus a full-width base
    capsule, drawn in the same order (base last, blending over the two
    side puffs) as the original LVGL version.
    """
    h = w // 2
    blob(base, (h * 3) // 2, (h * 3) // 2, x + h // 2, y, color, opa)
    blob(base, (h * 5) // 4, (h * 5) // 4, x + h * 3 // 2, y + h // 4, color, opa)
    blob(base, h, h, x, y + h // 2, color, opa)
    blob(base, h, h, x + w - h, y + h // 2, color, opa)
    blob(base, w, h, x, y + h // 2, color, opa)


def sun(base, cx, cy, d):
    blob(base, d * 2, d * 2, cx - d, cy - d, GOLD, OPA_20)
    blob(base, (d * 3) // 2, (d * 3) // 2, cx - d * 3 // 4, cy - d * 3 // 4, GOLD, OPA_30)
    blob(base, d, d, cx - d // 2, cy - d // 2, GOLD, OPA_COVER)


def rain(base, x, y):
    blob(base, 4, 22, x, y, RAIN_COLOR, OPA_70)


def snow_dot(base, x, y, d):
    blob(base, d, d, x, y, SNOW_WHITE, OPA_90)


def lightning(base, x, y):
    points = [(x + px, y + py) for px, py in ((0, 0), (14, 0), (4, 20), (16, 20), (2, 46))]
    layer = Image.new("RGBA", base.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    draw.line(points, fill=(*LIGHTNING_COLOR, OPA_COVER), width=4, joint="curve")
    base.alpha_composite(layer)


def populate_clear(base):
    sun(base, 110, 70, 90)


def populate_partly_cloudy(base):
    sun(base, 90, 60, 70)
    cloud(base, 130, 60, 170, CLOUD_LIGHT, OPA_90)
    cloud(base, 30, 120, 120, CLOUD_LIGHT, OPA_70)


def populate_fog(base):
    cloud(base, -20, 50, 220, CLOUD_GREY, OPA_50)
    cloud(base, 190, 90, 240, CLOUD_GREY, OPA_40)
    cloud(base, 50, 150, 280, CLOUD_GREY, OPA_50)


def populate_rain(base):
    cloud(base, 50, 30, 240, CLOUD_DARK, OPA_90)
    cloud(base, 250, 60, 190, CLOUD_GREY, OPA_80)
    for i, x in enumerate(RAIN_X):
        rain(base, x, 150 + (i % 3) * 12)


def populate_snow(base):
    cloud(base, 70, 30, 210, CLOUD_GREY, OPA_90)
    cloud(base, 260, 50, 190, CLOUD_LIGHT, OPA_80)
    for i, x in enumerate(SNOW_X):
        snow_dot(base, x, 150 + (i % 4) * 20, 6 + (i % 3) * 2)


def populate_showers(base):
    sun(base, 390, 55, 60)
    cloud(base, 70, 40, 230, CLOUD_GREY, OPA_90)
    for i in range(5):
        rain(base, RAIN_X[i] - 10, 150 + (i % 2) * 14)


def populate_thunderstorm(base):
    cloud(base, 50, 20, 250, CLOUD_STORM, OPA_COVER)
    cloud(base, 250, 40, 210, CLOUD_STORM_2, OPA_COVER)
    lightning(base, 200, 110)
    lightning(base, 300, 120)
    for i in range(4):
        rain(base, RAIN_X[i] * 3 // 2 - 40, 160)


def populate_unknown(base):
    pass


POPULATE = {
    "clear": populate_clear,
    "partly_cloudy": populate_partly_cloudy,
    "fog": populate_fog,
    "rain": populate_rain,
    "snow": populate_snow,
    "showers": populate_showers,
    "thunderstorm": populate_thunderstorm,
    "unknown": populate_unknown,
}


def to_rgb565_bytes(image):
    out = bytearray(SCREEN_W * SCREEN_H * 2)
    pixels = image.load()
    i = 0
    for y in range(SCREEN_H):
        for x in range(SCREEN_W):
            r, g, b = pixels[x, y]
            value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            struct.pack_into("<H", out, i, value)
            i += 2
    return bytes(out)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, (top, bottom) in SKY_COLORS.items():
        base = Image.new("RGBA", (SCREEN_W, SCREEN_H), (0, 0, 0, 255))
        draw_gradient(base, top, bottom)
        POPULATE[name](base)
        data = to_rgb565_bytes(base.convert("RGB"))
        out_path = OUT_DIR / f"scene_{name}.rgb565"
        out_path.write_bytes(data)
        print(f"wrote {out_path} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
