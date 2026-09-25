/*
 * mind_io: application API of the I/O card abstraction.
 *
 * At boot the card EEPROM is read and validated. Applications then use the
 * card's ports and lines by the labels printed on the card, without knowing
 * which host bus, chip-select or GPIO is behind them:
 *
 *   struct mio_line relay;
 *
 *   if (mio_line_get("RELAY1", &relay) == 0) {
 *           mio_line_set(&relay, 1);
 *   }
 *
 * Devices with a Zephyr driver are attached in a devicetree overlay under a
 * port slot (&mio_i2c_port0, &mio_spi_port0, ...) and used as usual.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MIND_IO_MIND_IO_H_
#define MIND_IO_MIND_IO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <zephyr/device.h>
#include <mind_io/card_format.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Result of reading the card EEPROM at boot. */
enum mio_card_status {
	MIO_CARD_OK = 0,  /**< valid card description */
	MIO_CARD_ABSENT,  /**< nothing answers at the card EEPROM address */
	MIO_CARD_BLANK,   /**< EEPROM present but not programmed */
	MIO_CARD_INVALID, /**< EEPROM programmed but the image is invalid */
};

/** A named line of the card, resolved by mio_line_get(). */
struct mio_line {
	const struct mio_line_desc *desc;
};

/**
 * @brief Card status determined at boot.
 *
 * @param reason If not NULL, set to a description of why the card is not
 *               usable (NULL when the status is MIO_CARD_OK).
 */
enum mio_card_status mio_card_status(const char **reason);

/** @brief Name of a card status ("ok", "absent", ...). */
const char *mio_card_status_name(enum mio_card_status status);

/** @brief Decoded card description, or NULL unless the status is MIO_CARD_OK. */
const struct mio_card *mio_card_get(void);

/** @brief Whether the card uses a connector resource. False without a valid card. */
bool mio_has_resource(enum mio_resource res);

/** @brief The card's I2C port in @p slot, or NULL. */
const struct mio_i2c_port_desc *mio_card_i2c_port(uint8_t slot);

/** @brief The card's SPI port in @p slot, or NULL. */
const struct mio_spi_port_desc *mio_card_spi_port(uint8_t slot);

/**
 * @brief Port slot device by card label, usable with the plain i2c_*() or
 *        spi_*() API.
 *
 * @return The ready slot device, or NULL if the card has no port with this
 *         label or the slot is not ready.
 */
const struct device *mio_port_by_label(const char *label);

/**
 * @brief Look up a line of the card by label.
 *
 * @retval 0 Found; @p line can be used with mio_line_set()/mio_line_read().
 * @retval -ENODEV No valid card.
 * @retval -ENOENT The card has no line with this label.
 */
int mio_line_get(const char *label, struct mio_line *line);

/**
 * @brief Set an output line to its logical state (1 = active).
 *
 * @retval 0 Success.
 * @retval -ENOTSUP The line is an input.
 * @retval -EIO Access to the line failed.
 */
int mio_line_set(const struct mio_line *line, int value);

/**
 * @brief Read the logical state of a line (1 = active).
 *
 * @retval 0 Inactive.
 * @retval 1 Active.
 * @retval -EIO Access to the line failed.
 */
int mio_line_read(const struct mio_line *line);

/**
 * @brief Switch the card's red fault LED (IO_F/A).
 *
 * @retval 0 Success.
 * @retval -ENODEV The processing board does not wire the fault LED.
 * @retval -ENOTSUP The card does not declare a fault LED.
 */
int mio_fault_led_set(bool on);

/**
 * @brief Read raw bytes of the card EEPROM (16-bit addressed, on I2C_CNF).
 *
 * @retval 0 Success.
 * @retval -EIO No answer from the card EEPROM.
 */
int mio_card_eeprom_read(off_t offset, uint8_t *buf, size_t len);

/**
 * @brief Write raw bytes to the card EEPROM, e.g. to program a card image.
 *
 * Writes are split so they never cross a 16-byte boundary. The new contents
 * take effect at the next boot.
 *
 * @retval 0 Success.
 * @retval -EIO The EEPROM did not accept or complete the write (write-
 *              protected with WC high, or absent).
 */
int mio_card_eeprom_write(off_t offset, const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MIND_IO_MIND_IO_H_ */
