
#include "bsp/board.h"
#include "tusb.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "fpga_program.h"
#include "flash_util.h"
#include "config.h"
#include "fat_util.h"
#include "util.h"
#include "main.h"
#include "config.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "error.h"
#include "crc32.h"


uint8_t FLASH_WRITE_BUF[CONST_64k];
uint32_t SPI_FLASH_BUF_INDEX = 0;

struct config_options CONFIG = {.dirty = 1, 
                                .fpga_prog_speed = CONF_DEFAULT_FPGA_PROG_SPEED,
                                .flash_prog_speed = CONF_DEFAULT_FLASH_PROG_SPEED,
                                .prog_flash = CONF_DEFAULT_FLASH_PROG_SPEED};

extern uint32_t blink_interval_ms;

// Invoked to determine max LUN
uint8_t tud_msc_get_maxlun_cb(void)
{
    return 1; // dual LUN
}

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun; // use same ID for both LUNs

    const char vid[] = "TinyUSB";
    const char pid[] = "Mass Storage";
    const char rev[] = "1.0";

    memcpy(vendor_id, vid, strlen(vid));
    memcpy(product_id, pid, strlen(pid));
    memcpy(product_rev, rev, strlen(rev));
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    if (lun == 1 && board_button_read())
        return false;

    return true; // RAM disk is always ready
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;

    // *block_count = DISK_BLOCK_NUM;
    // *block_size  = DISK_BLOCK_SIZE;
    *block_count = DISK_REPORT_SECTOR_NUM; // bytes per sector?// DISK_BLOCK_NUM;
    *block_size = DISK_SECTOR_SIZE;        // number of sectors? //DISK_BLOCK_SIZE;
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun;
    (void)power_condition;

    if (load_eject)
    {
        if (start)
        {
            // load disk storage
        }
        else
        {
            // unload disk storage
        }
    }

    return true;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    // out of ramdisk
    if (lba >= DISK_REAL_SECTOR_NUM)
    {
        // fake it?
        memset(buffer, 0x00, bufsize);
        return (int32_t)bufsize;
        // return -1;
        // return -1;
    }

    // uint8_t const* addr = msc_disk0[lba] + offset;
    struct fat_filesystem *fs = get_filesystem();
    uint8_t const *addr = fs->raw_sectors[lba] + offset;
    memcpy(buffer, addr, bufsize);
    // if (spi_flash_read(lba * DISK_SECTOR_SIZE, buffer, bufsize)) return -1;

    return (int32_t)bufsize;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;

#ifdef CFG_EXAMPLE_MSC_READONLY
    return false;
#else
    return true;
#endif
}

uint32_t LAST_BITSTREAM_SIZE = 0;
uint32_t FPGA_PROG_BYTES_LEFT = 0;
uint32_t FPGA_TOTAL_PROG_BYTES = 0;

uint32_t FLASH_CURRENT_OFFSET = 0;
uint32_t FLASH_BUF_CURRENT_OFFSET = 0;
uint32_t FLASH_TOTAL_PROG_BYTES = 0;
uint32_t FLASH_PROG_BYTES_LEFT = 0;

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes

uint8_t TEST_RD_BUF[256];

uint32_t BITSTREAM_CRC32 = 0;

uint32_t BLOCK_COUNTER = 0;

// uint8_t LAST_SECTOR[DISK_CLUSTER_SIZE];

uint32_t flash_calc_crc32(uint32_t addr)
{
    spi_flash_read(addr, TEST_RD_BUF, 256);
    uint32_t bs_len = get_bitstream_length(TEST_RD_BUF, 256);
    if (!bs_len) return 0;

    print_err_file(get_filesystem(), "Reading %lX bytes from flash\r\n", bs_len);

    uint32_t crc = crc32c(0x00, TEST_RD_BUF, 256);
    uint32_t i = 256;
    while (i < bs_len) {
        uint32_t read_len = ((i + 256) > bs_len) ? (bs_len - i) : 256;
        spi_flash_read(addr + i, TEST_RD_BUF, read_len);
        crc = crc32c(crc, TEST_RD_BUF, read_len);
        i += read_len;
    }

    print_err_file(get_filesystem(), "Read %lX bytes from flash\r\n", i);
    // for (uint32_t i = 0; i < bs_len; i += 256) {
    //     uint32_t read_len = ((i + 256) > bs_len) ? (bs_len - i) : 256;

    //     spi_flash_read(addr + i, TEST_RD_BUF, read_len);
    //     crc = crc32c(crc, TEST_RD_BUF, read_len);
    // }
    return crc;
}

