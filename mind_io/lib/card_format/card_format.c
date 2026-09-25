/*
 * mind_io card format: decoder, validator and encoder. Portable C99.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <mind_io/card_format.h>

#define FAIL(_code, _msg)                                                                          \
	do {                                                                                       \
		if (reason != NULL) {                                                              \
			*reason = (_msg);                                                          \
		}                                                                                  \
		return (_code);                                                                    \
	} while (0)

static uint16_t get_le16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static void put_le16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xFFU);
	p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xFFU);
	p[1] = (uint8_t)((v >> 8) & 0xFFU);
	p[2] = (uint8_t)((v >> 16) & 0xFFU);
	p[3] = (uint8_t)(v >> 24);
}

/* Copies a string into a fixed-size field, zero-filling the rest */
static void put_str(uint8_t *p, const char *s, size_t field_len)
{
	size_t n = 0U;

	while ((n < field_len) && (s[n] != '\0')) {
		p[n] = (uint8_t)s[n];
		n++;
	}
	memset(&p[n], 0, field_len - n);
}

uint32_t mio_crc32(const uint8_t *data, size_t len)
{
	uint32_t crc = 0xFFFFFFFFU;

	for (size_t i = 0U; i < len; i++) {
		crc ^= data[i];
		for (unsigned int bit = 0U; bit < 8U; bit++) {
			crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
		}
	}

	return ~crc;
}

size_t mio_card_image_len(const uint8_t hdr[MIO_CARD_HDR_SIZE])
{
	if (memcmp(hdr, MIO_CARD_MAGIC, 4) != 0) {
		return 0U;
	}

	return get_le16(&hdr[6]);
}

static const char *const resource_names[MIO_RES_COUNT] = {
	[MIO_RES_I2C_CNF] = "I2C_CNF",   [MIO_RES_I2C_IO] = "I2C_IO",
	[MIO_RES_SPI] = "SPI",           [MIO_RES_SPI_CS0] = "SPI_CS0",
	[MIO_RES_UART] = "UART",         [MIO_RES_RS485] = "RS485",
	[MIO_RES_I2S] = "I2S",           [MIO_RES_USB1] = "USB1",
	[MIO_RES_USB2] = "USB2",         [MIO_RES_FAULT_LED] = "FAULT_LED",
	[MIO_RES_GPIO0] = "GPIO0",       [MIO_RES_GPIO1] = "GPIO1",
	[MIO_RES_GPIO2] = "GPIO2",       [MIO_RES_GPIO3] = "GPIO3",
};

const char *mio_resource_name(enum mio_resource res)
{
	if (((unsigned int)res) >= (unsigned int)MIO_RES_COUNT) {
		return NULL;
	}

	return resource_names[res];
}

const char *mio_role_name(uint8_t role)
{
	static const char *const names[] = {
		[MIO_ROLE_GENERIC] = "GENERIC", [MIO_ROLE_RELAY] = "RELAY",
		[MIO_ROLE_LED] = "LED",         [MIO_ROLE_BUTTON] = "BUTTON",
		[MIO_ROLE_IRQ] = "IRQ",         [MIO_ROLE_RESET] = "RESET",
		[MIO_ROLE_ENABLE] = "ENABLE",   [MIO_ROLE_FAULT] = "FAULT",
	};

	if (role >= (sizeof(names) / sizeof(names[0]))) {
		return "UNKNOWN";
	}

	return names[role];
}

/* ---- validation, shared by the decoder and the encoder ---- */

static bool res_used(const struct mio_card *card, enum mio_resource res)
{
	return (card->resources & MIO_RES_BIT(res)) != 0U;
}

static bool label_ok(const char label[MIO_LABEL_LEN])
{
	return (label[0] != '\0') && (memchr(label, '\0', MIO_LABEL_LEN) != NULL);
}

