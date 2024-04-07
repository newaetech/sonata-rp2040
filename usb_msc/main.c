
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

uint8_t USER_LEDS[] = {26, 25, 24};
uint8_t BITSTREAM_SELECT_PINS[] = {29, 28, 27};

#ifdef DEBUG_LEVEL
  #define XSTR(X) STR(X)
  #define STR(X) #X
  #pragma message "TEST_DEBUG_LEVEL = " XSTR(DEBUG_LEVEL)
#endif

// TODO
// #define ERR_LED 26

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

// uint32_t xorshift_state = 0xF2BB9566;

// /*
// Basic RNG func
// */
// uint32_t xorshift(void)
// {
//   xorshift_state ^= xorshift_state << 13;
//   xorshift_state ^= xorshift_state >> 17;
//   xorshift_state ^= xorshift_state << 5;
//   return xorshift_state;
// }

/*
Fill buff randomly
*/
void fill_buf(uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < (len - 3); i += 4) {
        uint32_t current = 0;// xorshift();
        memcpy(buf + i, &current, 4);
    }
}

/*
Test function to erase, program, and read back "random" mem from spi flash
*/
int test_spi_flash_prog(void)
{
    for (uint32_t i = 0; i < ARR_LEN(test_mem); i++) test_mem[i] = i;
    spi_flash_sector_erase_blocking(0x00);
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
extern uint8_t FLASH_WRITE_BUF[CONST_64k];

int FLASH_BITSTREAM_SELECT = 0;
uint32_t FLASH_BITSTREAM_OFFSET[] = {0x00, 10*1024*1024, 10*2*1024*1024};

/*
Figure out which bitstream is selected from pins
*/
int read_bitstream_select_pins(void)
{
    for (uint8_t i = 0; i < ARR_LEN(BITSTREAM_SELECT_PINS); i++) {
      if (!gpio_get(BITSTREAM_SELECT_PINS[i])) {
        return i;
      }
    }

    return 0;
}

void setup_bitstream_select_pin(void)
{
    for (uint8_t i = 0; i < ARR_LEN(BITSTREAM_SELECT_PINS); i++) {
      gpio_init(BITSTREAM_SELECT_PINS[i]);
      gpio_set_dir(BITSTREAM_SELECT_PINS[i], GPIO_IN);
      gpio_pull_up(BITSTREAM_SELECT_PINS[i]);
    }
}

/*
Get the bitstream offset based on the switch selected on the board
*/
uint32_t flash_get_bitstream_offset(void)
{
    int pin = read_bitstream_select_pins();
    if (pin > 2) return 0;

    return FLASH_BITSTREAM_OFFSET[pin];
}

int main() 
{
    const uint LED_PIN = 24; // LED1
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    setup_bitstream_select_pin();

    // Setup LEDs
    for (uint8_t i =0; i < ARR_LEN(USER_LEDS); i++) {
      gpio_init(USER_LEDS[i]);
      gpio_set_dir(USER_LEDS[i], GPIO_OUT);

    }

    board_init();
    tud_init(BOARD_TUD_RHPORT);

    bitstream_init_spi(20E6);

    // check first 256 bytes to see if there's a bitstream in flash
    uint32_t bitstream_offset = flash_get_bitstream_offset();
    PRINT_INFO("Checking bitstream offset %lX", bitstream_offset);
    spi_flash_read(bitstream_offset, FLASH_WRITE_BUF, 256); // whatever, just duplicate the reads...
    uint32_t bs_len = get_bitstream_length(FLASH_WRITE_BUF, 256);

    if (bs_len > 0) {
      // TODO: calc/record CRC
      fpga_program_init(20E6);
      fpga_erase();
      PRINT_INFO("Bitstream in flash @ %lX, programming %lX bytes...", bitstream_offset, bs_len);
      uint32_t flash_addr = bitstream_offset;
      while (bs_len) {
        uint32_t read_len = min(sizeof(FLASH_WRITE_BUF), bs_len);
        spi_flash_read(flash_addr, FLASH_WRITE_BUF, read_len);
        fpga_program_sendchunk(FLASH_WRITE_BUF, read_len);
        bs_len -= read_len;
        flash_addr += read_len;
        PRINT_DEBUG("Prog %lX bytes, %lX left", read_len, bs_len);
      }
    } else {
      PRINT_INFO("No bitstream in flash @ %lX", bitstream_offset);
    }

    gpio_init(FPGA_CONFIG_LED);
    gpio_set_dir(FPGA_CONFIG_LED, GPIO_OUT);

    set_default_config(&CONFIG);
    write_config_to_file(get_filesystem(), &CONFIG);
    if (parse_config(get_filesystem(), &CONFIG)) {
        // if config parse fails, set everything back to default
        PRINT_WARN("Default config parse failed");
        set_default_config(&CONFIG);
    }

    #ifdef TESTING_BUILD
    test_crc(0);
    #endif


    while (true) {
        tud_task(); // tinyusb device task
        led_blinking_task();
        for (uint8_t i = 0; i < ARR_LEN(USER_LEDS); i++) {
          if (!gpio_get(BITSTREAM_SELECT_PINS[i])) gpio_put(USER_LEDS[i], 0);
          else gpio_put(USER_LEDS[i], 1) ;
        }
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