// idea - uint8_t + bufsize & 0xFF will always be aligned with flash page
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    // out of ramdisk
    if (!bufsize) return 0; //wtf?

    // cluster 2 is readme, cluster 3 is fpga folder, cluster 4 is flash folder
    uint8_t is_reserved_cluster = 0; // should change to "is known file/folder"

    int32_t reserved_clusters[] = {0, 1, get_file_cluster(get_filesystem(), 0, "README"), 
        get_file_cluster(get_filesystem(), 0, "OPTIONS"), get_file_cluster(get_filesystem(), 0, "ERROR")};

    int32_t opt_cluster = get_file_cluster(get_filesystem(), 0, "OPTIONS");

    if (opt_cluster > 0) {
        if (sector_to_cluster(lba) == opt_cluster) {
            CONFIG.dirty = 1; // if we touched OPTIONS.txt, mark it as dirty
            is_reserved_cluster = 1;
        }
    }

    for (uint16_t i = 0; i < ARR_LEN(reserved_clusters); i++) {
        if (reserved_clusters[i] < 0) continue;
        if (sector_to_cluster(lba) == reserved_clusters[i]) {
            is_reserved_cluster = 1;
        }
    }

    if (!is_reserved_cluster) {
        // if config is marked as dirty, reread it
        if (CONFIG.dirty) {
            if (parse_config(get_filesystem(), &CONFIG)) {
                // if config parse fails, set everything back to default
                set_default_config(&CONFIG);
            }
        }

        if (!FPGA_PROG_BYTES_LEFT) {
            FPGA_PROG_BYTES_LEFT = get_bitstream_length(buffer, bufsize);
            if (FPGA_PROG_BYTES_LEFT) {
                fpga_program_init(CONFIG.fpga_prog_speed);
                fpga_program_setup1(); // nprog low to erase
                // board_millis() seems to not work properly (not done by interrupt?)
                for (volatile uint32_t i = 0; i < 5000; i++);
                fpga_program_setup2(); // nprog back high
                for (volatile uint32_t i = 0; i < 5000; i++); // need to wait a bit after this before we start programming, 5ms from datasheet info
                print_err_file(get_filesystem(), "Programming %lX bytes\r\n", FPGA_PROG_BYTES_LEFT);

                bufsize = min(bufsize, FPGA_PROG_BYTES_LEFT);
                FPGA_TOTAL_PROG_BYTES = FPGA_PROG_BYTES_LEFT;
                FLASH_PROG_BYTES_LEFT = FPGA_PROG_BYTES_LEFT;
                fpga_program_sendchunk(buffer, bufsize);
                FPGA_PROG_BYTES_LEFT -= bufsize;
                FLASH_CURRENT_OFFSET = 0; // offset in actual flash
                FLASH_BUF_CURRENT_OFFSET = 0; // offset in our buffer


                if (CONFIG.prog_flash) {
                    bitstream_init_spi(CONFIG.flash_prog_speed);

                    // erase 64k of flash
                    while (spi_flash_is_busy());
                    spi_flash_64k_erase_nonblocking(FLASH_CURRENT_OFFSET); // todo add different index options to flash
                    BLOCK_COUNTER = 0;

                    // copy into buffer, but not past
                    bufsize = min(bufsize, (CONST_64k - SPI_FLASH_BUF_INDEX));
                    memcpy(FLASH_WRITE_BUF + FLASH_BUF_CURRENT_OFFSET, buffer, bufsize);

                    // update offsets with new data
                    FLASH_BUF_CURRENT_OFFSET += bufsize;

                    BITSTREAM_CRC32 = 0x00;
                    FLASH_TOTAL_PROG_BYTES = 0;
                }
            }

        } else {
            // this should be okay for now, may rework to follow file table to handle
            // writing to multiple files at a time
            bufsize = min(bufsize, FPGA_PROG_BYTES_LEFT);
            if (CONFIG.prog_flash && FLASH_PROG_BYTES_LEFT) {
                bitstream_init_spi(CONFIG.flash_prog_speed);

                // if at the beginning of our buffer, erase 64k of flash
                // todo just keep track of flash length separate from fpga length
                if (BLOCK_COUNTER >= 0x10000) {
                    while (spi_flash_is_busy());
                    if (spi_flash_64k_erase_nonblocking(FLASH_CURRENT_OFFSET)) { // todo add different index options to flash
                        print_err_file(get_filesystem(), "Erase error @ %lX\r\n", FLASH_CURRENT_OFFSET);
                    }
                    BLOCK_COUNTER = 0;
                }

                // write into buffer, but not past
                bufsize = min(bufsize, (CONST_64k - SPI_FLASH_BUF_INDEX));
                bufsize = min(bufsize, (CONST_64k - FLASH_PROG_BYTES_LEFT));
                // bufsize = min(bufsize, FPGA_PROG_BYTES_LEFT); // have to do this again...
                // if (bufsize + BLOCK_COUNTER > CONST_64k) bufsize = CONST_64k - BLOCK_COUNTER;
                memcpy(FLASH_WRITE_BUF + FLASH_BUF_CURRENT_OFFSET, buffer, bufsize);

                // update our buf offset
                FLASH_BUF_CURRENT_OFFSET += bufsize;

                // if we've maxed out the buffer
                if ((FLASH_BUF_CURRENT_OFFSET >= CONST_64k) // if we're at the end of our buffer
                 || (FLASH_BUF_CURRENT_OFFSET >= FLASH_PROG_BYTES_LEFT)) {  // or the end of the file

                    while (spi_flash_is_busy());
                    if (FLASH_BUF_CURRENT_OFFSET >= FLASH_PROG_BYTES_LEFT) {
                        FLASH_BUF_CURRENT_OFFSET = FLASH_PROG_BYTES_LEFT;
                        print_err_file(get_filesystem(), "Prog %lX bytes\r\n", FLASH_BUF_CURRENT_OFFSET);
                    }
                    uint32_t i = 0;
                    while (i < FLASH_BUF_CURRENT_OFFSET) {
                        uint16_t write_len = min(256, FLASH_BUF_CURRENT_OFFSET - i); // can write up to 256 bytes at a time

                        // program in the current offset in flash
                        if (spi_flash_page_program_blocking(FLASH_CURRENT_OFFSET, FLASH_WRITE_BUF + i, write_len)) {// do the write
                            print_err_file(get_filesystem(), "Prog error @ %lX\r\n", FLASH_CURRENT_OFFSET);
                            
                        }
                        // BITSTREAM_CRC32 = crc32c(BITSTREAM_CRC32, FLASH_WRITE_BUF + i, write_len);

                        // and verify what we wrote
                        spi_flash_read(FLASH_CURRENT_OFFSET, TEST_RD_BUF, write_len);
                        if (memcmp(FLASH_WRITE_BUF + i, TEST_RD_BUF, write_len)) {
                            print_err_file(get_filesystem(), "Verify error @ %lX\r\n", FLASH_CURRENT_OFFSET);
                        }
                        i += write_len;
                        BLOCK_COUNTER += write_len;
                        FLASH_CURRENT_OFFSET += write_len;
                        FLASH_TOTAL_PROG_BYTES += write_len;
                        FLASH_PROG_BYTES_LEFT -= write_len;
                    }

                    // update flash offset and set our buf offset to 0
                    // FLASH_CURRENT_OFFSET += FLASH_BUF_CURRENT_OFFSET;
                    // BLOCK_COUNTER += FLASH_BUF_CURRENT_OFFSET;
                    BITSTREAM_CRC32 = crc32c(BITSTREAM_CRC32, FLASH_WRITE_BUF, FLASH_BUF_CURRENT_OFFSET);
                    print_err_file(get_filesystem(), "Prog %lX bytes, CRC = %lX, %lX bytes left, bufsize %lX, total prog = %lX\r\n", FLASH_BUF_CURRENT_OFFSET, BITSTREAM_CRC32, FLASH_PROG_BYTES_LEFT, bufsize, FLASH_TOTAL_PROG_BYTES);
                    FLASH_BUF_CURRENT_OFFSET = 0;
                } 
            }
            fpga_program_sendchunk(buffer, bufsize);
            FPGA_PROG_BYTES_LEFT -= bufsize;

            if (!FPGA_PROG_BYTES_LEFT) {
                print_err_file(get_filesystem(), "Finished programming %lX bytes, verifying CRC\r\n", FPGA_TOTAL_PROG_BYTES);
                uint32_t read_crc = flash_calc_crc32(0x00);
                if (read_crc != BITSTREAM_CRC32) {
                    print_err_file(get_filesystem(), "CRC mismatch %lX on flash, %lX prog'd\r\n", read_crc, BITSTREAM_CRC32);
                } else {
                    print_err_file(get_filesystem(), "CRC matched (%lX)\r\n", read_crc);
                }
            }
        }
    }

    if (lba >= DISK_REAL_SECTOR_NUM)
        return bufsize; // fake it
    struct fat_filesystem *fs = get_filesystem();
    uint8_t *addr = fs->raw_sectors[lba] + offset;
    memcpy(addr, buffer, bufsize);
    write_file_info(fs, 0, "ERROR", &err_file_entry); // make sure PC doesn't overwrite our err file info
    return bufsize;
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    // read10 & write10 has their own callback and MUST not be handled here

    void const *response = NULL;
    int32_t resplen = 0;

    // most scsi handled is input
    bool in_xfer = true;

    switch (scsi_cmd[0])
    {
    default:
        // Set Sense = Invalid Command Operation
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

        // negative means error -> tinyusb could stall and/or response with failed status
        resplen = -1;
        break;
    }

    // return resplen must not larger than bufsize
    if (resplen > bufsize)
        resplen = bufsize;

    if (response && (resplen > 0))
    {
        if (in_xfer)
        {
            memcpy(buffer, response, (size_t)resplen);
        }
        else
        {
            // SCSI output
        }
    }

    return resplen;
}