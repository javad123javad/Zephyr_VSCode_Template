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
       main.c                      boot scan + the 500ms tick loop
       i2c_scan.c / .h             I2C bus scanner
       pca9557_test.c / .h         PCA9557 GPIO expander walking-bit test
       bme280_test.c / .h          BME280 sensor sampling
       can_test.c / .h             MCP2515 CAN loopback test
       i2s_test.c / .h             MAX98357A I2S test tone (runs its own thread)
       usb_power_test.c / .h       MIC2026-1YM USB port power switch test
       usb_device_test.c / .h      USB1 as a CDC-ACM device (runs its own thread)
       lan_test.c / .h             On-board Ethernet + DHCPv4 client
       axisram_test.c / .h         AXISRAM3 extra-SRAM write/read-back sanity check

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

``boards/*.overlay`` adds four devices found during bring-up:

============================ ======== =======================================
Device                       Bus      Notes
============================ ======== =======================================
PCA9557PW,118 GPIO expander  io_i2c   0x19 (A0=VCC, A1=GND, A2=GND)
BME280 environmental sensor  io_i2c   0x76 (SDO -> GND)
MCP2515 CAN controller       io_spi   8MHz osc, INT on PB3, CS via hw NSS
MAX98357A I2S amp/DAC        io_i2s   SD/GAIN tied to VCC on this module
============================ ======== =======================================

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

At boot, ``main.c`` scans addresses 0x04-0x77 on both I2C buses
(``i2c_scan.c``) and prints a scan table (same format as the
``i2c scan`` shell command) showing any address that ACKs. After the
boot-time scan, an interactive shell is available to re-scan or poke
at a device by hand::

   uart:~$ i2c scan i2c@50005400   # i2c1 / io_cnf_i2c
   uart:~$ i2c scan i2c@50005800   # i2c2 / io_i2c

(The shell device names are the raw devicetree node names because no
``label`` property is set on these nodes upstream.)

**PCA9557** (``pca9557_test.c``): configures all 8 pins as outputs and
continuously runs a walking-bit pattern (one pin at a time, every
500ms) through the standard ``gpio.h`` API against the out-of-tree
driver in ``pca9557_driver/`` - see that directory's README for why
Zephyr's in-tree ``nxp,pca95xx`` driver doesn't work with this part
(it targets a different, 16-bit chip family with an incompatible
register map). Nothing is logged per step; only write errors are.

**BME280** (``bme280_test.c``): added via the standard ``bosch,bme280``
devicetree binding and Zephyr's sensor API; every 2s fetches a sample
and prints temperature, pressure, and humidity.

**MCP2515** (``can_test.c``): added via the standard
``microchip,mcp2515`` devicetree binding. By default
(``CAN_TEST_LOOPBACK`` in that file) the controller runs in internal
loopback mode, so the SPI link and controller are exercised without
needing a second CAN node on the bus: every second it sends a frame
with ID ``0x123`` and an incrementing counter byte, and anything
received is printed. Disable ``CAN_TEST_LOOPBACK`` once you have a
real bus/transceiver and a second node to talk to.

**MAX98357A** (``i2s_test.c``): has no control bus of its own (no
I2C/SPI, no Zephyr devicetree binding) - it just plays whatever
standard I2S stream it's fed, so there's no separate devicetree node
for it; the "device" is simply the ``i2s1`` controller. Runs in its
own thread (``K_THREAD_DEFINE``, priority -1 - see the comment in
that file for why it needs to preempt ``main()``'s tick loop) that
configures ``i2s1`` as I2S clock master, 44.1 kHz / 16-bit / stereo,
and continuously streams a single-cycle 64-sample sine table
(~689 Hz, identical on both channels) back-to-back for a steady,
glitch-free test tone. Two non-obvious fixes were needed to get this
working reliably - both documented in ``i2s_test.c``:

* the whole TX queue must be pre-filled before ``i2s_trigger(START)``,
  or the DMA underruns waiting for the next ~1.45ms block before the
  thread can supply it;
* the thread's priority must be higher than ``main()``'s, or
  ``main()``'s periodic I2C/CAN housekeeping (BME280 sampling in
  particular) intermittently starves it of CPU time long enough to
  underrun anyway.

**MIC2026-1YM x2** (``usb_power_test.c``): each USB port's power switch
has an EN input (active-high, drive high to enable the 5V switch) and
an OCS/FLAG# output (open-drain, active-low, asserted on overcurrent
or thermal shutdown - there's no direct "5V present" sense pin on this
chip, so "EN asserted and OCS not asserted" is the closest available
signal that power is actually flowing without a fault). These are
plain GPIOs with no chip/protocol driver involved, so they're exposed
via the standard ``zephyr,user`` devicetree node rather than a custom
binding.

Only **USB2** is actually power-switched: at boot it's enabled and its
initial OCS state is printed, then its OCS is polled every 500ms tick
but only logged when it changes (fault raised or cleared), to keep
the console usable. **USB1's EN is deliberately held off** (driven
inactive, not left floating) - see "USB1 as a device" below for why.

**USB1 as a device** (``usb_device_test.c``): Zephyr has no host-mode
(UHC) driver for this SoC's OTG HS peripheral, so USB device mode -
enumerating this board *to* a PC - is the only way to exercise the
USB wiring at all with what's currently in-tree. USB1 (``usbotg_hs1``
/ ``zephyr_udc0``) is set up as a USB CDC-ACM serial device using the
new USB device stack; once a PC opens the enumerated serial port
(DTR asserted), anything typed is echoed straight back, confirming
both enumeration and bidirectional data transfer. Runs in its own
thread so it doesn't block the rest of bring-up waiting for a PC.

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
every network interface (just the one on-board MAC here); once a
lease is bound, the assigned address/netmask/gateway/lease time are
printed. Nothing is logged before that point beyond the "starting
DHCPv4" line, and ``CONFIG_NET_SHELL=y`` gives you ``net iface`` /
``net dhcpv4`` for further poking from the shell.

No MAC address is programmed in OTP on virgin boards (see the comment
on ``&mac`` in ``mindos_n6_common.dtsi``), so the driver falls back to
one derived from the chip's unique ID via ``HWINFO`` - stable per
board across reboots, but not a "real" assigned OUI; if you later
program a MAC into OTP, uncomment the ``nvmem-cells`` properties
there to use it instead.

**AXISRAM3 extra SRAM** (``axisram_test.c``): this board's ``zephyr,sram``
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
``drivers/misc/stm32n6_axisram/``). The address/size came from three
independently cross-checked sources (documented in the overlay
comment) since no full STM32N6 reference manual was available
locally: the gaps between AXISRAM3-6's addresses in the SoC
devicetree, the boundary in ST's own official ``STM32N657X0HXQ_LRUN.ld``
linker script, and an ST engineer's "3.75MB total across AXISRAM1-6"
figure on the ST community forum.

At boot, a 256-byte buffer is placed in AXISRAM3 via the standard
``zephyr,memory-region`` mechanism (``Z_GENERIC_SECTION`` +
``LINKER_DT_NODE_REGION_NAME_TOKEN``, not a plain static variable -
see ``axisram_test.c``), filled with a test pattern, and read back to
confirm the bank is genuinely clocked and addressable rather than
merely linked. Look for ``AXISRAM3: 256 B`` in the ``west build``
memory-region summary, and the ``AXISRAM3: write/read-back OK`` line
on the console.

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
(``.bss``, zero-init only) and is only touched at runtime from
``main()``, well after ``PRE_KERNEL_2`` has already run.

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

Getting I2S to build at all also required one devicetree fix: the SoC
devicetree defines the ``i2s1`` peripheral's DMA channels via
``&gpdma1``, but ``gpdma1`` itself is left ``status = "disabled"`` at
the SoC level and no board file enables it - so the driver failed to
build with an obscure ``DEVICE_DT_GET()`` error on a DMA channel
device that doesn't exist. ``boards/*.overlay`` sets
``&gpdma1 { status = "okay"; };`` to fix this.

Building and flashing
**********************

.. code-block:: console

   west build -p always -b mindos_n6/stm32n657xx/sb workspace/i2c_test
   west flash

Then open the console UART (``usart1``, 115200 8N1) to see the scan
results and periodic sensor/CAN/DHCP output, and listen at the
MAX98357A's speaker output for the test tone.
