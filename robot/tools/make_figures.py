#!/usr/bin/env python3
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""Make the figures of doc/collision-detection.tex from recordings.

Usage: make_figures.py <out dir> <collision.csv> <stop.csv> <spin.csv> \\
           --events class=file.csv [...]

The first three recordings are shown as time plots (a collision, a stop, a
spin); every file given with --events adds its events, or for class "spin" the
maxima while spinning, to the a25/vib scatter plot.
"""

import argparse
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze  # noqa: E402

A, V, A_TURN = 0.6, 0.9, 1.0
plt.rcParams.update({"font.size": 9, "axes.grid": True, "grid.alpha": 0.3,
                     "figure.dpi": 150, "savefig.bbox": "tight"})


def load(path):
    d = analyze.load(path)
    analyze.features(d)
    return d


def time_plot(d, t0, t1, title, out, marks=(), turning=False):
    w = (d["t"] >= t0) & (d["t"] <= t1)
    t = d["t"][w]
    fig, axs = plt.subplots(3, 1, sharex=True, figsize=(7.0, 4.6),
                            gridspec_kw={"height_ratios": [1.1, 1.1, 0.7]})
    axs[0].plot(t, d["ax"][w], lw=0.6, label="$a_x$")
    axs[0].plot(t, d["ay"][w], lw=0.6, label="$a_y$")
    axs[0].set_ylabel("raw [g]")
    axs[1].plot(t, d["a25"][w], lw=1.0, label="$a_{25}$")
    axs[1].plot(t, d["vib"][w], lw=1.0, label="vib")
    axs[1].axhline(A_TURN if turning else A, ls="--", c="C0", lw=0.8,
                   label=f"$A{'_t' if turning else ''}$ = {A_TURN if turning else A} g")
    if not turning:
        axs[1].axhline(V, ls="--", c="C1", lw=0.8, label=f"$V$ = {V} g")
    else:
        axs[1].axhline(V, ls=":", c="C1", lw=0.8, label=f"$V$ = {V} g (not used)")
    # Context shading.
    for ctx, col in (("driving", "#2a9d58"), ("turning", "#2f6fdb")):
        m = np.concatenate(([False], d["ctx"][w] == ctx, [False]))
        e = np.flatnonzero(np.diff(m.astype(int)))
        for a, b in zip(e[::2], e[1::2]):
            axs[1].axvspan(t[a], t[b - 1], color=col, alpha=0.12, lw=0)
    axs[1].set_ylabel("feature [g]")
    axs[2].plot(t, d["left"][w], lw=1.0, label="left")
    axs[2].plot(t, d["right"][w], lw=1.0, ls="--", label="right")
    axs[2].set_ylabel("side [%]")
    axs[2].set_xlabel("time [s]")
    for x, text in marks:
        for a in axs:
            a.axvline(x, c="#d92d20", lw=1.0)
        axs[0].annotate(text, (x, axs[0].get_ylim()[1]), xytext=(4, -10),
                        textcoords="offset points", color="#d92d20", fontsize=8)
    for a in axs:
        a.legend(loc="upper left", bbox_to_anchor=(1.01, 1.0), fontsize=7, framealpha=0.9)
    fig.suptitle(title, fontsize=10)
    fig.savefig(out)
    plt.close(fig)


def scatter(points, out):
    fig, ax = plt.subplots(figsize=(6.3, 4.0))
    style = {"collision": ("#d92d20", "o"), "stop": ("#2f6fdb", "s"),
             "auto-stop": ("#8a8a8a", "."), "spin": ("#d9822b", "^")}
    for cls, pts in points.items():
        pts = np.array(pts)
        c, m = style.get(cls, ("k", "x"))
        ax.scatter(pts[:, 1], pts[:, 0], c=c, marker=m, s=34 if m != "." else 60,
                   label=f"{cls} ({len(pts)})", edgecolors="none", alpha=0.85)
    ax.axhline(A, c="#2a9d58", ls="--", lw=0.9, label="straight: $A$ = 0.6 g")
    ax.axvline(V, c="#2a9d58", ls=":", lw=0.9, label="straight: $V$ = 0.9 g")
    ax.axhline(A_TURN, c="#2f6fdb", ls="--", lw=0.9, label="turning: $A_t$ = 1.0 g")
    ax.set_xlabel("vib, maximum [g]")
    ax.set_ylabel("$a_{25}$, maximum [g]")
    ax.set_xlim(0, 4)
    ax.set_ylim(0, 2.0)
    ax.legend(fontsize=7, loc="lower right", ncol=2, framealpha=0.9)
    ax.set_title("Collisions against stops and spins", fontsize=10)
    fig.savefig(out)
    plt.close(fig)


def response(out):
    fs = 400.0
    f = np.linspace(0.1, 200, 2000)
    fig, ax = plt.subplots(figsize=(6.3, 2.6))
    for n, lab in ((10, "$N_s$ = 10 (25 ms)"), (40, "$N_v$ = 40 (100 ms)")):
        h = np.abs(np.sin(np.pi * f * n / fs) / (n * np.sin(np.pi * f / fs)))
        ax.plot(f, h, label=lab)
    ax.axvline(40, c="#8a8a8a", ls=":", lw=0.8)
    ax.annotate("first zero of the 25 ms average: 40 Hz", (40, 0.85), xytext=(8, 0),
                textcoords="offset points", fontsize=8, color="#555")
    ax.set_xlabel("frequency [Hz]")
    ax.set_ylabel("$|H(f)|$")
    ax.set_xlim(0, 200)
    ax.legend(fontsize=8)
    ax.set_title("Moving-average frequency response at 400 Hz", fontsize=10)
    fig.savefig(out)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("collision")
    ap.add_argument("stop")
    ap.add_argument("spin")
    ap.add_argument("--events", action="append", default=[], metavar="CLASS=FILE")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    d = load(args.collision)
    ev = [e for e in analyze.find_events(d, A, V) if e["collision"]][0]
    time_plot(d, ev["t"] - 1.2, ev["t"] + 1.2, "Collision while driving forward",
              os.path.join(args.out, "collision.png"),
              marks=[(ev["t"], "detected")])

    d = load(args.stop)
    ev = analyze.find_events(d, A, V)[0]
    time_plot(d, ev["t"] - 1.2, ev["t"] + 1.0, "Hard stop from full speed",
              os.path.join(args.out, "stop.png"))

    d = load(args.spin)
    spin = np.flatnonzero((d["turn"] != 0) & (d["throttle"] == 0) &
                          ((d["left"] != 0) | (d["right"] != 0)))
    time_plot(d, d["t"][spin[0]] - 0.5, d["t"][spin[-1]] + 0.6, "Spin in place",
              os.path.join(args.out, "spin.png"), turning=True)

    points = {}
    for item in args.events:
        cls, path = item.split("=", 1)
        d = load(path)
        if cls == "spin":
            m = (d["turn"] != 0) & (d["throttle"] == 0) & ((d["left"] != 0) |
                                                           (d["right"] != 0))
            points.setdefault(cls, []).append((d["a25"][m].max(), d["vib"][m].max()))
            continue
        for e in analyze.find_events(d, A, V, A_TURN, 0):
            points.setdefault(cls, []).append((e["a25"], e["vib"]))
    scatter(points, os.path.join(args.out, "scatter.png"))
    response(os.path.join(args.out, "response.png"))
    print({k: len(v) for k, v in points.items()})
    return 0


if __name__ == "__main__":
    sys.exit(main())
