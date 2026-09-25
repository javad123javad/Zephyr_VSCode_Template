/*
 * mind_io sample: prints what the plugged-in I/O card provides and blinks
 * one of its lines, addressed only by the label printed on the card.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include <mind_io/mind_io.h>

int main(void)
{
	const struct mio_card *card = mio_card_get();
	struct mio_line line;
	const char *reason;

	if (card == NULL) {
		enum mio_card_status st = mio_card_status(&reason);

		printk("no usable I/O card (%s): %s\n", mio_card_status_name(st), reason);
		return 0;
	}

	printk("I/O card %s rev %u.%u, serial %s\n", card->identity.name,
	       card->identity.hw_major, card->identity.hw_minor, card->identity.serial);

	for (uint8_t i = 0; i < card->n_i2c_ports; i++) {
		const char *label = card->i2c_ports[i].label;

		printk("  I2C port %-15s %s\n", label,
		       (mio_port_by_label(label) != NULL) ? "ready" : "not ready");
	}
	for (uint8_t i = 0; i < card->n_spi_ports; i++) {
		const char *label = card->spi_ports[i].label;

		printk("  SPI port %-15s %s\n", label,
		       (mio_port_by_label(label) != NULL) ? "ready" : "not ready");
	}
	for (uint8_t i = 0; i < card->n_lines; i++) {
		printk("  line     %-15s %s\n", card->lines[i].label,
		       mio_role_name(card->lines[i].role));
	}

	if (mio_line_get(CONFIG_SAMPLE_BLINK_LINE, &line) != 0) {
		printk("the card has no line '%s' to blink\n", CONFIG_SAMPLE_BLINK_LINE);
		return 0;
	}

	for (int on = 1;; on = !on) {
		if (mio_line_set(&line, on) != 0) {
			printk("'%s' is not an output\n", CONFIG_SAMPLE_BLINK_LINE);
			return 0;
		}
		k_msleep(500);
	}

	return 0;
}
