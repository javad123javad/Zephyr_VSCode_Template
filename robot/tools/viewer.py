#!/usr/bin/env python3
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""Offline viewer for robot recordings.

Usage: viewer.py [files or folders ...]

Shows the raw IMU data, the collision features (a25, vib) with their
thresholds, rotation rate and side speeds of a recording, with the driving
context shaded, the labels set in the app, the events the analysis rule finds
and the collisions the robot itself reported (from the .events.csv file the
app writes next to a recording). Thresholds can be changed and the events
are recomputed at once; clicking an event zooms to it. The analysis is the
one of analyze.py.
"""

import csv
import os
import sys

import numpy as np
import pyqtgraph as pg
from PySide6 import QtCore, QtGui, QtWidgets

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze  # noqa: E402

COLORS = ["#2f6fdb", "#d9822b", "#2a9d58", "#8a8a8a"]
EVENT_COLS = ["time s", "dur s", "a25 g", "vib g", "|gz|", "context", "hit from", "label",
              "rule"]


def robot_events(path):
    """Collisions the robot reported during the recording, as (t_us, text)."""
    side = path[:-4] + ".events.csv"
    if not os.path.exists(side):
        return []
    with open(side, newline="") as f:
        return [(int(r["t_us"]), f"robot: {r['hit_from']} {int(r['a25_mg']) / 1000:.2f} g")
                for r in csv.DictReader(f)]


class Viewer(QtWidgets.QMainWindow):
    def __init__(self, paths):
        super().__init__()
        self.setWindowTitle("Robot recordings")
        self.data = None
        self.path = None
        self.events = []
        self.overlays = []

        split = QtWidgets.QSplitter()
        self.setCentralWidget(split)

        left = QtWidgets.QWidget()
        ll = QtWidgets.QVBoxLayout(left)
        buttons = QtWidgets.QHBoxLayout()
        buttons.addWidget(QtWidgets.QPushButton("Open files…", clicked=self.open_files))
        buttons.addWidget(QtWidgets.QPushButton("Open folder…", clicked=self.open_folder))
        ll.addLayout(buttons)
        self.files = QtWidgets.QListWidget()
        self.files.currentRowChanged.connect(self.load_selected)
        ll.addWidget(self.files, 1)

        form = QtWidgets.QFormLayout()
        self.a25 = QtWidgets.QDoubleSpinBox(minimum=0.1, maximum=8, singleStep=0.05,
                                            value=0.6, suffix=" g", decimals=2)
        self.vib = QtWidgets.QDoubleSpinBox(minimum=0.1, maximum=8, singleStep=0.05,
                                            value=0.9, suffix=" g", decimals=2)
        self.a25_turn = QtWidgets.QDoubleSpinBox(minimum=0.1, maximum=8, singleStep=0.05,
                                                 value=1.0, suffix=" g", decimals=2)
        self.vib_turn = QtWidgets.QDoubleSpinBox(minimum=0, maximum=8, singleStep=0.05,
                                                 value=0, suffix=" g", decimals=2,
                                                 specialValueText="off")
        for box in (self.a25, self.vib, self.a25_turn, self.vib_turn):
            box.valueChanged.connect(self.recompute)
        form.addRow("a25 threshold", self.a25)
        form.addRow("vib threshold", self.vib)
        form.addRow("a25 turning", self.a25_turn)
        form.addRow("vib turning", self.vib_turn)
        self.label_override = QtWidgets.QLineEdit(placeholderText="use labels in the file")
        self.label_override.setToolTip("Label all events of this file, for recordings\n"
                                       "whose labels are missing or wrong")
        self.label_override.editingFinished.connect(self.recompute)
        form.addRow("Label as", self.label_override)
        ll.addLayout(form)
        self.summary = QtWidgets.QLabel(wordWrap=True)
        ll.addWidget(self.summary)
        ll.addWidget(QtWidgets.QPushButton("Show all", clicked=self.show_all))
        ll.addWidget(QtWidgets.QPushButton("Save view as PNG…", clicked=self.save_png))
        split.addWidget(left)

        right = QtWidgets.QSplitter(QtCore.Qt.Vertical)
        pg.setConfigOptions(antialias=False, background="w", foreground="#444")
        self.plots = pg.GraphicsLayoutWidget()
        specs = [("Acceleration", "g", ["ax", "ay", "az"]),
                 ("Collision features", "g", ["a25", "vib"]),
                 ("Rotation rate", "deg/s", ["gx", "gy", "gz"]),
                 ("Side speed", "%", ["left", "right"])]
        self.plot_items, self.curves = [], {}
        for row, (title, unit, names) in enumerate(specs):
            p = self.plots.addPlot(row=row, col=0, title=title)
            p.setLabel("left", unit)
            p.showGrid(x=True, y=True, alpha=0.25)
            p.setClipToView(True)
            p.setDownsampling(auto=True, mode="peak")
            p.addLegend(offset=(5, 5))
            if row:
                p.setXLink(self.plot_items[0])
            for i, n in enumerate(names):
                self.curves[n] = p.plot(pen=pg.mkPen(COLORS[i], width=1.2), name=n)
            self.plot_items.append(p)
        self.plot_items[-1].setLabel("bottom", "s")
        self.thr_a25 = pg.InfiniteLine(angle=0, pen=pg.mkPen(COLORS[0], style=QtCore.Qt.DashLine))
        self.thr_vib = pg.InfiniteLine(angle=0, pen=pg.mkPen(COLORS[1], style=QtCore.Qt.DashLine))
        self.plot_items[1].addItem(self.thr_a25)
        self.plot_items[1].addItem(self.thr_vib)
        right.addWidget(self.plots)

        self.table = QtWidgets.QTableWidget(0, len(EVENT_COLS))
        self.table.setHorizontalHeaderLabels(EVENT_COLS)
        self.table.verticalHeader().hide()
        self.table.setEditTriggers(QtWidgets.QAbstractItemView.NoEditTriggers)
        self.table.setSelectionBehavior(QtWidgets.QAbstractItemView.SelectRows)
        self.table.horizontalHeader().setSectionResizeMode(QtWidgets.QHeaderView.Stretch)
        self.table.cellClicked.connect(self.zoom_to_event)
        right.addWidget(self.table)
        right.setSizes([620, 180])
        split.addWidget(right)
        split.setSizes([260, 1040])
        self.resize(1320, 860)

        self.add_paths(paths)

    # Files ------------------------------------------------------------------

    def add_paths(self, paths):
        found = []
        for p in paths:
            if os.path.isdir(p):
                found += sorted(os.path.join(p, f) for f in os.listdir(p)
                                if f.endswith(".csv") and not f.endswith(".events.csv"))
            elif p.endswith(".csv") and not p.endswith(".events.csv"):
                found.append(p)
        for f in found:
            item = QtWidgets.QListWidgetItem(os.path.basename(f))
            item.setData(QtCore.Qt.UserRole, f)
            item.setToolTip(f)
            self.files.addItem(item)
        if found and self.files.currentRow() < 0:
            self.files.setCurrentRow(0)

    def open_files(self):
        paths, _ = QtWidgets.QFileDialog.getOpenFileNames(self, "Recordings", "",
                                                          "Recordings (*.csv)")
        self.add_paths(paths)

    def open_folder(self):
        path = QtWidgets.QFileDialog.getExistingDirectory(self, "Folder with recordings")
        if path:
            self.add_paths([path])

    def load_selected(self, row):
        if row < 0:
            return
        self.path = self.files.item(row).data(QtCore.Qt.UserRole)
        d = analyze.load(self.path)
        if d is None:
            self.summary.setText("empty file")
            return
        analyze.features(d)
        self.data = d
        for n in ("ax", "ay", "az", "a25", "vib", "gx", "gy", "gz", "left", "right"):
            self.curves[n].setData(d["t"], d[n])
        self.recompute()
        self.show_all()

    # Analysis ---------------------------------------------------------------

    def clear_overlays(self):
        for plot, item in self.overlays:
            plot.removeItem(item)
        self.overlays = []

    def overlay(self, plot, item):
        plot.addItem(item)
        self.overlays.append((plot, item))

    def recompute(self):
        d = self.data
        if d is None:
            return
        self.thr_a25.setPos(self.a25.value())
        self.thr_vib.setPos(self.vib.value())
        self.clear_overlays()
        t = d["t"]

        # Context, shaded in the features plot: green driving, blue turning.
        for ctx, rgba in (("driving", (42, 157, 88, 28)), ("turning", (47, 111, 219, 28))):
            mask = np.concatenate(([False], d["ctx"] == ctx, [False]))
            edges = np.flatnonzero(np.diff(mask.astype(int)))
            for a, b in zip(edges[::2], edges[1::2]):
                region = pg.LinearRegionItem((t[a], t[b - 1]), movable=False,
                                             brush=pg.mkBrush(*rgba), pen=pg.mkPen(None))
                self.overlay(self.plot_items[1], region)

        # Labels set in the app.
        windows = analyze.label_windows(d)
        for t0, name in windows:
            for i, p in enumerate(self.plot_items):
                self.overlay(p, pg.InfiniteLine(
                    pos=t0, angle=90, label=name if i == 0 else None,
                    pen=pg.mkPen("#b45309", style=QtCore.Qt.DashLine),
                    labelOpts={"position": 0.9, "color": "#b45309"}))

        # Collisions the robot reported.
        t_us0 = None
        with open(self.path, newline="") as f:
            first = next(csv.DictReader(f), None)
            if first:
                t_us0 = int(first["t_us"])
        for t_us, text in robot_events(self.path):
            x = ((t_us - t_us0) & 0xFFFFFFFF) / 1e6
            for i, p in enumerate(self.plot_items):
                self.overlay(p, pg.InfiniteLine(
                    pos=x, angle=90, label=text if i == 0 else None,
                    pen=pg.mkPen("#d92d20", width=2),
                    labelOpts={"position": 0.75, "color": "#d92d20"}))

        # Events of the analysis rule.
        override = self.label_override.text().strip()
        self.events = analyze.find_events(d, self.a25.value(), self.vib.value(),
                                          self.a25_turn.value(), self.vib_turn.value())
        self.table.setRowCount(len(self.events))
        for row, ev in enumerate(self.events):
            ev["label"] = override or analyze.match_label(ev, windows, "")
            span = pg.LinearRegionItem((ev["t"], ev["t"] + max(ev["dur"], 0.01)), movable=False,
                                       brush=pg.mkBrush(217, 45, 32, 60 if ev["collision"]
                                                        else 25),
                                       pen=pg.mkPen(None))
            self.overlay(self.plot_items[0], span)
            vals = [f"{ev['t']:.2f}", f"{ev['dur']:.2f}", f"{ev['a25']:.2f}", f"{ev['vib']:.2f}",
                    f"{ev['gz']:.0f}", ev["ctx"], ev["hit_from"], ev["label"],
                    f"COLLISION ({ev['trigger']})" if ev["collision"] else "-"]
            for col, v in enumerate(vals):
                item = QtWidgets.QTableWidgetItem(v)
                if ev["collision"]:
                    item.setForeground(QtGui.QColor("#b42318"))
                self.table.setItem(row, col, item)

        drive = d["ctx"] == "driving"
        base = (f"driving 99th percentile: a25 {np.percentile(d['a25'][drive], 99):.2f} g, "
                f"vib {np.percentile(d['vib'][drive], 99):.2f} g" if drive.any()
                else "no driving")
        hits = sum(e["collision"] for e in self.events)
        bad = f"<br>{d['bad']} corrupted samples dropped" if d["bad"] else ""
        self.summary.setText(f"<b>{os.path.basename(self.path)}</b><br>{t[-1]:.1f} s, {base}"
                             f"<br>{len(self.events)} events, {hits} collision(s) by the rule, "
                             f"{len(robot_events(self.path))} reported by the robot{bad}")

    # View -------------------------------------------------------------------

    def show_all(self):
        if self.data is not None:
            self.plot_items[0].setXRange(0, self.data["t"][-1], padding=0.01)
            for p in self.plot_items:
                p.enableAutoRange(y=True)

    def zoom_to_event(self, row, _col):
        ev = self.events[row]
        self.plot_items[0].setXRange(ev["t"] - 1.0, ev["t"] + ev["dur"] + 1.0, padding=0)

    def save_png(self):
        if self.path is None:
            return
        default = os.path.splitext(self.path)[0] + ".png"
        path, _ = QtWidgets.QFileDialog.getSaveFileName(self, "Save view", default, "PNG (*.png)")
        if path:
            self.plots.grab().save(path)


def main():
    app = QtWidgets.QApplication(sys.argv)
    w = Viewer(sys.argv[1:])
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
