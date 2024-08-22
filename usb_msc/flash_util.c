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
    SPI_CMD_READ_DATA_4ADDR = 0x13, // 0x13
    SPI_CMD_READ_DATA_FAST = 0x0B,
    SPI_CMD_READ_DATA_4ADDR_FAST = 0x0C,
    SPI_CMD_PAGE_PROGRAM = 0x02,
    SPI_CMD_PAGE_4ADDR_PROGRAM = 0x12, // 0x12
    SPI_CMD_SECTOR_ERASE_4ADDR = 0x21,

    SPI_CMD_READ_STATUS1 = 0x05,
    SPI_CMD_WRITE_STATUS1 = 0x01,

    SPI_CMD_READ_STATUS2 = 0x35,
    SPI_CMD_WRITE_STATUS2 = 0x31,

    SPI_CMD_READ_STATUS3 = 0x15,
    SPI_CMD_WRITE_STATUS3 = 0x11,

    SPI_CMD_ENTER_4BYTE_ADDR_MODE = 0xB7,
    SPI_CMD_EXIT_4BYTE_ADDR_MODE = 0xE9,

    SPI_CMD_64K_BLOCK_ERASE = 0xDC, // 0xDC
    SPI_CMD_32K_BLOCK_ERASE = 0x52,
    SPI_CMD_CHIP_ERASE = 0xC7,

    SPI_CMD_WRITE_EXT_ADDR = 0xC5,
    SPI_CMD_READ_EXT_ADDR = 0xC8,

};


#define RD_WR_BUF 512

const uint32_t BITSTREAM_FLASH_OFFSETS[] = {0x00, 0x00, 0x00};

void delay_short(void)
{
    for (volatile uint32_t i = 0; i < 50; i++);
}

// each bitstream 32MB apart
#define BITSTREAM_FLASH_OFFSET 0x2000000

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

enum firmware_spi_pins {
    FW_SPI_DI = 3, // QSPI_D0
    FW_SPI_DO = 0, // QSPI_D1
    FW_SPI_CLK = 2, // QSPI_SCLK
    FW_SPI_CS = 1, //QSPI_CS
    // NOTE: For single bit SPI, set these both high
    FW_SPI_W_NEN = 4, // QSPI_D2/write enable (low)
    FW_SPI_NHOLD = 5 // QSPI_D3/hold (low)
};

int SPI_FLASH_CS_PIN = 0;

void spi_write_extended_addr_reg(uint8_t addr)
{
    spi_flash_write_enable();
    uint8_t cmd = SPI_CMD_WRITE_EXT_ADDR;

    spi_cs_put(0);
    spi_write_blocking(flash_spi, &cmd, 1);
    spi_write_blocking(flash_spi, &addr, 1);
    spi_cs_put(1);

}

uint8_t spi_read_extended_addr_reg(void)
{
    uint8_t addr = 0;
    uint8_t cmd = SPI_CMD_READ_EXT_ADDR;
    spi_cs_put(0);
    spi_write_blocking(flash_spi, &cmd, 1);
    spi_read_blocking(flash_spi, 0x00, &addr, 1);
    spi_cs_put(1);
    return addr;
}

void enter_4byte_mode(void)
{
    uint8_t cmd = SPI_CMD_ENTER_4BYTE_ADDR_MODE;
    // gpio_put(SPI_FLASH_CS_PIN, 0);
    spi_cs_put(0);
    spi_write_blocking(flash_spi, &cmd, 1);
    spi_cs_put(1);
    // gpio_put(SPI_FLASH_CS_PIN, 1);

}

void spi_cs_put(uint8_t val)
{
    delay_short();
    gpio_put(SPI_FLASH_CS_PIN, val);
    delay_short();
}

/*
    Enable bistream spi flash IO, disable firmware IO, and (re)initialize SPI peripheral
*/
void bitstream_init_spi(uint32_t baud)
{
    spi_deinit(flash_spi);
    spi_init(flash_spi, baud);

    release_spi_io();

    // enable BS_SPI pins
    gpio_init(BS_SPI_DI); // RX pin
    gpio_init(BS_SPI_DO); // TX pin
    gpio_init(BS_SPI_CLK); // CLK pin

    gpio_set_function(BS_SPI_DI, GPIO_FUNC_SPI); // RX pin
    gpio_set_function(BS_SPI_DO, GPIO_FUNC_SPI); // TX pin
    gpio_set_function(BS_SPI_CLK, GPIO_FUNC_SPI); // CLK pin

    // enable CS pin
    gpio_init(BS_SPI_CS);
    gpio_set_dir(BS_SPI_CS, GPIO_OUT);
    gpio_put(BS_SPI_CS, 1);
    SPI_FLASH_CS_PIN = BS_SPI_CS;

    // enter_4byte_mode();
}

