#!/usr/bin/env python3
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""PC app for the robot: drive it, plot the IMU live and record labelled data.

Usage: robot_app.py [host]          (default robot.local)

Driving: arrow keys (Shift for half speed) or a gamepad's left stick.
Labels while recording: C collision, B brake, V reverse, P push, L lift,
S bump, N none. The protocol is described in src/link.h.
"""

import csv
import math
import os
import socket
import struct
import sys
import time

import numpy as np
import pyqtgraph as pg
from PySide6 import QtCore, QtGui, QtWidgets

PORT = 4244
IMU_HEADER = struct.Struct("<IIIIHBB")
IMU_SAMPLE = struct.Struct("<I3h3hbbBbbB")
STATUS = struct.Struct("<I3BbBBBBiiIIIIiiiI")
IMU_MAGIC = 0x31554D49
STATUS_MAGIC = 0x31415453
ACCEL_G = 0.0039
GYRO_DPS = 1 / 14.375

HISTORY_S = 5.0
BUFFER = 4096
REPEAT_S = 0.15
KEEPALIVE_S = 1.0
MIN_SPEED = 30

INHIBIT_NAMES = {1: "battery empty", 2: "obstacle", 4: "collision"}
LABELS = [("C", "collision"), ("B", "brake"), ("V", "reverse"), ("P", "push"),
          ("L", "lift"), ("S", "bump"), ("N", "")]
ARROWS = {QtCore.Qt.Key_Up: "up", QtCore.Qt.Key_Down: "down",
          QtCore.Qt.Key_Left: "left", QtCore.Qt.Key_Right: "right"}


class Link:
    """UDP session with the robot."""

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setblocking(False)
        self.addr = None
        self.last_keepalive = 0.0
        self.last_status = 0.0
        self.last_seq = None
        self.lost_packets = 0

    def connect(self, host):
        self.addr = (socket.gethostbyname(host), PORT)
        self.last_seq = None
        self.lost_packets = 0
        self.send("S")

    def send(self, text):
        if self.addr:
            try:
                self.sock.sendto(text.encode(), self.addr)
            except OSError:
                pass
            self.last_keepalive = time.time()

    def close(self):
        self.send("X")
        self.addr = None

    def poll(self):
        """Return (imu sample tuples, latest status tuple or None)."""
        samples, status = [], None
        if self.addr and time.time() - self.last_keepalive >= KEEPALIVE_S:
            self.send("S")
        while True:
            try:
                data, _ = self.sock.recvfrom(4096)
            except (BlockingIOError, OSError):
                break
            if len(data) < 4:
                continue
            magic = struct.unpack_from("<I", data)[0]
            if magic == STATUS_MAGIC and len(data) >= STATUS.size:
                status = STATUS.unpack_from(data)
                self.last_status = time.time()
            elif magic == IMU_MAGIC and len(data) >= IMU_HEADER.size:
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
        return self.addr is not None and time.time() - self.last_status < 1.5


class Gamepad:
    """Left stick of the first gamepad via pygame, if available."""

    def __init__(self):
        self.js = None
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


class Window(QtWidgets.QMainWindow):
    def __init__(self, host):
        super().__init__()
        self.setWindowTitle("Robot")
        self.link = Link()
        self.pad = Gamepad()
        self.held = set()
        self.slow = False
        self.current = (0, 0, 0)
        self.last_sent = 0.0
        self.label = ""
        self.recorder = None
        self.rec_count = 0

        self.t = np.zeros(BUFFER)
        self.data = np.zeros((BUFFER, 8))  # ax ay az gx gy gz left right
        self.n = 0
        self.t_last = 0.0

        self._build_ui(host)

        self.timer = QtCore.QTimer(self, interval=5, timeout=self.tick)
        self.timer.start()
        self.plot_timer = QtCore.QTimer(self, interval=33, timeout=self.redraw)
        self.plot_timer.start()
        QtWidgets.QApplication.instance().installEventFilter(self)
        self.connect_robot()

    # UI -----------------------------------------------------------------

    def _build_ui(self, host):
        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        layout = QtWidgets.QVBoxLayout(central)

        top = QtWidgets.QHBoxLayout()
        self.host = QtWidgets.QLineEdit(host)
        self.host.setMaximumWidth(180)
        connect = QtWidgets.QPushButton("Connect", clicked=self.connect_robot)
        self.state = QtWidgets.QLabel("not connected")
        top.addWidget(QtWidgets.QLabel("Robot"))
        top.addWidget(self.host)
        top.addWidget(connect)
        top.addWidget(self.state, 1)
        layout.addLayout(top)

        self.banner = QtWidgets.QWidget()
        bl = QtWidgets.QHBoxLayout(self.banner)
        bl.setContentsMargins(8, 4, 8, 4)
        self.banner_text = QtWidgets.QLabel()
        self.reset_btn = QtWidgets.QPushButton("Reset", clicked=lambda: self.link.send("R"))
        bl.addWidget(self.banner_text, 1)
        bl.addWidget(self.reset_btn)
        self.banner.setStyleSheet("background:#fde8e8; color:#9b1c1c; border-radius:6px")
        self.banner.hide()
        layout.addWidget(self.banner)

        pg.setConfigOptions(antialias=True)
        self.plots = pg.GraphicsLayoutWidget()
        colors = ["#2f6fdb", "#d9822b", "#2a9d58"]
        self.curves = []
        specs = [("Acceleration", "g", ["x", "y", "z"], (-2.5, 2.5)),
                 ("Rotation rate", "deg/s", ["x", "y", "z"], (-250, 250)),
                 ("Side speed", "%", ["left", "right"], (-105, 105))]
        for row, (title, unit, names, yr) in enumerate(specs):
            p = self.plots.addPlot(row=row, col=0, title=title)
            p.setLabel("left", unit)
            p.setYRange(*yr)
            p.setXRange(-HISTORY_S, 0)
            p.showGrid(x=True, y=True, alpha=0.2)
            p.addLegend(offset=(5, 5))
            for i, name in enumerate(names):
                self.curves.append(p.plot(pen=pg.mkPen(colors[i], width=1.5), name=name))
        layout.addWidget(self.plots, 1)

        bottom = QtWidgets.QHBoxLayout()
        bottom.addWidget(QtWidgets.QLabel("Speed"))
        self.speed = QtWidgets.QSlider(QtCore.Qt.Horizontal, minimum=MIN_SPEED, maximum=100,
                                       value=100, singleStep=5, pageStep=10)
        self.speed.setFocusPolicy(QtCore.Qt.NoFocus)
        self.speed_label = QtWidgets.QLabel("100 %")
        self.speed.valueChanged.connect(lambda v: self.speed_label.setText(f"{v} %"))
        bottom.addWidget(self.speed, 1)
        bottom.addWidget(self.speed_label)
        bottom.addSpacing(20)
        self.rec_btn = QtWidgets.QPushButton("Record", checkable=True, toggled=self.toggle_record)
        bottom.addWidget(self.rec_btn)
        for key, name in LABELS:
            b = QtWidgets.QPushButton(f"{name or 'none'} ({key})")
            b.setFocusPolicy(QtCore.Qt.NoFocus)
            b.clicked.connect(lambda _=False, n=name: self.set_label(n))
            QtGui.QShortcut(QtGui.QKeySequence(key), self, activated=lambda n=name: self.set_label(n))
            bottom.addWidget(b)
        layout.addLayout(bottom)

        self.info = QtWidgets.QLabel()
        self.statusBar().addPermanentWidget(self.info, 1)
        self.resize(1100, 800)

    # Connection and status ----------------------------------------------

    def connect_robot(self):
        try:
            self.link.connect(self.host.text().strip())
            self.state.setText(f"connecting to {self.link.addr[0]} ...")
        except OSError as e:
            self.state.setText(f"cannot resolve: {e}")

    def show_status(self, st):
        (_, v0, v1, v2, rssi, inhibit, low, imu_ok, _, motor_mv, board_mv, samples,
         timeouts, dropped, bias_updates, bx, by, bz, uptime) = st
        bat = []
        if motor_mv >= 0:
            bat.append(f"motor {motor_mv / 1000:.2f} V" + (" LOW" if low & 1 else ""))
        if board_mv >= 0:
            bat.append(f"board {board_mv / 1000:.2f} V" + (" LOW" if low & 2 else ""))
        pad = f" · gamepad {self.pad.name}" if self.pad.name else ""
        self.state.setText(f"connected · firmware {v0}.{v1}.{v2} · {rssi} dBm · "
                           f"{' · '.join(bat) or 'battery not measured'}{pad}")
        imu = (f"IMU {samples} samples, {timeouts} int timeouts, {dropped} dropped, "
               f"{self.link.lost_packets} lost packets, gyro bias measured {bias_updates}x"
               if imu_ok else "IMU not running")
        rec = f" · recording {self.rec_count} samples" if self.recorder else ""
        self.info.setText(f"{imu} · uptime {uptime / 1000:.0f} s{rec}")
        reasons = [n for bit, n in INHIBIT_NAMES.items() if inhibit & bit]
        self.banner.setVisible(bool(reasons))
        self.banner_text.setText("Stopped: " + ", ".join(reasons))
        self.reset_btn.setVisible(bool(inhibit & 4))

    # Driving --------------------------------------------------------------

    def eventFilter(self, obj, ev):
        if ev.type() in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
            key = ev.key()
            if key in ARROWS or key == QtCore.Qt.Key_Shift:
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

    def command(self):
        pad = self.pad.command()
        if pad:
            return pad
        throttle = ("up" in self.held) - ("down" in self.held)
        turn = ("right" in self.held) - ("left" in self.held)
        if throttle == 0 and turn == 0:
            return (0, 0, 0)
        speed = self.speed.value()
        if self.slow:
            speed = max(MIN_SPEED, speed // 2)
        return (throttle, turn, speed)

    def drive(self):
        cmd = self.command()
        now = time.time()
        changed = cmd != self.current
        if changed or (cmd != (0, 0, 0) and now - self.last_sent >= REPEAT_S):
            self.link.send("D %d %d %d" % cmd)
            self.current = cmd
            self.last_sent = now

    # Data -------------------------------------------------------------------

    def tick(self):
        samples, status = self.link.poll()
        self.drive()
        if status:
            self.show_status(status)
        elif self.link.addr and not self.link.alive:
            self.state.setText("no answer from the robot")
        for s in samples:
            t = s[0] / 1e6
            row = [s[1] * ACCEL_G, s[2] * ACCEL_G, s[3] * ACCEL_G,
                   s[4] * GYRO_DPS, s[5] * GYRO_DPS, s[6] * GYRO_DPS, s[10], s[11]]
            i = self.n % BUFFER
            self.t[i] = t
            self.data[i] = row
            self.n += 1
            self.t_last = t
            if self.recorder:
                self.recorder.writerow([s[0], *(round(v, 4) for v in row[:3]),
                                        *(round(v, 2) for v in row[3:6]),
                                        *s[7:13], self.label])
                self.rec_count += 1

    def redraw(self):
        if self.n == 0:
            return
        k = min(self.n, BUFFER)
        idx = (np.arange(self.n - k, self.n)) % BUFFER
        t = self.t[idx] - self.t_last
        keep = t >= -HISTORY_S
        t = t[keep]
        d = self.data[idx][keep]
        for i, curve in enumerate(self.curves):
            curve.setData(t, d[:, i])

    # Recording --------------------------------------------------------------

    def set_label(self, name):
        self.label = name
        self.statusBar().showMessage(f"label: {name or 'none'}", 3000)

    def toggle_record(self, on):
        if on:
            path = time.strftime("robot-%Y%m%d-%H%M%S.csv")
            self.rec_file = open(path, "w", newline="")
            self.recorder = csv.writer(self.rec_file)
            self.recorder.writerow(["t_us", "ax_g", "ay_g", "az_g", "gx_dps", "gy_dps",
                                    "gz_dps", "throttle", "turn", "speed", "left", "right",
                                    "inhibit", "label"])
            self.rec_count = 0
            self.rec_btn.setText(f"Stop ({path})")
        else:
            self.recorder = None
            self.rec_file.close()
            self.statusBar().showMessage(f"saved {self.rec_count} samples to "
                                         f"{os.path.abspath(self.rec_file.name)}", 10000)
            self.rec_btn.setText("Record")

    def closeEvent(self, ev):
        self.link.send("D 0 0 0")
        self.link.close()
        if self.recorder:
            self.toggle_record(False)
        super().closeEvent(ev)


def main():
    app = QtWidgets.QApplication(sys.argv)
    w = Window(sys.argv[1] if len(sys.argv) > 1 else "robot.local")
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
