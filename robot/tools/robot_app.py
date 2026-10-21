#!/usr/bin/env python3
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""PC app for the robot: drive it, plot the IMU live and record labelled data.

Usage: robot_app.py [host]

Without a host the app uses the last address that worked, then robot.local,
then searches the PC's subnets for a robot.

Driving: arrow keys (Shift for half speed) or a gamepad's left stick; Space
stops. Labels while recording: C collision, B brake, V reverse, P push,
L lift, S bump, N none. The protocol is described in src/link.h.
"""

import csv
import ipaddress
import math
import os
import socket
import struct
import subprocess
import sys
import time

import numpy as np
import pyqtgraph as pg
from PySide6 import QtCore, QtGui, QtWidgets

PORT = 4244
IMU_HEADER = struct.Struct("<IIIIHBB")
IMU_SAMPLE = struct.Struct("<I3h3hbbBbbB")
STATUS = struct.Struct("<I3BbBBBBiiIIIIiiiIIII")
STATUS_V070 = struct.Struct("<I3BbBBBBiiIIIIiiiI")
IMU_MAGIC = 0x31554D49
STATUS_MAGIC = 0x31415453
ACCEL_G = 0.0039
GYRO_DPS = 1 / 14.375

BUFFER = 16384           # > 30 s at 400 Hz
WINDOWS = [2, 5, 10, 30]
REPEAT_S = 0.15
KEEPALIVE_S = 1.0
LOST_S = 2.0             # no status for this long: connection lost
SEARCH_AFTER_S = 6.0     # lost this long: look for the robot again
MIN_SPEED = 30

INHIBIT_NAMES = {1: "battery empty", 2: "obstacle", 4: "collision"}
LABELS = [("C", "collision"), ("B", "brake"), ("V", "reverse"), ("P", "push"),
          ("L", "lift"), ("S", "bump"), ("N", "")]
ARROWS = {QtCore.Qt.Key_Up: "up", QtCore.Qt.Key_Down: "down",
          QtCore.Qt.Key_Left: "left", QtCore.Qt.Key_Right: "right"}
COLORS = ["#2f6fdb", "#d9822b", "#2a9d58", "#8a8a8a"]

THEMES = {
    "light": {"bg": "#f6f6f4", "card": "#ffffff", "fg": "#1c1c1a", "muted": "#6b6b66",
              "line": "#d8d8d3", "accent": "#2f6fdb", "warn": "#b45309",
              "stop_bg": "#fde8e8", "stop_fg": "#9b1c1c", "plot": "w", "axis": "#444"},
    "dark": {"bg": "#161615", "card": "#21211f", "fg": "#ececea", "muted": "#9a9a95",
             "line": "#3a3a37", "accent": "#5b8ff0", "warn": "#f59e0b",
             "stop_bg": "#3b1515", "stop_fg": "#fca5a5", "plot": "#1b1b1a", "axis": "#bbb"},
}


# Network ---------------------------------------------------------------------

def local_subnets():
    """IPv4 networks of this PC's interfaces, limited to /22 or smaller."""
    nets = []
    try:
        out = subprocess.run(["ip", "-o", "-4", "addr", "show"], capture_output=True,
                             text=True, timeout=2).stdout
        for line in out.splitlines():
            parts = line.split()
            if "inet" in parts:
                iface = ipaddress.ip_interface(parts[parts.index("inet") + 1])
                if not iface.ip.is_loopback:
                    nets.append(iface.network if iface.network.prefixlen >= 22
                                else ipaddress.ip_network(f"{iface.ip}/24", strict=False))
    except (OSError, ValueError, subprocess.SubprocessError):
        pass
    if not nets:
        # Fall back to the address of the default route, as a /24.
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect(("10.255.255.255", 1))
            nets.append(ipaddress.ip_network(f"{s.getsockname()[0]}/24", strict=False))
        except OSError:
            pass
        finally:
            s.close()
    return nets