/*
    Enable firmware spi flash IO, disable bitstream IO, and (re)initialize SPI peripheral
*/
void firmware_init_spi(uint32_t baud)
{
    spi_deinit(flash_spi);
    spi_init(flash_spi, baud);

    release_spi_io();

    fpga_set_sw_nrst(0); // hold FPGA software in reset
    fpga_set_io_tristate(1); // tristate FPGA pins

    // enable FW_SPI pins
    gpio_init(FW_SPI_DI); // RX pin
    gpio_init(FW_SPI_DO); // TX pin
    gpio_init(FW_SPI_CLK); // CLK pin

    gpio_set_function(FW_SPI_DI, GPIO_FUNC_SPI); // RX pin
    gpio_set_function(FW_SPI_DO, GPIO_FUNC_SPI); // TX pin
    gpio_set_function(FW_SPI_CLK, GPIO_FUNC_SPI); // CLK pin

    // set extra data pins high
    gpio_init(FW_SPI_W_NEN);
    gpio_set_dir(FW_SPI_W_NEN, GPIO_OUT);
    gpio_put(FW_SPI_W_NEN, 1);

    gpio_init(FW_SPI_NHOLD);
    gpio_set_dir(FW_SPI_NHOLD, GPIO_OUT);
    gpio_put(FW_SPI_NHOLD, 1);


    // enable CS pin
    gpio_init(FW_SPI_CS);
    gpio_set_dir(FW_SPI_CS, GPIO_OUT);
    gpio_put(FW_SPI_CS, 1);
    SPI_FLASH_CS_PIN = FW_SPI_CS;

    // hold FPGA in reset and tristate its IO pins

}

void release_spi_io(void)
{
    // spi_deinit(flash_spi);

    // disable BS_SPI pins
    gpio_set_function(BS_SPI_DI , GPIO_FUNC_NULL);
    gpio_set_function(BS_SPI_DO , GPIO_FUNC_NULL);
    gpio_set_function(BS_SPI_CLK, GPIO_FUNC_NULL);

    // // disable FW_SPI pins
    gpio_set_function(FW_SPI_DI , GPIO_FUNC_NULL);
    gpio_set_function(FW_SPI_DO , GPIO_FUNC_NULL);
    gpio_set_function(FW_SPI_CLK, GPIO_FUNC_NULL);

    gpio_set_function(FW_SPI_CS, GPIO_FUNC_NULL);
    gpio_set_function(FW_SPI_NHOLD, GPIO_FUNC_NULL);
    gpio_set_function(FW_SPI_W_NEN, GPIO_FUNC_NULL);
    gpio_set_function(BS_SPI_CS, GPIO_FUNC_NULL);

    fpga_set_sw_nrst(1); // release SW reset
    fpga_set_io_tristate(0); // untristate pins
}

/*
    Read chip/manufacturer ID
*/
uint16_t spi_flash_read_id(void)
{
    uint8_t cmd = 0x90;
    uint16_t read_data = 0;

    spi_cs_put(0);
    spi_write_blocking(flash_spi, &cmd, 1);

    spi_read_blocking(flash_spi, 0x00, (void *)&read_data, 2); // dummy read
    spi_read_blocking(flash_spi, 0x00, (void *)&read_data, 2);
    spi_cs_put(1);

    return read_data;
}

/*
    WARNING: You're supposed to be able to continuously read the status register
    like we're doing below by keeping CS low and doing SPI reads, but this doesn't
    appear to work (status will be 0x00 even if the device is busy, for example)
*/
#if 0
/*
    Poll SPI flash until the device is ready for an operation
*/
int spi_flash_poll_busy(void)
{
    uint8_t cmd = SPI_CMD_READ_STATUS1;
    uint8_t rtn = 0;

    spi_cs_put(0);

    spi_write_blocking(flash_spi, &cmd, 1);

    while (rtn & SPI_FLASH_STATUS_BUSY) {
        spi_read_blocking(flash_spi, 0x00, &rtn, 1);
    }
    spi_cs_put(1);

    return 0;
}

/*
    Poll SPI flash until the device has writes/erases enabled
*/
int spi_flash_poll_write_enable(void)
{
    uint8_t cmd = SPI_CMD_READ_STATUS1;
    uint8_t rtn = 0;

    spi_cs_put(0);

    spi_write_blocking(flash_spi, &cmd, 1);

    while (!(rtn & SPI_FLASH_WRITE_ENABLED)) {
        spi_read_blocking(flash_spi, 0x00, &rtn, 1);
    }
    spi_cs_put(1);

    return 0;
}
#endif

/*
    Check if SPI flash is busy (usually doing an erase or write)
*/
int spi_flash_is_busy(void) 
{
    return spi_flash_read_status() & SPI_FLASH_STATUS_BUSY;
}

int spi_flash_is_write_enabled(void)
{
    return spi_flash_read_status() & SPI_FLASH_WRITE_ENABLED;
}

/*
    Read status1 register of SPI flash
*/
enum spi_flash_status1 spi_flash_read_status(void)
{
    uint8_t cmd = SPI_CMD_READ_STATUS1;
    uint8_t rtn = 0;

    spi_cs_put(0);

    spi_write_blocking(flash_spi, &cmd, 1);
    spi_read_blocking(flash_spi, 0x00, &rtn, 1);

    spi_cs_put(1);

    return rtn;
}

