Collision Detection
###################

This document describes how the robot tells a collision from braking, turning and ordinary
driving, using one 3-axis accelerometer, the drive command and the side speeds. It covers the
physics the method relies on, every formula and constant in the firmware
(``src/collision.c``, ``src/imu.c``) and the analysis tool (``tools/analyze.py``), the
measurements the thresholds come from, and the limits of the method.

.. contents::
   :local:
   :depth: 1

Problem
*******

A collision and a hard stop both decelerate the robot. A single acceleration threshold cannot
separate them, and on a skid-steer robot it cannot separate either of them from driving:

* driving straight at full speed, single samples reach ±1 to ±2 g of vibration;
* a stop from full speed reaches 0.75 g averaged over 25 ms;
* a spin shakes the chassis with up to 1.6 g of vibration, more than a soft collision.

The method therefore combines two features that suppress vibration differently with the
context the robot knows from its own commands: a deceleration while the robot is told to
keep driving is unexpected, the same deceleration right after a stop command is not.

Physics
*******

Collision versus braking
========================

Both events take the robot from its speed ``v`` to rest. By Newton's second law the mean
acceleration over the change is the change of velocity divided by the time it takes::

   a_mean = Δv / Δt

The robot drives at about ``v = 0.3 m/s`` at full power (0.27 to 0.35 m/s, integrated from
the acceleration of the recordings). What differs is ``Δt``:

* Braking: when the drive stops, the PWM inputs go to zero and the gear motors coast; the
  wheels' rolling resistance and the gearbox friction stop the robot in about 0.1 s. The
  recordings show a mean of 0.26 to 0.28 g over 100 ms, which gives
  ``Δt = 0.3 / (0.27 · 9.81) ≈ 0.11 s``.
* Collision: the obstacle stops the robot within the deformation of the contact. With a
  mean of 1.0 to 1.8 g, ``Δt = 0.3 / (1.0 … 1.8 · 9.81) ≈ 17 to 31 ms``.

A collision is the same velocity change in a quarter to a sixth of the time, hence a several
times larger acceleration over a short window. Modelling the contact as a spring of stiffness
``k`` against the robot's mass ``m`` gives::

   a_peak    = v · √(k / m)
   t_contact = π · √(m / k)
   a_peak · t_contact = π · v

The product is fixed by the speed: a hard obstacle (large ``k``) gives a short, high peak,
a soft one (a shoe) a longer, lower peak. The measurements follow this: the shoe reached
1.0 g, the box 1.7 to 1.8 g.

The window over which the acceleration is averaged must therefore be about as long as the
contact (tens of milliseconds): long enough to average out vibration, short enough that the
impact is not diluted by the time before and after it.

Vibration
=========

The chassis vibrates from the gearboxes, the wheels and the floor. Its acceleration is
oscillating and close to zero-mean over a few milliseconds, unlike the one-sided push of an
impact. Averaging ``N`` samples of uncorrelated vibration reduces its RMS by ``√N`` (by
about 3 for ``N = 10``). The moving average is also a low-pass filter with the response::

   |H(f)| = | sin(π f N / fs) / (N · sin(π f / fs)) |

With ``N = 10`` and ``fs = 400 Hz`` its first zero is at ``fs / N = 40 Hz``: vibration above
about 40 Hz is strongly attenuated, while an impact of 25 ms or longer passes almost entirely.
What the average removes is the second feature: the remaining high-frequency energy, which
rises sharply when the robot hits something rigid and keeps pushing.

Turning
=======

A skid-steer robot turns by driving its sides at different speeds; its wheels cannot roll in
the direction of the turn, so they slide sideways. The sliding friction, about ``μ · N`` per
wheel, alternates between sticking and slipping and excites the chassis broadly. A spin on
this robot produced up to 1.64 g of vibration, more than the softest collision (1.17 g).

The centripetal acceleration of the turn itself is negligible: at the measured spin rate
``ω ≈ 35 °/s = 0.61 rad/s`` and a sensor ``r = 0.1 m`` from the turning centre::

   a_c = ω² · r = 0.37 · 0.1 = 0.037 m/s² ≈ 0.004 g

