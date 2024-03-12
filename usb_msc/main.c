
/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "bsp/board.h"
#include "tusb.h"
#include "hardware/spi.h"
#include "fpga_program.h"
#include "fat_util.h"
#include "config.h"
#include "flash_util.h"
#include "util.h"
#include "error.h"

#define spi_default PICO_DEFAULT_SPI_INSTANCE
#define FPGA_CONFIG_LED 18

#define STATEA_LED 26
#define STATEB_LED 25

#define GPIO_SW0 29
#define GPIO_SW1 28
#define GPIO_SW2 27

// TODO
#define ERR_LED 26

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum  {
  BLINK_NOT_MOUNTED = 500,
  BLINK_MOUNTED = 500,
  BLINK_SUSPENDED = 500,
  BLINK_TOUCHED_SRAM = 100,
  BLINK_TOUCHED_FPGA = 2500,
};
void led_blinking_task(void);
void dir_fill_req_entries(uint16_t cluster_num, uint16_t parent_cluster);

void set_err_led(int on)
{
  // TODO
}
extern struct config_options CONFIG;
uint8_t test_rdmem[256];
uint8_t test_mem[256];

uint32_t xorshift_state = 0xF2BB9566;

uint32_t xorshift(void)
{
  xorshift_state ^= xorshift_state << 13;
  xorshift_state ^= xorshift_state >> 17;
  xorshift_state ^= xorshift_state << 5;
  return xorshift_state;
}

void fill_buf(uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < (len - 3); i += 4) {
        uint32_t current = xorshift();
        memcpy(buf + i, &current, 4);
    }
}

int test_spi_flash_prog(void)
{
    for (uint32_t i = 0; i < ARR_LEN(test_mem); i++) test_mem[i] = i;
    spi_flash_sector_erase_blocking(0x00);
    volatile enum spi_flash_status1 status = spi_flash_read_status();
    spi_flash_read(0x00, test_rdmem, ARR_LEN(test_rdmem));
    for (uint16_t i = 0; i < ARR_LEN(test_rdmem); i++) {
      if ((test_rdmem[i] != 0xFF)) { // erased mem is 0xFF
        return -1;
      }
    }

    for (uint16_t i = 0; i < CONST_4k; i += 256) {
        fill_buf(test_mem, 256);
        spi_flash_page_program_blocking(i, test_mem, ARR_LEN(test_mem));
        spi_flash_read(i, test_rdmem, ARR_LEN(test_rdmem));
        if (memcmp(test_mem, test_rdmem, sizeof(test_rdmem))) return -i;
    }
    return 0;

}

uint32_t flash_calc_crc32(uint32_t addr);

uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

int main() 
{
    const uint LED_PIN = 24; // LED1
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    gpio_init(STATEA_LED);
    gpio_set_dir(STATEA_LED, GPIO_OUT);
    gpio_init(STATEB_LED);
    gpio_set_dir(STATEB_LED, GPIO_OUT);
    gpio_put(25, 0);
    gpio_put(26, 0);

    board_init();
    tud_init(BOARD_TUD_RHPORT);
    spi_init(spi1, 10E6); //Min speed seems to be ~100kHz, below that USB gets angry

    bitstream_init_spi(20E6);
    // spi_flash_read(0x00, test_rdmem, ARR_LEN(test_rdmem));
    // volatile int offset = find_bitstream_len_offset(test_rdmem, ARR_LEN(test_rdmem));
    uint32_t bscrc = flash_calc_crc32(0x00);

    // test_spi_flash_prog();
    if (bscrc > 0) {
      print_err_file(get_filesystem(), "bitstream in flash, CRC = %lX\r\n", bscrc);
    } else {
      print_err_file(get_filesystem(), "no bitstream in flash\r\n");
    }
    // print_err_file(get_filesystem(), "test print %d", (int)128);
    // bitstream_init_spi(1E6);
    // test_spi_flash_prog();

    // bitstream_init_spi(10E6);
    // test_spi_flash_prog();

    // volatile uint16_t dev_id = spi_flash_read_id();

    gpio_set_function(11, GPIO_FUNC_SPI); // TX pin
    gpio_set_function(10, GPIO_FUNC_SPI); // CLK pin
    gpio_set_function(PICO_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);

    gpio_init(FPGA_CONFIG_LED); // LED0 // note should be LED3 at somepoint
    gpio_set_dir(FPGA_CONFIG_LED, GPIO_OUT);

    FPGA_DONE_PIN_SETUP();
    FPGA_NPROG_SETUP();

    // test_spi_flash_prog();



    // dir_fill_req_entries(3, 0);
    // dir_fill_req_entries(4, 0);
    set_default_config(&CONFIG);
    write_config_to_file(get_filesystem(), &CONFIG);
    if (parse_config(get_filesystem(), &CONFIG)) {
        // if config parse fails, set everything back to default
        set_default_config(&CONFIG);
    }


    while (true) {
        tud_task(); // tinyusb device task
        led_blinking_task();
        if (!FPGA_ISDONE()) {
          gpio_put(FPGA_CONFIG_LED, 0);
        } else {
          gpio_put(FPGA_CONFIG_LED, 1);
        }
    }
}

// Invoked when device is mounted
void tud_mount_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // Blink every interval ms
  if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}