class Link:
    """UDP session with the robot, including finding it."""

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setblocking(False)
        self.addr = None
        self.searching = False
        self.last_keepalive = 0.0
        self.last_status = 0.0
        self.last_seq = None
        self.lost_packets = 0

    def connect(self, host):
        """Connect to a host name or address; returns False if it does not resolve."""
        try:
            ip = socket.gethostbyname(host)
        except OSError:
            return False
        self.addr = (ip, PORT)
        self.searching = False
        self.last_seq = None
        self.send("S")
        return True

    def search(self):
        """Say hello to every address of the PC's subnets; the robot answers."""
        self.searching = True
        self.addr = None
        for net in local_subnets():
            for ip in net.hosts():
                try:
                    self.sock.sendto(b"S", (str(ip), PORT))
                except OSError:
                    pass
        self.last_keepalive = time.time()

    def send(self, text):
        if self.addr:
            try:
                self.sock.sendto(text.encode(), self.addr)
            except OSError:
                pass
            self.last_keepalive = time.time()

    def close(self):
        self.send("D 0 0 0")
        self.send("X")

    def poll(self):
        """Return (IMU sample tuples, latest status tuple or None)."""
        samples, status = [], None
        if self.addr and time.time() - self.last_keepalive >= KEEPALIVE_S:
            self.send("S")
        while True:
            try:
                data, src = self.sock.recvfrom(4096)
            except (BlockingIOError, OSError):
                break
            if len(data) < 4:
                continue
            magic = struct.unpack_from("<I", data)[0]
            if magic == STATUS_MAGIC and len(data) >= STATUS_V070.size:
                if self.searching or self.addr is None:
                    # First robot that answers the search.
                    self.addr = (src[0], PORT)
                    self.searching = False
                if src[0] != self.addr[0]:
                    continue
                if len(data) >= STATUS.size:
                    status = STATUS.unpack_from(data)
                else:
                    status = STATUS_V070.unpack_from(data) + (None, None, None)
                self.last_status = time.time()
            elif magic == IMU_MAGIC and len(data) >= IMU_HEADER.size and self.addr \
                    and src[0] == self.addr[0]:
                _, seq, _, _, _, count, size = IMU_HEADER.unpack_from(data)
                if size != IMU_SAMPLE.size:
                    continue
                if self.last_seq is not None and seq != self.last_seq + 1:
                    self.lost_packets += (seq - self.last_seq - 1) & 0xFFFFFFFF
                self.last_seq = seq
                for i in range(count):
                    samples.append(IMU_SAMPLE.unpack_from(data, IMU_HEADER.size +
                                                          i * IMU_SAMPLE.size))
        return samples, status

    @property
    def alive(self):
        return self.addr is not None and time.time() - self.last_status < LOST_S


class Gamepad:
    """Left stick of the first gamepad via pygame, if available."""

    def __init__(self):
        self.js = None
        self.pg = None
        try:
            os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
            os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
            import pygame

            pygame.init()
            pygame.joystick.init()
            self.pg = pygame
            if pygame.joystick.get_count() > 0:
                self.js = pygame.joystick.Joystick(0)
                self.js.init()
        except Exception:
            self.pg = None

    @property
    def name(self):
        return self.js.get_name() if self.js else None

    def command(self):
        """Return (throttle, turn, speed) from the stick, or None when centred."""
        if not self.js:
            return None
        self.pg.event.pump()
        x, y = self.js.get_axis(0), -self.js.get_axis(1)
        mag = min(1.0, math.hypot(x, y))
        if mag < 0.2:
            return None
        throttle = (1 if y > 0 else -1) if abs(y) > 0.35 else 0
        turn = (1 if x > 0 else -1) if abs(x) > 0.35 else 0
        if throttle == 0 and turn == 0:
            return None
        return throttle, turn, round(MIN_SPEED + (100 - MIN_SPEED) * mag)


# Widgets -----------------------------------------------------------------------

class Card(QtWidgets.QFrame):
    """A titled block of label/value rows."""

    def __init__(self, title, rows):
        super().__init__()
        self.setObjectName("card")
        grid = QtWidgets.QGridLayout(self)
        grid.setContentsMargins(12, 10, 12, 10)
        grid.setHorizontalSpacing(12)
        grid.setVerticalSpacing(3)
        head = QtWidgets.QLabel(title)
        head.setObjectName("cardTitle")
        grid.addWidget(head, 0, 0, 1, 2)
        self.values = {}
        for i, name in enumerate(rows, start=1):
            key = QtWidgets.QLabel(name)
            key.setObjectName("muted")
            val = QtWidgets.QLabel("–")
            val.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse)
            grid.addWidget(key, i, 0)
            grid.addWidget(val, i, 1)
            self.values[name] = val
        grid.setRowStretch(len(rows) + 1, 1)

    def set(self, name, text, warn=False):
        label = self.values[name]
        label.setText(text)
        label.setProperty("warn", warn)
        label.style().unpolish(label)
        label.style().polish(label)


