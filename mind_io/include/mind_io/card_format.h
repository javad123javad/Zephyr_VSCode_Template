/*
 * mind_io card format: the self-description stored in the EEPROM of every
 * I/O card. Portable C99 with no Zephyr dependency, so the same code can be
 * used by the Zephyr module, a Linux library and host tools.
 *
 * See doc/card-format.rst for the binary layout.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MIND_IO_CARD_FORMAT_H_
#define MIND_IO_CARD_FORMAT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIO_CARD_MAGIC          "MIOC"
#define MIO_CARD_VERSION        1U
#define MIO_CARD_HDR_SIZE       8U
#define MIO_CARD_CRC_SIZE       4U
#define MIO_CARD_MIN_SIZE       (MIO_CARD_HDR_SIZE + MIO_CARD_CRC_SIZE)

/**
 * Connector contract: every card has its description EEPROM at this 7-bit
 * address on I2C_CNF, 16-bit word addressed, with the image at offset 0.
 */
#define MIO_CARD_EEPROM_ADDR    0x50U

/** Label buffer size: up to 15 characters plus the terminating NUL. */
#define MIO_LABEL_LEN           16U

#define MIO_MAX_I2C_PORTS       4U
#define MIO_MAX_SPI_PORTS       4U
#define MIO_MAX_ONBOARD_DEVS    2U
#define MIO_NUM_GPIO            4U
#define MIO_EXPANDER_PINS       8U
#define MIO_MAX_LINES           (MIO_NUM_GPIO + MIO_MAX_ONBOARD_DEVS * MIO_EXPANDER_PINS)

/**
 * Connector resources, named after the DIN41612 signals. The values are bit
 * positions in the RESOURCES record and must never change once released.
 */
enum mio_resource {
	MIO_RES_I2C_CNF = 0,
	MIO_RES_I2C_IO = 1,
	MIO_RES_SPI = 2,
	MIO_RES_SPI_CS0 = 3,
	MIO_RES_UART = 4,
	MIO_RES_RS485 = 5,
	MIO_RES_I2S = 6,
	MIO_RES_USB1 = 7,
	MIO_RES_USB2 = 8,
	MIO_RES_FAULT_LED = 9,
	MIO_RES_GPIO0 = 10,
	MIO_RES_GPIO1 = 11,
	MIO_RES_GPIO2 = 12,
	MIO_RES_GPIO3 = 13,
	MIO_RES_COUNT = 14,
};

#define MIO_RES_BIT(res) (1UL << (res))
#define MIO_RES_GPIO(n)  ((enum mio_resource)(MIO_RES_GPIO0 + (n)))

enum mio_record_type {
	MIO_REC_IDENTITY = 0x01,
	MIO_REC_RESOURCES = 0x02,
	MIO_REC_I2C_PORT = 0x10,
	MIO_REC_SPI_PORT = 0x11,
	MIO_REC_LINE = 0x20,
	MIO_REC_ONBOARD_DEVICE = 0x30,
	/* 0xE0..0xFE: customer/vendor records, skipped by the decoder */
};

/* Payload sizes of version 1 records. Longer payloads are accepted and the
 * extra bytes ignored, so fields can be appended in later versions.
 */
#define MIO_REC_IDENTITY_LEN       38U
#define MIO_REC_RESOURCES_LEN      4U
#define MIO_REC_I2C_PORT_LEN       20U
#define MIO_REC_SPI_PORT_LEN       26U
#define MIO_REC_LINE_LEN           22U
#define MIO_REC_ONBOARD_DEVICE_LEN 6U

enum mio_pin_source {
	MIO_PIN_NONE = 0,
	MIO_PIN_SPI_CS0 = 1,  /**< the dedicated IO_SPI_CS0 signal */
	MIO_PIN_GPIO = 2,     /**< connector IO_GPIO<pin> */
	MIO_PIN_EXPANDER = 3, /**< pin <pin> of on-card device <dev> */
};

/** Reference to a single signal line, on the connector or on the card. */
struct mio_pin_ref {
	uint8_t source; /**< enum mio_pin_source */
	uint8_t dev;    /**< on-card device index (MIO_PIN_EXPANDER only) */
	uint8_t pin;
};

enum mio_i2c_topology {
	MIO_I2C_DIRECT = 0,
	MIO_I2C_REPEATER = 1, /**< buffered, same address space as the host bus */
};

enum mio_i2c_speed {
	MIO_I2C_SPEED_ANY = 0,
	MIO_I2C_SPEED_STANDARD = 1, /**< 100 kHz */
	MIO_I2C_SPEED_FAST = 2,     /**< 400 kHz */
	MIO_I2C_SPEED_FAST_PLUS = 3,/**< 1 MHz */
};

/* SPI port chip-select flags */
#define MIO_CS_ACTIVE_HIGH (1U << 0)