So turning raises the vibration but hardly the averaged acceleration, which is why the
turning rule drops the vibration feature and keeps a higher averaged-acceleration threshold.

Direction of a hit
==================

The obstacle pushes the robot away from itself (Newton's third law); the robot's acceleration
``a = F / m`` points away from the obstacle. The obstacle lies opposite to the acceleration::

   θ_obstacle = atan2(−a_y, −a_x)

with ``x`` forward and ``y`` left, ``θ`` counter-clockwise from the front.

Signal Chain
************

Sensor
======

* ADXL345 accelerometer, ±16 g, full resolution: 3.9 mg per count, 13 bits, ±4096 counts.
* Output data rate 400 Hz nominal (measured 392 Hz, sample spacing 2.55 ms; within the
  sensor's clock tolerance), output bandwidth ``ODR / 2 = 200 Hz``.
* Each sample is timed by the sensor's data-ready interrupt (``t_us``, microseconds since
  boot); the ITG3205 gyro (±2000 °/s, 188 Hz low-pass, 14.375 counts per °/s) is read right
  after it.
* Noise at rest: about 5 mg RMS per axis.

Plausibility
============

A raw reading beyond ±4096 counts (16 g) cannot come from the sensor at this range; it is a
corrupted I2C read (seen once as 57.9 g, from electrical noise of the motors). Such a sample
is discarded and counted (``rejected`` in the status packet)::

   discard if |raw_x| > 4096 or |raw_y| > 4096 or |raw_z| > 4096

Calibration
===========

A reading is ``r = o + s · a`` with zero-g offset ``o`` and scale ``s``. Resting the robot
level and then upside down gives ``a = −1 g`` and ``+1 g`` on the vertical axis::

   o = (r₊ + r₋) / 2
   s = (r₊ − r₋) / (2 g)

On this module the Z axis read −0.846 g and +1.148 g: ``o = +0.151 g``, ``s = 0.997``. The
scale is within 0.3 % and left at 1; the offsets ``CONFIG_ROBOT_IMU_ACCEL_OFS_X/Y/Z``
(14, 0, 151 mg) are subtracted in counts: ``ofs = mg · 1000 / 3900``.

The gyro bias drifts with temperature and is measured whenever the robot rests: after the
motors have been stopped for 400 samples (1 s), 512 samples (1.3 s) are averaged, and the
mean becomes the bias, unless any sample differs from the current bias by more than 3 °/s
(the robot is being moved by hand), which restarts the average. The bias is kept in counts
times 16 for sub-count resolution and rounded when subtracted.

Robot frame
===========

On this robot both chips have ``x`` forward, ``y`` right and ``z`` down. They are turned into
the frame of ROS REP-103, ``x`` forward, ``y`` left, ``z`` up, by negating ``y`` and ``z`` of
both sensors. At rest the accelerometer then reads +1 g on ``z`` and a left turn gives a
positive yaw rate. All features below use the horizontal components ``a_x``, ``a_y``.

Features
********

Notation: ``n`` is the sample index, ``a_x[n]``, ``a_y[n]`` the calibrated horizontal
acceleration, ``N_s = 10`` (25 ms) and ``N_v = 40`` (100 ms) the window lengths at 400 Hz.

Tilt offset
===========

On a slope the accelerometer measures a component of gravity, ``g · sin(tilt)``: 17 mg per
degree. It is learned while the robot does not move and subtracted::

   firmware (exponential average, time constant 256 samples = 0.64 s):
      o_x ← o_x + (a_x − o_x) / 256        while left = right = 0
      a_x' = a_x − o_x                      (same for y)

   analysis tool:
      o_x = median of a_x over all samples with left = right = 0

a25: short averaged acceleration
================================

The horizontal acceleration averaged over the last 25 ms::

   m_x[n] = (1 / N_s) · Σ_{k=0}^{N_s−1} a_x'[n−k]
   m_y[n] = (1 / N_s) · Σ_{k=0}^{N_s−1} a_y'[n−k]
   a25[n] = √(m_x[n]² + m_y[n]²)

It captures the one-sided push of an impact and attenuates vibration (see Physics).

vib: vibration
==============

The RMS over the last 100 ms of what the 25 ms average leaves out::

   vib[n] = √( (1 / N_v) · Σ_{k=0}^{N_v−1} [ (a_x'[n−k] − m_x[n−k])² + (a_y'[n−k] − m_y[n−k])² ] )

It measures how hard the chassis shakes, mostly above 40 Hz.

Computation on the robot
========================

The firmware keeps running sums over ring buffers, so each sample costs a few additions and
multiplications regardless of the window length::

   S_x ← S_x + a_x'[n] − a_x'[n − N_s]         m_x = S_x / N_s
   Q   ← Q + q[n] − q[n − N_v]                 q[n] = (a_x' − m_x)² + (a_y' − m_y)²
   a25² = m_x² + m_y²                          vib² = Q / N_v

All values are in sensor counts. The comparisons are done on squares against squared
thresholds (``thr_counts = thr_mg · 1000 / 3900``), so no square root is taken per sample.
The first 100 samples (250 ms) after boot fill the windows and are not evaluated.

Context
*******

From each sample's drive state (commanded throttle and turn, actual left and right side
speeds)::

   moving    = left ≠ 0 or right ≠ 0
   commanded = throttle ≠ 0 or turn ≠ 0
   t_start   = time of the last change of (sign(left), sign(right)) while moving
               (a start from rest or a reversal of a side)

   driving   = moving and commanded and t − t_start ≥ 0.8 s
   turning   = driving and turn ≠ 0          (spin or curve)
   straight  = driving and turn = 0

