/*
 * MAX98357A I2S amp/DAC on io_i2s (i2s1). It has no control bus (no
 * I2C/SPI, no Zephyr devicetree binding of its own) - it just plays
 * whatever standard I2S stream it's fed, so the "device" here is
 * simply the i2s1 controller. This runs its own thread that
 * continuously streams a ~689Hz test tone (identical on both
 * channels); nothing is logged per block.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i2s_test.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/sys/iterable_sections.h>

#define I2S_SAMPLE_NO    64
#define I2S_FRAME_CLK_HZ 44100
#define I2S_NUM_BLOCKS   4
#define I2S_BLOCK_SIZE   (2 * sizeof(int16_t) * I2S_SAMPLE_NO) /* stereo, 16-bit */

/* One full cycle of a sine wave; tiling these blocks back-to-back
 * produces a continuous, glitch-free ~689Hz (44100/64) tone.
 */
static const int16_t i2s_sine[I2S_SAMPLE_NO] = {
	  3211,   6392,   9511,  12539,  15446,  18204,  20787,  23169,
	 25329,  27244,  28897,  30272,  31356,  32137,  32609,  32767,
	 32609,  32137,  31356,  30272,  28897,  27244,  25329,  23169,
	 20787,  18204,  15446,  12539,   9511,   6392,   3211,      0,
	 -3212,  -6393,  -9512, -12540, -15447, -18205, -20788, -23170,
	-25330, -27245, -28898, -30273, -31357, -32138, -32610, -32767,
	-32610, -32138, -31357, -30273, -28898, -27245, -25330, -23170,
	-20788, -18205, -15447, -12540,  -9512,  -6393,  -3212,     -1,
};

#ifdef CONFIG_NOCACHE_MEMORY
#define I2S_MEM_SLAB_CACHE_ATTR __nocache
#else
#define I2S_MEM_SLAB_CACHE_ATTR
#endif

static char I2S_MEM_SLAB_CACHE_ATTR __aligned(WB_UP(32))
	i2s_mem_slab_buf[I2S_NUM_BLOCKS * WB_UP(I2S_BLOCK_SIZE)];

static STRUCT_SECTION_ITERABLE(k_mem_slab, i2s_tx_mem_slab) =
	Z_MEM_SLAB_INITIALIZER(i2s_tx_mem_slab, i2s_mem_slab_buf,
				WB_UP(I2S_BLOCK_SIZE), I2S_NUM_BLOCKS);

static const struct device *i2s_dev = DEVICE_DT_GET(DT_NODELABEL(i2s1));

static void i2s_fill_block(int16_t *block)
{
	for (int i = 0; i < I2S_SAMPLE_NO; i++) {
		block[2 * i] = i2s_sine[i];     /* left */
		block[2 * i + 1] = i2s_sine[i]; /* right */
	}
}

static void i2s_playback_thread(void *p1, void *p2, void *p3)
{
	struct i2s_config cfg;
	void *block;
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (!device_is_ready(i2s_dev)) {
		printk("I2S: device not ready\n");
		return;
	}

	cfg.word_size = 16U;
	cfg.channels = 2U;
	cfg.format = I2S_FMT_DATA_FORMAT_I2S;
	cfg.frame_clk_freq = I2S_FRAME_CLK_HZ;
	cfg.block_size = I2S_BLOCK_SIZE;
	cfg.timeout = 2000;
	/* STM32 is the I2S clock master; the MAX98357A is always a slave. */
	cfg.options = I2S_OPT_FRAME_CLK_CONTROLLER | I2S_OPT_BIT_CLK_CONTROLLER;
	cfg.mem_slab = &i2s_tx_mem_slab;

	ret = i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
	if (ret < 0) {
		printk("I2S: configure failed (%d)\n", ret);
		return;
	}

	/* Pre-fill the whole TX queue before starting playback. Each block
	 * is only ~1.45ms of audio (64 samples @ 44.1kHz); starting with
	 * just one block queued left no headroom for this thread to keep
	 * up once playing, and the DMA underran waiting for the next one
	 * (queue_get() failed with an empty queue right after the first
	 * block finished). Filling all I2S_NUM_BLOCKS slots first gives
	 * several block-times of buffer headroom.
	 */
	for (unsigned int i = 0; i < I2S_NUM_BLOCKS; i++) {
		ret = k_mem_slab_alloc(&i2s_tx_mem_slab, &block, K_FOREVER);
		if (ret < 0) {
			printk("I2S: prefill alloc %u failed (%d)\n", i, ret);
			return;
		}
		i2s_fill_block(block);

		ret = i2s_write(i2s_dev, block, I2S_BLOCK_SIZE);
		if (ret < 0) {
			printk("I2S: prefill write %u failed (%d)\n", i, ret);
			return;
		}
	}

	printk("I2S: prefilled %u/%u blocks, starting\n", I2S_NUM_BLOCKS, I2S_NUM_BLOCKS);

	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret < 0) {
		printk("I2S: trigger start failed (%d)\n", ret);
		return;
	}

	printk("I2S @ io_i2s: playing ~%u Hz test tone into MAX98357A\n",
	       I2S_FRAME_CLK_HZ / I2S_SAMPLE_NO);

	for (uint32_t block_no = 1;; block_no++) {
		ret = k_mem_slab_alloc(&i2s_tx_mem_slab, &block, K_FOREVER);
		if (ret < 0) {
			printk("I2S: alloc failed after %u blocks (%d)\n", block_no, ret);
			return;
		}
		i2s_fill_block(block);

		ret = i2s_write(i2s_dev, block, I2S_BLOCK_SIZE);
		if (ret < 0) {
			printk("I2S: stream write failed after %u blocks (%d)\n",
			       block_no, ret);
			return;
		}
	}
}

/* Priority -1: higher priority than main()'s default (0). Keeping the
 * TX queue fed is time-critical (~1.45ms per block); main()'s tick
 * loop (I2C/CAN housekeeping) is not, so audio should preempt it
 * rather than the other way around - with equal-or-lower priority,
 * main()'s periodic work (BME280 sampling in particular) was eating
 * into the buffer margin and causing underruns after a few hundred
 * blocks.
 *
 * Delayed start so this thread's prints don't interleave with
 * main()'s boot-time scan/init output on the shared UART.
 */
K_THREAD_DEFINE(i2s_playback_tid, 1024, i2s_playback_thread, NULL, NULL, NULL, -1, 0, 1500);
