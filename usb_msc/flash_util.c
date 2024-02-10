#include <stdint.h>
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "fpga_program.h"
#include "flash_util.h"
#include "util.h"

// both SPI flashes use the same peripheral on different IO pins

enum spi_flash_commands {
    SPI_CMD_WRITE_ENABLE = 0x06,
    SPI_CMD_WRITE_DISABLE = 0x04,
    SPI_CMD_READ_DATA = 0x03,
    SPI_CMD_READ_DATA_4ADDR = 0x13,
    SPI_CMD_READ_DATA_FAST = 0x0B,
    SPI_CMD_READ_DATA_4ADDR_FAST = 0x0C,
    SPI_CMD_PAGE_PROGRAM = 0x02,
    SPI_CMD_PAGE_4ADDR_PROGRAM = 0x12,
    SPI_CMD_SECTOR_ERASE_4ADDR = 0x21,

    SPI_CMD_READ_STATUS1 = 0x05,
    SPI_CMD_WRITE_STATUS1 = 0x01,

    SPI_CMD_READ_STATUS2 = 0x35,
    SPI_CMD_WRITE_STATUS2 = 0x31,

    SPI_CMD_READ_STATUS3 = 0x15,
    SPI_CMD_WRITE_STATUS3 = 0x11,

    SPI_CMD_ENTER_4BYTE_ADDR_MODE = 0xB7,
    SPI_CMD_EXIT_4BYTE_ADDR_MODE = 0xE9
};

#define RD_WR_BUF 512

const uint32_t BITSTREAM_FLASH_OFFSETS[] = {0x00, 0x00, 0x00};

static volatile uint8_t RD_WR_BUF_A[RD_WR_BUF];
static volatile uint8_t RD_WR_BUF_B[RD_WR_BUF];
dma_channel_config bs_spi_dma_config;
int bs_spi_dma = -1;
spi_inst_t *flash_spi = spi0;

enum bitstream_spi_pins {
    BS_SPI_DI = 23,
    BS_SPI_DO = 20,
    BS_SPI_CLK = 22,
    BS_SPI_CS = 21
};

int SPI_FLASH_CS_PIN = 0;


void bitstream_init_spi(void)
{
    spi_init(flash_spi, 25E6); //Try 25MHz

    gpio_set_function(BS_SPI_DI, GPIO_FUNC_SPI); // RX pin
    gpio_set_function(BS_SPI_DO, GPIO_FUNC_SPI); // TX pin
    gpio_set_function(BS_SPI_CLK, GPIO_FUNC_SPI); // CLK pin

    gpio_init(BS_SPI_CLK);
    gpio_set_dir(BS_SPI_CLK, GPIO_OUT);
}

void firmware_init_spi(void)
{

}

// void spi_init_dma(void)
// {
//     bs_spi_dma = dma_claim_unused_channel(true);
//     bs_spi_dma_config = dma_channel_get_default_config(bs_spi_dma);

//     channel_config_set_transfer_data_size(&bs_spi_dma_config, DMA_SIZE_8);
//     channel_config_set_dreq(&bs_spi_dma_config, spi_get_dreq(spi1, true));
//     channel_config_set_write_increment(&bs_spi_dma_config, true);
//     channel_config_set_read_increment(&bs_spi_dma_config, false);
// }

int spi_flash_poll_status(uint8_t and_match)
{
    uint8_t cmd = SPI_CMD_READ_STATUS1;
    uint8_t rtn = 0;

    gpio_put(SPI_FLASH_CS_PIN, 0);

    spi_write_blocking(flash_spi, &cmd, 1);

    while (!(rtn & and_match)) {
        spi_read_blocking(flash_spi, 0x00, &rtn, 1);
    }

    return 0;
}

int spi_flash_read_status(void)
{
    uint8_t cmd = SPI_CMD_READ_STATUS1;
    uint8_t rtn = 0;

    gpio_put(SPI_FLASH_CS_PIN, 0);

    spi_write_blocking(flash_spi, &cmd, 1);
    spi_read_blocking(flash_spi, 0x00, &rtn, 1);

    gpio_put(SPI_FLASH_CS_PIN, 1);

    return rtn;
}

int spi_flash_write_enable(void)
{
    uint8_t cmd = SPI_CMD_WRITE_ENABLE;
    gpio_put(SPI_FLASH_CS_PIN, 0);
    spi_write_blocking(flash_spi, &cmd, 1);
    gpio_put(SPI_FLASH_CS_PIN, 1);

    // check that write enable status is set
    spi_flash_poll_status(0x02);
    // if ((spi_flash_read_status()) & 0x02) {
    //     return -1;
    // }

    return 0;
}


int spi_flash_sector_erase(uint32_t addr)
{
    uint8_t cmd = SPI_CMD_SECTOR_ERASE_4ADDR;
    uint8_t addr_u8[] = {BE_U32_TO_4U8(addr)}; // ensure proper endianness

    if (spi_flash_write_enable()) return -1;

    gpio_put(SPI_FLASH_CS_PIN, 0);
    spi_write_blocking(flash_spi, &cmd, 1);

    spi_write_blocking(flash_spi, addr_u8, 4);

    gpio_put(SPI_FLASH_CS_PIN, 1);

    // wait for erase to finish
    // while (!(spi_flash_read_status() & 0x01));
    spi_flash_poll_status(0x01);
    return 0;
}

int spi_flash_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    uint8_t cmd = SPI_CMD_READ_DATA_4ADDR_FAST;
    uint8_t addr_u8[] = {BE_U32_TO_4U8(addr)}; // ensure proper endianness

    gpio_put(SPI_FLASH_CS_PIN, 0);
    spi_write_blocking(flash_spi, &cmd, 1);

    spi_write_blocking(flash_spi, addr_u8, 4);

    // need a dummy byte for fast read
    cmd = 0x00;
    spi_write_blocking(flash_spi, &cmd, 1); 

    spi_read_blocking(flash_spi, 0x00, data, len);

    gpio_put(SPI_FLASH_CS_PIN, 1);

    return 0;
}

int spi_flash_page_program(uint32_t addr, uint8_t data[256])
{
    uint16_t write_len = 256 - (addr & 0xFF);
    uint8_t cmd = SPI_CMD_PAGE_4ADDR_PROGRAM;
    uint8_t addr_u8[] = {BE_U32_TO_4U8(addr)}; // ensure proper endianness

    if (spi_flash_write_enable()) return -1; // ensure write is enabled

    gpio_put(SPI_FLASH_CS_PIN, 0);
    spi_write_blocking(flash_spi, &cmd, 1);

    spi_write_blocking(flash_spi, addr_u8, 4);

    spi_write_blocking(flash_spi, data, write_len);

    gpio_put(SPI_FLASH_CS_PIN, 1);

    // wait for programming to finish
    // while (!(spi_flash_read_status() & 0x01));
    spi_flash_poll_status(0x01);

    return 0;
}

int check_flash_for_bitstream(uint32_t offset)
{
    return 0;
}