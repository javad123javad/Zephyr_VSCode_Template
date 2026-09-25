.. SPDX-License-Identifier: Apache-2.0

I/O card description format (version 1)
#######################################

Every I/O card carries a description of itself in an EEPROM. The processing
board reads it at boot and learns which connector resources the card uses,
which ports it exposes, and what its lines are for. The processing board keeps
no database of cards.

The format is implemented by ``lib/card_format/card_format.c`` (portable C99)
and ``scripts/mio_card.py``. Both are tested against
``tests/golden/dev_card.bin``.

Where the image lives
*********************

The connector contract fixes the location, so every processing board can find
the description before it knows anything else about the card:

* 7-bit address ``0x50`` on I2C_CNF (``IO_CNF_SCL`` / ``IO_CNF_SDA``);
* a 16-bit word-addressed EEPROM (such as the M24C64), with the image at
  offset 0;
* a page size that is a multiple of 16 bytes.

The rest of the EEPROM is free. In production, the card's WC pin should
write-protect the part holding the description.

Layout
******

All multi-byte values are little-endian.

.. code-block:: none

   offset  size  field
   0       4     magic "MIOC"
   4       1     format version (1)
   5       1     flags (0)
   6       2     total image length, header and CRC included
   8       ...   records
   len-4   4     CRC-32 (IEEE 802.3 / zlib) of bytes 0 .. len-5

Each record is ``type (1 byte), length (1 byte), payload``.

* A payload longer than the version 1 size below is accepted, and the extra
  bytes are ignored. That leaves room to append fields later.
* Record types the decoder doesn't know, including the customer/vendor range
  ``0xE0``–``0xFE``, are skipped. Older firmware can therefore read newer
  cards.
* An image that is erased (``0xFF``) or has no magic means "card not
  programmed".

Labels are 16-byte fields holding 1 to 15 ASCII characters, NUL-padded. Port
and line labels must be unique on a card, because applications look them up
by name.

Records
*******

``0x01 IDENTITY`` (38 bytes, optional, informative only)
   HW revision major (1), minor (1), manufacture date ``YYYYMMDD`` (4),
   name (16), serial (16).

``0x02 RESOURCES`` (4 bytes, required)
   Bitmap of the connector resources the card uses (see below). Unknown bits
   are ignored. ``I2C_CNF`` must always be set.

``0x30 ONBOARD_DEVICE`` (6 bytes)
   Index (1), type (2: ``1`` = PCA9557), bus resource (1: ``I2C_IO`` or
   ``I2C_CNF``), 7-bit address (1), reserved (1). This record also marks the
   address as taken on that bus.

``0x10 I2C_PORT`` (20 bytes)
   Slot (1, 0–3), bus resource (1), topology (1: ``0`` direct, ``1`` through
   a repeater), maximum speed (1: ``0`` any, ``1`` 100 kHz, ``2`` 400 kHz,
   ``3`` 1 MHz), label (16). Ports on the same host bus share one address
   space, even behind repeaters.

``0x11 SPI_PORT`` (26 bytes)
   Slot (1, 0–3), chip-select pin reference (3), CS flags (1: bit 0 set =
   active high), reserved (1), maximum frequency in Hz (4, ``0`` = no limit),
   label (16).

``0x20 LINE`` (22 bytes)
   Pin reference (3), flags (1), role (1), port reference (1), label (16).

   Flags:

   * bit 0: output;
   * bit 1: active low;
   * bit 2: pull-up;
   * bit 3: pull-down;
   * bit 4: initially active;
   * bit 5: open drain.

   Roles (informative only):

   * 0: generic;
   * 1: relay;
   * 2: LED;
   * 3: button;
   * 4: IRQ;
   * 5: reset;
   * 6: enable;
   * 7: fault.

   Port reference: ``0xFF`` for none. Otherwise the port kind is in the high
   nibble (``1`` I2C, ``2`` SPI) and the slot in the low nibble, e.g. "the
   interrupt line of SPI port 0" is ``0x20``.

A **pin reference** is ``source, device, pin`` (one byte each):

* source ``1``: ``IO_SPI_CS0`` (SPI chip-selects only);
* source ``2``: connector ``IO_GPIO<pin>``;
* source ``3``: pin ``<pin>`` of on-card device ``<device>`` (a PCA9557).

Validation
**********

The decoder rejects an image with ``-EINVAL`` when:

* a record refers to a resource that the ``RESOURCES`` record doesn't declare;
* a pin is used by two lines, or both as a line and as an SPI chip-select;
* two SPI ports share a chip-select, or two ports share a slot;
* an expander pin refers to a device that isn't an on-card PCA9557;
* a line refers to a port the card doesn't define;
* a label is empty, too long, or used twice;
* an on-card device sits at the card EEPROM address on I2C_CNF.

Connector resources
*******************

Resources are named after the DIN41612 signals. The values are bit positions
in ``RESOURCES`` and never change. Pin numbers depend on the side you look
from: the I/O-board pin is ``17 - n`` for MCU-side pin ``n`` in the same row
(A1 is A16 on the I/O board).

=========  ===  ====================================  ==========================
Resource   Bit  Signals                               MCU-side pins
=========  ===  ====================================  ==========================
I2C_CNF    0    IO_CNF_SCL / SDA                      A7 / A8
I2C_IO     1    IO_I2C_SCL / SDA                      C6 / C7
SPI        2    IO_SPI_SCK / MISO / MOSI              B6 / B4 / B7
SPI_CS0    3    IO_SPI_CS0                            B5
UART       4    IO_UART_RX / TX                       C8 / C9
RS485      5    IO_RS485_IN_A / B                     B15 / B16
I2S        6    IO_I2S_SDI / SCK / WS / SDO           C10 / C11 / C12 / C13
USB1       7    USB1_DI_N / P                         C4 / C5
USB2       8    USB2_DI_N / P                         C1 / C2
FAULT_LED  9    IO_F/A (red fault LED)                A6
GPIO0..3   10+  IO_GPIO0..3                           B3, B2, B1, A1
=========  ===  ====================================  ==========================

USB port power and over-current signals (MIC2026 PRTPWR / OCS) belong to the
processing board and are not card resources.
