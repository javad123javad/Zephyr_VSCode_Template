/* SPDX-License-Identifier: Apache-2.0 */

#ifndef AXISRAM_TEST_H_
#define AXISRAM_TEST_H_

/* Writes and reads back a test pattern in a buffer placed in
 * AXISRAM3 (see the boards/ overlay files for the &axisram3 node and
 * axisram_test.c for the full story). One-shot sanity check, not a
 * periodic test - call once at boot.
 */
void axisram_test_run(void);

#endif /* AXISRAM_TEST_H_ */