The 0.8 s after a start or a reversal are excluded: the ramp from 35 % and the coast before a
reversal shake the robot without a collision. When the robot stops itself (auto-stop, an
inhibit, the 150 ms coast before a reversal) the sides go to zero and the robot is not
"driving", even if the command is still active.

The analysis tool distinguishes the samples that are not driving further, for its tables:
``starting`` (the 0.8 s above), ``stopping`` (0.6 s after the command went to stop, when a
deceleration is expected) and ``idle``.

Decision Rule
*************

A collision is detected at sample ``n`` when::

   straight[n] and ( a25[n] > A  or  vib[n] > V )
   or
   turning[n]  and ( a25[n] > A_t  or  (V_t > 0 and vib[n] > V_t) )

with the default thresholds

====================  ========  ========================================  ==========================
Threshold             Default   Kconfig                                   Link command (mg)
====================  ========  ========================================  ==========================
``A`` straight        0.6 g     ``CONFIG_ROBOT_COLLISION_A25_MG``         ``T A V [A_t V_t]``
``V`` straight        0.9 g     ``CONFIG_ROBOT_COLLISION_VIB_MG``
``A_t`` turning       1.0 g     ``CONFIG_ROBOT_COLLISION_A25_TURN_MG``
``V_t`` turning       off (0)   ``CONFIG_ROBOT_COLLISION_VIB_TURN_MG``
====================  ========  ========================================  ==========================

Why these values (see Evidence): straight driving stays below 0.12 g ``a25`` and 0.61 g
``vib`` (99th percentiles), collisions start at 1.0 g and 1.17 g; turning reaches 0.67 g
``a25`` and 1.64 g ``vib``, so only ``a25`` separates there.

Braking is excluded by the context, not by the thresholds: the jolt of a stop comes after
the sides went to zero.

IMU-only Context
****************

A vehicle may offer no access to its drive: the collision module then sees only its own sensors.
A second detector uses the same two features with a context derived from the IMU alone. It runs
next to the drive-context detector on every sample; ``CONFIG_ROBOT_COLLISION_ACTING`` or the
link command ``C <0|1>`` selects the one that acts, the other one only reports, so both can be
compared on the same drives.

