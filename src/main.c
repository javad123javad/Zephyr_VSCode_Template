/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
int i;
int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);
	i = 10;

	printf("Int is: %d\n", i);
	
	return 0;
}
