#pragma once
#include "pico/stdlib.h"
#include "bsp/board.h"
#define FPGA_NPROG_PIN 9
#define FPGA_INITB_PIN 8
#define FPGA_DONE_PIN 7

#define FPGA_NPROG_LOW() gpio_put(FPGA_NPROG_PIN, 0)
#define FPGA_NPROG_HIGH() gpio_put(FPGA_NPROG_PIN, 1)
#define FPGA_NPROG_SETUP() do {gpio_init(FPGA_NPROG_PIN); gpio_set_dir(FPGA_NPROG_PIN, GPIO_OUT);} while(0)


#define FPGA_INITB_LOW()  gpio_put(FPGA_INITB_PIN, 0)
#define FPGA_INITB_HIGH() gpio_put(FPGA_INITB_PIN, 1)
#define FPGA_INITB_SETUP() do {gpio_init(FPGA_INITB_PIN); gpio_set_dir(FPGA_INITB_PIN, GPIO_OUT);} while(0)
#define FPGA_INITB_STATUS()

#define FPGA_DONE_PIN_SETUP() do {gpio_init(FPGA_DONE_PIN); gpio_set_dir(FPGA_DONE_PIN, GPIO_IN);} while(0)
#define FPGA_ISDONE() gpio_get(FPGA_DONE_PIN)

#define FPGA_BUF_SIZE 512

void fpga_program_sendbyte(uint8_t databyte);

void fpga_program_init(void);

void fpga_program_setup1(void);

void fpga_program_setup2(void);

void fpga_program_finish(void);

int find_bitstream_len_offset(uint8_t *bitstream, int len);

int fpga_init_dma(void);

int fpga_send_dma(uint8_t *buf, uint16_t len);

int is_fpga_dma_ready(void);