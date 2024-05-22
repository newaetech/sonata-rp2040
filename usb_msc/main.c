
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
#include "crc32.h"
#include "tests.h"
#include "uf2.h"

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
enum
{
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

uint32_t fpga_flash_calc_crc32(uint32_t addr);

uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;
// extern uint8_t FLASH_WRITE_BUF[CONST_64k];
extern uint8_t TEST_RD_BUF[4096]; //TODO: use the actual #define for this size

int FLASH_BITSTREAM_SELECT = 0;
uint32_t FLASH_BITSTREAM_OFFSET[] = {0x00, 10 * 1024 * 1024, 10 * 2 * 1024 * 1024};

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
    if (pin > 2)
        return 0;

    return FLASH_BITSTREAM_OFFSET[pin];
}

static uint8_t test_rdmem[256];
static uint8_t test_mem[256];

void xor_fill_buf(uint32_t *buf, int len, uint32_t seed);

/*
Erase/read/write for first 256 bytes of flash
*/
int test_fw_flash(void)
{
    firmware_init_spi(10E6);
    int passed = 1;
    for (uint32_t i = 0; i < sizeof(test_mem); i++)
        test_mem[i] = i;
    spi_flash_sector_erase_blocking(0x00);
    spi_flash_read(0x00, test_rdmem, sizeof(test_rdmem));
    for (uint16_t i = 0; i < ARR_LEN(test_rdmem); i++) {
        if ((test_rdmem[i] != 0xFF)) { // erased mem is 0xFF
            passed = 0;
        }
    }
    PRINT_CRIT("Erase flash %d", passed);

    passed = 1;
    xor_fill_buf((void *)test_mem, 256, 0x11223344);
    spi_flash_page_program_blocking(0, test_mem, sizeof(test_mem));
    spi_flash_read(0, test_rdmem, sizeof(test_rdmem));
    if (memcmp(test_mem, test_rdmem, sizeof(test_rdmem)))
        passed = 0;
    PRINT_CRIT("Program flash %d", passed);
    return passed;
}

uint16_t initial_fat[] = {0xFFF8, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

// TODO: replicate for firmware flash
/*
    Check first 256 bytes of each flash slot for Xilinx bitstream header

    Print result to LOG.txt
*/
void check_flash_for_bitstreams(void)
{
    bitstream_init_spi(20E6);
    uint32_t bs_len = 0;
    for (int i = 0; i < ARR_LEN(FLASH_BITSTREAM_OFFSET); i++) {
        spi_flash_read(FLASH_BITSTREAM_OFFSET[i], TEST_RD_BUF, 256);
        bs_len = get_bitstream_length(TEST_RD_BUF, 256);
        if (bs_len > 0) {
            PRINT_INFO("Bitstream found in slot %d", i);
        }
        else
        {
            PRINT_INFO("No bitstream in slot %d", i);
        }
    }
    // release_spi_io();
}

void check_flash_for_firmware(void)
{
    firmware_init_spi(20E6);
    fpga_erase();
    uint32_t bs_len = 0;
    struct UF2_Block block;
    for (int i = 0; i < ARR_LEN(FLASH_BITSTREAM_OFFSET); i++) {
        spi_flash_read(FLASH_BITSTREAM_OFFSET[i], &block, sizeof(block));
        if (is_uf2_block(&block)) {
            PRINT_INFO("Firmware found in slot %d", i);
        } else {
            PRINT_INFO("No firmware in slot %d", i);
        }
    }
    release_spi_io();
}

/*
    Check the slot currently selected by the bitstream select switch

    If there's a bitstream in that slot, program it into the FPGA
*/
void startup_program_bitstream(void)
{
    bitstream_init_spi(20E6);
    uint32_t bitstream_offset = flash_get_bitstream_offset();
    spi_flash_read(bitstream_offset, TEST_RD_BUF, 256); // whatever, just duplicate the reads...
    uint32_t bs_len = get_bitstream_length(TEST_RD_BUF, 256);

    if (bs_len > 0) {
        // TODO: calc/record CRC
        fpga_program_init(20E6);
        fpga_erase();
        PRINT_INFO("Bitstream in flash @ %lX, programming %lX bytes...", bitstream_offset, bs_len);
        uint32_t flash_addr = bitstream_offset;
        uint32_t crc = 0x00;
        while (bs_len) {
            uint32_t read_len = min(sizeof(TEST_RD_BUF), bs_len);
            spi_flash_read(flash_addr, TEST_RD_BUF, read_len);
            fpga_program_sendchunk(TEST_RD_BUF, read_len);
            crc = crc32c(crc, TEST_RD_BUF, read_len);
            bs_len -= read_len;
            flash_addr += read_len;
            // PRINT_DEBUG("Prog %lX bytes, %lX left", read_len, bs_len);
        }
        PRINT_INFO("Finished programming CRC=%lX", crc);
    } else {
        PRINT_INFO("No bitstream in flash @ %lX", bitstream_offset);
    }
    // release_spi_io();
}

int main()
{
    const uint LED_PIN = 24; // LED1
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    setup_bitstream_select_pin();
    fpga_setup_nrst_preq();

    fpga_program_init(5E6);
    fpga_erase();

    // Setup LEDs
    for (uint8_t i = 0; i < ARR_LEN(USER_LEDS); i++) {
        gpio_init(USER_LEDS[i]);
        gpio_set_dir(USER_LEDS[i], GPIO_OUT);
    }

    board_init();
    tud_init(BOARD_TUD_RHPORT);

    // bitstream_init_spi(20E6);
#ifdef TESTING_BUILD
    test_crc(0);
// this stops USB from working for some reason...
// test_basic_flash(0);
#endif

    // Print firmware version
    PRINT_CRIT("FW_VER %d.%d.%d", FW_MAJOR_VER, FW_MINOR_VER, FW_DEBUG_VER);
    // test_fw_flash();

    // check first 256 bytes to see if there's a bitstream in flash
    check_flash_for_bitstreams();
    check_flash_for_firmware();

    PRINT_INFO("Using slot %d", read_bitstream_select_pins());

    startup_program_bitstream();

    gpio_init(FPGA_CONFIG_LED);
    gpio_set_dir(FPGA_CONFIG_LED, GPIO_OUT);

    // Write default config values to CONFIG.txt
    set_default_config(&CONFIG);
    write_config_to_file(get_filesystem(), &CONFIG);

    // Try parsing default config to make sure it works
    if (parse_config(get_filesystem(), &CONFIG)) {
        // if config parse fails, set everything back to default
        PRINT_WARN("Default config parse failed");
        set_default_config(&CONFIG);
        int i = fat_strlen(get_filesystem()->root_dir[0].filename);
    }

    while (true) {
        tud_task(); // tinyusb device task
        led_blinking_task();

        // Light up LED associated with selected flash slot
        for (uint8_t i = 0; i < ARR_LEN(USER_LEDS); i++) {
            if (!gpio_get(BITSTREAM_SELECT_PINS[i]))
                gpio_put(USER_LEDS[i], 0);
            else
                gpio_put(USER_LEDS[i], 1);
        }

        // Light up LED FPGA Config LED if FPGA done pin is high
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
    (void)remote_wakeup_en;
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
    if (board_millis() - start_ms < blink_interval_ms)
        return; // not enough time
    start_ms += blink_interval_ms;

    board_led_write(led_state);
    led_state = 1 - led_state; // toggle
}
