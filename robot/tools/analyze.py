#!/usr/bin/env python3
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""Find and characterise impact-like events in robot recordings.

Usage:
  analyze.py rec1.csv rec2.csv ...            tables
  analyze.py *.csv --plots out/               also one PNG per event
  analyze.py a.csv --label a.csv=collision    label a file without labels

For every recording the tool computes, per sample:
  a25   horizontal acceleration averaged over 25 ms (g), the short, hard
        push of an impact; driving vibration averages out
  vib   RMS of the horizontal acceleration above that average, over 100 ms
        (g), how hard the chassis shakes
and the context from the drive state: idle, starting (first 0.8 s of a
move or of a direction change), driving, or stopping (0.6 s after the
command went to stop, when a deceleration is expected).

An event is a stretch where a25 or vib crosses its threshold. The rule
under test flags it as a collision when a threshold is crossed while
driving; a jolt that follows a stop is braking, even if the event began
while driving. Each
event is matched with the label set in the app (or given with --label).
The summary per label shows how well the features and the rule separate
collisions from everything else.
"""

import argparse
import csv
import math
import os
import sys
from collections import defaultdict

import numpy as np

RATE = 400.0
ACCEL_MAX_G = 16.0
GYRO_MAX_DPS = 2000.0
START_S = 0.8
STOP_S = 0.6
MERGE_S = 0.3
SECTORS = ["front", "front-left", "left", "back-left", "back", "back-right", "right",
           "front-right"]


def load(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        return None
    col = lambda k, typ=float: np.array([typ(r[k]) for r in rows])
    t_us = col("t_us", int)
    # The microsecond counter wraps every 71.6 minutes.
    t = np.unwrap(t_us.astype(np.float64), period=2 ** 32) / 1e6
    d = {
        "t": t - t[0],
        "ax": col("ax_g"), "ay": col("ay_g"), "az": col("az_g"),
        "gx": col("gx_dps"), "gy": col("gy_dps"), "gz": col("gz_dps"),
        "throttle": col("throttle", int), "turn": col("turn", int),
        "left": col("left", int), "right": col("right", int),
        "label": np.array([r["label"] for r in rows]),
    }
    # Drop physically impossible samples (corrupted reads) and fill the gap.
    bad = (np.abs(np.stack([d["ax"], d["ay"], d["az"]])) > ACCEL_MAX_G).any(axis=0) | \
          (np.abs(np.stack([d["gx"], d["gy"], d["gz"]])) > GYRO_MAX_DPS).any(axis=0)
    good = ~bad
    for k in ("ax", "ay", "az", "gx", "gy", "gz"):
        d[k] = np.interp(d["t"], d["t"][good], d[k][good])
    d["bad"] = int(bad.sum())
    return d


def moving_mean(x, n):
    return np.convolve(x, np.ones(n) / n, mode="same")


def features(d):
    moving = (d["left"] != 0) | (d["right"] != 0)
    # Remove the tilt offset seen at rest so a25 reads zero when standing.
    rest = ~moving
    ox = np.median(d["ax"][rest]) if rest.any() else 0.0
    oy = np.median(d["ay"][rest]) if rest.any() else 0.0
    ax, ay = d["ax"] - ox, d["ay"] - oy
    n25, n100 = int(0.025 * RATE), int(0.1 * RATE)
    mx, my = moving_mean(ax, n25), moving_mean(ay, n25)
    d["a25x"], d["a25y"] = mx, my
    d["a25"] = np.hypot(mx, my)
    d["vib"] = np.sqrt(moving_mean((ax - mx) ** 2 + (ay - my) ** 2, n100))

    # Context from the commanded and actual side speeds.
    t = d["t"]
    cmd = np.stack([d["throttle"], d["turn"]], axis=1)
    side_sign = np.sign(d["left"]) * 2 + np.sign(d["right"])
    ctx = np.empty(len(t), dtype=object)
    last_change = -1e9
    last_stop = -1e9
    prev_sign = 0
    prev_cmd = (0, 0)
    for i in range(len(t)):
        c = (cmd[i, 0], cmd[i, 1])
        if c == (0, 0) and prev_cmd != (0, 0):
            last_stop = t[i]
        if side_sign[i] != prev_sign and side_sign[i] != 0:
            last_change = t[i]   # start or direction change of a side
        prev_sign, prev_cmd = side_sign[i], c
        if not moving[i] and t[i] - last_stop > STOP_S:
            ctx[i] = "idle"
        elif c == (0, 0) or t[i] - last_stop <= STOP_S and not moving[i]:
            ctx[i] = "stopping"
        elif t[i] - last_change < START_S:
            ctx[i] = "starting"
        else:
            ctx[i] = "driving"
    d["ctx"] = ctx
    return d


def sector(x, y):
    ang = math.degrees(math.atan2(y, x)) % 360
    return SECTORS[int(((ang + 22.5) % 360) // 45)]


def find_events(d, a25_thr, vib_thr):
    hit = (d["a25"] > a25_thr) | (d["vib"] > vib_thr)
    events = []
    idx = np.where(hit)[0]
    if not len(idx):
        return events
    groups = [[idx[0]]]
    for i in idx[1:]:
        if d["t"][i] - d["t"][groups[-1][-1]] <= MERGE_S:
            groups[-1].append(i)
        else:
            groups.append([i])
    for g in groups:
        g = np.array(g)
        p = g[np.argmax(d["a25"][g])]
        ctx = d["ctx"][p]
        driving_hit = g[d["ctx"][g] == "driving"]
        trig = []
        if (d["a25"][driving_hit] > a25_thr).any():
            trig.append("a25")
        if (d["vib"][driving_hit] > vib_thr).any():
            trig.append("vib")
        events.append({
            "t": d["t"][g[0]], "t_peak": d["t"][p], "dur": d["t"][g[-1]] - d["t"][g[0]],
            "a25": float(d["a25"][g].max()), "vib": float(d["vib"][g].max()),
            "gz": float(np.abs(d["gz"][g]).max()),
            # Direction from the first 20 ms after the threshold is crossed,
            # the contact itself; later the robot may push and shake. The
            # acceleration points away from what was hit.
            "hit_from": sector(-d["a25x"][g[0]:g[0] + 8].mean(),
                               -d["a25y"][g[0]:g[0] + 8].mean()),
            "ctx": ctx, "collision": bool(trig), "trigger": "+".join(trig) or "-",
        })
    return events


def label_windows(d):
    """(start, label) for each label the app set, until the next one."""
    out = []
    prev = ""
    for i, lab in enumerate(d["label"]):
        if lab != prev and lab:
            out.append((d["t"][i], lab))
        prev = lab
    return out


def match_label(ev, windows, default):
    lab = default
    for t0, name in windows:
        if t0 - 0.5 <= ev["t"] <= t0 + 3.0:
            lab = name
    return lab or "unlabelled"


def plot(path, d, ev, a25_thr, vib_thr, out_dir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    w = (d["t"] > ev["t"] - 1.0) & (d["t"] < ev["t"] + ev["dur"] + 1.0)
    t = d["t"][w]
    fig, axs = plt.subplots(4, 1, sharex=True, figsize=(9, 8))
    axs[0].plot(t, d["ax"][w], lw=0.7, label="ax")
    axs[0].plot(t, d["ay"][w], lw=0.7, label="ay")
    axs[0].set_ylabel("raw g")
    axs[1].plot(t, d["a25"][w], label="a25")
    axs[1].plot(t, d["vib"][w], label="vib")
    axs[1].axhline(a25_thr, ls="--", c="C0", lw=0.8)
    axs[1].axhline(vib_thr, ls="--", c="C1", lw=0.8)
    axs[1].set_ylabel("feature g")
    axs[2].plot(t, d["gz"][w], lw=0.8, c="C2", label="gz")
    axs[2].set_ylabel("deg/s")
    axs[3].plot(t, d["left"][w], label="left")
    axs[3].plot(t, d["right"][w], label="right")
    axs[3].set_ylabel("side %")
    axs[3].set_xlabel("s")
    for a in axs:
        a.axvspan(ev["t"], ev["t"] + max(ev["dur"], 0.005), color="red", alpha=0.12)
        a.legend(loc="upper right", fontsize=8)
        a.grid(alpha=0.3)
    verdict = "COLLISION" if ev["collision"] else "no collision"
    fig.suptitle(f"{os.path.basename(path)} at {ev['t']:.2f} s: {ev['label']}, {ev['ctx']}, "
                 f"{verdict}, hit from {ev['hit_from']}")
    fig.tight_layout()
    name = f"{os.path.splitext(os.path.basename(path))[0]}_{ev['t']:07.2f}s.png"
    fig.savefig(os.path.join(out_dir, name), dpi=90)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--a25", type=float, default=0.6, help="a25 threshold, g (0.6)")
    ap.add_argument("--vib", type=float, default=0.9, help="vibration threshold, g (0.9)")
    ap.add_argument("--label", action="append", default=[], metavar="FILE=LABEL",
                    help="label for events in a file recorded without labels")
    ap.add_argument("--plots", metavar="DIR", help="write one PNG per event")
    args = ap.parse_args()

    given = dict(x.split("=", 1) for x in args.label)
    if args.plots:
        os.makedirs(args.plots, exist_ok=True)

    summary = defaultdict(list)
    print(f"{'file':28} {'time':>7} {'dur':>5} {'a25':>5} {'vib':>5} {'|gz|':>5} "
          f"{'context':9} {'hit from':11} {'label':10} rule")
    for path in args.files:
        d = load(path)
        if d is None:
            continue
        features(d)
        default = given.get(path, given.get(os.path.basename(path), ""))
        windows = label_windows(d)
        drive = d["ctx"] == "driving"
        base_a25 = np.percentile(d["a25"][drive], 99) if drive.any() else float("nan")
        base_vib = np.percentile(d["vib"][drive], 99) if drive.any() else float("nan")
        note = f", {d['bad']} corrupted samples dropped" if d["bad"] else ""
        print(f"{os.path.basename(path)}: {d['t'][-1]:.1f} s, driving 99th percentile "
              f"a25 {base_a25:.2f} g, vib {base_vib:.2f} g{note}")
        for ev in find_events(d, args.a25, args.vib):
            ev["label"] = match_label(ev, windows, default)
            summary[ev["label"]].append(ev)
            print(f"{'':28} {ev['t']:7.2f} {ev['dur']:5.2f} {ev['a25']:5.2f} {ev['vib']:5.2f} "
                  f"{ev['gz']:5.0f} {ev['ctx']:9} {ev['hit_from']:11} {ev['label']:10} "
                  f"{'COLLISION (' + ev['trigger'] + ')' if ev['collision'] else '-'}")
            if args.plots:
                plot(path, d, ev, args.a25, args.vib, args.plots)

    print("\nper label: events, rule says collision, a25 and vib (min / median / max)")
    for lab, evs in sorted(summary.items()):
        a = np.array([e["a25"] for e in evs])
        v = np.array([e["vib"] for e in evs])
        hits = sum(e["collision"] for e in evs)
        print(f"  {lab:12} {len(evs):3}  collision {hits:3}/{len(evs):<3}  "
              f"a25 {a.min():.2f}/{np.median(a):.2f}/{a.max():.2f}  "
              f"vib {v.min():.2f}/{np.median(v):.2f}/{v.max():.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
