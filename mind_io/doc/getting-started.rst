.. SPDX-License-Identifier: Apache-2.0

Writing an application for an I/O card
######################################

Plug the card in and write the application against the names printed on the
card. You don't need to know which host bus, chip-select or GPIO is behind
them: the card's EEPROM says so, and ``mind_io`` resolves it at boot.

1. Add the module
*****************

In the application's ``CMakeLists.txt``, before ``find_package(Zephyr)``:

.. code-block:: cmake

   list(APPEND ZEPHYR_EXTRA_MODULES /path/to/mind_io)

On a processing board whose devicetree has the ``mind,din41612-io``
connector node (mindos_n6 does), ``CONFIG_MIND_IO`` is enabled automatically.
Also enable the bus drivers you use (``CONFIG_I2C``, ``CONFIG_SPI``,
``CONFIG_GPIO``).

2. Attach your devices to the card's ports
******************************************

Devices that have a Zephyr driver go in a devicetree overlay under a port
slot. Use the card's slot numbers (``mio info`` or ``mio ports`` shows them),
and connector GPIO numbers through ``mio_gpios``:

.. code-block:: devicetree

   &mio_i2c_port0 {
           bme280@76 {
                   compatible = "bosch,bme280";
                   reg = <0x76>;
           };
   };

   &mio_spi_port0 {
           can@0 {
                   compatible = "microchip,mcp2515";
                   reg = <0>;                  /* always 0: the slot drives the CS */
                   spi-max-frequency = <1000000>;
                   osc-freq = <8000000>;
                   int-gpios = <&mio_gpios 3 GPIO_ACTIVE_LOW>;  /* IO_GPIO3 */
           };
   };

The standard drivers then work unchanged: ``DEVICE_DT_GET_ONE(bosch_bme280)``,
``sensor_sample_fetch()``, ``can_send()``, and so on. If the plugged-in card
has no port in that slot, the slot and the devices under it are simply not
ready (``device_is_ready()`` returns false).

Devices under a slot must initialise after it
(``CONFIG_MIND_IO_PORT_INIT_PRIORITY``, 56). Most drivers already do:
sensors at 90, CAN controllers at 80, AT24 EEPROMs at 80. If a driver uses
the common device default of 50, raise its init priority option. Otherwise
the build stops with an error such as ``eeprom@57 ... is initialized before
its dependency /connector/i2c-port-0``.

3. Use lines and ports by name
******************************

.. code-block:: c

   #include <mind_io/mind_io.h>

   struct mio_line relay;

   if (mio_line_get("RELAY1", &relay) == 0) {
           mio_line_set(&relay, 1);            /* logical: 1 = active */
   }

   const struct device *bus = mio_port_by_label("SENSOR2");
   if (bus != NULL) {
           i2c_write_read(bus, 0x40, &reg, 1, buf, 2);   /* plain Zephyr API */
   }

What the application gets from the card:

* ``mio_card_status()`` and ``mio_card_get()`` tell whether a valid card is
  present, and what it is.
* ``mio_has_resource(MIO_RES_RS485)`` and similar tell which connector
  resources the card uses, e.g. whether to start a Modbus server on the
  RS-485 UART.
* ``mio_fault_led_set()`` drives the card's red fault LED. ``mind_io`` lights
  it itself when the card is blank or its description is invalid.

Lines keep the direction, active level and pulls the card declares.
Connector GPIOs that the card doesn't declare are never touched.

4. Program a card
*****************

Describe the card in YAML (see ``tests/golden/dev_card.yaml``), then build and
program it through the shell of any firmware that includes ``mind_io``:

.. code-block:: console

   python3 scripts/mio_card.py build card.yaml -o card.bin
   python3 scripts/mio_card.py decode card.bin            # check what you wrote
   python3 scripts/mio_card.py program card.bin --port /dev/ttyACM0

``program`` saves the current EEPROM contents to a backup file first. It then
writes the image with ``mio eeprom write`` and verifies it by reading it back.
Reset the board afterwards, and check the result with ``mio info``.

Shell commands
**************

.. code-block:: none

   mio info                          card status, identity, resources, warnings
   mio ports                         I2C/SPI ports and whether their slot is ready
   mio lines                         lines and their current state
   mio line <label> [0|1]            read or set a line
   mio led <on|off>                  fault LED
   mio eeprom read <offset> <len>    raw card EEPROM bytes
   mio eeprom write <offset> <hex>   raw write
