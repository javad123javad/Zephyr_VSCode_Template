4WD Robot
#########

Four DC motors driven through GPIO from an ESP32-WROOM (``esp32_devkitc``). All
motor driver inputs are active low. Pin assignment is in
``boards/esp32_devkitc_procpu.overlay``.

Wiring
******

Each motor has two driver inputs: ``-1`` drives forward, ``-2`` drives reverse.
All inputs are active low and driven by LEDC PWM channels 0-7 at 1 kHz, so
the motors start at 35 % power and ramp up to full speed in about 0.6 s.
Stops are immediate.

+-------+-------------+-------------+-------+
| Motor | -1 (fwd)    | -2 (rev)    | Side  |
+=======+=============+=============+=======+
| M1    | GPIO13      | GPIO2       | Left  |
+-------+-------------+-------------+-------+
| M2    | GPIO14      | GPIO27      | Left  |
+-------+-------------+-------------+-------+
| M3    | GPIO26      | GPIO25      | Right |
+-------+-------------+-------------+-------+
| M4    | GPIO33      | GPIO32      | Right |
+-------+-------------+-------------+-------+

The overlay disables SPI2, which the board routes to GPIO13/14.

GPIO2 is a strapping pin: it must be low or floating to enter the serial
bootloader. If flashing fails to connect, disconnect M1-2 while flashing.

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

The page posts ``<throttle> <turn> <speed>`` to ``/drive``, for example
``1 -1 80`` for a forward-left curve at 80 %.