static bool pin_equal(const struct mio_pin_ref *a, const struct mio_pin_ref *b)
{
	if ((a->source != b->source) || (a->pin != b->pin)) {
		return false;
	}

	return (a->source != MIO_PIN_EXPANDER) || (a->dev == b->dev);
}

static const struct mio_onboard_dev *find_device(const struct mio_card *card, uint8_t index)
{
	for (uint8_t i = 0U; i < card->n_devices; i++) {
		if (card->devices[i].index == index) {
			return &card->devices[i];
		}
	}

	return NULL;
}

static bool is_i2c_bus(uint8_t bus)
{
	return (bus == MIO_RES_I2C_IO) || (bus == MIO_RES_I2C_CNF);
}

static int check_pin(const struct mio_card *card, const struct mio_pin_ref *ref, bool allow_cs0,
		     const char **reason)
{
	const struct mio_onboard_dev *dev;

	switch (ref->source) {
	case MIO_PIN_SPI_CS0:
		if (!allow_cs0) {
			FAIL(-EINVAL, "SPI_CS0 can only be used as an SPI chip-select");
		}
		if (!res_used(card, MIO_RES_SPI_CS0)) {
			FAIL(-EINVAL, "SPI_CS0 used but not declared in RESOURCES");
		}
		return 0;
	case MIO_PIN_GPIO:
		if (ref->pin >= MIO_NUM_GPIO) {
			FAIL(-EINVAL, "connector GPIO number out of range");
		}
		if (!res_used(card, MIO_RES_GPIO(ref->pin))) {
			FAIL(-EINVAL, "connector GPIO used but not declared in RESOURCES");
		}
		return 0;
	case MIO_PIN_EXPANDER:
		dev = find_device(card, ref->dev);
		if ((dev == NULL) || (dev->type != MIO_DEV_PCA9557)) {
			FAIL(-EINVAL, "expander pin refers to no on-card PCA9557");
		}
		if (ref->pin >= MIO_EXPANDER_PINS) {
			FAIL(-EINVAL, "expander pin number out of range");
		}
		return 0;
	default:
		FAIL(-EINVAL, "invalid pin source");
	}
}

static bool port_ref_ok(const struct mio_card *card, uint8_t ref)
{
	uint8_t kind = ref >> 4;
	uint8_t slot = ref & 0x0FU;

	if (ref == MIO_PORT_REF_NONE) {
		return true;
	}

	if (kind == MIO_PORT_KIND_I2C) {
		for (uint8_t i = 0U; i < card->n_i2c_ports; i++) {
			if (card->i2c_ports[i].slot == slot) {
				return true;
			}
		}
	} else if (kind == MIO_PORT_KIND_SPI) {
		for (uint8_t i = 0U; i < card->n_spi_ports; i++) {
			if (card->spi_ports[i].slot == slot) {
				return true;
			}
		}
	} else {
		/* unknown port kind */
	}

	return false;
}

/* Collects every label so they can be checked for uniqueness */
static int check_labels_unique(const struct mio_card *card, const char **reason)
{
	const char *labels[MIO_MAX_I2C_PORTS + MIO_MAX_SPI_PORTS + MIO_MAX_LINES];
	size_t n = 0U;

	for (uint8_t i = 0U; i < card->n_i2c_ports; i++) {
		labels[n++] = card->i2c_ports[i].label;
	}
	for (uint8_t i = 0U; i < card->n_spi_ports; i++) {
		labels[n++] = card->spi_ports[i].label;
	}
	for (uint8_t i = 0U; i < card->n_lines; i++) {
		labels[n++] = card->lines[i].label;
	}

	for (size_t i = 0U; i < n; i++) {
		for (size_t j = i + 1U; j < n; j++) {
			if (strncmp(labels[i], labels[j], MIO_LABEL_LEN) == 0) {
				FAIL(-EINVAL, "duplicate port/line label");
			}
		}
	}

	return 0;
}

