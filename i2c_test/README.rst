Peripheral Bring-Up Test (mind,mindos_n6)
##########################################

Bring-up test for peripherals on the mindos_n6 custom board. Started
as an I2C-only test (hence the ``i2c_test`` directory name) and has
grown to also cover devices on the SPI and I2S buses; the project
hasn't been renamed/split yet.

Layout
******

.. code-block:: text

   i2c_test/
     Kconfig                       app-level Kconfig root (see "USB1 as a device" below)
     boards/                       board-target-specific devicetree overlays
     pca9557_driver/               out-of-tree GPIO driver (see its own README)
     src/
       main.c                      boot: USB2 5V on, DHCPv4 client started
       mind_shell.c                "mind" shell command (test_all + single tests)
       i2c_scan.c / .h             I2C bus scan + expected-device check
       pca9557_test.c / .h         PCA9557 GPIO expander walking-bit test
       bme280_test.c / .h          BME280 sample + range check
       can_test.c / .h             MCP2515 CAN loopback test
       i2s_test.c / .h             MAX98357A I2S test tone (playback thread)
       usb_power_test.c / .h       MIC2026-1YM USB port power switch test
       usb_device_test.c / .h      USB1 as a CDC-ACM device, enumeration check
       lan_test.c / .h             On-board Ethernet + DHCPv4 lease check
       axisram_test.c / .h         AXISRAM3 extra-SRAM write/read-back sanity check
       eeprom_test.c / .h          M24C64 board-configuration EEPROM check

Buses and devices
******************

I2C (both already enabled, 100 kHz standard mode, in
``boards/mind/mindos_n6/mindos_n6_common.dtsi``):

* ``i2c1`` (nodelabel ``io_cnf_i2c``) - SCL on PH9, SDA on PC1
* ``i2c2`` (nodelabel ``io_i2c``)     - SCL on PD14, SDA on PD15

SPI (also already enabled in the same file):

* ``spi2`` (nodelabel ``io_spi``) - SCK on PF2, MISO on PD6, MOSI on
  PD2, hardware NSS (CS) on PA11

I2S (also already enabled in the same file):

* ``i2s1`` (nodelabel ``io_i2s``) - WS on PA15, SDO on PB5, SDI on
  PB8, CK on PB9

Ethernet (also already enabled in the same file):

* ``mac`` - RMII, MDIO on PD12/PD1, data/clock on PF7/PF10/PF14/PF15/
  PF11/PF12/PF13, PHY at MDIO address 0 (nodelabel ``eth_phy``,
  generic ``ethernet-phy`` binding)

