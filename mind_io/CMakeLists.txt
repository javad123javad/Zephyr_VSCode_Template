# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MIND_IO_CARD_FORMAT)
  zephyr_include_directories(include)
  zephyr_library_named(mind_io_card_format)
  zephyr_library_sources(lib/card_format/card_format.c)
endif()

add_subdirectory_ifdef(CONFIG_MIND_IO subsys/mind_io)
add_subdirectory_ifdef(CONFIG_MIND_IO drivers)