static int validate(const struct mio_card *card, const char **reason)
{
	int ret;

	if ((card->n_i2c_ports > MIO_MAX_I2C_PORTS) || (card->n_spi_ports > MIO_MAX_SPI_PORTS) ||
	    (card->n_lines > MIO_MAX_LINES) || (card->n_devices > MIO_MAX_ONBOARD_DEVS)) {
		FAIL(-EINVAL, "too many records of one kind");
	}

	if (!res_used(card, MIO_RES_I2C_CNF)) {
		FAIL(-EINVAL, "I2C_CNF must always be declared");
	}

	for (uint8_t i = 0U; i < card->n_devices; i++) {
		const struct mio_onboard_dev *dev = &card->devices[i];

		if (dev->index >= MIO_MAX_ONBOARD_DEVS) {
			FAIL(-EINVAL, "on-card device index out of range");
		}
		for (uint8_t j = i + 1U; j < card->n_devices; j++) {
			if (card->devices[j].index == dev->index) {
				FAIL(-EINVAL, "duplicate on-card device index");
			}
		}
		if (!is_i2c_bus(dev->bus) || !res_used(card, (enum mio_resource)dev->bus)) {
			FAIL(-EINVAL, "on-card device on an undeclared or non-I2C bus");
		}
		if ((dev->addr < 0x08U) || (dev->addr > 0x77U)) {
			FAIL(-EINVAL, "on-card device address out of range");
		}
		if ((dev->bus == MIO_RES_I2C_CNF) && (dev->addr == MIO_CARD_EEPROM_ADDR)) {
			FAIL(-EINVAL, "on-card device collides with the card EEPROM address");
		}
	}

	for (uint8_t i = 0U; i < card->n_i2c_ports; i++) {
		const struct mio_i2c_port_desc *port = &card->i2c_ports[i];

		if (port->slot >= MIO_MAX_I2C_PORTS) {
			FAIL(-EINVAL, "I2C port slot out of range");
		}
		for (uint8_t j = i + 1U; j < card->n_i2c_ports; j++) {
			if (card->i2c_ports[j].slot == port->slot) {
				FAIL(-EINVAL, "duplicate I2C port slot");
			}
		}
		if (!is_i2c_bus(port->bus) || !res_used(card, (enum mio_resource)port->bus)) {
			FAIL(-EINVAL, "I2C port on an undeclared or non-I2C bus");
		}
		if ((port->topology > MIO_I2C_REPEATER) || (port->speed > MIO_I2C_SPEED_FAST_PLUS)) {
			FAIL(-EINVAL, "I2C port topology or speed invalid");
		}
		if (!label_ok(port->label)) {
			FAIL(-EINVAL, "I2C port label empty or too long");
		}
	}

	for (uint8_t i = 0U; i < card->n_spi_ports; i++) {
		const struct mio_spi_port_desc *port = &card->spi_ports[i];

		if (port->slot >= MIO_MAX_SPI_PORTS) {
			FAIL(-EINVAL, "SPI port slot out of range");
		}
		if (!res_used(card, MIO_RES_SPI)) {
			FAIL(-EINVAL, "SPI port but SPI not declared in RESOURCES");
		}
		ret = check_pin(card, &port->cs, true, reason);
		if (ret != 0) {
			return ret;
		}
		for (uint8_t j = i + 1U; j < card->n_spi_ports; j++) {
			if (card->spi_ports[j].slot == port->slot) {
				FAIL(-EINVAL, "duplicate SPI port slot");
			}
			if (pin_equal(&card->spi_ports[j].cs, &port->cs)) {
				FAIL(-EINVAL, "two SPI ports share a chip-select");
			}
		}
		if (!label_ok(port->label)) {
			FAIL(-EINVAL, "SPI port label empty or too long");
		}
	}

	for (uint8_t i = 0U; i < card->n_lines; i++) {
		const struct mio_line_desc *line = &card->lines[i];

		ret = check_pin(card, &line->pin, false, reason);
		if (ret != 0) {
			return ret;
		}
		for (uint8_t j = i + 1U; j < card->n_lines; j++) {
			if (pin_equal(&card->lines[j].pin, &line->pin)) {
				FAIL(-EINVAL, "two lines use the same pin");
			}
		}
		for (uint8_t j = 0U; j < card->n_spi_ports; j++) {
			if (pin_equal(&card->spi_ports[j].cs, &line->pin)) {
				FAIL(-EINVAL, "pin used both as a line and as an SPI chip-select");
			}
		}
		if (((line->flags & MIO_LINE_PULL_UP) != 0U) &&
		    ((line->flags & MIO_LINE_PULL_DOWN) != 0U)) {
			FAIL(-EINVAL, "line has both pull-up and pull-down");
		}
		if (!port_ref_ok(card, line->port)) {
			FAIL(-EINVAL, "line refers to a port the card does not define");
		}
		if (!label_ok(line->label)) {
			FAIL(-EINVAL, "line label empty or too long");
		}
	}

	return check_labels_unique(card, reason);
}

