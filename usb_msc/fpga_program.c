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
static volatile uint8_t FPGA_WRITE_BUF[512];
dma_channel_config fpga_dma_config;
int fpga_dma = -1;


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


// should be offset 120 i think
/*

    0xFFFFFFFF's are NOP
    0x000000BB and 0x11220044 are sync words

*/
int find_bitstream_len_offset(uint8_t *bitstream, uint16_len len)
{
    uint8_t magic_seq[] = {
        0x00, 0x00, 0x00, 0xBB, 0x11, 0x22, 0x00, 0x44
    };

    uint16_t header_end_loc = 0;
    for (; header_end_loc < (len - sizeof(magic_seq)); header_end_loc++) {
        if (bitstream[header_end_loc] != 0xFF) continue; //find first 0xFF
        if (!memcmp(bitstream + header_end_loc, magic_seq, sizeof(magic_seq))) break;
    }
    if ((header_end_loc + sizeof(magic_seq)) >= len) {
        return -1;
    }
    // work backwards to find len
    for (; header_end_loc > 0; header_end_loc -= 4) {
        if (bitstream[header_end_loc] != 0xFF) break;
    }
    if (header_end_loc <= 4) return -1;

    return header_end_loc - 4;
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