``boards/*.overlay`` adds five devices found during bring-up:

============================ ========== =====================================
Device                       Bus        Notes
============================ ========== =====================================
M24C64-RMN6TP EEPROM         io_cnf_i2c 0x50 (E0-E2 -> GND), board config
PCA9557PW,118 GPIO expander  io_i2c     0x19 (A0=VCC, A1=GND, A2=GND)
BME280 environmental sensor  io_i2c     0x76 (SDO -> GND)
MCP2515 CAN controller       io_spi     8MHz osc, INT on PB3, CS via hw NSS
MAX98357A I2S amp/DAC        io_i2s     SD/GAIN tied to VCC on this module
============================ ========== =====================================

Plus two plain GPIO pin pairs, one per USB port's MIC2026-1YM power
switch (not on any of the buses above):

================ ====== ================================================
Signal           Pin    Notes
================ ====== ================================================
USB1 EN          PE1    active-high output, drive high to enable 5V
USB1 OCS/FLAG#   PB5    active-low input, asserted on overcurrent/fault
USB2 EN          PE3    active-high output, drive high to enable 5V
USB2 OCS/FLAG#   PB14   active-low input, asserted on overcurrent/fault
================ ====== ================================================

The overlays also enable ``&gpdma1``, which the I2S driver needs (see
"MAX98357A" below).

What it does
************

The tests run from the shell, through the ``mind`` command
(``mind_shell.c``)::

   uart:~$ mind test_all    # every test below, then a PASS/FAIL summary
   uart:~$ mind eeprom      # a single test; plain "mind" lists them all

================ ===========================================================
Command          Passes when
================ ===========================================================
``axisram``      a buffer in AXISRAM3 reads back what was written
``eeprom``       the M24C64 last-page write/read-back and restore succeed
``i2c_scan``     every device described in the overlays ACKs its address
``pca9557``      the Output Port register matches at each walking-bit step
``bme280``       a sample is within the sensor's operating range
``can``          an MCP2515 loopback frame is received back
``usb_power``    USB2's 5V switch reports no OCS/fault
``i2s``          a 2 s test tone streams without error (listen for it)
``lan``          the Ethernet interface gets a DHCPv4 lease within 15 s
``usb``          a host enumerates USB1 within 10 s
================ ===========================================================

Each command prints its details and returns 0 on success or a
negative errno value, so it also works from shell scripts.
``test_all`` runs the two tests that wait on something external
(``lan`` and ``usb``) last. Only two things still happen at boot,
because they are services rather than tests: USB2's 5V switch is
turned on, and the DHCPv4 client is started so ``net ping`` works
straight away.

Zephyr's own shell commands stay available for poking at a device by
hand::

   uart:~$ i2c scan i2c@50005400   # i2c1 / io_cnf_i2c
   uart:~$ i2c scan i2c@50005800   # i2c2 / io_i2c

(The shell device names are the raw devicetree node names because no
``label`` property is set on these nodes upstream.)

**PCA9557** (``pca9557_test.c``): configures all 8 pins as outputs and
walks a single high bit across them (100 ms per pin) through the
standard ``gpio.h`` API against the out-of-tree driver in
``pca9557_driver/`` - see that directory's README for why Zephyr's
in-tree ``nxp,pca95xx`` driver doesn't work with this part (it targets
a different, 16-bit chip family with an incompatible register map).
Each step is verified by reading the chip's Output Port register back
over I2C rather than the pins: IO0 is open-drain (reads low without a
pull-up) and the Polarity Inversion register resets to ``0xf0``,
inverting IO4-IO7 in the Input Port register.

**BME280** (``bme280_test.c``): added via the standard ``bosch,bme280``
devicetree binding and Zephyr's sensor API; fetches one sample, prints
temperature, pressure and humidity, and fails if any is outside the
datasheet operating range (-40..85 C, 30..110 kPa, 0..100 %RH).

**MCP2515** (``can_test.c``): added via the standard
``microchip,mcp2515`` devicetree binding. By default
(``CAN_TEST_LOOPBACK`` in that file) the controller runs in internal
loopback mode, so the SPI link and controller are exercised without
needing a second CAN node on the bus: the controller is started on the
first run, then each run sends a frame with ID ``0x123`` and an
incrementing counter byte and checks it is received back within
100 ms. Disable ``CAN_TEST_LOOPBACK`` once you have a real
bus/transceiver and a second node to talk to.

**MAX98357A** (``i2s_test.c``): has no control bus of its own (no
I2C/SPI, no Zephyr devicetree binding) - it just plays whatever
standard I2S stream it's fed, so there's no separate devicetree node
for it; the "device" is simply the ``i2s1`` controller. Each run
configures ``i2s1`` as I2S clock master, 44.1 kHz / 16-bit / stereo,
and streams a single-cycle 64-sample sine table (~689 Hz, identical on
both channels) back-to-back for 2 s, then drains the stream. The test
can only detect driver errors and underruns; whether the tone was
audible has to be checked by ear. Streaming is done by a dedicated
priority -1 thread that the shell command hands the request to. Two
non-obvious fixes were needed to get this working reliably - both
documented in ``i2s_test.c``:

* the whole TX queue must be pre-filled before ``i2s_trigger(START)``,
  or the DMA underruns waiting for the next ~1.45ms block before the
  thread can supply it;
* the streaming thread's priority must be high: with equal or lower
  priority than other periodic work (BME280 sampling in particular,
  during bring-up), it was intermittently starved of CPU time long
  enough to underrun anyway. That is also why the shell thread does
  not stream the audio itself.

**MIC2026-1YM x2** (``usb_power_test.c``): each USB port's power switch
has an EN input (active-high, drive high to enable the 5V switch) and
an OCS/FLAG# output (open-drain, active-low, asserted on overcurrent
or thermal shutdown - there's no direct "5V present" sense pin on this
chip, so "EN asserted and OCS not asserted" is the closest available
signal that power is actually flowing without a fault). These are
plain GPIOs with no chip/protocol driver involved, so they're exposed
via the standard ``zephyr,user`` devicetree node rather than a custom
binding.

Only **USB2** is actually power-switched: it's enabled at boot so its
5V is always available, and ``mind usb_power`` reads its OCS flag and
fails on a fault. **USB1's EN is deliberately held off** (driven
inactive, not left floating) - see "USB1 as a device" below for why.

**USB1 as a device** (``usb_device_test.c``): Zephyr has no host-mode
(UHC) driver for this SoC's OTG HS peripheral, so USB device mode -
enumerating this board *to* a PC - is the only way to exercise the
USB wiring at all with what's currently in-tree. USB1 (``usbotg_hs1``
/ ``zephyr_udc0``) is set up as a USB CDC-ACM serial device using the
new USB device stack, brought up on the first ``mind usb`` run. The
test passes once the host has enumerated and configured the device
(``USBD_MSG_CONFIGURATION``); after that, anything typed into the
enumerated serial port is echoed straight back, confirming data
transfer in both directions.

USB1 and USB2 share their physical USB-A connectors with the
MIC2026-switched 5V described above. Since a PC supplies its own VBUS
on that same pin when USB1 is plugged in for this test,
``usb_power_test.c`` holds USB1's EN off rather than driving it -
otherwise the board's own 5V output and the PC's VBUS would both be
driving the same pin at once.

Reusing Zephyr's shared USB sample bring-up code
(``samples/subsys/usb/common/``) needed two things beyond the
``cdc_acm`` sample's own ``prj.conf``/overlay pattern, since this is
an out-of-tree app rather than a sample inside the Zephyr tree:

* ``CMakeLists.txt`` does
  ``include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)``
  to pull in ``sample_usbd_init.c`` and its include path.
* This directory's own ``Kconfig`` sources
  ``samples/subsys/usb/common/Kconfig.sample_usbd`` (for
  ``SAMPLE_USBD_PID`` etc., referenced from ``prj.conf``) followed by
  ``source "Kconfig.zephyr"``. An app's own ``./Kconfig``, if
  present, replaces ``$ZEPHYR_BASE/Kconfig`` as the Kconfig root (see
  ``cmake/modules/kconfig.cmake``), so it must still pull in the
  normal tree via ``Kconfig.zephyr`` or everything else breaks.

**On-board Ethernet + DHCPv4** (``lan_test.c``): the MAC uses the
official STM32Cube HAL Ethernet driver (``ETH_STM32_HAL_API_V2``,
which explicitly supports ``SOC_SERIES_STM32N6X``); the PHY node uses
Zephyr's generic ``ethernet-phy`` binding (``CONFIG_PHY_GENERIC_MII``),
which talks standard MDIO management registers for link/speed/duplex
autonegotiation - no vendor-specific PHY driver needed unless you want
chip-specific extras later. At boot, a DHCPv4 client is started on
every network interface (just the one on-board MAC here) and the lease
is printed once bound. ``mind lan`` prints the interface and MAC, then
waits up to 15 s for a lease; on timeout it reports whether the link
is up and the DHCP client's state. ``CONFIG_NET_SHELL=y`` gives you
``net iface`` / ``net dhcpv4`` / ``net ping`` for further poking.

No MAC address is programmed in OTP on virgin boards (see the comment
on ``&mac`` in ``mindos_n6_common.dtsi``), so the driver falls back to
one derived from the chip's unique ID via ``HWINFO`` - stable per
board across reboots, but not a "real" assigned OUI; if you later
program a MAC into OTP, uncomment the ``nvmem-cells`` properties
there to use it instead.

**AXISRAM3 extra SRAM** (``axisram_test.c``): on the RAM-loaded ``/sb``
and ``/fsbl`` variants this board's ``zephyr,sram``
is ``axisram2``, which - despite the name - is not "the second SRAM
block". It's ST's name for the top 511KB of the combined ~2MB
axisram1/2 address window, and it's the *only* part of that window
valid for the Boot ROM to RAM-load an image into on this
secure/serial-boot ("/sb") target; the rest of that window
(``axisram1`` spans the whole thing) is reserved during SB/FSBL boot.
Switching ``zephyr,sram`` to ``axisram1`` therefore doesn't give you
"the other 1.5MB" - it links the image at an address/size the SB
boot loader won't load at all, and the app silently never starts.
Both official ST reference boards (``stm32n6570_dk``,
``nucleo_n657x0_q``) use exactly this same ``axisram2``-for-SB
convention.

The actual extra RAM is ``AXISRAM3`` - a genuinely separate 448KB
bank (one of four, AXISRAM3-6, together ~1.75MB more), already
enabled by default in the SoC devicetree but with no ``reg``
(address/size) set anywhere upstream, including in either reference
board - so there's no existing example to copy. ``boards/*.overlay``
sets ``reg = <0x34200000 DT_SIZE_K(448)>;`` and an explicit
``status = "okay";`` (relying on the default status alone did not
turn on ``CONFIG_STM32N6_AXISRAM``, the driver that clocks and
enables the bank at ``PRE_KERNEL_2`` - see
``soc/st/stm32/stm32n6x/axisram/``). The address/size came from three
independently cross-checked sources (documented in the overlay
comment) since no full STM32N6 reference manual was available
locally: the gaps between AXISRAM3-6's addresses in the SoC
devicetree, the boundary in ST's own official ``STM32N657X0HXQ_LRUN.ld``
linker script, and an ST engineer's "3.75MB total across AXISRAM1-6"
figure on the ST community forum.

A 256-byte buffer is placed in AXISRAM3 via the standard
``zephyr,memory-region`` mechanism (``Z_GENERIC_SECTION`` +
``LINKER_DT_NODE_REGION_NAME_TOKEN``, not a plain static variable -
see ``axisram_test.c``), filled with a test pattern, and read back to
confirm the bank is genuinely clocked and addressable rather than
merely linked. Look for ``AXISRAM3: 256 B`` in the ``west build``
memory-region summary, and run ``mind axisram``.

**Freeing primary-image space with AXISRAM3 - tried, reverted, doesn't
work as hoped.** Since this ``/sb`` target RAM-loads the *whole*
image (code and data both) into the 511KB ``axisram2`` window, moving
things out of it looks like a direct way to make room for more
application code. ``CONFIG_CODE_DATA_RELOCATION`` +
``zephyr_code_relocate(FILES src/i2s_test.c LOCATION AXISRAM3_RODATA)``
was tried, to move that file's sine table (an initialized ``const``
array) into AXISRAM3. It built cleanly and the memory-region summary
looked right (primary RAM usage down, ``AXISRAM3`` usage up by a
matching amount) - but **the board never booted**: nothing, not even
the earliest boot banner, printed.

The reason: relocating anything with *initial values* (``.rodata``,
``.data`` - not ``.bss``, see below) requires copying those values
into AXISRAM3 during early boot. That copy
(``data_copy_xip_relocation()``, called from ``arch_data_copy()`` in
``arch/common/xip.c``) runs as part of the C runtime startup, before
*any* Zephyr-managed init - including the ``stm32n6_axisram`` driver
that enables AXISRAM3's RCC clock at ``PRE_KERNEL_2``. So the copy
tries to write into a not-yet-clocked SRAM bank, which hangs the bus
before the CPU can do anything else, including print. This is why
``axisram_test.c``'s own buffer *does* work: it has no initializer
(``.bss``, zero-init only) and is only touched at runtime by
``mind axisram``, well after ``PRE_KERNEL_2`` has already run.

So, on this board, as things stand:

* ``.bss`` (uninitialized data/buffers) - relocates to AXISRAM3 fine,
  as long as nothing touches it before ``main()`` runs.
* ``.rodata``/``.data`` (anything with initial values) - relocating
  it hangs the board at boot, for the reason above. Fixing this would
  need AXISRAM3's clock enabled *earlier* than ``PRE_KERNEL_2`` -
  realistically in ``soc.c``'s ``SystemInit()``, which runs before
  ``arch_data_copy()`` - a real Zephyr-tree change, not an app-level
  one.
* ``.text`` (code) - not relocatable at all here regardless of the
  above: this SoC uses ``CONFIG_CPU_HAS_CUSTOM_FIXED_SOC_MPU_REGIONS``,
  and ``soc/st/stm32/stm32n6x/mpu_regions.c`` defines exactly two
  fixed MPU regions (read+execute for the primary image's code,
  read/write-but-not-executable for its data) that don't cover
  AXISRAM3; the generic ``zephyr,memory-attr`` used for it
  (``ATTR_MPU_RAM``) also maps to non-executable RAM
  (``arch/arm/core/mpu/arm_mpu.c`` -> ``REGION_RAM_ATTR`` in
  ``arm_mpu_v8.h``, which sets the MPU's ``NOT_EXEC`` bit).

**M24C64 board-configuration EEPROM** (``eeprom_test.c``): 8 KiB,
32-byte pages, 16-bit word address, driven by Zephyr's in-tree
``atmel,at24`` driver (``compatible = "st,m24c64", "atmel,at24"``).
Because it holds real board settings, the test changes nothing: it hex-dumps the first 64 bytes read-only, then writes the
bitwise complement of the last page (0x1fe0-0x1fff), verifies it,
writes the saved original back and verifies that too. Only a power
loss during those few milliseconds could leave the last page altered;
if the restore fails the original bytes are printed so they can be
written back by hand. A write failure with ``-EIO`` usually means the
chip's WC (write control) pin is held high; if WC is wired to a GPIO,
add it to the node as ``wp-gpios`` and the driver will drive it. For
manual access, ``CONFIG_EEPROM_SHELL`` provides e.g.
``eeprom read eeprom@50 0 32`` in the shell.

**XIP is the real answer to "space for application code".** On the
default ``mindos_n6`` variant (see "Building and flashing" below) the
code runs in place from the external W25Q64JV and is no longer copied
into the 511KB boot window at all: it gets a ~4MB flash slot, and
``zephyr,sram`` becomes the full 2MB ``axisram1``, since the boot
window restriction only applies to images loaded by the Boot ROM, not
to an application chainloaded by MCUboot. The memory-region summary
of an XIP build shows it, e.g. ``FLASH: 208936 B / 4120240 B`` and
``RAM: 67216 B / 2 MB``.

Getting I2S to build at all also required one devicetree fix: the SoC
devicetree defines the ``i2s1`` peripheral's DMA channels via
``&gpdma1``, but ``gpdma1`` itself is left ``status = "disabled"`` at
the SoC level and no board file enables it - so the driver failed to
build with an obscure ``DEVICE_DT_GET()`` error on a DMA channel
device that doesn't exist. ``boards/*.overlay`` sets
``&gpdma1 { status = "okay"; };`` to fix this.

Building and flashing
**********************

Two boot modes are supported:

* **XIP from external flash (default ``mindos_n6`` variant).** MCUboot
  (built for the ``/fsbl`` variant) is loaded by the Boot ROM, then
  chainloads this application from the ``slot0`` partition of the
  W25Q64JV, where it executes in place. Must be built with
  ``--sysbuild``; building without it fails on purpose (see
  ``CMakeLists.txt``), since the resulting image could not boot.
  ``west flash`` programs both MCUboot and the signed application
  through the ``Template_FSBL_XIP_ExtMemLoader.stldr`` external loader.

  .. code-block:: console

     west build -p always -b mindos_n6 --sysbuild workspace/i2c_test
     west flash

  Flash with the boot pins set to development boot, then switch them to
  flash boot and reset. The console shows the MCUboot banner and
  ``Jumping to the first image slot`` before the test output.

* **RAM-loaded over USB (``/sb`` variant).** The whole image is loaded
  into the 511KB boot window; nothing is written to external flash.

  .. code-block:: console

     west build -p always -b mindos_n6/stm32n657xx/sb workspace/i2c_test
     west flash

``boards/mindos_n6_stm32n657xx.overlay`` (XIP) and
``boards/mindos_n6_stm32n657xx_sb.overlay`` (``/sb``) have the same
contents and must be kept in sync. There is no overlay for the
standalone ``/fsbl`` variant.

Then open the console UART (``usart1``, 115200 8N1), run
``mind test_all``, and listen at the
MAX98357A's speaker output for the test tone.
