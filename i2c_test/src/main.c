/*
 * Peripheral bring-up test for the mind,mindos_n6 board. The tests run
 * from the shell ("mind test_all", or "mind <part>" for a single one;
 * see mind_shell.c). main() only starts the services that should be
 * available from boot: 5V on USB2 and the DHCPv4 client.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include "lan_test.h"
#include "usb_power_test.h"

#include <mind_io/mind_io.h>

int main(void)
{
	const struct mio_card *card = mio_card_get();
	const char *reason;

	printk("mindos_n6 peripheral test\n");

	if (card != NULL) {
		printk("I/O card: %s (serial %s)\n", card->identity.name, card->identity.serial);
	} else {
		enum mio_card_status st = mio_card_status(&reason);

		printk("I/O card: %s - %s\n", mio_card_status_name(st), reason);
	}

	usb_power_init();
	lan_start();

	printk("Type 'mind test_all' to test every part, or 'mind' to list single tests.\n");

	return 0;
}
