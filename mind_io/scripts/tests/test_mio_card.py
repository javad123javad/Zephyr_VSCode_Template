# SPDX-License-Identifier: Apache-2.0
"""Unit tests of scripts/mio_card.py. Run with:

    python3 -m unittest discover -s scripts/tests
"""

import copy
import pathlib
import struct
import sys
import unittest
import zlib

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import mio_card  # noqa: E402  pylint: disable=wrong-import-position

GOLDEN_YAML = ROOT / "tests" / "golden" / "dev_card.yaml"
GOLDEN_BIN = ROOT / "tests" / "golden" / "dev_card.bin"


def load_desc():
    with open(GOLDEN_YAML, encoding="utf-8") as f:
        return yaml.safe_load(f)


def reseal(body_without_crc):
    """Fixes the length field and appends a fresh CRC."""
    image = bytearray(body_without_crc)
    struct.pack_into("<H", image, 6, len(image) + mio_card.CRC_SIZE)
    return bytes(image) + struct.pack("<I", zlib.crc32(bytes(image)) & 0xFFFFFFFF)


class GoldenImage(unittest.TestCase):
    def test_build_matches_golden(self):
        image = mio_card.encode(mio_card.card_from_yaml(load_desc()))
        self.assertEqual(image, GOLDEN_BIN.read_bytes())

    def test_decode_round_trip(self):
        golden = GOLDEN_BIN.read_bytes()
        desc = mio_card.card_to_yaml(mio_card.decode(golden))
        self.assertEqual(mio_card.encode(mio_card.card_from_yaml(desc)), golden)

    def test_trailing_bytes_ignored(self):
        golden = GOLDEN_BIN.read_bytes()
        card = mio_card.decode(golden + b"\xff" * 64)
        self.assertEqual(mio_card.encode(card), golden)

    def test_unknown_record_skipped(self):
        golden = GOLDEN_BIN.read_bytes()
        body = golden[:-mio_card.CRC_SIZE] + bytes([0xE0, 3, 1, 2, 3])
        card = mio_card.decode(reseal(body))
        self.assertEqual(mio_card.encode(card), golden)


class BadImages(unittest.TestCase):
    def test_blank_eeprom(self):
        with self.assertRaisesRegex(mio_card.CardError, "no card header"):
            mio_card.decode(b"\xff" * 64)

    def test_crc_mismatch(self):
        image = bytearray(GOLDEN_BIN.read_bytes())
        image[20] ^= 0x01
        with self.assertRaisesRegex(mio_card.CardError, "CRC"):
            mio_card.decode(bytes(image))

    def test_truncated(self):
        with self.assertRaisesRegex(mio_card.CardError, "length"):
            mio_card.decode(GOLDEN_BIN.read_bytes()[:100])

    def test_wrong_version(self):
        image = bytearray(GOLDEN_BIN.read_bytes())
        image[4] = 2
        with self.assertRaisesRegex(mio_card.CardError, "version"):
            mio_card.decode(bytes(image))


class Validation(unittest.TestCase):
    def build(self, mutate):
        desc = copy.deepcopy(load_desc())
        mutate(desc)
        return mio_card.card_from_yaml(desc)

    def test_line_on_chip_select_rejected(self):
        def mutate(desc):
            desc["spi_ports"].append({"slot": 1, "label": "SPI2", "cs": "GPIO1"})
            desc["lines"].append({"label": "X", "pin": "GPIO1"})
        with self.assertRaisesRegex(mio_card.CardError, "chip-select"):
            self.build(mutate)

    def test_duplicate_label_rejected(self):
        def mutate(desc):
            desc["lines"][1]["label"] = "SENSOR1"
        with self.assertRaisesRegex(mio_card.CardError, "duplicate"):
            self.build(mutate)

    def test_nonexistent_gpio_rejected(self):
        def mutate(desc):
            desc["lines"].append({"label": "X", "pin": "GPIO4"})
        with self.assertRaisesRegex(mio_card.CardError, "does not exist"):
            self.build(mutate)

    def test_expander_without_device_rejected(self):
        def mutate(desc):
            desc["onboard_devices"] = []
        with self.assertRaisesRegex(mio_card.CardError, "PCA9557"):
            self.build(mutate)

    def test_port_reference_must_exist(self):
        def mutate(desc):
            desc["lines"][0]["port"] = "spi3"
        with self.assertRaisesRegex(mio_card.CardError, "port"):
            self.build(mutate)

    def test_label_too_long_rejected(self):
        def mutate(desc):
            desc["i2c_ports"][0]["label"] = "A" * 16
        with self.assertRaisesRegex(mio_card.CardError, "label"):
            self.build(mutate)


class FakeShell:
    """Serial-port stand-in answering like the Zephyr shell with "mio"
    commands: VT100 colored prompt, echo of each command, and an initial
    log line."""

    PROMPT = b"\x1b[1;32muart:~$ \x1b[m"

    def __init__(self, mem, write_protected=False):
        self.mem = bytearray(mem)
        self.write_protected = write_protected
        self.pending = bytearray(b"[00:00:01.000,000] <inf> mind_io: boot log\r\n")

    def reset_input_buffer(self):
        self.pending.clear()

    def write(self, data):
        line = data.decode("ascii").strip()
        out = bytearray(data.rstrip(b"\r") + b"\r\n")
        words = line.split()
        if words[:3] == ["mio", "eeprom", "read"]:
            off, length = int(words[3], 0), int(words[4], 0)
            out += b"DATA " + self.mem[off:off + length].hex().encode() + b"\r\n"
        elif words[:3] == ["mio", "eeprom", "write"]:
            if self.write_protected:
                out += b"\x1b[1;31mcard EEPROM write failed (-5)\x1b[m\r\n"
            else:
                off, data_bytes = int(words[3], 0), bytes.fromhex(words[4])
                self.mem[off:off + len(data_bytes)] = data_bytes
                out += b"wrote %d bytes\r\n" % len(data_bytes)
        self.pending += out + self.PROMPT

    def read(self, size):
        chunk = bytes(self.pending[:size])
        del self.pending[:size]
        return chunk


class Programming(unittest.TestCase):
    def setUp(self):
        self.image = GOLDEN_BIN.read_bytes()
        self.backup = pathlib.Path(self.id() + ".bin")

    def tearDown(self):
        self.backup.unlink(missing_ok=True)

    def test_program_with_colored_prompt(self):
        fake = FakeShell(b"\xff" * 512)
        mio_card.program(mio_card.MioShell(fake, timeout=0.5), self.image, self.backup)
        self.assertEqual(bytes(fake.mem[:len(self.image)]), self.image)
        self.assertEqual(self.backup.read_bytes(), b"\xff" * len(self.image))

    def test_write_protected(self):
        fake = FakeShell(b"\xff" * 512, write_protected=True)
        with self.assertRaisesRegex(mio_card.CardError, "failed"):
            mio_card.program(mio_card.MioShell(fake, timeout=0.5), self.image, self.backup)

    def test_wrong_prompt_reports_received_bytes(self):
        with self.assertRaisesRegex(mio_card.CardError, "received"):
            mio_card.MioShell(FakeShell(b"\xff" * 16), timeout=0.2, prompt="shell>")


if __name__ == "__main__":
    unittest.main()
