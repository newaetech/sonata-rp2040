#pragma once

#include <stdint.h>

enum spi_flash_status1 {
    SPI_FLASH_STATUS_BUSY    = 0b1,
    SPI_FLASH_WRITE_ENABLED  = 0b10,
    SPI_FLASH_BLOCK_PROTECT0 = 0b100,
    SPI_FLASH_BLOCK_PROTECT1 = 0b1000,
    SPI_FLASH_BLOCK_PROTECT2 = 0b10000,
    SPI_FLASH_BLOCK_PROTECT3 = 0b100000,
    SPI_FLASH_TOPBOT_PROTECT = 0b1000000,
    SPI_FLASH_STATUS_PROTECT = 0b10000000,
};

#define CONST_64k 0x10000
#define CONST_32k 0x8000
#define CONST_4k  0x1000

int spi_flash_read(uint32_t addr, uint8_t *data, uint32_t len); // use fast read?

// writes always go from addr to page end
int spi_flash_page_program_blocking(uint32_t addr, uint8_t *data, uint16_t len);
int spi_flash_sector_erase_blocking(uint32_t addr);
void bitstream_init_spi(uint32_t baud);
void firmware_init_spi(uint32_t baud);
int spi_flash_is_busy(void);
int spi_flash_64k_erase_nonblocking(uint32_t addr);
// int spi_flash_poll_busy(void);
// int spi_flash_poll_write_enable(void);
enum spi_flash_status1 spi_flash_read_status(void);