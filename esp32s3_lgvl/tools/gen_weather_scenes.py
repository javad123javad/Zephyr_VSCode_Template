#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Regenerates Home's weather-scene backgrounds from real photographs.

Downloads one real photo per weather_scene (see SCENES below - each entry
records its exact source and license), center-crops it to the panel's
480x320 aspect ratio, and converts it to raw RGB565 pixel data under
src/ui/weather_scenes/. Those blobs are converted to C byte arrays at
build time (generate_inc_file_for_target() in CMakeLists.txt, the same
mechanism already used for the TLS CA certificate), so this script and
its Pillow dependency are needed only when *changing* the art, never for
a normal `west build`.

This is deliberately real photography, not drawn/illustrated art - see
the commit that introduced this version and docs/ARCHITECTURE.md's AD-7.
Three of the eight sources are CC BY / CC BY-SA (attribution and, for the
two CC BY-SA ones, share-alike terms apply to the derived crop) - see
src/ui/weather_scenes/CREDITS.md, which this script does not generate
and which must stay in sync with SCENES below by hand.

Usage: python3 tools/gen_weather_scenes.py
Requires: pip install pillow
"""
import io
import struct
import urllib.request
from pathlib import Path

from PIL import Image

SCREEN_W = 480
SCREEN_H = 320

OUT_DIR = Path(__file__).resolve().parent.parent / "src" / "ui" / "weather_scenes"

USER_AGENT = "esp32s3_lgvl-weather-scene-fetch/1.0 (personal hobby project)"

# One real photo per weather_scene. Every url is a Wikimedia Commons
# "Original file" link (stable, versioned). license/author are the exact
# values from each file's page at the time it was picked - re-verify
# against the source url if this script is ever re-run long after.
SCENES = {
    "clear": {
        "url": "https://upload.wikimedia.org/wikipedia/commons/d/d8/Clear_Blue_Sky.jpg",
        "source": "https://commons.wikimedia.org/wiki/File:Clear_Blue_Sky.jpg",
        "author": "Perumalnadar",
        "license": "CC0 1.0",
    },
    "partly_cloudy": {
        "url": "https://upload.wikimedia.org/wikipedia/commons/a/aa/Blue-skies-cumulus-clouds.jpg",
        "source": "https://commons.wikimedia.org/wiki/File:Blue-skies-cumulus-clouds.jpg",
        "author": "Cbuske46",
        "license": "CC BY-SA 4.0",
    },
    "fog": {
        "url": "https://upload.wikimedia.org/wikipedia/commons/a/aa/Foggy_countryside_%28Unsplash%29.jpg",
        "source": "https://commons.wikimedia.org/wiki/File:Foggy_countryside_(Unsplash).jpg",
        "author": "Anton Repponen",
        "license": "CC0 1.0",
    },
    "rain": {
        "url": "https://upload.wikimedia.org/wikipedia/commons/e/ec/Dark_sky_and_clouds_01.jpg",
        "source": "https://commons.wikimedia.org/wiki/File:Dark_sky_and_clouds_01.jpg",
        "author": "Giorgio Galeotti",
        "license": "CC BY-SA 2.0",
    },
    "snow": {
        "url": "https://upload.wikimedia.org/wikipedia/commons/4/49/Winter-260817.jpg",
        "source": "https://commons.wikimedia.org/wiki/File:Winter-260817.jpg",
        "author": "Jarmoluk",
        "license": "CC0 1.0",
    },
    "showers": {
        "url": "https://upload.wikimedia.org/wikipedia/commons/0/08/A_rainbow_appears_in_the_sky_after_the_rain_shower.jpg",
        "source": "https://commons.wikimedia.org/wiki/File:A_rainbow_appears_in_the_sky_after_the_rain_shower.jpg",
        "author": "Paul-shy",
        "license": "CC BY 4.0",
    },
    "thunderstorm": {
        "url": "https://upload.wikimedia.org/wikipedia/commons/9/90/Lightning_In_Sky.jpg",
        "source": "https://commons.wikimedia.org/wiki/File:Lightning_In_Sky.jpg",
        "author": "Felix Mittermeier",
        "license": "CC0 1.0",
    },
    "unknown": {
        "url": "https://upload.wikimedia.org/wikipedia/commons/3/30/C%C3%A9vennes_France_night_sky_with_stars_02.jpg",
        "source": "https://commons.wikimedia.org/wiki/File:C%C3%A9vennes_France_night_sky_with_stars_02.jpg",
        "author": "Arnaud Vergnet",
        "license": "CC0 1.0",
    },
}


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req) as resp:
        return resp.read()


def center_crop_to_panel(image):
    target_ratio = SCREEN_W / SCREEN_H
    w, h = image.size
    ratio = w / h
    if ratio > target_ratio:
        new_w = int(h * target_ratio)
        x0 = (w - new_w) // 2
        image = image.crop((x0, 0, x0 + new_w, h))
    else:
        new_h = int(w / target_ratio)
        y0 = (h - new_h) // 2
        image = image.crop((0, y0, w, y0 + new_h))
    return image.resize((SCREEN_W, SCREEN_H), Image.LANCZOS)


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
    for name, info in SCENES.items():
        raw = fetch(info["url"])
        image = Image.open(io.BytesIO(raw)).convert("RGB")
        image = center_crop_to_panel(image)
        data = to_rgb565_bytes(image)
        out_path = OUT_DIR / f"scene_{name}.rgb565"
        out_path.write_bytes(data)
        print(f"wrote {out_path} ({len(data)} bytes) from {info['url']}")


if __name__ == "__main__":
    main()
