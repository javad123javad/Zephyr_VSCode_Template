/*
 * Internal interfaces shared by the mind_io core, the pin backends and the
 * port slot drivers. Not part of the application API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MIND_IO_MIO_INTERNAL_H_
#define MIND_IO_MIO_INTERNAL_H_

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <mind_io/card_format.h>

/** Host I2C bus behind a connector bus resource (MIO_RES_I2C_IO / I2C_CNF). */
const struct device *mio_host_i2c(uint8_t bus_res);

/** Host SPI bus of the connector, or NULL if this board does not wire it. */
const struct device *mio_host_spi(void);

/*
 * Pin backends: a pin reference from the card (connector GPIO, IO_SPI_CS0 or
 * on-card expander pin) used with Zephyr GPIO flag semantics. The active
 * level given at configure time applies to set/get, which work on logical
 * values (1 = active).
 */
int mio_pin_configure(const struct mio_pin_ref *ref, gpio_flags_t flags);
int mio_pin_set(const struct mio_pin_ref *ref, int value);
int mio_pin_get(const struct mio_pin_ref *ref);

/** Resets and prepares every on-card expander of @p card. */
void mio_expanders_init(const struct mio_card *card);

/** Configures every line of @p card; called once after a valid card is read. */
void mio_lines_init(const struct mio_card *card);

/** Configures the fault LED pin (off); returns false if the board does not wire it. */
bool mio_fault_led_init(void);

/** Drives the fault LED pin. */
int mio_fault_led_write(bool on);

/** Human-readable pin name, e.g. "GPIO3" or "EXP0.1". */
const char *mio_pin_name(const struct mio_pin_ref *ref, char *buf, size_t size);

#endif /* MIND_IO_MIO_INTERNAL_H_ */