/* ---- decoder ---- */

static void get_label(char dst[MIO_LABEL_LEN], const uint8_t *src)
{
	memcpy(dst, src, MIO_LABEL_LEN);
}

static void get_str(char dst[MIO_LABEL_LEN + 1U], const uint8_t *src)
{
	memcpy(dst, src, MIO_LABEL_LEN);
	dst[MIO_LABEL_LEN] = '\0';
}

static int decode_record(struct mio_card *card, uint8_t type, const uint8_t *p, uint8_t len,
			 bool *have_resources, const char **reason)
{
	switch (type) {
	case MIO_REC_IDENTITY:
		if (len < MIO_REC_IDENTITY_LEN) {
			FAIL(-EINVAL, "IDENTITY record too short");
		}
		if (card->has_identity) {
			FAIL(-EINVAL, "duplicate IDENTITY record");
		}
		card->has_identity = true;
		card->identity.hw_major = p[0];
		card->identity.hw_minor = p[1];
		card->identity.date = get_le32(&p[2]);
		get_str(card->identity.name, &p[6]);
		get_str(card->identity.serial, &p[6U + MIO_LABEL_LEN]);
		return 0;

	case MIO_REC_RESOURCES:
		if (len < MIO_REC_RESOURCES_LEN) {
			FAIL(-EINVAL, "RESOURCES record too short");
		}
		if (*have_resources) {
			FAIL(-EINVAL, "duplicate RESOURCES record");
		}
		*have_resources = true;
		card->resources = get_le32(p);
		return 0;

	case MIO_REC_I2C_PORT: {
		struct mio_i2c_port_desc *port;

		if (len < MIO_REC_I2C_PORT_LEN) {
			FAIL(-EINVAL, "I2C_PORT record too short");
		}
		if (card->n_i2c_ports >= MIO_MAX_I2C_PORTS) {
			FAIL(-EINVAL, "too many I2C ports");
		}
		port = &card->i2c_ports[card->n_i2c_ports++];
		port->slot = p[0];
		port->bus = p[1];
		port->topology = p[2];
		port->speed = p[3];
		get_label(port->label, &p[4]);
		return 0;
	}

	case MIO_REC_SPI_PORT: {
		struct mio_spi_port_desc *port;

		if (len < MIO_REC_SPI_PORT_LEN) {
			FAIL(-EINVAL, "SPI_PORT record too short");
		}
		if (card->n_spi_ports >= MIO_MAX_SPI_PORTS) {
			FAIL(-EINVAL, "too many SPI ports");
		}
		port = &card->spi_ports[card->n_spi_ports++];
		port->slot = p[0];
		port->cs.source = p[1];
		port->cs.dev = p[2];
		port->cs.pin = p[3];
		port->cs_flags = p[4];
		port->max_freq = get_le32(&p[6]);
		get_label(port->label, &p[10]);
		return 0;
	}

	case MIO_REC_LINE: {
		struct mio_line_desc *line;

		if (len < MIO_REC_LINE_LEN) {
			FAIL(-EINVAL, "LINE record too short");
		}
		if (card->n_lines >= MIO_MAX_LINES) {
			FAIL(-EINVAL, "too many lines");
		}
		line = &card->lines[card->n_lines++];
		line->pin.source = p[0];
		line->pin.dev = p[1];
		line->pin.pin = p[2];
		line->flags = p[3];
		line->role = p[4];
		line->port = p[5];
		get_label(line->label, &p[6]);
		return 0;
	}

	case MIO_REC_ONBOARD_DEVICE: {
		struct mio_onboard_dev *dev;

		if (len < MIO_REC_ONBOARD_DEVICE_LEN) {
			FAIL(-EINVAL, "ONBOARD_DEVICE record too short");
		}
		if (card->n_devices >= MIO_MAX_ONBOARD_DEVS) {
			FAIL(-EINVAL, "too many on-card devices");
		}
		dev = &card->devices[card->n_devices++];
		dev->index = p[0];
		dev->type = get_le16(&p[1]);
		dev->bus = p[3];
		dev->addr = p[4];
		return 0;
	}

	default:
		/* Unknown or vendor record: skipped for forward compatibility */
		return 0;
	}
}

