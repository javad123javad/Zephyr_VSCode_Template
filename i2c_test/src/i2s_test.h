/* SPDX-License-Identifier: Apache-2.0 */

#ifndef I2S_TEST_H_
#define I2S_TEST_H_

/* No API: this module runs itself. Linking i2s_test.c in is enough -
 * it starts its own K_THREAD_DEFINE'd playback thread that streams a
 * continuous test tone into the MAX98357A on io_i2s. See i2s_test.c
 * for details (buffer prefill and thread priority notes in
 * particular - both were the fix for real underrun bugs found while
 * bringing this up).
 */

#endif /* I2S_TEST_H_ */
