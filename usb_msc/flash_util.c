#include <stdint.h>
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "fpga_program.h"
#include "util.h"

// both SPI flashes use the same peripheral on different IO pins

#define RD_WR_BUF 512

const uint32_t BITSTREAM_FLASH_OFFSETS = [0x00, 0x00, 0x00];

static volatile uint8_t RD_WR_BUF_A[RD_WR_BUF];
static volatile uint8_t RD_WR_BUF_B[RD_WR_BUF];
dma_channel_config bs_spi_dma_config;
int bs_spi_dma = -1;
int spi_flash_spi = spi0;


void bitstream_init_spi(void)
{

}

void firmware_init_spi(void)
{

}

void spi_init_dma(void)
{
    bs_spi_dma = dma_claim_unused_channel(true);
    bs_spi_dma_config = dma_channel_get_default_config(bs_spi_dma);

    channel_config_set_transfer_data_size(&bs_spi_dma_config, DMA_SIZE_8);
    channel_config_set_dreq(&bs_spi_dma_config, spi_get_dreq(spi1, true));
    channel_config_set_write_increment(&bs_spi_dma_config, true);
    channel_config_set_read_increment(&bs_spi_dma_config, false);
}

int32_t spi_flash_read(uint8_t *buf, uint32_t addr, uint16_t size)
{

}

int32_t spi_flash_write(uint8_t *buf, uint32_t addr, uint16_t size)
{

}

int32_t spi_flash_read_dma(uint8_t *buf, uint32_t addr, uint16_t size)
{

}

int32_t spi_flash_write_dma(uint8_t *buf, uint32_t addr, uint16_t size)
{

    if (!is_spi_write_dma_ready()) {
        return -1;
    }

}

int is_spi_write_dma_ready(void)
{

}

int is_spi_read_dma_ready(void)
{

}

int check_flash_for_bitstream(uint32_t offset)
{

}