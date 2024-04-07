#include "tests.h"
#include "stdint.h"
#include "error.h"
#include "util.h"
#include "crc32.h"
#include "config.h"
#include "fpga_program.h"

int test_program_bitstream(int interation)
{
    PRINT_TEST(FPGA_ISDONE(), "FPGA Done Pin");
    return 0;
}

int test_program_flash(int iteration)
{
    return 0;
}

int test_config(int iteration)
{
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
        crc = crc32c(0x00, crc_buf, sizeof(crc_buf));
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