int mio_card_decode(const uint8_t *buf, size_t len, struct mio_card *card, const char **reason)
{
	bool have_resources = false;
	size_t total;
	size_t off;
	size_t end;
	int ret;

	if ((buf == NULL) || (card == NULL)) {
		FAIL(-EINVAL, "invalid arguments");
	}
	if ((len < MIO_CARD_HDR_SIZE) || (memcmp(buf, MIO_CARD_MAGIC, 4) != 0)) {
		FAIL(-ENODATA, "no card header (blank or unprogrammed EEPROM)");
	}
	if (buf[4] != MIO_CARD_VERSION) {
		FAIL(-ENOTSUP, "unsupported card format version");
	}

	total = get_le16(&buf[6]);
	if (total < MIO_CARD_MIN_SIZE) {
		FAIL(-EBADMSG, "image length too small");
	}
	if (total > len) {
		FAIL(-EBADMSG, "image longer than the data available");
	}

	end = total - MIO_CARD_CRC_SIZE;
	if (get_le32(&buf[end]) != mio_crc32(buf, end)) {
		FAIL(-EBADMSG, "CRC mismatch");
	}

	memset(card, 0, sizeof(*card));

	for (off = MIO_CARD_HDR_SIZE; off < end;) {
		uint8_t type;
		uint8_t rlen;

		if ((end - off) < 2U) {
			FAIL(-EINVAL, "truncated record header");
		}
		type = buf[off];
		rlen = buf[off + 1U];
		if (rlen > (end - off - 2U)) {
			FAIL(-EINVAL, "record runs past the end of the image");
		}

		ret = decode_record(card, type, &buf[off + 2U], rlen, &have_resources, reason);
		if (ret != 0) {
			return ret;
		}

		off += 2U + rlen;
	}

	if (!have_resources) {
		FAIL(-EINVAL, "no RESOURCES record");
	}

	return validate(card, reason);
}

/* ---- encoder ---- */

static uint8_t *put_rec_hdr(uint8_t *p, uint8_t type, uint8_t len)
{
	p[0] = type;
	p[1] = len;
	return &p[2];
}

