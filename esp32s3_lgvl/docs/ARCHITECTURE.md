# Architecture

This document records the reasoning behind this app's structure so it
survives past the planning conversation that produced it. Update it when a
decision below is revisited, not just when new code is added.

## Screen hierarchy

```
Home (Clock + Weather)                  <- default at boot
  └─ [gear icon] -> Main Menu (2x2 icon grid)
        ├─ Settings
        │    ├─ Network        (Wi-Fi setup: scan, connect, forget)
        │    ├─ Clock          (UTC offset, quarter-hour resolution)
        │    ├─ Local          (city name -> Open-Meteo geocoding, cached)
        │    └─ Calibrate Touch
        ├─ Shell   (planned: interactive Zephyr shell)
        ├─ Radio   (planned: internet radio via Radio Browser API)
        └─ About   (planned: build info, uptime, heap, RSSI, IP)
```

## AD-1: Navigation — explicit screen stack

**Decision**: `push_screen()`/`pop_screen()` (`src/ui/ui_nav.c`) instead of
each screen hardcoding its own "go back to X" callback. Every "Back" button
calls the same `nav_pop_screen`.

**Why**: a hardcoded-parent pattern already needed editing multiple files
once Settings gained sub-screens. A stack makes "Back" correct by
construction at any depth, and is the one piece every screen module
depends on (a leaf dependency — no cycles between screen modules).

## AD-2: File organization — one file per screen

**Decision**: `src/ui/ui_<screen>.c`, one per screen, each owning its own
`lv_obj_t *screen` and event callbacks, plus a shared `ui_common.h` for
cross-screen layout constants and helpers. Each screen exposes
`X_screen_get(void)` (built once, cached) so a parent screen's callback can
push a child directly — no dynamic string-keyed registry; with roughly a
dozen screens, direct C function calls are simpler and the compiler catches
mistakes a registry would hide.

**Why**: a single `ui.c` covering every screen was already ~600 lines for 5
screens before this restructure; splitting keeps each screen's
state/callbacks scoped and independently reviewable, and is what makes
per-screen unit testing (AD-5) practical.

**Alternative considered**: keep one `ui.c`, better organized with
comments/regions. Rejected — doesn't solve reviewability or testability,
just delays the same split until the file is even bigger.

## AD-3: State management — module-owned state, no event bus

**Decision**: each domain module (`wifi.c`, `weather.c`, future `radio.c`)
owns its state and exposes getters (e.g. `wifi_get_status()`,
`wifi_get_ssid()`, `weather_get_status()`). Screens poll what they need via small per-screen tick
functions (`ui_home_tick()`, ...) called from a thin central dispatcher
(`ui_tick()` in `ui.c`), instead of one function whose parameter list grows
with every new domain.

**Why**: LVGL is already a poll-and-redraw system; a pub-sub/event bus on
top of that would add indirection without solving a real problem at this
app's scale.

**Alternative considered**: central `app_state` struct shared by all
screens. Rejected — would recouple screens AD-2 decoupled, and no incoming
state needs cross-screen sharing beyond what a getter already provides.

## AD-4: Threading — extend existing thread, no new ones for early phases

**Decision**:
- Periodic network-bound work (SNTP resync, weather polling) runs from
  the *existing* `network_thread` in `wifi.c`, not a new thread per
  feature. `weather_poll()` is called from the same connected-loop tick
  that used to only handle SNTP; the tick itself was shortened from 1
  hour to 5 seconds so a freshly-submitted city resolves within seconds
  rather than up to an hour, while SNTP's own resync cadence is kept at
  1 hour via a tick counter, not by slowing the loop back down.
- The planned interactive Shell needs no thread we manage — Zephyr's shell
  subsystem spawns its own processing thread per registered `shell`
  instance once the transport (`shell_transport_api`) is implemented.
- Radio (audio streaming/decoding) will likely need its own thread for
  timed buffer feeding; decided when that feature is built.

**Why**: DRAM has run 83-91% used on this board across this app's
development; a new thread's stack for something that runs every 15-30
minutes is wasted budget when the existing periodic-refresh loop is a
direct fit.

## AD-5: Testing & coverage — testable-logic layer vs. manually-verified layer

**Decision**: split code into two honestly-labeled categories:
- **Testable layer** (ztest, runs on `native_sim`/`unit_testing`, no
  hardware): offset math/formatting, screen-stack push/pop, Wi-Fi scan
  dedup and credential save/forget logic, weather forecast/geocoding JSON
  parsing (`weather_json.c`, split out of `weather.c` specifically so the
  test links it without pulling in sockets/TLS), and (once built) Radio
  Browser JSON parsing. Structure: `tests/<module>/` mirroring
  `src/<module>.c`, run via `west twister -p native_sim -T tests/`.
  Coverage tracked via Twister's built-in `--coverage` (gcov) on this
  layer specifically.
- **Manually-verified layer**: actual LVGL rendering, real touch
  calibration accuracy, real audio playback — verified via native_sim
  visual checks and real-hardware checks, never conflated with the
  automated coverage number.

