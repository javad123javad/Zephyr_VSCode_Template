/*
 * AXISRAM3: a 448KB SRAM bank separate from the primary axisram1/2
 * region already used as zephyr,sram. See the &axisram3 overlay
 * comment (in the boards/ overlay files) for why zephyr,sram itself
 * can't just be switched to axisram1 to get "more RAM" - short: on
 * this secure/serial-boot ("/sb") target, the Boot ROM RAM-loads the
 * whole image into a fixed 511KB window at the top of the
 * axisram1/2 range (what "axisram2" actually denotes here), and
 * anywhere else in that range is invalid during SB/FSBL boot.
 * AXISRAM3-6 are a separate, additional bank entirely, unaffected
 * by that restriction.
 *
 * Placing data there needs two things beyond the overlay's `reg`:
 *   - CONFIG_STM32N6_AXISRAM (see soc/st/stm32/stm32n6x/axisram/) -
 *     a dedicated driver that turns on the bank's RCC clock gate and
 *     calls HAL_RAMCFG_EnableAXISRAM() at PRE_KERNEL_2, before this
 *     buffer is ever touched. It auto-selects once the &axisram3
 *     overlay sets status = "okay", no prj.conf line needed.
 *   - placing the buffer via the standard zephyr,memory-region
 *     mechanism (Z_GENERIC_SECTION + the node's generated linker
 *     region name), not just declaring a normal static variable.
 *
 * This does a write/read-back sanity check to confirm the bank is
 * actually clocked, enabled, and addressable - not just linked.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "axisram_test.h"

#include <zephyr/kernel.h>
#include <zephyr/linker/devicetree_regions.h>

#define AXISRAM3_NODE DT_NODELABEL(axisram3)

static uint8_t axisram3_buf[256] Z_GENERIC_SECTION(LINKER_DT_NODE_REGION_NAME_TOKEN(AXISRAM3_NODE));

int axisram_test_run(const struct shell *sh)
{
	shell_print(sh, "AXISRAM3: %u KB @ 0x%08lx (test buffer @ %p)",
		    (unsigned int)(DT_REG_SIZE(AXISRAM3_NODE) / 1024),
		    (unsigned long)DT_REG_ADDR(AXISRAM3_NODE), (void *)axisram3_buf);

	for (size_t i = 0; i < sizeof(axisram3_buf); i++) {
		axisram3_buf[i] = (uint8_t)i;
	}

	for (size_t i = 0; i < sizeof(axisram3_buf); i++) {
		if (axisram3_buf[i] != (uint8_t)i) {
			shell_error(sh, "AXISRAM3: mismatch at offset %u (wrote 0x%02x, read 0x%02x)",
				    (unsigned int)i, (unsigned int)(uint8_t)i,
				    (unsigned int)axisram3_buf[i]);
			return -EIO;
		}
	}

	shell_print(sh, "AXISRAM3: %u bytes written and read back",
		    (unsigned int)sizeof(axisram3_buf));
	return 0;
}
