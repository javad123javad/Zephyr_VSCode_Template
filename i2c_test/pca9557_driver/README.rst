PCA9557 GPIO Driver (out-of-tree)
##################################

Zephyr's in-tree GPIO expander driver (``nxp,pca95xx``,
``drivers/gpio/gpio_pca95xx.c``) targets the 16-bit PCA9535/39/55
family: paired port0/port1 registers at offsets 0x00-0x07. Pointing
it at a PCA9557 makes its boot-time init write the "direction"
register at offset 0x06 - out of range on this part - which NACKs
and leaves the device permanently stuck at "not ready".

The PCA9557 is an 8-bit, single-port part with a much shorter
register map:

* 0x00 - Input Port (read-only, actual pin state)
* 0x01 - Output Port (read/write, driven level)
* 0x02 - Polarity Inversion (read/write, not used by this driver)
* 0x03 - Configuration (read/write, 0 = output, 1 = input)

This directory is a self-contained Zephyr module (see
``zephyr/module.yml``) providing a real ``nxp,pca9557`` driver and
devicetree binding for that register map, implementing the standard
``gpio.h`` API (``pin_configure``, ``port_get/set/clear/toggle``, and
optional interrupt support via ``CONFIG_GPIO_PCA9557_INTERRUPT`` +
an ``interrupt-gpios`` property).

Usage
*****

The app's ``CMakeLists.txt`` adds this directory to
``ZEPHYR_EXTRA_MODULES`` before ``find_package(Zephyr ...)``, which
makes both the driver and its devicetree binding available with no
changes to the Zephyr tree itself. In a devicetree overlay::

   &i2c2 {
           io_expander: pca9557@19 {
                   compatible = "nxp,pca9557";
                   reg = <0x19>;
                   gpio-controller;
                   #gpio-cells = <2>;
                   ngpios = <8>;
           };
   };

Two things worth knowing if you copy this pattern for another
out-of-tree driver:

* ``ngpios`` is set explicitly in the overlay above even though it's
  always 8 on this part. ``dts/bindings/gpio/nxp,pca9557.yaml``
  deliberately does *not* try to default it: dtschema refuses to let
  a binding silently override a property's default that it inherited
  via ``include:`` (here, ``gpio-controller.yaml``'s ``ngpios``
  default of 32), and separately, ``gen_defines.py`` doesn't emit a
  ``_P_ngpios`` C macro for a property that's merely defaulted and
  absent from the actual devicetree source anyway - so the only
  reliable option is to just set it explicitly in every node.

* ``zephyr/module.yml`` declares ``build.settings.dts_root: .``.
  Without it, normal devicetree parsing still finds this binding
  correctly (so the compatible property, child nodes, etc. all look
  right in the generated ``zephyr.dts``) - but the *separate*
  Kconfig-side ``DT_HAS_NXP_PCA9557_ENABLED`` generator uses a
  bindings-dirs list that silently isn't extended with an
  otherwise-fully-discovered extra module's ``dts/bindings``, so
  ``GPIO_PCA9557``'s ``depends on DT_HAS_NXP_PCA9557_ENABLED`` would
  evaluate false and the driver would never get compiled in, with no
  error - just a link failure for the missing device instance
  further down the line. Declaring ``dts_root`` fixes both at once.