int mio_card_encode(const struct mio_card *card, uint8_t *buf, size_t size, const char **reason)
{
	size_t total;
	uint8_t *p;
	int ret;

	if ((card == NULL) || (buf == NULL)) {
		FAIL(-EINVAL, "invalid arguments");
	}

	ret = validate(card, reason);
	if (ret != 0) {
		return ret;
	}

	total = MIO_CARD_HDR_SIZE + (card->has_identity ? (2U + MIO_REC_IDENTITY_LEN) : 0U) +
		(2U + MIO_REC_RESOURCES_LEN) +
		(card->n_devices * (2U + MIO_REC_ONBOARD_DEVICE_LEN)) +
		(card->n_i2c_ports * (2U + MIO_REC_I2C_PORT_LEN)) +
		(card->n_spi_ports * (2U + MIO_REC_SPI_PORT_LEN)) +
		(card->n_lines * (2U + MIO_REC_LINE_LEN)) + MIO_CARD_CRC_SIZE;
	if (total > size) {
		FAIL(-ENOSPC, "buffer too small for the image");
	}

	memcpy(buf, MIO_CARD_MAGIC, 4);
	buf[4] = MIO_CARD_VERSION;
	buf[5] = 0U;
	put_le16(&buf[6], (uint16_t)total);
	p = &buf[MIO_CARD_HDR_SIZE];

	if (card->has_identity) {
		p = put_rec_hdr(p, MIO_REC_IDENTITY, MIO_REC_IDENTITY_LEN);
		p[0] = card->identity.hw_major;
		p[1] = card->identity.hw_minor;
		put_le32(&p[2], card->identity.date);
		put_str(&p[6], card->identity.name, MIO_LABEL_LEN);
		put_str(&p[6U + MIO_LABEL_LEN], card->identity.serial, MIO_LABEL_LEN);
		p += MIO_REC_IDENTITY_LEN;
	}

	p = put_rec_hdr(p, MIO_REC_RESOURCES, MIO_REC_RESOURCES_LEN);
	put_le32(p, card->resources);
	p += MIO_REC_RESOURCES_LEN;

	for (uint8_t i = 0U; i < card->n_devices; i++) {
		const struct mio_onboard_dev *dev = &card->devices[i];

		p = put_rec_hdr(p, MIO_REC_ONBOARD_DEVICE, MIO_REC_ONBOARD_DEVICE_LEN);
		p[0] = dev->index;
		put_le16(&p[1], dev->type);
		p[3] = dev->bus;
		p[4] = dev->addr;
		p[5] = 0U;
		p += MIO_REC_ONBOARD_DEVICE_LEN;
	}

	for (uint8_t i = 0U; i < card->n_i2c_ports; i++) {
		const struct mio_i2c_port_desc *port = &card->i2c_ports[i];

		p = put_rec_hdr(p, MIO_REC_I2C_PORT, MIO_REC_I2C_PORT_LEN);
		p[0] = port->slot;
		p[1] = port->bus;
		p[2] = port->topology;
		p[3] = port->speed;
		put_str(&p[4], port->label, MIO_LABEL_LEN);
		p += MIO_REC_I2C_PORT_LEN;
	}

	for (uint8_t i = 0U; i < card->n_spi_ports; i++) {
		const struct mio_spi_port_desc *port = &card->spi_ports[i];

		p = put_rec_hdr(p, MIO_REC_SPI_PORT, MIO_REC_SPI_PORT_LEN);
		p[0] = port->slot;
		p[1] = port->cs.source;
		p[2] = port->cs.dev;
		p[3] = port->cs.pin;
		p[4] = port->cs_flags;
		p[5] = 0U;
		put_le32(&p[6], port->max_freq);
		put_str(&p[10], port->label, MIO_LABEL_LEN);
		p += MIO_REC_SPI_PORT_LEN;
	}

	for (uint8_t i = 0U; i < card->n_lines; i++) {
		const struct mio_line_desc *line = &card->lines[i];

		p = put_rec_hdr(p, MIO_REC_LINE, MIO_REC_LINE_LEN);
		p[0] = line->pin.source;
		p[1] = line->pin.dev;
		p[2] = line->pin.pin;
		p[3] = line->flags;
		p[4] = line->role;
		p[5] = line->port;
		put_str(&p[6], line->label, MIO_LABEL_LEN);
		p += MIO_REC_LINE_LEN;
	}

	put_le32(p, mio_crc32(buf, total - MIO_CARD_CRC_SIZE));

	return (int)total;
}
