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

Own Network
***********

When the robot cannot join ``CONFIG_ROBOT_WIFI_SSID`` within 20 s, or no SSID
is set, it opens its own WPA2 network ``robot-xxxx`` (last MAC bytes). Join it
and point the PC app at 192.168.4.1 or robot.local. Set its password in
``wifi.conf``; without one the robot never opens a network::

   CONFIG_ROBOT_AP_PSK="at-least-8-characters"

The station keeps retrying in the background, every 60 s while the own network
is up and not at all while a device is connected to it, because each attempt
moves the shared radio channel and briefly drops those devices. Once the
station has an address and nobody is on the own network, the robot closes it.
``CONFIG_ROBOT_AP_FALLBACK_SEC`` sets the 20 s.

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

Blocked driving is shown in the PC app with its reason. A low battery
clears by itself when the voltage recovers; a detected collision is latched
until the app's Reset button, ``robot reset`` or an ``R`` command.

Building and Flashing
*********************

Put the Wi-Fi credentials in ``wifi.conf`` (ignored by git):

.. code-block:: console

   cp wifi.conf.example wifi.conf

The robot boots through MCUboot and only runs images signed with
``keys/robot-ec-p256.pem``. That key is private and ignored by git: keep a
backup, or updates can only be installed over USB with a new key. Create it
once with:

.. code-block:: console

   python3 bootloader/mcuboot/scripts/imgtool.py keygen -t ecdsa-p256 \
      -k zephyr/workspace/robot/keys/robot-ec-p256.pem

Always build with sysbuild, which builds MCUboot and the signed application.
The first flash over USB installs both:

.. code-block:: console

   west build -p --sysbuild -b esp32_devkitc/esp32/procpu workspace/robot
   west flash

.. code-block:: console

   west espressif monitor

Firmware Update over Wi-Fi
**************************

Updates use MCUmgr over UDP port 1337. With `smpmgr`_ on the PC:

.. code-block:: console

   pip install smpmgr
   west build --sysbuild -b esp32_devkitc/esp32/procpu workspace/robot
   smpmgr --ip <robot address> upgrade build/robot/zephyr/zephyr.signed.bin

``upgrade`` uploads the image, marks it for a test boot and resets the robot.
If the robot keeps running the old version, mark and reset by hand::

   smpmgr --ip <robot address> image state-read
   smpmgr --ip <robot address> image state-write <hash of slot 1>
   smpmgr --ip <robot address> os reset

MCUboot swaps the images on the reset. The new firmware confirms itself as
soon as it is on the network; an image that never gets there is not
confirmed, and MCUboot returns to the previous one on the next reset (a power
cycle if the new image hangs). Bump ``VERSION`` for each release so
``image state-read`` shows which one runs.

The update port accepts uploads from anyone on the network, but MCUboot
refuses to boot an image that is not signed with the robot's key.

.. _smpmgr: https://pypi.org/project/smpmgr/

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

PC App
******

The robot has no web server; a Python app on the PC drives it, plots the IMU
live and records labelled data:

.. code-block:: console

   pip install -r workspace/robot/tools/requirements.txt
   python3 workspace/robot/tools/robot_app.py robot.local

* The app connects to the last address that worked, else ``robot.local``,
  else it searches the PC's subnets for a robot (also the robot's own
  network). It reconnects by itself after a robot reset, also when the robot
  got a new address.
* Arrow keys drive; up or down together with left or right drives a curve
  with the inner side slowed to 30 %. Left or right alone spins in place.
  The slider sets the speed, Shift halves it. Space or the STOP button stops.
* A gamepad's left stick drives too (needs pygame): its direction picks the
  move, how far it is pushed sets the speed. The arrow pad shows the active
  direction and the bars the actual side speeds.
* The plots show acceleration (with its magnitude ``|a|``) and rotation rate
  in the robot frame (x forward, y left, z up) and both side speeds, over
  2, 5, 10 or 30 s. Pause freezes them for zooming with the mouse.
* Record writes a CSV into the chosen folder. Label what happens next with
  the buttons or keys: C collision, B brake, V reverse, P push, L lift,
  S bump, N none. Labels appear as dashed lines in the plots and in the
  event list.
* The cards show the link (signal, drive commands received and the longest
  gap between them), the batteries, IMU health and the drive state. Dark
  and light themes; the address, speed, window, theme and folder are
  remembered.

Acceleration is ramped, slowing down and stopping are immediate. A side that
changes direction coasts for about 150 ms before starting the other way. Each
drive command runs for 700 ms and the app repeats it every 150 ms while a key
is held; the robot stops when the key is released, the window loses focus,
the app closes or the link drops.

The link is a UDP session on port 4244, described in ``src/link.h``: text
commands from the PC (``S`` keep alive, ``D <throttle> <turn> <speed>``,
``R`` reset, ``X`` end) and binary IMU and status packets from the robot.
``tools/imu_log.py`` is a command-line recorder for the same stream.

