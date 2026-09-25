/*
 * mind_io lines: the card's named signals (connector GPIOs and on-card
 * expander pins), configured from their LINE records at boot.
 *
 * Connector GPIOs that the card does not declare are never touched, so they
 * keep their reset state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <mind_io/mind_io.h>
#include "mio_internal.h"

LOG_MODULE_DECLARE(mind_io, CONFIG_MIND_IO_LOG_LEVEL);

static gpio_flags_t line_gpio_flags(const struct mio_line_desc *line)
{
	gpio_flags_t flags = 0U;

	if ((line->flags & MIO_LINE_ACTIVE_LOW) != 0U) {
		flags |= GPIO_ACTIVE_LOW;
	}
	if ((line->flags & MIO_LINE_PULL_UP) != 0U) {
		flags |= GPIO_PULL_UP;
	}
	if ((line->flags & MIO_LINE_PULL_DOWN) != 0U) {
		flags |= GPIO_PULL_DOWN;
	}

	if ((line->flags & MIO_LINE_OUTPUT) != 0U) {
		flags |= ((line->flags & MIO_LINE_INIT_ACTIVE) != 0U) ? GPIO_OUTPUT_ACTIVE
								       : GPIO_OUTPUT_INACTIVE;
		if ((line->flags & MIO_LINE_OPEN_DRAIN) != 0U) {
			flags |= GPIO_OPEN_DRAIN;
		}
	} else {
		flags |= GPIO_INPUT;
	}

	return flags;
}

void mio_lines_init(const struct mio_card *card)
{
	char name[12];

	for (uint8_t i = 0U; i < card->n_lines; i++) {
		const struct mio_line_desc *line = &card->lines[i];
		int ret = mio_pin_configure(&line->pin, line_gpio_flags(line));

		if (ret != 0) {
			LOG_ERR("line %s on %s: configuration failed (%d)", line->label,
				mio_pin_name(&line->pin, name, sizeof(name)), ret);
		}
	}
}

int mio_line_get(const char *label, struct mio_line *line)
{
	const struct mio_card *card = mio_card_get();

	if (card == NULL) {
		return -ENODEV;
	}
	if ((label == NULL) || (line == NULL)) {
		return -EINVAL;
	}

	for (uint8_t i = 0U; i < card->n_lines; i++) {
		if (strncmp(card->lines[i].label, label, MIO_LABEL_LEN) == 0) {
			line->desc = &card->lines[i];
			return 0;
		}
	}

	return -ENOENT;
}

int mio_line_set(const struct mio_line *line, int value)
{
	if ((line == NULL) || (line->desc == NULL)) {
		return -EINVAL;
	}
	if ((line->desc->flags & MIO_LINE_OUTPUT) == 0U) {
		return -ENOTSUP;
	}

	return (mio_pin_set(&line->desc->pin, value) == 0) ? 0 : -EIO;
}

int mio_line_read(const struct mio_line *line)
{
	int ret;

	if ((line == NULL) || (line->desc == NULL)) {
		return -EINVAL;
	}

	ret = mio_pin_get(&line->desc->pin);

	return (ret < 0) ? -EIO : ret;
}