**Why**: "full coverage" on a UI-heavy embedded app is not an honest claim
if it includes unverifiable-by-test UI/hardware behavior. This split
directs unit-testing effort at the exact class of bug that has actually
caused problems here (an unchecked `wifi_credentials_set_personal()`
return value, a connect-retry loop that could hang forever, a static
fallback credential that silently blocked every real connection attempt) —
all of which live in the testable layer.

## AD-6: Coding standards — Zephyr's own MISRA-C subset

**Decision**: follow `doc/contribute/coding_guidelines/index.rst` (already
referenced by this repo's own `AGENTS.md`) rather than a separate standard.
Priority order, given what has already caused real bugs:
1. Check every fallible return value.
2. No magic numbers: `ui_common.h` for cross-screen layout constants,
   per-module `#define`s for module-specific values (e.g.
   `TZ_OFFSET_QUARTERS_MIN/MAX` in `ui_clock.c`,
   `WIFI_RESULTS_PER_PAGE`/`MAX_SCAN_RESULTS` in the Wi-Fi code).
3. Fixed-width types (`uint8_t`/`int32_t`/`size_t`) instead of bare `int`
   for anything that isn't a genuinely platform-native loop counter.
4. Explicit comparisons (`if (ret != 0)`, `if (ptr == NULL)`).
5. No dynamic allocation beyond already-configured pools (LVGL's own pool,
   `k_heap`, Zephyr subsystem pools).

**Enforcement**: `scripts/checkpatch.pl` (from the Zephyr tree this app
lives in), run from that tree's root so it picks up its own
`.checkpatch.conf` (100-column limit, ignores upstream-only rules like
`SPDX_LICENSE_TAG`), as a before-commit check even though this app isn't
pushed upstream. Its indentation rule (tabs, Linux kernel style) is not
applied here: this app has used 4-space indentation consistently since
its first commit, predating this decision, and reformatting the whole
tree to tabs now would be unrelated, unasked-for churn for no functional
gain. Everything else checkpatch flags is treated as a real defect to
fix before committing.

## AD-7: Visual design - static, pre-rendered scenes, no runtime drawing

**Decision**: the Home screen's weather-condition background (sunny/cloudy/
rain/snow/fog/storm) is one pre-rendered RGB565 bitmap per `weather_scene`,
generated offline by `tools/gen_weather_scenes.py` (Pillow) and compiled in
as a `const lv_image_dsc_t` (`src/ui/weather_scenes.c`/`.h` +
`src/ui/weather_scenes/scene_*.rgb565`, converted to C byte arrays at
build time by `generate_inc_file_for_target()`, the same mechanism already
used for the TLS CA certificate). `ui_home.c` holds exactly one
`lv_image` object (`scene_img`); `show_scene()` just calls
`lv_image_set_src()` on it when `weather_get_scene()` changes, which only
happens on `weather_poll()`'s cadence (minutes), not per frame. The script
is run manually to edit the art (`python3 tools/gen_weather_scenes.py`,
needs Pillow); its *output* is committed, so a normal `west build` never
needs Python beyond what west itself already requires. A shared accent/
warning/text color set keyed by scene brightness lives in
`ui/ui_palette.c` (`ui_scene_is_light()` and friends), meant to be reused
as later screens are revisited; the actual scene pixel colors now live in
the generator script, which must be kept classified consistently with it.

**Why**: this board has no GPU and drives an SPI panel with `dram0_0_seg`
already reported at 82-91% used, while flash sits under 7% of 16 MB. The
scenes were originally built from live LVGL draw primitives (gradients,
layered circles, a polyline bolt) instead - reviewable as plain diffable
code, no asset pipeline - but that spent the scarce resource to save the
abundant one, and caused two real crashes: a shadow-blur buffer alone
exceeding the entire 64 KB `LV_Z_MEM_POOL_SIZE`, and (after fixing that)
the same pool overrunning on `native_sim/native/64` once ~100 decoration
objects across 8 pre-built scenes existed at once (LVGL's per-object
structs are larger on a 64-bit host build, so the same object count didn't
fit there even though it fit on the real 32-bit target). Both are detailed
in the commits that fixed them. A single `lv_image` object per scene
removes that whole class of bug regardless of how many shapes the art
has, at a real but affordable cost of ~300 KB flash per scene (~2.4 MB for
all 8, confirmed against a real build: FLASH usage moved from 6.95% to
21.60% of 16 MB, `dram0_0_seg` unchanged). No compression: still trivial
against the 16 MB budget, and avoids paying RLE/LZ4 decode time on the
ESP32 for a background that's static anyway.

**Alternative considered**: keep the hand-drawn LVGL primitives (the
original version of this decision). Rejected after the crashes above -
lower flash cost, but the wrong resource to optimize for on this specific
board, and the object-count-scales-with-scene-count failure mode was
already generalizing to two chip/build-width-dependent bugs in one
feature.

The WMO `weather_code` -> `weather_scene` mapping lives in its own
`weather_scene.c`/`.h`, split out of `weather.c` for the same reason
`weather_json.c` was (AD-5): a pure function with no sockets/TLS
dependency, so `tests/weather` can link and assert it directly.
