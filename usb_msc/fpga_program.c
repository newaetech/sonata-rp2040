#include <stdint.h>
#include "fpga_program.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "util.h"
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

int fpga_program_sendchunk(uint8_t *data, uint32_t len)
{
    return spi_write_blocking(spi1, data, len);
}
static volatile uint8_t FPGA_WRITE_BUF[512];
dma_channel_config fpga_dma_config;
int fpga_dma = -1;


void fpga_program_init(uint32_t baud)
{
    // set prog high
    FPGA_NPROG_SETUP();
    FPGA_NPROG_HIGH();

    FPGA_DONE_PIN_SETUP();

    spi_deinit(spi1);
    spi_init(spi1, baud);

    gpio_set_function(11, GPIO_FUNC_SPI); // TX pin
    gpio_set_function(10, GPIO_FUNC_SPI); // CLK pin
    // gpio_set_slew_rate(11, GPIO_SLEW_RATE_FAST);
    // gpio_set_slew_rate(10, GPIO_SLEW_RATE_FAST);

    // gpio_set_drive_strength(11, GPIO_DRIVE_STRENGTH_12MA);
    // gpio_set_drive_strength(10, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_function(PICO_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);
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


// should be offset 120 i think
/*

    0xFFFFFFFF's are NOP
    0x000000BB and 0x11220044 are sync words

    Bitstream length should be preceeded by 0x65

*/
int find_bitstream_len_offset(uint8_t *bitstream, uint16_t len)
{
    uint8_t magic_seq[] = {
        0x00, 0x00, 0x00, 0xBB, 0x11, 0x22, 0x00, 0x44
    };

    uint16_t header_end_loc = 0;
    uint8_t found_ff = 0;
    for (; header_end_loc < (len - sizeof(magic_seq)); header_end_loc++) {
        if (!found_ff) {
            if (bitstream[header_end_loc] != 0xFF)  {
                continue; //find first 0xFF
            } else {
                found_ff = 1;
            }
        }
        if (!memcmp(bitstream + header_end_loc, magic_seq, sizeof(magic_seq))) break;
    }
    if ((header_end_loc + sizeof(magic_seq)) >= len) {
        return -1;
    }
    header_end_loc--;
    while (bitstream[header_end_loc] == 0xFF) header_end_loc--;
    if (header_end_loc <= 3) return -1;
    header_end_loc -= 4; // if bitstream doesn't end in 0xFF, this should get us to 0x65 before
    header_end_loc++; // then advance one ahead to get length
    return header_end_loc;

    // work backwards to find len
    // for (; header_end_loc > 0; header_end_loc -= 4) {
    //     if (bitstream[header_end_loc] != 0xFF) break;
    // }
}

void fpga_init_dma(void)
{
    fpga_dma = dma_claim_unused_channel(true);
    fpga_dma_config = dma_channel_get_default_config(fpga_dma);

    channel_config_set_transfer_data_size(&fpga_dma_config, DMA_SIZE_8);
    channel_config_set_dreq(&fpga_dma_config, spi_get_dreq(spi1, true));
    channel_config_set_write_increment(&fpga_dma_config, false);
    channel_config_set_read_increment(&fpga_dma_config, true);
}

int32_t fpga_send_dma(uint8_t *buf, uint16_t len)
{
    if (!is_fpga_dma_ready()) return -1;

    len = min(len, sizeof(FPGA_WRITE_BUF));
    memcpy(FPGA_WRITE_BUF, buf, len);

    dma_channel_configure(fpga_dma, &fpga_dma_config, &spi_get_hw(spi1)->dr,
        FPGA_WRITE_BUF, len, true);
    return len;
}

int is_fpga_dma_ready(void)
{
    return dma_channel_is_busy(fpga_dma);
}