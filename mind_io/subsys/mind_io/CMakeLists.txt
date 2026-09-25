# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(mind_io)
zephyr_library_sources(core.c lines.c pins.c)
zephyr_library_sources_ifdef(CONFIG_MIND_IO_SHELL shell.c)
