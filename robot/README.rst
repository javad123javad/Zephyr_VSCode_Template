4WD Robot
#########

Four DC motors driven through GPIO from an ESP32-WROOM (``esp32_devkitc``). All
motor driver inputs are active low. Pin assignment is in
``boards/esp32_devkitc_procpu.overlay``.

Wiring
******

Each motor has two driver inputs, one per direction. Which input turns the
wheel forward depends on how the motor is mounted and wired; ``motor_inverted``
in ``src/drive.c`` reverses M2 and M3 so that a positive speed is forward for
every wheel.

All inputs are active low and driven by LEDC PWM channels 0-7 at 1 kHz, so
the motors start at 35 % power and ramp up to full speed in about 0.6 s.
Stops are immediate.

+-------+-------------+-------------+-------------+
| Motor | -1          | -2          | Wheel       |
+=======+=============+=============+=============+
| M1    | GPIO13      | GPIO2       | Rear left   |
+-------+-------------+-------------+-------------+
| M2    | GPIO14      | GPIO27      | Rear right  |
+-------+-------------+-------------+-------------+
| M3    | GPIO26      | GPIO25      | Front right |
+-------+-------------+-------------+-------------+
| M4    | GPIO33      | GPIO32      | Front left  |
+-------+-------------+-------------+-------------+

The overlay disables SPI2, which the board routes to GPIO13/14.

GPIO2 is a strapping pin: it must be low or floating to enter the serial
bootloader. If flashing fails to connect, disconnect M1-2 while flashing.

Battery Monitoring
******************

Both batteries are measured through resistor dividers on ADC1, which keeps
working while Wi-Fi is active:

+-------------------------+--------+-----------------------+-----------------+
| Battery                 | Pin    | Divider (top/bottom)  | Pin at full     |
+=========================+========+=======================+=================+
| Motor pack, 3S Li-ion   | GPIO34 | 100 kOhm / 22 kOhm    | 2.27 V          |
+-------------------------+--------+-----------------------+-----------------+
| Board cell, 1S Li-ion   | GPIO35 | 100 kOhm / 100 kOhm   | 2.10 V          |
+-------------------------+--------+-----------------------+-----------------+

Put 100 nF from each pin to ground. The ADC reads up to about 2.5 V. Monitoring
is off by default because unconnected pins float; enable it once the dividers
are fitted::

   CONFIG_ROBOT_BATTERY=y

Levels per cell, evaluated after the motors have rested for 1 s so that load
sag does not trigger them:

* motor pack below 3.4 V: speed capped at 60 % (clears above 3.5 V); below
  3.2 V: driving blocked (clears above 3.4 V);
* board cell below 3.5 V: speed capped (clears above 3.6 V); below 3.3 V:
  driving blocked (clears above 3.5 V);
* while driving, a pack below 3.0 V per cell (board: 3.1 V) for 3 s blocks
  driving;
* a pack reading below 1 V per cell counts as switched off, not empty.

To calibrate, compare ``robot status`` with a multimeter and set
``CONFIG_ROBOT_BATT_MOTOR_CAL`` / ``CONFIG_ROBOT_BATT_BOARD_CAL`` to
multimeter / shown x 1000. Different resistors are set with the
``CONFIG_ROBOT_BATT_*_DIV_*`` options.

Blocked driving is shown on the web page with its reason. A low battery
clears by itself when the voltage recovers; a detected collision is latched
until the page's Reset button, ``robot reset`` or a ``reset`` message.

Building and Flashing
*********************

Put the Wi-Fi credentials in ``wifi.conf`` (ignored by git):

.. code-block:: console

   cp wifi.conf.example wifi.conf

.. code-block:: console

   west build -b esp32_devkitc/esp32/procpu workspace/robot
   west flash
   west espressif monitor

Shell Commands
**************

.. code-block:: console

   robot fwd [ms]      # drive forward, stops after ms (default 1000, 0 = until stop)
   robot back [ms]
   robot left [ms]     # spin in place
   robot right [ms]
   robot stop
   robot motor <1-4> <fwd|rev|stop|-100..100>   # one motor, no ramp
   robot status        # battery voltages and inhibit state
   robot reset         # clear a latched inhibit (collision)

Web Control
***********

After joining the network the robot logs its address::

   <inf> wifi: Open http://robot.local/ or http://192.168.1.42/ to drive the robot

Open either one in a browser. ``robot.local`` is answered over mDNS; Linux
needs Avahi and Windows 10 or later resolves it natively.

* Arrow keys drive; up or down together with left or right drives a curve with
  the inner side slowed to 30 %. Left or right alone spins in place.
* The slider sets the speed (30-100 %, remembered by the browser). Hold Shift
  for half of it.
* On a phone, hold the on-screen arrows; two fingers give a curve.

Acceleration is ramped, slowing down and stopping are immediate. A side that
changes direction coasts for about 150 ms before starting the other way. Each command
runs for 700 ms and the page repeats it every 150 ms while a key is held, so
the robot stops when the key is released, the tab loses focus or the
connection drops.

The page keeps a WebSocket open on ``/ws`` and sends
``<throttle> <turn> <speed>`` text messages, for example ``1 -1 80`` for a
forward-left curve at 80 %. The robot answers with a status message twice a
second (signal strength for now). Closing the socket stops the robot, and a
new connection takes over from an old one, so a phone that reconnects is not
locked out. While the socket is down the page posts the same text to
``/drive``, which also works from the command line::

   curl -d "1 0 60" http://robot.local/drive
