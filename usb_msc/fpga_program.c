#include <stdint.h>
#include "fpga_program.h"
#include "hardware/spi.h"
// GPIO7 = FPGA_DONE
// GPIO8 = FPGA_INITB
// GPIO9 = FPGA PGMB = NPROG_LOW
// GPIO10 = FPGA CCLK = SPI1 SCK
// GPIO11 = FPGA Data
// data pin can be 11, 15, 27

void fpga_program_sendbyte(uint8_t databyte)
{
    // set nprog high
    spi_write_blocking(spi1, &databyte, 1);
}

void fpga_program_init(void)
{
    // set prog high
    FPGA_NPROG_SETUP();
    FPGA_NPROG_HIGH();
}

void fpga_program_setup1(void)
{
    // configure SPI?
    FPGA_NPROG_LOW();
}

void fpga_program_setup2(void)
{
    FPGA_NPROG_HIGH();
}

void fpga_program_finish(void)
{
    // no op
}