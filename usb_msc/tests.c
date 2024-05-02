#include "tests.h"
#include "stdint.h"
#include "error.h"
#include "util.h"
#include "crc32.h"
#include "config.h"
#include "fpga_program.h"
#include "flash_util.h"
extern struct config_options CONFIG;
static uint8_t test_rdmem[256];
static uint8_t test_mem[256];
// int test_program_bitstream(int interation)
// {
//     // PRINT_TEST(FPGA_ISDONE(), "FPGA Done Pin");
//     return 0;
// }
void xor_fill_buf(uint32_t *buf, int len, uint32_t seed);

int test_done_program(int iteration)
{
    PRINT_TEST(FPGA_ISDONE(), "FPGA Done Pin");
    return 0;
}

int test_basic_flash(int iteration)
{
    int passed = 1;
    for (uint32_t i = 0; i < ARR_LEN(test_mem); i++) test_mem[i] = i;
    spi_flash_sector_erase_blocking(0x00);
    spi_flash_read(0x00, test_rdmem, sizeof(test_rdmem));
    for (uint16_t i = 0; i < ARR_LEN(test_rdmem); i++) {
        if ((test_rdmem[i] != 0xFF)) { // erased mem is 0xFF
            passed = 0;
        }
    }
    PRINT_TEST(passed, "Erase flash");

    passed = 1;
    xor_fill_buf((void *)test_mem, 256, 0x11223344);
    spi_flash_page_program_blocking(0, test_mem, sizeof(test_mem));
    spi_flash_read(0, test_rdmem, sizeof(test_rdmem));
    if (memcmp(test_mem, test_rdmem, sizeof(test_rdmem))) passed = 0;
    PRINT_TEST(passed, "Program flash");
    return 0;
}

// int test_erase_flash_bitstream(int section)
// {
//     int i = 0;
// }

int test_config(int iteration)
{
    struct config_options comp = {
        .fpga_prog_speed = 7.77E6,
        .flash_prog_speed = 5.12E6,
        .prog_flash = false,
        .dirty = false
    };
    PRINT_TEST(!memcmp(&comp, &CONFIG, sizeof(comp)), "Match config");
    return 0;
}

uint32_t crc_buf[32] = {};

uint32_t crc_results[3] = {0x632C14D2, 0x30B74855, 0x0419985C};
uint32_t crc_seeds[3] = {0xDEADBEEF, 0x11223344, 0xF0F0A7A7};
uint32_t xorshift_state = 0xF2BB9566;

/*
Basic RNG func
*/
uint32_t xorshift(void)
{
    xorshift_state ^= xorshift_state << 13;
    xorshift_state ^= xorshift_state >> 17;
    xorshift_state ^= xorshift_state << 5;
    return xorshift_state;
}

void xor_fill_buf(uint32_t *buf, int len, uint32_t seed)
{
    if (seed) xorshift_state = seed;
    for (int i = 0; i < len; i++) {
        buf[i] = xorshift();

    }
}

int test_crc(int iteration)
{
    int iteration_failed = -1;
    uint32_t crc = 0;
    for (int i = 0; i < ARR_LEN(crc_results); i++) {
        xor_fill_buf(crc_buf, ARR_LEN(crc_buf), crc_seeds[i]);
        crc = crc32c(0x00, (void *)crc_buf, sizeof(crc_buf));
        if (crc != crc_results[i]) {
            iteration_failed = i;
            break;
        }
    }

    if (iteration_failed >= 0) {
        PRINT_TEST(0, "CRC test failed on iteration %d, got %lX expected %lX", iteration_failed, crc, crc_results[iteration_failed]);
    } else {
        PRINT_TEST(1, "CRC test");
    }
    return 0;
}