/* Line flags */
#define MIO_LINE_OUTPUT      (1U << 0)
#define MIO_LINE_ACTIVE_LOW  (1U << 1)
#define MIO_LINE_PULL_UP     (1U << 2)
#define MIO_LINE_PULL_DOWN   (1U << 3)
#define MIO_LINE_INIT_ACTIVE (1U << 4)
#define MIO_LINE_OPEN_DRAIN  (1U << 5)

enum mio_line_role {
	MIO_ROLE_GENERIC = 0,
	MIO_ROLE_RELAY = 1,
	MIO_ROLE_LED = 2,
	MIO_ROLE_BUTTON = 3,
	MIO_ROLE_IRQ = 4,
	MIO_ROLE_RESET = 5,
	MIO_ROLE_ENABLE = 6,
	MIO_ROLE_FAULT = 7,
};

/* Port reference of a line: none, or kind (high nibble) and slot (low nibble) */
#define MIO_PORT_REF_NONE     0xFFU
#define MIO_PORT_KIND_I2C     1U
#define MIO_PORT_KIND_SPI     2U
#define MIO_PORT_REF(kind, slot) ((uint8_t)(((kind) << 4) | ((slot) & 0x0FU)))

enum mio_device_type {
	MIO_DEV_PCA9557 = 1,
};

struct mio_identity {
	uint8_t hw_major;
	uint8_t hw_minor;
	uint32_t date; /**< manufacture date as YYYYMMDD, 0 if unknown */
	char name[MIO_LABEL_LEN + 1];
	char serial[MIO_LABEL_LEN + 1];
};

struct mio_i2c_port_desc {
	uint8_t slot;
	uint8_t bus; /**< MIO_RES_I2C_IO or MIO_RES_I2C_CNF */
	uint8_t topology;
	uint8_t speed;
	char label[MIO_LABEL_LEN];
};

struct mio_spi_port_desc {
	uint8_t slot;
	struct mio_pin_ref cs;
	uint8_t cs_flags;
	uint32_t max_freq; /**< Hz, 0 = no limit */
	char label[MIO_LABEL_LEN];
};

struct mio_line_desc {
	struct mio_pin_ref pin;
	uint8_t flags;
	uint8_t role;
	uint8_t port;
	char label[MIO_LABEL_LEN];
};

struct mio_onboard_dev {
	uint8_t index;
	uint16_t type;
	uint8_t bus;
	uint8_t addr;
};

/** Decoded card description. */
struct mio_card {
	bool has_identity;
	struct mio_identity identity;
	uint32_t resources;
	uint8_t n_i2c_ports;
	uint8_t n_spi_ports;
	uint8_t n_lines;
	uint8_t n_devices;
	struct mio_i2c_port_desc i2c_ports[MIO_MAX_I2C_PORTS];
	struct mio_spi_port_desc spi_ports[MIO_MAX_SPI_PORTS];
	struct mio_line_desc lines[MIO_MAX_LINES];
	struct mio_onboard_dev devices[MIO_MAX_ONBOARD_DEVS];
};

/**
 * @brief Decode and validate a card image.
 *
 * @param buf Image, starting with the header.
 * @param len Number of valid bytes in @p buf (may exceed the image length).
 * @param card Decoded description (only valid when 0 is returned).
 * @param reason If not NULL, set to a static string describing a failure.
 *
 * @retval 0 Valid image.
 * @retval -ENODATA No card image (erased or unprogrammed memory).
 * @retval -ENOTSUP Unsupported format version.
 * @retval -EBADMSG Length or CRC mismatch.
 * @retval -EINVAL Malformed or inconsistent records.
 */
int mio_card_decode(const uint8_t *buf, size_t len, struct mio_card *card, const char **reason);

/**
 * @brief Encode a card description into an image.
 *
 * @retval >0 Image length in bytes.
 * @retval -ENOSPC @p size is too small.
 * @retval -EINVAL The description does not pass the decoder's validation.
 */
int mio_card_encode(const struct mio_card *card, uint8_t *buf, size_t size, const char **reason);

/** @brief Total image length announced by a header, or 0 if it's no card header. */
size_t mio_card_image_len(const uint8_t hdr[MIO_CARD_HDR_SIZE]);

/** @brief CRC-32 (IEEE 802.3 / zlib) of @p len bytes. */
uint32_t mio_crc32(const uint8_t *data, size_t len);

/** @brief Name of a connector resource ("I2C_IO", ...), or NULL. */
const char *mio_resource_name(enum mio_resource res);

/** @brief Name of a line role ("RELAY", ...), or "UNKNOWN". */
const char *mio_role_name(uint8_t role);

#ifdef __cplusplus
}
#endif

#endif /* MIND_IO_CARD_FORMAT_H_ */
