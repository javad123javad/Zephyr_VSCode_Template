# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MIND_IO_I2C_PORT OR CONFIG_MIND_IO_SPI_PORT)
  zephyr_library_named(mind_io_drivers)
  zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../subsys/mind_io)
  zephyr_library_sources_ifdef(CONFIG_MIND_IO_I2C_PORT i2c/mio_i2c_port.c)
  zephyr_library_sources_ifdef(CONFIG_MIND_IO_SPI_PORT spi/mio_spi_port.c)
endif()