class DrivePad(QtWidgets.QWidget):
    """Arrow pad lit by the active direction, plus side-speed bars."""

    def __init__(self):
        super().__init__()
        self.setMinimumSize(220, 150)
        self.throttle = 0
        self.turn = 0
        self.left = 0
        self.right = 0
        self.theme = THEMES["light"]

    def update_state(self, throttle, turn, left, right):
        if (throttle, turn, left, right) != (self.throttle, self.turn, self.left, self.right):
            self.throttle, self.turn, self.left, self.right = throttle, turn, left, right
            self.update()

    def paintEvent(self, ev):
        th = self.theme
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.Antialiasing)
        w, h = self.width(), self.height()
        key = min(44, (h - 20) // 2 - 4)
        cx = 70
        cells = {"up": (cx - key // 2, 6), "left": (cx - key * 3 // 2 - 4, 10 + key),
                 "down": (cx - key // 2, 10 + key), "right": (cx + key // 2 + 4, 10 + key)}
        active = {"up": self.throttle > 0, "down": self.throttle < 0,
                  "left": self.turn < 0, "right": self.turn > 0}
        glyph = {"up": "↑", "down": "↓", "left": "←", "right": "→"}
        font = p.font()
        font.setPointSize(16)
        p.setFont(font)
        for name, (x, y) in cells.items():
            on = active[name]
            p.setPen(QtGui.QPen(QtGui.QColor(th["accent"] if on else th["line"]), 1.5))
            p.setBrush(QtGui.QColor(th["accent"] if on else th["card"]))
            p.drawRoundedRect(x, y, key, key, 8, 8)
            p.setPen(QtGui.QColor("#ffffff" if on else th["fg"]))
            p.drawText(QtCore.QRect(x, y, key, key), QtCore.Qt.AlignCenter, glyph[name])

        # Side speed bars, -100..100 around the middle.
        font.setPointSize(9)
        p.setFont(font)
        top, bottom = 8, h - 22
        mid = (top + bottom) / 2
        for i, (name, val) in enumerate((("L", self.left), ("R", self.right))):
            x = w - 70 + i * 34
            p.setPen(QtGui.QPen(QtGui.QColor(th["line"]), 1))
            p.setBrush(QtGui.QColor(th["card"]))
            p.drawRect(x, top, 20, bottom - top)
            p.drawLine(x - 3, int(mid), x + 23, int(mid))
            if val:
                hgt = (bottom - top) / 2 * abs(val) / 100
                y = mid - hgt if val > 0 else mid
                p.setPen(QtCore.Qt.NoPen)
                p.setBrush(QtGui.QColor(th["accent"] if val > 0 else th["warn"]))
                p.drawRect(QtCore.QRectF(x + 1, y, 18, hgt))
            p.setPen(QtGui.QColor(th["muted"]))
            p.drawText(QtCore.QRect(x - 6, bottom + 2, 32, 16), QtCore.Qt.AlignCenter,
                       f"{name} {val}")
        p.end()


# Main window ------------------------------------------------------------------

class Window(QtWidgets.QMainWindow):
    def __init__(self, host):
        super().__init__()
        self.setWindowTitle("Robot")
        self.settings = QtCore.QSettings("robot", "robot_app")
        self.link = Link()
        self.pad = Gamepad()
        self.held = set()
        self.slow = False
        self.current = (0, 0, 0)
        self.last_sent = 0.0
        self.lost_since = None
        self.label = ""
        self.events = []         # (robot time s, label)
        self.recorder = None
        self.rec_file = None
        self.rec_count = 0
        self.rec_start_t = None
        self.paused = False
        self.rate_count = 0
        self.rate_t = time.time()
        self.rate = 0.0

        self.t = np.zeros(BUFFER)
        self.data = np.zeros((BUFFER, 9))  # ax ay az |a| gx gy gz left right
        self.n = 0
        self.t_last = 0.0

        self._build_ui()
        self.apply_theme(self.settings.value("theme", "light"))

        self.timer = QtCore.QTimer(self, interval=5, timeout=self.tick)
        self.timer.start()
        self.plot_timer = QtCore.QTimer(self, interval=33, timeout=self.redraw)
        self.plot_timer.start()
        QtWidgets.QApplication.instance().installEventFilter(self)

        start = host or self.settings.value("last_address", "") or "robot.local"
        self.host.setText(start)
        self.connect_robot()

    # UI -------------------------------------------------------------------

    def _build_ui(self):
        central = QtWidgets.QWidget()
        central.setFocusPolicy(QtCore.Qt.StrongFocus)
        self.setCentralWidget(central)
        root = QtWidgets.QVBoxLayout(central)
        root.setContentsMargins(12, 10, 12, 8)
        root.setSpacing(8)

        top = QtWidgets.QHBoxLayout()
        self.host = QtWidgets.QLineEdit()
        self.host.setMaximumWidth(170)
        self.host.setPlaceholderText("robot.local or address")
        # Typing here only after a click, so the arrow keys drive by default.
        self.host.setFocusPolicy(QtCore.Qt.ClickFocus)
        self.host.returnPressed.connect(self.connect_robot)
        top.addWidget(QtWidgets.QLabel("Robot"))
        top.addWidget(self.host)
        top.addWidget(QtWidgets.QPushButton("Connect", clicked=self.connect_robot))
        top.addWidget(QtWidgets.QPushButton("Search", clicked=self.search_robot))
        self.conn = QtWidgets.QLabel()
        top.addWidget(self.conn, 1)
        self.theme_btn = QtWidgets.QPushButton("Dark", clicked=self.toggle_theme)
        top.addWidget(self.theme_btn)
        root.addLayout(top)

        self.banner = QtWidgets.QFrame(objectName="banner")
        bl = QtWidgets.QHBoxLayout(self.banner)
        bl.setContentsMargins(10, 6, 10, 6)
        self.banner_text = QtWidgets.QLabel(objectName="bannerText")
        self.reset_btn = QtWidgets.QPushButton("Reset", clicked=lambda: self.link.send("R"))
        bl.addWidget(self.banner_text, 1)
        bl.addWidget(self.reset_btn)
        self.banner.hide()
        root.addWidget(self.banner)

        cards = QtWidgets.QHBoxLayout()
        self.card_link = Card("Link", ["Address", "Firmware", "Signal", "Uptime", "Commands"])
        self.card_bat = Card("Battery", ["Motor pack", "Board cell"])
        self.card_imu = Card("IMU", ["Rate", "Samples", "Missed", "Gyro bias"])
        self.card_drive = Card("Drive", ["Command", "Input", "Inhibit"])
        for c in (self.card_link, self.card_bat, self.card_imu, self.card_drive):
            cards.addWidget(c, 1)
        root.addLayout(cards)

        middle = QtWidgets.QHBoxLayout()
        pg.setConfigOptions(antialias=False)
        self.plots = pg.GraphicsLayoutWidget()
        self.plot_items = []
        self.curves = []
        specs = [("Acceleration", "g", ["x", "y", "z", "|a|"], (-2.5, 2.5)),
                 ("Rotation rate", "deg/s", ["x", "y", "z"], (-250, 250)),
                 ("Side speed", "%", ["left", "right"], (-105, 105))]
        for row, (title, unit, names, yr) in enumerate(specs):
            p = self.plots.addPlot(row=row, col=0, title=title)
            p.setLabel("left", unit)
            p.setYRange(*yr)
            p.showGrid(x=True, y=True, alpha=0.2)
            p.setClipToView(True)
            p.setDownsampling(auto=True, mode="peak")
            p.addLegend(offset=(5, 5))
            if row > 0:
                p.setXLink(self.plot_items[0])
            self.plot_items.append(p)
            for i, name in enumerate(names):
                self.curves.append(p.plot(pen=pg.mkPen(COLORS[i], width=1.3), name=name))
        self.markers = []        # (robot time s, label, lines in each plot)
        self.marked = []
        middle.addWidget(self.plots, 1)

        side = QtWidgets.QVBoxLayout()
        self.drive_pad = DrivePad()
        side.addWidget(self.drive_pad)
        sp = QtWidgets.QHBoxLayout()
        sp.addWidget(QtWidgets.QLabel("Speed"))
        self.speed = QtWidgets.QSlider(QtCore.Qt.Horizontal, minimum=MIN_SPEED, maximum=100,
                                       singleStep=5, pageStep=10)
        self.speed.setValue(int(self.settings.value("speed", 100)))
        self.speed.setFocusPolicy(QtCore.Qt.NoFocus)
        self.speed_label = QtWidgets.QLabel(f"{self.speed.value()} %")
        self.speed.valueChanged.connect(self.speed_changed)
        sp.addWidget(self.speed, 1)
        sp.addWidget(self.speed_label)
        side.addLayout(sp)
        self.stop_btn = QtWidgets.QPushButton("STOP  (Space)", objectName="stop",
                                              clicked=self.emergency_stop)
        self.stop_btn.setMinimumHeight(44)
        self.stop_btn.setFocusPolicy(QtCore.Qt.NoFocus)
        side.addWidget(self.stop_btn)
        hint = QtWidgets.QLabel("Arrows drive, Shift = half speed.\n"
                                "Up/Down + Left/Right = curve.")
        hint.setObjectName("muted")
        side.addWidget(hint)

        view = QtWidgets.QHBoxLayout()
        view.addWidget(QtWidgets.QLabel("Window"))
        self.window_box = QtWidgets.QComboBox()
        self.window_box.addItems([f"{s} s" for s in WINDOWS])
        self.window_box.setCurrentIndex(WINDOWS.index(int(self.settings.value("window", 5))))
        self.window_box.setFocusPolicy(QtCore.Qt.NoFocus)
        view.addWidget(self.window_box)
        self.pause_btn = QtWidgets.QPushButton("Pause", checkable=True, toggled=self.toggle_pause)
        self.pause_btn.setFocusPolicy(QtCore.Qt.NoFocus)
        view.addWidget(self.pause_btn)
        side.addLayout(view)

        rec_box = QtWidgets.QGroupBox("Recording")
        rl = QtWidgets.QVBoxLayout(rec_box)
        folder = QtWidgets.QHBoxLayout()
        self.folder = self.settings.value("folder", os.getcwd())
        self.folder_label = QtWidgets.QLabel()
        self.folder_label.setObjectName("muted")
        self._show_folder()
        folder.addWidget(self.folder_label, 1)
        folder.addWidget(QtWidgets.QPushButton("Folder…", clicked=self.choose_folder))
        rl.addLayout(folder)
        self.rec_btn = QtWidgets.QPushButton("Record", checkable=True, toggled=self.toggle_record)
        self.rec_btn.setFocusPolicy(QtCore.Qt.NoFocus)
        rl.addWidget(self.rec_btn)
        labels = QtWidgets.QGridLayout()
        for i, (key, name) in enumerate(LABELS):
            b = QtWidgets.QPushButton(f"{name or 'none'}  {key}")
            b.setFocusPolicy(QtCore.Qt.NoFocus)
            b.clicked.connect(lambda _=False, n=name: self.set_label(n))
            QtGui.QShortcut(QtGui.QKeySequence(key), self,
                            activated=lambda n=name: self.set_label(n))
            labels.addWidget(b, i // 2, i % 2)
        rl.addLayout(labels)
        self.label_now = QtWidgets.QLabel("Label: none")
        rl.addWidget(self.label_now)
        self.event_list = QtWidgets.QListWidget()
        self.event_list.setFocusPolicy(QtCore.Qt.NoFocus)
        self.event_list.setMaximumHeight(110)
        rl.addWidget(self.event_list)
        side.addWidget(rec_box)
        side.addStretch(1)

        side_w = QtWidgets.QWidget()
        side_w.setLayout(side)
        side_w.setFixedWidth(270)
        middle.addWidget(side_w)
        root.addLayout(middle, 1)

        QtGui.QShortcut(QtGui.QKeySequence(QtCore.Qt.Key_Space), self,
                        activated=self.emergency_stop)
        QtGui.QShortcut(QtGui.QKeySequence(QtCore.Qt.Key_Escape), self.host,
                        activated=self.host.clearFocus)
        central.setFocus()
        self.resize(1250, 850)

    def apply_theme(self, name):
        self.theme_name = name if name in THEMES else "light"
        th = THEMES[self.theme_name]
        self.theme_btn.setText("Light" if self.theme_name == "dark" else "Dark")
        self.setStyleSheet(f"""
            QMainWindow, QWidget {{ background: {th['bg']}; color: {th['fg']}; }}
            QFrame#card {{ background: {th['card']}; border: 1px solid {th['line']};
                           border-radius: 8px; }}
            QFrame#card QLabel {{ background: transparent; }}
            QLabel#cardTitle {{ font-weight: 600; }}
            QLabel#muted {{ color: {th['muted']}; }}
            QLabel[warn="true"] {{ color: {th['warn']}; font-weight: 600; }}
            QFrame#banner {{ background: {th['stop_bg']}; border-radius: 6px; }}
            QLabel#bannerText {{ color: {th['stop_fg']}; background: transparent;
                                 font-weight: 600; }}
            QPushButton {{ background: {th['card']}; border: 1px solid {th['line']};
                           border-radius: 6px; padding: 5px 10px; }}
            QPushButton:checked {{ background: {th['accent']}; color: white; }}
            QPushButton#stop {{ background: #c0262d; color: white; font-weight: 700;
                                border: none; font-size: 15px; }}
            QLineEdit, QComboBox, QListWidget {{ background: {th['card']};
                border: 1px solid {th['line']}; border-radius: 5px; padding: 3px; }}
            QGroupBox {{ border: 1px solid {th['line']}; border-radius: 8px;
                         margin-top: 14px; padding-top: 6px; }}
            QGroupBox::title {{ subcontrol-origin: margin; left: 10px; }}
        """)
        self.plots.setBackground(th["plot"])
        for p in self.plot_items:
            for ax in ("left", "bottom"):
                p.getAxis(ax).setPen(th["axis"])
                p.getAxis(ax).setTextPen(th["axis"])
            p.titleLabel.setText(p.titleLabel.text, color=th["axis"])
        self.drive_pad.theme = th
        self.drive_pad.update()
        self.settings.setValue("theme", self.theme_name)

    def toggle_theme(self):
        self.apply_theme("light" if self.theme_name == "dark" else "dark")

    # Connection ---------------------------------------------------------------

    def connect_robot(self):
        self.host.clearFocus()
        host = self.host.text().strip() or "robot.local"
        if self.link.connect(host):
            self.conn.setText(f"connecting to {self.link.addr[0]} …")
        else:
            self.conn.setText(f"{host} not found, searching the network …")
            self.search_robot()
        self.lost_since = None

    def search_robot(self):
        nets = ", ".join(str(n) for n in local_subnets()) or "no network"
        self.conn.setText(f"searching {nets} …")
        self.link.search()

    def check_connection(self):
        now = time.time()
        if self.link.alive:
            self.lost_since = None
            return
        if self.link.searching:
            if now - self.link.last_keepalive > 3:
                self.search_robot()
            return
        if self.lost_since is None:
            self.lost_since = now
        self.conn.setText("no answer from the robot, retrying …")
        if now - self.lost_since > SEARCH_AFTER_S:
            # The robot may have a new address after a reset.
            self.lost_since = now
            self.connect_robot()

    def show_status(self, st):
        (_, v0, v1, v2, rssi, inhibit, low, imu_ok, _, motor_mv, board_mv, samples,
         timeouts, dropped, bias_updates, bx, by, bz, uptime, cmds, gap_max, send_max) = st
        addr = self.link.addr[0]
        if self.host.text().strip() not in (addr, "robot.local") and not self.host.hasFocus():
            self.host.setText(addr)
        self.settings.setValue("last_address", addr)
        self.conn.setText("connected" + (f" · gamepad {self.pad.name}" if self.pad.name else ""))

        c = self.card_link
        c.set("Address", addr)
        c.set("Firmware", f"{v0}.{v1}.{v2}")
        c.set("Signal", f"{rssi} dBm", warn=rssi < -75)
        c.set("Uptime", f"{uptime / 1000:.0f} s")
        if cmds is None:
            c.set("Commands", "firmware too old")
        else:
            c.set("Commands", f"{cmds} rx, max gap {gap_max} ms, send {send_max} ms",
                  warn=gap_max > 500 and cmds > 1)

        b = self.card_bat
        b.set("Motor pack", f"{motor_mv / 1000:.2f} V" if motor_mv >= 0 else "not measured",
              warn=bool(low & 1))
        b.set("Board cell", f"{board_mv / 1000:.2f} V" if board_mv >= 0 else "not measured",
              warn=bool(low & 2))

        i = self.card_imu
        if imu_ok:
            i.set("Rate", f"{self.rate:.0f} Hz received")
            i.set("Samples", f"{samples}")
            i.set("Missed", f"{timeouts} int, {dropped} dropped, {self.link.lost_packets} lost",
                  warn=bool(dropped or self.link.lost_packets))
            i.set("Gyro bias", f"{bx / 1000:+.2f} {by / 1000:+.2f} {bz / 1000:+.2f} °/s "
                               f"({bias_updates}x)")
        else:
            i.set("Rate", "IMU not running", warn=True)

        reasons = [n for bit, n in INHIBIT_NAMES.items() if inhibit & bit]
        self.card_drive.set("Inhibit", ", ".join(reasons) or "none", warn=bool(reasons))
        self.banner.setVisible(bool(reasons))
        self.banner_text.setText("Stopped: " + ", ".join(reasons))
        self.reset_btn.setVisible(bool(inhibit & 4))

    # Driving ------------------------------------------------------------------

    def eventFilter(self, obj, ev):
        if ev.type() in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
            key = ev.key()
            if (key in ARROWS or key == QtCore.Qt.Key_Shift) and not self.host.hasFocus():
                if not ev.isAutoRepeat():
                    pressed = ev.type() == QtCore.QEvent.KeyPress
                    if key == QtCore.Qt.Key_Shift:
                        self.slow = pressed
                    elif pressed:
                        self.held.add(ARROWS[key])
                    else:
                        self.held.discard(ARROWS[key])
                return True
        if ev.type() == QtCore.QEvent.ApplicationDeactivate:
            # Never keep driving with keys the window no longer sees.
            self.held.clear()
            self.slow = False
        return super().eventFilter(obj, ev)

    def emergency_stop(self):
        self.held.clear()
        self.link.send("D 0 0 0")
        self.current = (0, 0, 0)
        self.statusBar().showMessage("stopped", 2000)

    def speed_changed(self, v):
        self.speed_label.setText(f"{v} %")
        self.settings.setValue("speed", v)

    def command(self):
        pad = self.pad.command()
        if pad:
            return pad, "gamepad"
        throttle = ("up" in self.held) - ("down" in self.held)
        turn = ("right" in self.held) - ("left" in self.held)
        if throttle == 0 and turn == 0:
            return (0, 0, 0), "keyboard"
        speed = self.speed.value()
        if self.slow:
            speed = max(MIN_SPEED, speed // 2)
        return (throttle, turn, speed), "keyboard"

    def drive(self):
        cmd, source = self.command()
        now = time.time()
        if cmd != self.current or (cmd != (0, 0, 0) and now - self.last_sent >= REPEAT_S):
            self.link.send("D %d %d %d" % cmd)
            self.current = cmd
            self.last_sent = now
        moving = cmd != (0, 0, 0)
        words = {(1, 0): "forward", (-1, 0): "backward", (0, -1): "spin left",
                 (0, 1): "spin right", (1, -1): "forward left", (1, 1): "forward right",
                 (-1, -1): "backward left", (-1, 1): "backward right"}
        self.card_drive.set("Command", f"{words[cmd[:2]]} at {cmd[2]} %" if moving else "stop")
        self.card_drive.set("Input", source if moving else "–")
        left = int(self.data[(self.n - 1) % BUFFER, 7]) if self.n else 0
        right = int(self.data[(self.n - 1) % BUFFER, 8]) if self.n else 0
        self.drive_pad.update_state(cmd[0], cmd[1], left, right)

    # Data -----------------------------------------------------------------------

    def tick(self):
        samples, status = self.link.poll()
        self.drive()
        if status:
            self.show_status(status)
        self.check_connection()
        for s in samples:
            t = s[0] / 1e6
            if self.n and t < self.t_last - 1000:
                t += 4294.967296  # microsecond counter wrapped
            ax, ay, az = s[1] * ACCEL_G, s[2] * ACCEL_G, s[3] * ACCEL_G
            row = [ax, ay, az, math.sqrt(ax * ax + ay * ay + az * az),
                   s[4] * GYRO_DPS, s[5] * GYRO_DPS, s[6] * GYRO_DPS, s[10], s[11]]
            i = self.n % BUFFER
            self.t[i] = t
            self.data[i] = row
            self.n += 1
            self.t_last = t
            if self.recorder:
                self.recorder.writerow([s[0], *(round(v, 4) for v in row[:3]),
                                        *(round(v, 2) for v in row[4:7]),
                                        *s[7:13], self.label])
                self.rec_count += 1
        self.rate_count += len(samples)
        if time.time() - self.rate_t >= 1.0:
            self.rate = self.rate_count / (time.time() - self.rate_t)
            self.rate_count = 0
            self.rate_t = time.time()

    def redraw(self):
        if self.n == 0 or self.paused:
            return
        window = WINDOWS[self.window_box.currentIndex()]
        k = min(self.n, BUFFER)
        idx = np.arange(self.n - k, self.n) % BUFFER
        t = self.t[idx] - self.t_last
        keep = t >= -window
        t = t[keep]
        d = self.data[idx][keep]
        for i, curve in enumerate(self.curves):
            curve.setData(t, d[:, i])
        self.plot_items[0].setXRange(-window, 0, padding=0)
        self._draw_markers(window)

    def _draw_markers(self, window):
        """Place one dashed line per label change; lines move with the data."""
        th = THEMES[self.theme_name]
        for t_ev, name, lines in self.markers:
            x = t_ev - self.t_last
            for line in lines:
                line.setPos(x)
                line.setVisible(x >= -window)
        while self.markers and self.t_last - self.markers[0][0] > WINDOWS[-1]:
            for p, line in zip(self.plot_items, self.markers.pop(0)[2]):
                p.removeItem(line)
        for t_ev, name in self.events[len(self.marked):]:
            lines = []
            for p in self.plot_items:
                first = p is self.plot_items[0]
                line = pg.InfiniteLine(pos=t_ev - self.t_last, angle=90,
                                       label=(name or "none") if first else None,
                                       pen=pg.mkPen(th["warn"], width=1,
                                                    style=QtCore.Qt.DashLine),
                                       labelOpts={"position": 0.92, "color": th["warn"]})
                p.addItem(line)
                lines.append(line)
            self.markers.append((t_ev, name, lines))
        self.marked = list(self.events)

    def toggle_pause(self, on):
        self.paused = on
        self.pause_btn.setText("Resume" if on else "Pause")
        # Paused plots can be zoomed and panned with the mouse.
        for p in self.plot_items:
            p.setMouseEnabled(x=on, y=on)
        if not on:
            for p in self.plot_items:
                p.enableAutoRange(y=False)

    # Recording ----------------------------------------------------------------

    def _show_folder(self):
        text = self.folder.replace(os.path.expanduser("~"), "~")
        metrics = self.folder_label.fontMetrics()
        self.folder_label.setText(metrics.elidedText(text, QtCore.Qt.ElideMiddle, 160))
        self.folder_label.setToolTip(self.folder)

    def choose_folder(self):
        path = QtWidgets.QFileDialog.getExistingDirectory(self, "Recording folder", self.folder)
        if path:
            self.folder = path
            self.settings.setValue("folder", path)
            self._show_folder()

    def set_label(self, name):
        self.label = name
        self.label_now.setText(f"Label: {name or 'none'}")
        self.events.append((self.t_last, name))
        if self.recorder and self.rec_start_t is not None:
            self.event_list.addItem(f"{self.t_last - self.rec_start_t:6.1f} s  {name or 'none'}")
            self.event_list.scrollToBottom()

    def toggle_record(self, on):
        if on:
            path = os.path.join(self.folder, time.strftime("robot-%Y%m%d-%H%M%S.csv"))
            try:
                self.rec_file = open(path, "w", newline="")
            except OSError as e:
                self.statusBar().showMessage(f"cannot record: {e}", 8000)
                self.rec_btn.setChecked(False)
                return
            self.recorder = csv.writer(self.rec_file)
            self.recorder.writerow(["t_us", "ax_g", "ay_g", "az_g", "gx_dps", "gy_dps",
                                    "gz_dps", "throttle", "turn", "speed", "left", "right",
                                    "inhibit", "label"])
            self.rec_count = 0
            self.rec_start_t = self.t_last
            self.event_list.clear()
            self.rec_btn.setText(f"Stop  ({os.path.basename(path)})")
        elif self.recorder:
            self.recorder = None
            self.rec_file.close()
            self.statusBar().showMessage(f"saved {self.rec_count} samples to "
                                         f"{self.rec_file.name}", 15000)
            self.rec_btn.setText("Record")

    def closeEvent(self, ev):
        self.link.close()
        if self.recorder:
            self.toggle_record(False)
        self.settings.setValue("window", WINDOWS[self.window_box.currentIndex()])
        super().closeEvent(ev)


def main():
    app = QtWidgets.QApplication(sys.argv)
    w = Window(sys.argv[1] if len(sys.argv) > 1 else "")
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
