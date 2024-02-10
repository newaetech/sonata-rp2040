#pragma once
#include <stdint.h>

int spi_flash_read(uint32_t addr, uint8_t *data, uint32_t len); // use fast read?

// writes always go from addr to page end
int spi_flash_page_program(uint32_t addr, uint8_t data[256]);
int spi_flash_sector_erase(uint32_t addr);
void bitstream_init_spi(void);