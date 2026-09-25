.. SPDX-License-Identifier: Apache-2.0

mind_io: self-describing I/O cards for Zephyr
#############################################

A universal I/O card plugs into a processing board through a DIN41612
connector with a fixed pinout. The card's M24C64 EEPROM describes the card:
which connector resources it uses, which I2C/SPI ports it exposes, and what
its GPIO lines do. ``mind_io`` reads that description at boot, so an
application can use ``SENSOR1``, ``RELAY1`` or ``CAN1`` without knowing which
host bus, chip-select or pin is behind them.

* ``doc/getting-started.rst``: writing an application for a card, and
  programming cards.
* ``doc/card-format.rst``: the EEPROM format and the connector resources.

Layout
******

.. code-block:: none

   include/mind_io/card_format.h   portable card format API (no Zephyr dependency)
   include/mind_io/mind_io.h       application API
   lib/card_format/                card format encoder/decoder (C99)
   subsys/mind_io/                 core (boot-time read), lines, pins, shell
   drivers/i2c/, drivers/spi/      I2C / SPI port slot drivers
   dts/bindings/                   connector, GPIO nexus, port slot bindings
   scripts/mio_card.py             build / decode / program card images
   samples/card_info/              minimal application
   tests/                          ztest suites (native_sim) and golden image

How it fits together
********************

* **Connector node** (``mind,din41612-io``) in the processing board's
  devicetree. It maps the fixed connector signals to that board's
  peripherals, and holds the port slot nodes ``mio_i2c_port0..3`` and
  ``mio_spi_port0..3``.
* **GPIO nexus** (``mind,din41612-gpios``). Overlays refer to connector GPIOs
  as ``<&mio_gpios n flags>``.
* **Core**, running at ``POST_KERNEL`` 55. It reads the image with raw I2C at
  address 0x50 on I2C_CNF, validates it, sets up the lines, and lights the
  fault LED for a blank or invalid card.
* **Port slots**, at ``POST_KERNEL`` 56. Each slot binds to the host bus the
  card assigns to it; SPI slots also drive the chip-select. A slot the card
  doesn't provide fails init, so the devices under it are not ready.

Testing
*******

.. code-block:: console

   west twister -p native_sim -T mind_io/tests
   python3 -m unittest discover -s mind_io/scripts/tests

The ztest suites run on ``native_sim``, with emulators for:

* the 16-bit card EEPROM, preloaded with the golden image;
* the PCA9557;
* an SPI echo device.

They cover a valid, a blank and an absent card. ``tests/golden/dev_card.bin``
is built from ``dev_card.yaml`` by ``mio_card.py``, and the C tests check the
C encoder produces the same bytes.

Status
******

Version 1 of the format. Not yet in the format: I2C mux ports, expander
interrupts, and a writable installation area. A Linux library reusing
``lib/card_format`` is planned.
