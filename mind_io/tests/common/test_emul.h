/*
 * Test-only emulators used by the mind_io tests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MIND_IO_TEST_EMUL_H_
#define MIND_IO_TEST_EMUL_H_

#include <stdint.h>

#define TEST_CARD_EEPROM_SIZE 8192U

/** Backing memory of the emulated card EEPROM. */
uint8_t *test_card_eeprom_mem(void);

struct test_pca9557_regs {
	uint8_t out;
	uint8_t pol;
	uint8_t cfg;
};

/** Current register state of the emulated PCA9557. */
struct test_pca9557_regs test_pca9557_regs(void);

/** Levels driven onto the PCA9557 pins from outside (for input pins). */
void test_pca9557_set_inputs(uint8_t levels);

struct test_spi_echo_stats {
	unsigned int transfers;
	uint32_t last_freq;
	int cs_level; /**< physical IO_SPI_CS0 level seen during the last transfer */
};

struct test_spi_echo_stats test_spi_echo_stats(void);

#endif /* MIND_IO_TEST_EMUL_H_ */
