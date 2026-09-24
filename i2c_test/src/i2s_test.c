/*
 * MAX98357A I2S amp/DAC on io_i2s (i2s1). It has no control bus (no
 * I2C/SPI, no Zephyr devicetree binding of its own) - it just plays
 * whatever standard I2S stream it's fed, so the "device" here is
 * simply the i2s1 controller. Each test run plays a ~689Hz test tone
 * (identical on both channels) for I2S_TEST_DURATION_MS: audible
 * output is the actual pass criterion, the test itself can only check
 * that the stream ran without a driver error or DMA underrun.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i2s_test.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>

#define I2S_SAMPLE_NO    64
#define I2S_FRAME_CLK_HZ 44100
#define I2S_NUM_BLOCKS   4
#define I2S_BLOCK_SIZE   (2 * sizeof(int16_t) * I2S_SAMPLE_NO) /* stereo, 16-bit */
#define I2S_TEST_DURATION_MS 2000
#define I2S_ALLOC_TIMEOUT_MS 100

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

K_SEM_DEFINE(i2s_start_sem, 0, 1);
K_SEM_DEFINE(i2s_done_sem, 0, 1);
static int i2s_result;

static int i2s_queue_block(void)
{
	void *block;
	int ret;

	ret = k_mem_slab_alloc(&i2s_tx_mem_slab, &block, K_MSEC(I2S_ALLOC_TIMEOUT_MS));
	if (ret < 0) {
		printk("I2S: block alloc failed (%d)\n", ret);
		return ret;
	}
	i2s_fill_block(block);

	ret = i2s_write(i2s_dev, block, I2S_BLOCK_SIZE);
	if (ret < 0) {
		printk("I2S: write failed (%d)\n", ret);
		k_mem_slab_free(&i2s_tx_mem_slab, block);
	}

	return ret;
}

static int i2s_play(uint32_t duration_ms)
{
	const uint32_t n_blocks = duration_ms * (I2S_FRAME_CLK_HZ / 1000U) / I2S_SAMPLE_NO;
	struct i2s_config cfg;
	int ret;

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
		return ret;
	}

	/* Pre-fill the whole TX queue before starting playback. Each block
	 * is only ~1.45ms of audio (64 samples @ 44.1kHz); starting with
	 * just one block queued left no headroom for this thread to keep
	 * up once playing, and the DMA underran waiting for the next one.
	 * Filling all I2S_NUM_BLOCKS slots first gives several block-times
	 * of buffer headroom.
	 */
	for (unsigned int i = 0; i < I2S_NUM_BLOCKS; i++) {
		ret = i2s_queue_block();
		if (ret < 0) {
			(void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
			return ret;
		}
	}

	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret < 0) {
		printk("I2S: trigger start failed (%d)\n", ret);
		(void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
		return ret;
	}

	for (uint32_t i = I2S_NUM_BLOCKS; i < n_blocks; i++) {
		ret = i2s_queue_block();
		if (ret < 0) {
			printk("I2S: stream stopped after %u of %u blocks\n", i, n_blocks);
			(void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
			return ret;
		}
	}

	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
	if (ret < 0) {
		printk("I2S: trigger drain failed (%d)\n", ret);
		(void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
		return ret;
	}

	/* Let the queued blocks finish so the next run finds the stream idle */
	k_msleep(DIV_ROUND_UP((I2S_NUM_BLOCKS + 1U) * I2S_SAMPLE_NO * 1000U, I2S_FRAME_CLK_HZ));

	return 0;
}

static void i2s_playback_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		k_sem_take(&i2s_start_sem, K_FOREVER);
		i2s_result = i2s_play(I2S_TEST_DURATION_MS);
		k_sem_give(&i2s_done_sem);
	}
}

/* Priority -1: keeping the TX queue fed is time-critical (~1.45ms per
 * block). Streaming from a lower-priority thread (such as the shell's)
 * lets other work eat into the buffer margin; that caused underruns
 * after a few hundred blocks during bring-up.
 */
K_THREAD_DEFINE(i2s_playback_tid, 1024, i2s_playback_thread, NULL, NULL, NULL, -1, 0, 0);

int i2s_test_run(const struct shell *sh)
{
	if (!device_is_ready(i2s_dev)) {
		shell_error(sh, "I2S: device not ready");
		return -ENODEV;
	}

	shell_print(sh, "I2S @ io_i2s: playing ~%u Hz test tone into the MAX98357A for %u ms",
		    I2S_FRAME_CLK_HZ / I2S_SAMPLE_NO, I2S_TEST_DURATION_MS);

	k_sem_reset(&i2s_done_sem);
	k_sem_give(&i2s_start_sem);

	if (k_sem_take(&i2s_done_sem, K_MSEC(I2S_TEST_DURATION_MS + 3000)) != 0) {
		shell_error(sh, "I2S: playback did not finish");
		return -ETIMEDOUT;
	}

	if (i2s_result != 0) {
		shell_error(sh, "I2S: playback failed (%d)", i2s_result);
		return i2s_result;
	}

	shell_print(sh, "I2S: stream completed without errors - check the tone was audible");
	return 0;
}
