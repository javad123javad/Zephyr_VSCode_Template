#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build, decode and program mind_io I/O card description images.

  mio_card.py build card.yaml -o card.bin    YAML description -> EEPROM image
  mio_card.py decode card.bin                image -> YAML (validates it)
  mio_card.py program card.bin --port /dev/ttyACM0
                                             write it through the "mio eeprom"
                                             shell commands, after saving a
                                             backup of the current contents

The binary layout is documented in doc/card-format.rst and must stay in sync
with lib/card_format/card_format.c (the golden-image tests check both).
"""

import argparse
import datetime
import re
import struct
import sys
import time
import zlib

MAGIC = b"MIOC"
VERSION = 1
HDR_SIZE = 8
CRC_SIZE = 4
LABEL_LEN = 16
CARD_EEPROM_ADDR = 0x50

MAX_I2C_PORTS = 4
MAX_SPI_PORTS = 4
MAX_ONBOARD_DEVS = 2
NUM_GPIO = 4
EXPANDER_PINS = 8
MAX_LINES = NUM_GPIO + MAX_ONBOARD_DEVS * EXPANDER_PINS

RESOURCES = ["I2C_CNF", "I2C_IO", "SPI", "SPI_CS0", "UART", "RS485", "I2S",
             "USB1", "USB2", "FAULT_LED", "GPIO0", "GPIO1", "GPIO2", "GPIO3"]
RES = {name: bit for bit, name in enumerate(RESOURCES)}

REC_IDENTITY, REC_RESOURCES = 0x01, 0x02
REC_I2C_PORT, REC_SPI_PORT = 0x10, 0x11
REC_LINE, REC_ONBOARD_DEVICE = 0x20, 0x30

PIN_SPI_CS0, PIN_GPIO, PIN_EXPANDER = 1, 2, 3
TOPOLOGIES = ["direct", "repeater"]
SPEEDS = ["any", "standard", "fast", "fast_plus"]
ROLES = ["generic", "relay", "led", "button", "irq", "reset", "enable", "fault"]
DEVICE_TYPES = {"PCA9557": 1}

LINE_OUTPUT, LINE_ACTIVE_LOW, LINE_PULL_UP = 0x01, 0x02, 0x04
LINE_PULL_DOWN, LINE_INIT_ACTIVE, LINE_OPEN_DRAIN = 0x08, 0x10, 0x20
CS_ACTIVE_HIGH = 0x01
PORT_REF_NONE = 0xFF
PORT_KIND_I2C, PORT_KIND_SPI = 1, 2


class CardError(Exception):
    """Invalid card description or image."""


# ---------------------------------------------------------------- helpers

def _label(value, what):
    text = str(value)
    raw = text.encode("ascii")
    if not raw or len(raw) >= LABEL_LEN:
        raise CardError(f"{what} '{text}': must be 1..{LABEL_LEN - 1} ASCII characters")
    return raw.ljust(LABEL_LEN, b"\0")


def _text(value, what):
    raw = str(value).encode("ascii")
    if len(raw) > LABEL_LEN:
        raise CardError(f"{what} '{value}': at most {LABEL_LEN} ASCII characters")
    return raw.ljust(LABEL_LEN, b"\0")


def _choice(value, choices, what):
    key = str(value).lower()
    if key not in choices:
        raise CardError(f"{what} '{value}': expected one of {', '.join(choices)}")
    return choices.index(key)


def _resource(name, what):
    key = str(name).upper()
    if key not in RES:
        raise CardError(f"{what} '{name}': unknown resource")
    return RES[key]


def parse_pin(text):
    """'SPI_CS0', 'GPIO<n>' or 'EXP<dev>.<pin>' -> (source, dev, pin)."""
    key = str(text).upper()
    if key == "SPI_CS0":
        return (PIN_SPI_CS0, 0, 0)
    match = re.fullmatch(r"GPIO(\d+)", key)
    if match:
        return (PIN_GPIO, 0, int(match.group(1)))
    match = re.fullmatch(r"EXP(\d+)\.(\d+)", key)
    if match:
        return (PIN_EXPANDER, int(match.group(1)), int(match.group(2)))
    raise CardError(f"pin '{text}': expected SPI_CS0, GPIO<n> or EXP<dev>.<pin>")


def pin_name(pin):
    source, dev, num = pin
    if source == PIN_SPI_CS0:
        return "SPI_CS0"
    if source == PIN_GPIO:
        return f"GPIO{num}"
    if source == PIN_EXPANDER:
        return f"EXP{dev}.{num}"
    return f"?{source}"


def _port_ref(text):
    if text is None:
        return PORT_REF_NONE
    match = re.fullmatch(r"(i2c|spi)(\d+)", str(text).lower())
    if not match:
        raise CardError(f"port '{text}': expected i2c<slot> or spi<slot>")
    kind = PORT_KIND_I2C if match.group(1) == "i2c" else PORT_KIND_SPI
    return (kind << 4) | (int(match.group(2)) & 0x0F)


def _int(value):
    """Accepts ints and numeric strings such as '0x19'."""
    return value if isinstance(value, int) else int(str(value), 0)


def _date(value):
    if value is None:
        return 0
    if isinstance(value, (datetime.date, datetime.datetime)):
        return value.year * 10000 + value.month * 100 + value.day
    return int(str(value).replace("-", ""))


# ------------------------------------------------- description <-> model

def card_from_yaml(desc):
    """Turns a YAML description (dict) into the internal card model."""
    card = {"identity": None, "resources": 0, "devices": [], "i2c": [], "spi": [],
            "lines": []}
    used = {RES["I2C_CNF"]}

    ident = desc.get("identity")
    if ident is not None:
        major, _, minor = str(ident.get("hw_rev", "0.0")).partition(".")
        card["identity"] = {
            "hw_major": int(major), "hw_minor": int(minor or 0),
            "date": _date(ident.get("date")),
            "name": _text(ident.get("name", ""), "identity name"),
            "serial": _text(ident.get("serial", ""), "identity serial"),
        }

    for name in desc.get("resources", []):
        used.add(_resource(name, "resources entry"))

    for dev in desc.get("onboard_devices", []):
        type_name = str(dev["type"]).upper()
        if type_name not in DEVICE_TYPES:
            raise CardError(f"on-card device type '{dev['type']}' unknown")
        bus = _resource(dev.get("bus", "I2C_IO"), "on-card device bus")
        used.add(bus)
        card["devices"].append({"index": _int(dev["index"]), "type": DEVICE_TYPES[type_name],
                                "bus": bus, "addr": _int(dev["address"])})

    for port in desc.get("i2c_ports", []):
        bus = _resource(port.get("bus", "I2C_IO"), "I2C port bus")
        used.add(bus)
        card["i2c"].append({
            "slot": _int(port["slot"]), "bus": bus,
            "topology": _choice(port.get("topology", "direct"), TOPOLOGIES, "topology"),
            "speed": _choice(port.get("speed", "any"), SPEEDS, "speed"),
            "label": _label(port["label"], "I2C port label"),
        })

    for port in desc.get("spi_ports", []):
        cs = parse_pin(port.get("cs", "SPI_CS0"))
        used.add(RES["SPI"])
        if cs[0] == PIN_SPI_CS0:
            used.add(RES["SPI_CS0"])
        elif cs[0] == PIN_GPIO and cs[2] < NUM_GPIO:
            used.add(RES["GPIO0"] + cs[2])
        active = _choice(port.get("cs_active", "low"), ["low", "high"], "cs_active")
        card["spi"].append({
            "slot": _int(port["slot"]), "cs": cs,
            "cs_flags": CS_ACTIVE_HIGH if active == 1 else 0,
            "max_freq": _int(port.get("max_freq", 0)),
            "label": _label(port["label"], "SPI port label"),
        })

    for line in desc.get("lines", []):
        pin = parse_pin(line["pin"])
        if pin[0] == PIN_GPIO and pin[2] < NUM_GPIO:
            used.add(RES["GPIO0"] + pin[2])
        flags = 0
        if _choice(line.get("direction", "input"), ["input", "output"], "direction") == 1:
            flags |= LINE_OUTPUT
        if _choice(line.get("active", "high"), ["high", "low"], "active") == 1:
            flags |= LINE_ACTIVE_LOW
        pull = _choice(line.get("pull", "none"), ["none", "up", "down"], "pull")
        flags |= {0: 0, 1: LINE_PULL_UP, 2: LINE_PULL_DOWN}[pull]
        if _choice(line.get("init", "inactive"), ["inactive", "active"], "init") == 1:
            flags |= LINE_INIT_ACTIVE
        if line.get("open_drain", False):
            flags |= LINE_OPEN_DRAIN
        card["lines"].append({
            "pin": pin, "flags": flags,
            "role": _choice(line.get("role", "generic"), ROLES, "role"),
            "port": _port_ref(line.get("port")),
            "label": _label(line["label"], "line label"),
        })

    for bit in used:
        card["resources"] |= 1 << bit
    validate(card)
    return card


def card_to_yaml(card):
    """Turns the internal card model back into a YAML-style description."""
    desc = {}
    ident = card["identity"]
    if ident is not None:
        date = ident["date"]
        desc["identity"] = {
            "name": ident["name"].rstrip(b"\0").decode("ascii"),
            "serial": ident["serial"].rstrip(b"\0").decode("ascii"),
            "hw_rev": f"{ident['hw_major']}.{ident['hw_minor']}",
            "date": f"{date // 10000:04d}-{date // 100 % 100:02d}-{date % 100:02d}"
                    if date else None,
        }
    desc["resources"] = [name for bit, name in enumerate(RESOURCES)
                         if card["resources"] & (1 << bit)]
    unknown = card["resources"] & ~((1 << len(RESOURCES)) - 1)
    if unknown:
        desc["unknown_resource_bits"] = hex(unknown)
    type_names = {v: k for k, v in DEVICE_TYPES.items()}
    desc["onboard_devices"] = [
        {"index": d["index"], "type": type_names.get(d["type"], d["type"]),
         "bus": RESOURCES[d["bus"]] if d["bus"] < len(RESOURCES) else d["bus"],
         "address": hex(d["addr"])} for d in card["devices"]]
    desc["i2c_ports"] = [
        {"slot": p["slot"], "label": _unlabel(p["label"]), "bus": RESOURCES[p["bus"]],
         "topology": TOPOLOGIES[p["topology"]], "speed": SPEEDS[p["speed"]]}
        for p in card["i2c"]]
    desc["spi_ports"] = [
        {"slot": p["slot"], "label": _unlabel(p["label"]), "cs": pin_name(p["cs"]),
         "cs_active": "high" if p["cs_flags"] & CS_ACTIVE_HIGH else "low",
         "max_freq": p["max_freq"]} for p in card["spi"]]
    lines = []
    for line in card["lines"]:
        flags = line["flags"]
        entry = {
            "label": _unlabel(line["label"]), "pin": pin_name(line["pin"]),
            "direction": "output" if flags & LINE_OUTPUT else "input",
            "active": "low" if flags & LINE_ACTIVE_LOW else "high",
            "pull": "up" if flags & LINE_PULL_UP else
                    "down" if flags & LINE_PULL_DOWN else "none",
            "init": "active" if flags & LINE_INIT_ACTIVE else "inactive",
            "role": ROLES[line["role"]] if line["role"] < len(ROLES) else line["role"],
        }
        if flags & LINE_OPEN_DRAIN:
            entry["open_drain"] = True
        if line["port"] != PORT_REF_NONE:
            kind = "i2c" if line["port"] >> 4 == PORT_KIND_I2C else "spi"
            entry["port"] = f"{kind}{line['port'] & 0x0F}"
        lines.append(entry)
    desc["lines"] = lines
    return desc


def _unlabel(raw):
    return raw.split(b"\0", 1)[0].decode("ascii")


# ------------------------------------------------------------ validation

def validate(card):
    """Same rules as validate() in lib/card_format/card_format.c."""
    res = card["resources"]

    def used(name):
        return bool(res & (1 << RES[name]))

    if not used("I2C_CNF"):
        raise CardError("I2C_CNF must always be declared")
    if (len(card["i2c"]) > MAX_I2C_PORTS or len(card["spi"]) > MAX_SPI_PORTS
            or len(card["lines"]) > MAX_LINES or len(card["devices"]) > MAX_ONBOARD_DEVS):
        raise CardError("too many records of one kind")

    devices = {}
    for dev in card["devices"]:
        if dev["index"] >= MAX_ONBOARD_DEVS or dev["index"] in devices:
            raise CardError(f"on-card device index {dev['index']} invalid or duplicate")
        if dev["bus"] not in (RES["I2C_IO"], RES["I2C_CNF"]) or not res & (1 << dev["bus"]):
            raise CardError("on-card device on an undeclared or non-I2C bus")
        if not 0x08 <= dev["addr"] <= 0x77:
            raise CardError(f"on-card device address {dev['addr']:#x} out of range")
        if dev["bus"] == RES["I2C_CNF"] and dev["addr"] == CARD_EEPROM_ADDR:
            raise CardError("on-card device collides with the card EEPROM address")
        devices[dev["index"]] = dev

    def check_pin(pin, allow_cs0, what):
        source, dev, num = pin
        if source == PIN_SPI_CS0:
            if not allow_cs0:
                raise CardError(f"{what}: SPI_CS0 can only be used as an SPI chip-select")
            if not used("SPI_CS0"):
                raise CardError(f"{what}: SPI_CS0 not declared")
        elif source == PIN_GPIO:
            if num >= NUM_GPIO:
                raise CardError(f"{what}: connector GPIO{num} does not exist")
            if not res & (1 << (RES["GPIO0"] + num)):
                raise CardError(f"{what}: GPIO{num} not declared")
        elif source == PIN_EXPANDER:
            if dev not in devices or devices[dev]["type"] != DEVICE_TYPES["PCA9557"]:
                raise CardError(f"{what}: EXP{dev} is not an on-card PCA9557")
            if num >= EXPANDER_PINS:
                raise CardError(f"{what}: expander pin {num} out of range")
        else:
            raise CardError(f"{what}: invalid pin source")

    i2c_slots = set()
    for port in card["i2c"]:
        if port["slot"] >= MAX_I2C_PORTS or port["slot"] in i2c_slots:
            raise CardError(f"I2C port slot {port['slot']} invalid or duplicate")
        if port["bus"] not in (RES["I2C_IO"], RES["I2C_CNF"]) or not res & (1 << port["bus"]):
            raise CardError("I2C port on an undeclared or non-I2C bus")
        if port["topology"] >= len(TOPOLOGIES) or port["speed"] >= len(SPEEDS):
            raise CardError("I2C port topology or speed invalid")
        i2c_slots.add(port["slot"])

    spi_slots, cs_pins = set(), []
    for port in card["spi"]:
        if port["slot"] >= MAX_SPI_PORTS or port["slot"] in spi_slots:
            raise CardError(f"SPI port slot {port['slot']} invalid or duplicate")
        if not used("SPI"):
            raise CardError("SPI port but SPI not declared")
        check_pin(port["cs"], True, f"SPI port {port['slot']} chip-select")
        if _pin_key(port["cs"]) in cs_pins:
            raise CardError("two SPI ports share a chip-select")
        spi_slots.add(port["slot"])
        cs_pins.append(_pin_key(port["cs"]))

    line_pins = []
    for line in card["lines"]:
        what = f"line {_unlabel(line['label'])}"
        check_pin(line["pin"], False, what)
        key = _pin_key(line["pin"])
        if key in line_pins:
            raise CardError(f"{what}: pin already used by another line")
        if key in cs_pins:
            raise CardError(f"{what}: pin already used as an SPI chip-select")
        line_pins.append(key)
        if line["flags"] & LINE_PULL_UP and line["flags"] & LINE_PULL_DOWN:
            raise CardError(f"{what}: both pull-up and pull-down")
        ref = line["port"]
        if ref != PORT_REF_NONE:
            slots = i2c_slots if ref >> 4 == PORT_KIND_I2C else \
                spi_slots if ref >> 4 == PORT_KIND_SPI else set()
            if (ref & 0x0F) not in slots:
                raise CardError(f"{what}: refers to a port the card does not define")

    labels = [p["label"] for p in card["i2c"]] + [p["label"] for p in card["spi"]] + \
        [line["label"] for line in card["lines"]]
    for raw in labels:
        if not raw.rstrip(b"\0") or b"\0" not in raw:
            raise CardError("port/line label empty or too long")
    if len(set(labels)) != len(labels):
        raise CardError("duplicate port/line label")


def _pin_key(pin):
    source, dev, num = pin
    return (source, dev if source == PIN_EXPANDER else 0, num)


# ------------------------------------------------------ encode / decode

def encode(card):
    validate(card)
    body = bytearray()

    def rec(rtype, payload):
        body.extend(struct.pack("<BB", rtype, len(payload)))
        body.extend(payload)

    ident = card["identity"]
    if ident is not None:
        rec(REC_IDENTITY, struct.pack("<BBI", ident["hw_major"], ident["hw_minor"],
                                      ident["date"]) + ident["name"] + ident["serial"])
    rec(REC_RESOURCES, struct.pack("<I", card["resources"]))
    for dev in card["devices"]:
        rec(REC_ONBOARD_DEVICE, struct.pack("<BHBBB", dev["index"], dev["type"], dev["bus"],
                                            dev["addr"], 0))
    for port in card["i2c"]:
        rec(REC_I2C_PORT, struct.pack("<BBBB", port["slot"], port["bus"], port["topology"],
                                      port["speed"]) + port["label"])
    for port in card["spi"]:
        source, dev, num = port["cs"]
        rec(REC_SPI_PORT, struct.pack("<BBBBBBI", port["slot"], source, dev, num,
                                      port["cs_flags"], 0, port["max_freq"]) + port["label"])
    for line in card["lines"]:
        source, dev, num = line["pin"]
        rec(REC_LINE, struct.pack("<BBBBBB", source, dev, num, line["flags"], line["role"],
                                  line["port"]) + line["label"])

    total = HDR_SIZE + len(body) + CRC_SIZE
    image = bytearray(MAGIC + struct.pack("<BBH", VERSION, 0, total)) + body
    image += struct.pack("<I", zlib.crc32(bytes(image)) & 0xFFFFFFFF)
    return bytes(image)


def decode(image):
    if len(image) < HDR_SIZE or image[:4] != MAGIC:
        raise CardError("no card header (blank or unprogrammed EEPROM)")
    if image[4] != VERSION:
        raise CardError(f"unsupported card format version {image[4]}")
    total = struct.unpack_from("<H", image, 6)[0]
    if total < HDR_SIZE + CRC_SIZE or total > len(image):
        raise CardError("image length mismatch")
    end = total - CRC_SIZE
    if struct.unpack_from("<I", image, end)[0] != zlib.crc32(image[:end]) & 0xFFFFFFFF:
        raise CardError("CRC mismatch")

    card = {"identity": None, "resources": None, "devices": [], "i2c": [], "spi": [],
            "lines": []}
    off = HDR_SIZE
    while off < end:
        if end - off < 2:
            raise CardError("truncated record header")
        rtype, rlen = image[off], image[off + 1]
        payload = image[off + 2:off + 2 + rlen]
        if len(payload) != rlen or off + 2 + rlen > end:
            raise CardError("record runs past the end of the image")
        _decode_record(card, rtype, payload)
        off += 2 + rlen

    if card["resources"] is None:
        raise CardError("no RESOURCES record")
    validate(card)
    return card


def _decode_record(card, rtype, p):
    def need(size, name):
        if len(p) < size:
            raise CardError(f"{name} record too short")

    if rtype == REC_IDENTITY:
        need(38, "IDENTITY")
        if card["identity"] is not None:
            raise CardError("duplicate IDENTITY record")
        major, minor, date = struct.unpack_from("<BBI", p)
        card["identity"] = {"hw_major": major, "hw_minor": minor, "date": date,
                            "name": bytes(p[6:22]), "serial": bytes(p[22:38])}
    elif rtype == REC_RESOURCES:
        need(4, "RESOURCES")
        if card["resources"] is not None:
            raise CardError("duplicate RESOURCES record")
        card["resources"] = struct.unpack_from("<I", p)[0]
    elif rtype == REC_ONBOARD_DEVICE:
        need(6, "ONBOARD_DEVICE")
        index, dtype, bus, addr, _ = struct.unpack_from("<BHBBB", p)
        card["devices"].append({"index": index, "type": dtype, "bus": bus, "addr": addr})
    elif rtype == REC_I2C_PORT:
        need(20, "I2C_PORT")
        slot, bus, topology, speed = struct.unpack_from("<BBBB", p)
        card["i2c"].append({"slot": slot, "bus": bus, "topology": topology, "speed": speed,
                            "label": bytes(p[4:20])})
    elif rtype == REC_SPI_PORT:
        need(26, "SPI_PORT")
        slot, source, dev, num, flags, _, freq = struct.unpack_from("<BBBBBBI", p)
        card["spi"].append({"slot": slot, "cs": (source, dev, num), "cs_flags": flags,
                            "max_freq": freq, "label": bytes(p[10:26])})
    elif rtype == REC_LINE:
        need(22, "LINE")
        source, dev, num, flags, role, port = struct.unpack_from("<BBBBBB", p)
        card["lines"].append({"pin": (source, dev, num), "flags": flags, "role": role,
                              "port": port, "label": bytes(p[6:22])})
    # anything else: unknown/vendor record, skipped


# ------------------------------------------------------------- programming

ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def _clean(raw):
    """Shell output as plain text: VT100 color/cursor codes and CRs removed."""
    return ANSI_ESCAPE.sub("", raw.decode("ascii", "replace")).replace("\r", "")


class MioShell:
    """Minimal driver for the Zephyr shell 'mio eeprom' commands.

    @p ser is an open pyserial port or any object with the same read(),
    write() and reset_input_buffer() methods.
    """

    def __init__(self, ser, timeout=3.0, prompt="uart:~$"):
        self.ser = ser
        self.timeout = timeout
        self.prompt = prompt.rstrip()
        # The first attempt may only flush pending log output
        for attempt in range(3):
            try:
                self.command("")
                return
            except CardError:
                if attempt == 2:
                    raise

    @classmethod
    def open(cls, port, baud, timeout=3.0, prompt="uart:~$"):
        import serial  # pylint: disable=import-outside-toplevel
        return cls(serial.Serial(port, baud, timeout=0.1), timeout, prompt)

    def command(self, line):
        self.ser.reset_input_buffer()
        self.ser.write(line.encode("ascii") + b"\r")
        raw = bytearray()
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            raw += self.ser.read(256)
            text = _clean(bytes(raw))
            if text.rstrip(" \n").endswith(self.prompt):
                break
        else:
            raise CardError(f"no shell prompt '{self.prompt}' after '{line}'; "
                            f"received: {bytes(raw[-160:])!r}")
        if "error" in text.lower() or "failed" in text.lower():
            raise CardError(f"'{line}' failed:\n{text}")
        return text

    def read(self, offset, length):
        text = self.command(f"mio eeprom read {offset} {length}")
        match = re.search(r"DATA ([0-9a-fA-F]*)", text)
        if not match or len(match.group(1)) != 2 * length:
            raise CardError(f"unexpected read output:\n{text}")
        return bytes.fromhex(match.group(1))

    def write(self, offset, data):
        self.command(f"mio eeprom write {offset} {data.hex()}")


CHUNK = 16  # matches the page-aligned chunking of "mio eeprom write"


def _read_all(shell, length):
    return b"".join(shell.read(off, min(CHUNK, length - off))
                    for off in range(0, length, CHUNK))


def program(shell, image, backup_path):
    old = _read_all(shell, len(image))
    with open(backup_path, "wb") as f:
        f.write(old)
    print(f"saved previous contents ({len(old)} bytes) to {backup_path}")
    for off in range(0, len(image), CHUNK):
        shell.write(off, image[off:off + CHUNK])
    if _read_all(shell, len(image)) != image:
        raise CardError("read-back does not match the written image")
    print(f"wrote and verified {len(image)} bytes; reset the board to apply the card")


# ------------------------------------------------------------------ main

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p_build = sub.add_parser("build", help="YAML description -> image")
    p_build.add_argument("yaml")
    p_build.add_argument("-o", "--output", required=True)
    p_decode = sub.add_parser("decode", help="image -> YAML description")
    p_decode.add_argument("image")
    p_prog = sub.add_parser("program", help="write an image to a card over the shell")
    p_prog.add_argument("image")
    p_prog.add_argument("--port", required=True)
    p_prog.add_argument("--baud", type=int, default=115200)
    p_prog.add_argument("--backup", default=None,
                        help="where to save the previous contents")
    p_prog.add_argument("--prompt", default="uart:~$",
                        help="shell prompt of the firmware (default: %(default)s)")
    args = parser.parse_args(argv)

    import yaml  # pylint: disable=import-outside-toplevel

    try:
        if args.cmd == "build":
            with open(args.yaml, encoding="utf-8") as f:
                image = encode(card_from_yaml(yaml.safe_load(f)))
            with open(args.output, "wb") as f:
                f.write(image)
            print(f"{args.output}: {len(image)} bytes")
        elif args.cmd == "decode":
            with open(args.image, "rb") as f:
                card = decode(f.read())
            yaml.safe_dump(card_to_yaml(card), sys.stdout, sort_keys=False)
        else:
            with open(args.image, "rb") as f:
                image = f.read()
            decode(image)
            backup = args.backup or time.strftime("card-backup-%Y%m%d-%H%M%S.bin")
            shell = MioShell.open(args.port, args.baud, prompt=args.prompt)
            program(shell, image, backup)
    except (CardError, KeyError, ValueError, OSError) as err:
        print(f"error: {err}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
