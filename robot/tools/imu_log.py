#!/usr/bin/env python3
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""Record the robot's IMU stream to CSV, with event labels from the keyboard.

Usage: imu_log.py <robot address> [-o out.csv] [-s seconds]

Command-line alternative to robot_app.py for recording only.

While recording, press a key to label what happens next:
  c collision   b hard stop   r reverse   p push by hand   l lift
  s doorsill/bump   n none (clear label)   q quit
"""

import argparse
import csv
import os
import select
import socket
import struct
import sys
import time

HEADER = struct.Struct("<IIIIHBB")  # magic, seq, dropped, int_timeouts, rate, count, size
SAMPLE = struct.Struct("<I3h3hbbBbbB")  # t_us, accel, gyro, throttle, turn, speed, l, r, inh
MAGIC = 0x31554D49
ACCEL_G_PER_LSB = 0.0039
GYRO_DPS_PER_LSB = 1 / 14.375
LABELS = {"c": "collision", "b": "brake", "r": "reverse", "p": "push", "l": "lift",
          "s": "bump", "n": ""}


class Keys:
    """Non-blocking single-key input when stdin is a terminal."""

    def __init__(self):
        self.tty = sys.stdin.isatty()
        if self.tty:
            import termios
            import tty

            self.fd = sys.stdin.fileno()
            self.old = termios.tcgetattr(self.fd)
            tty.setcbreak(self.fd)

    def get(self):
        if self.tty and select.select([sys.stdin], [], [], 0)[0]:
            return sys.stdin.read(1)
        return None

    def close(self):
        if self.tty:
            import termios

            termios.tcsetattr(self.fd, termios.TCSADRAIN, self.old)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host")
    ap.add_argument("-p", "--port", type=int, default=4244)
    ap.add_argument("-o", "--out", default=time.strftime("imu-%Y%m%d-%H%M%S.csv"))
    ap.add_argument("-s", "--seconds", type=float, default=0, help="stop after this long")
    args = ap.parse_args()

    addr = (socket.gethostbyname(args.host), args.port)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.2)
    keys = Keys()
    label = ""
    start = time.time()
    last_sub = 0.0
    last_print = 0.0
    last_seq = None
    lost_packets = 0
    n_samples = 0
    prev_t = None
    gaps = 0
    stats = (0, 0, 0)

    with open(args.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["t_us", "ax_g", "ay_g", "az_g", "gx_dps", "gy_dps", "gz_dps",
                    "throttle", "turn", "speed", "left", "right", "inhibit", "label"])
        try:
            while True:
                now = time.time()
                if args.seconds and now - start >= args.seconds:
                    break
                if now - last_sub >= 1.0:
                    sock.sendto(b"SUB", addr)
                    last_sub = now

                k = keys.get()
                if k == "q":
                    break
                if k in LABELS:
                    label = LABELS[k]

                try:
                    data, _ = sock.recvfrom(2048)
                except socket.timeout:
                    data = None

                if data and len(data) >= HEADER.size:
                    magic, seq, dropped, timeouts, rate, count, size = HEADER.unpack_from(data)
                    if magic != MAGIC or size != SAMPLE.size:
                        continue
                    if last_seq is not None and seq != last_seq + 1:
                        lost_packets += (seq - last_seq - 1) & 0xFFFFFFFF
                    last_seq = seq
                    stats = (rate, dropped, timeouts)
                    for i in range(count):
                        s = SAMPLE.unpack_from(data, HEADER.size + i * SAMPLE.size)
                        t = s[0]
                        # A gap longer than 1.5 sample periods is a missed sample.
                        if prev_t is not None and ((t - prev_t) & 0xFFFFFFFF) > 1.5e6 / rate:
                            gaps += 1
                        prev_t = t
                        w.writerow([t,
                                    *(round(v * ACCEL_G_PER_LSB, 4) for v in s[1:4]),
                                    *(round(v * GYRO_DPS_PER_LSB, 2) for v in s[4:7]),
                                    *s[7:13], label])
                        n_samples += 1
                    last = s

                if now - last_print >= 0.5 and n_samples:
                    rate, dropped, timeouts = stats
                    el = now - start
                    a = [v * ACCEL_G_PER_LSB for v in last[1:4]]
                    g = [v * GYRO_DPS_PER_LSB for v in last[4:7]]
                    sys.stderr.write(
                        f"\r{el:6.1f}s {n_samples / el:6.1f} Hz  gaps {gaps}  lost pkts "
                        f"{lost_packets}  robot drops {dropped}  int timeouts {timeouts}  "
                        f"a[g] {a[0]:+.2f} {a[1]:+.2f} {a[2]:+.2f}  "
                        f"g[dps] {g[0]:+6.1f} {g[1]:+6.1f} {g[2]:+6.1f}  [{label or '-'}] ")
                    sys.stderr.flush()
                    last_print = now
        finally:
            keys.close()
            sys.stderr.write(f"\n{n_samples} samples written to {os.path.abspath(args.out)}\n")


if __name__ == "__main__":
    main()