/*
    Enable writes/erase of SPI flash

    NOTE: WE is disabled again after each write/erase
*/
int spi_flash_write_enable(void)
{
    uint8_t cmd = SPI_CMD_WRITE_ENABLE;
    spi_cs_put(0);
    spi_write_blocking(flash_spi, &cmd, 1);
    spi_cs_put(1);

    // check that write enable status is set
    while (!spi_flash_is_write_enabled());

    return 0;
}

/*
    Erases a sector (4k) of flash memory specified by addr. 
    
    Blocks until the erase is complete
*/
int spi_flash_sector_erase_blocking(uint32_t addr)
{
    uint8_t cmd = SPI_CMD_SECTOR_ERASE_4ADDR;
    uint8_t addr_u8[] = {BE_U32_TO_4U8(addr)}; // ensure proper endianness

    if (spi_flash_write_enable()) return -1;

    spi_cs_put(0);
    spi_write_blocking(flash_spi, &cmd, 1);

    spi_write_blocking(flash_spi, addr_u8, 4);

    spi_cs_put(1);

    // wait for erase to finish
    // spi_flash_poll_busy();
    while (spi_flash_is_busy());
    return 0;
}

/*
    Erases a block (64k) of flash memory specified by addr. 
    
    Does not block until the erase is finished. Subsequent operations should check that the erase
    is finished (via the busy status) before talking to the chip
*/
int spi_flash_64k_erase_nonblocking(uint32_t addr)
{
    uint8_t cmd = SPI_CMD_64K_BLOCK_ERASE;
    uint8_t addr_u8[] = {BE_U32_TO_4U8(addr)}; // ensure proper endianness

    if (spi_flash_write_enable()) return -1;

    spi_cs_put(0);

    spi_write_blocking(flash_spi, &cmd, 1);
    spi_write_blocking(flash_spi, addr_u8, 4);

    spi_cs_put(1);

    return 0;
}

/*
    Read len memory from SPI flash from addr into data.
*/
int spi_flash_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    uint8_t cmd = SPI_CMD_READ_DATA_4ADDR;
    uint8_t addr_u8[] = {BE_U32_TO_4U8(addr)}; // ensure proper endianness

    spi_cs_put(0);
    spi_write_blocking(flash_spi, &cmd, 1);

    spi_write_blocking(flash_spi, addr_u8, 4);

    // need a dummy byte for fast read
    // cmd = 0x00;
    // spi_write_blocking(flash_spi, &cmd, 1); 

    spi_read_blocking(flash_spi, 0x00, data, len);

    spi_cs_put(1);

    return 0;
}

/*
    Writes up to 1 page (256 bytes) of memory into the SPI flash

    Will write len + 1 bytes

    Note that writes cannot cross page boundries, meaning writes that start
    on byte 0-255 cannot continue on to byte 256+. If this function is specified to
    write beyond a page boundary, the attempt will be aborted and -1 will be returned.
*/
int spi_flash_page_program_blocking(uint32_t addr, uint8_t *data, uint16_t len)
{
    uint16_t write_len = len;
    // write_len++;

    // addr & ~0xFF should be start of page, so +0x100 should be next page
    if ((addr + (uint32_t)write_len) > ((addr & ~(uint32_t)0xFF) + (uint32_t)0x100)) return -1; // ensure write does not go past end of page

    uint8_t cmd = SPI_CMD_PAGE_4ADDR_PROGRAM;
    uint8_t addr_u8[] = {BE_U32_TO_4U8(addr)}; // ensure proper endianness

    if (spi_flash_write_enable()) return -1; // ensure write is enabled

    spi_cs_put(0);
    spi_write_blocking(flash_spi, &cmd, 1);

    spi_write_blocking(flash_spi, addr_u8, 4);

    spi_write_blocking(flash_spi, data, len);

    spi_cs_put(1);

    // wait for write to finish
    // spi_flash_poll_busy();
    while (spi_flash_is_busy());

    return 0;
}

/*
    Checks flash memory for a bitstream beginning at offset
*/
int check_flash_for_bitstream(uint32_t offset)
{
    return 0;
}

/*
Erases memory of whole chip

Takes a while (like 2 minutes)
*/
int spi_flash_chip_erase_blocking(void)
{
    if (spi_flash_write_enable()) return -1; // ensure write is enabled

    uint8_t cmd = SPI_CMD_CHIP_ERASE;
    spi_cs_put(0);
    spi_write_blocking(flash_spi, &cmd, 1);
    spi_cs_put(1);

    while (spi_flash_is_busy());
    return 0;
}

/*
    Writes an arbitrary amount of data to the flash chip
*/
int spi_flash_write_buffer(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t bytes_written = 0;
    int rtn = 0;
    while (bytes_written < len) {
        uint32_t next_page = (addr + 256) & ~0xFF;
        uint16_t to_write = min(next_page - addr, len);
        if (rtn = spi_flash_page_program_blocking(addr, buf + bytes_written, to_write), rtn) return rtn;
        addr += to_write;
        bytes_written += to_write;
    }
    return 0;
}