Turning comes from the gyroscope. The yaw rate ``ω_z`` (robot frame, bias removed) is averaged
over the last 250 ms, causally::

   ω̄_z[n] = (1 / 100) · Σ_{k=0}^{99} ω_z[n−k]
   turning_imu[n] = ( |ω̄_z[m]| > Ω  for some m with t[n] − t[m] ≤ 0.5 s )

The hold of 0.5 s is needed because the scrubbing wheels turn the robot in jerks: during a spin
the averaged yaw rate dropped below 15 °/s for moments, and the vibration rule fired. On the
robot the sum is compared directly: ``|Σ ω_z| > Ω · 14.375 · 100`` counts.

Without the drive state, braking is excluded by higher thresholds only. It takes about 0.1 s
against 17 to 31 ms for an impact, so it stays lower: 0.77 g ``a25`` and 0.59 g ``vib`` at most,
against 1.03 g and 1.17 g for the softest collision. The rule::

   turning_imu[n]      and  a25[n] > A_t'
   or
   not turning_imu[n]  and  ( a25[n] > A'  or  (V' > 0 and vib[n] > V') )

======================  ========  ==========================================  =================
Threshold               Default   Kconfig                                     Link command
======================  ========  ==========================================  =================
``A'``                  0.9 g     ``CONFIG_ROBOT_COLLISION_IMU_A25_MG``       ``I A' V' A_t' Ω``
``V'`` (0 = unused)     0.9 g     ``CONFIG_ROBOT_COLLISION_IMU_VIB_MG``       (mg, mg, mg, °/s)
``A_t'``                1.0 g     ``CONFIG_ROBOT_COLLISION_IMU_A25_TURN_MG``
``Ω`` turning yaw rate  10 °/s    ``CONFIG_ROBOT_COLLISION_IMU_TURN_DPS``
======================  ========  ==========================================  =================

There is no start-up exclusion: starts and restarts stay below 0.9 g.

On the 13 recordings the IMU-only detector finds the same 5 collisions with the same directions,
2 to 5 ms after the drive-context detector because of its higher threshold, and no false alarm
on 23 stops, about 40 restarts and 5 spins. The yaw rates of 8 °/s without hold and of 15 °/s
with the hold gave the same result; 10 °/s with the hold sits between them.

The differences that matter:

* The margin between the strongest braking (0.77 g) and the softest collision (1.03 g) is a
  threshold gap, not a logical exclusion. A vehicle that brakes harder, or a softer collision,
  falls into it.
* A collision twists the robot (76 to 112 °/s yaw); the hold then applies the turning threshold
  for 0.5 s. On the recordings the detection came first, because the yaw lags the acceleration;
  a soft side hit may be missed.
* A robot pushing slowly against an obstacle looks like a parked one.
* Without context the detector depends fully on the plausibility filter: a corrupted sample of
  −36.9 g in one recording triggered it in a test without the filter, while the drive-context
  detector ignored it because the robot was not driving.

Response
********

Mode ``CONFIG_ROBOT_COLLISION_MODE`` or link command ``M <mode>``:

* ``2`` stop: at the detecting sample of the acting detector the drive inhibit
  ``DRIVE_INHIBIT_COLLISION`` is set,
  which sets both sides to zero at once and blocks drive commands until a reset (``R``).
* ``1`` monitor: the collision is reported, the robot keeps driving; one report per 1 s.
* ``0`` off.

The 40 samples after detection (100 ms) characterise the event:

* direction from the first 8 samples (20 ms), the contact itself; later the robot may push
  and shake::

     θ = atan2(−Σ_{k=1}^{8} m_y[n+k], −Σ_{k=1}^{8} m_x[n+k])     (degrees, 0..360)
     sector = floor((θ + 22.5°) / 45°) mod 8
     0 front, 1 front-left, 2 left, 3 back-left, 4 back, 5 back-right, 6 right, 7 front-right

* strength: the peaks of ``a25`` and ``vib`` over the 40 samples.

The event (count, sample time, peaks, sector) is in the status packet; the app shows it,
draws it in the plots and writes it to ``<recording>.events.csv``.

Detection latency
=================

An impact that raises the acceleration to a plateau ``P`` fills the 25 ms average linearly::

   m[k] = P · k / N_s          for k = 1 … N_s samples after contact
   k*   = ⌈ N_s · A / P ⌉      samples until a25 crosses A

For ``A = 0.6 g``: ``P = 1.8 g`` gives ``k* = 4`` (10 ms), ``P = 1.0 g`` gives ``k* = 6``
(15 ms). Measured, the firmware code stops the drive 5 to 15 ms after the analysis tool's
event start. Stopping itself takes one sample; the gear motors then coast.

Analysis Tool
*************

``tools/analyze.py`` computes the same features and context from a recording, with the
median offset instead of the exponential one. It groups samples where ``a25 > min(A, A_t)``
or ``vib > V`` (or ``V_t``) into events, merging those less than 0.3 s apart, and applies the
rule above per sample: an event is a collision only if a threshold was crossed while driving
or turning. Each event is matched with the label set in the app within −0.5 s to +3 s. Samples
beyond ±16 g or ±2000 °/s are dropped and interpolated. ``tools/viewer.py`` shows the same
analysis interactively.

Evidence
********

Recordings of 21 October 2026 on this robot (full speed unless noted):

=====================================  =====  ===========  ===========  ======================
Event                                  Count  a25 (g)      vib (g)      Rule
=====================================  =====  ===========  ===========  ======================
Straight driving, 99th percentile      –      0.09 – 0.12  0.18 – 0.61  –
Collision forward (box)                2      1.77, 1.81   3.16, 3.73   2 / 2 detected
Collision backward (box)               2      1.72, 1.73   3.10, 2.33   2 / 2 detected
Collision backward (shoe)              1      1.03         1.17         1 / 1 detected
Stop by command, forward / backward    3      0.54 – 0.72  0.32 – 0.33  0 / 3, context stopping
Auto-stop at full speed                21     0.67 – 0.77  0.35 – 0.59  0 / 21, context idle
Spin, left and right                   5      0.27 – 0.67  0.97 – 1.64  0 / 5 with turning rule
=====================================  =====  ===========  ===========  ======================

The a25 and vib values are the event maxima from the analysis tool; the firmware reports the
peaks over its first 100 ms (1.0 to 1.2 g a25 for the collisions above). Replaying every
recording through the firmware code on a PC gives the same detections as the tool, with the
same directions (front for forward hits, back or back-left for backward ones).

Before the turning rule, the spins raised false alarms through ``vib`` (0.92 to 0.99 g against
the 0.9 g threshold).

Limits
******

* The evidence is one robot, one floor and 34 events. The thresholds are empirical and must
  be checked again with more data, other floors, speeds and loads.
* Collisions while turning have the thinnest margin: the softest collision reached 1.03 g,
  the turning threshold is 1.0 g, and no collision during a turn has been recorded yet.
* A slow collision changes the velocity by less and gives a smaller ``a25``; below about 30 %
  speed a soft obstacle may not be detected.
* A collision during the 0.8 s after a start or a reversal is not detected.
* Without wheel encoders the robot cannot see that it stopped moving while its wheels turn
  (pushing against an obstacle, stuck); a real AGV's odometry would add that signal.
* The sensor must be mounted rigidly; foam or a loose mount changes both features.
* Motor noise on the ground disturbs the I2C bus (corrupted samples are discarded) and the
  Wi-Fi link; both need the wiring fixed, not more filtering.

Towards Real AGVs
*****************

The structure carries over, the numbers do not: a heavier vehicle has lower peaks and longer
contacts (``a_peak · t_contact = π v``), different vibration and slower braking. Relative
criteria transfer better than fixed thresholds: an acceleration beyond what the brakes and
the tyre grip can produce (``μ g``), a deceleration that does not match the command, and a
mismatch between wheel odometry and the integrated acceleration. IMU-based detection is a
diagnostic and logging layer; the protective function on an industrial AGV stays with
certified devices (ISO 3691-4, ISO 13849).
