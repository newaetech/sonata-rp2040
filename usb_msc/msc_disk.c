
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
#include "tests.h"
#include "uf2.h"
#include "tusb_config.h"


uint8_t FLASH_WRITE_BUF[CONST_64k];
uint32_t SPI_FLASH_BUF_INDEX = 0;

struct config_options CONFIG = {.dirty = 1, 
                                .fpga_prog_speed = CONF_DEFAULT_FPGA_PROG_SPEED,
                                .flash_prog_speed = CONF_DEFAULT_FLASH_PROG_SPEED,
                                .prog_flash = CONF_DEFAULT_FLASH_PROG_SPEED};

extern uint32_t blink_interval_ms;
uint32_t flash_get_bitstream_offset(void);

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
// callback when PC wants to read from our filesystem
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    // out of ramdisk
    if (lba >= DISK_REAL_SECTOR_NUM)
    {
        // report all reads outside of our RAM buffer as 0x00
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

/* 
    Bitstream fpga and flash write globals 
*/
uint32_t FPGA_PROG_BYTES_LEFT = 0; // bytes remaining in the bitstream file
uint32_t FPGA_TOTAL_PROG_BYTES = 0; // number of bytes we've written to the FPGA, mostly just for logging purposes

uint32_t FLASH_CURRENT_OFFSET = 0; // current bitstream flash offset from start of slot
uint32_t FLASH_BUF_CURRENT_OFFSET = 0; // current offset in 64k storage buffer for flash writes
uint32_t FLASH_TOTAL_PROG_BYTES = 0; // number of bytes we've written to bitstream flash, mostly just for logging purposes
uint32_t FLASH_PROG_BYTES_LEFT = 0; // number of bytes remaining in the bitstream file
uint32_t FPGA_FLASH_BLOCK_COUNTER = 0; // counter to keep track of when we reach a new flash erase sector (every 64kB)

uint32_t FLASH_BASE_OFFSET = 0; // address of fpga bitstream slot
uint32_t BITSTREAM_CRC32 = 0; // running crc32c of bitstream


/*
    Firmware flash write globals

    Note: We do progamming of UF2 by 512 block, so no need to keep track of size. May want to change this in the future
*/
uint32_t FIRMWARE_FLASH_ADDR = 0; // current firmware flash offset from start of slot
uint32_t FIRMWARE_BLOCK_COUNTER = 0; // counter to keep track of when we read a new flash erase sector (every 64kB)
uint32_t FIRMWARE_CRC32 = 0; // running crc32c of firmware

/* shared flash globals */
uint8_t TEST_RD_BUF[CFG_TUD_MSC_EP_BUFSIZE]; // buffer for reading flash data into to verify that writes worked correctly

// uint8_t LAST_SECTOR[DISK_CLUSTER_SIZE];

/*
    Read firmware from flash and calc its CRC
*/
uint32_t firmware_calc_crc(uint32_t addr)
{
    uint32_t crc = 0;
    uint32_t uf2_sz = sizeof(struct UF2_Block);
    struct UF2_Block read_block;
    uint32_t i = 0;
    for (i = 0; i < DISK_FULL_SIZE; i += uf2_sz) {
        spi_flash_read(i + addr, (void*) &read_block, uf2_sz);
        crc = crc32c(crc, (void *)&read_block, uf2_sz);
        if (uf2_is_last_block(&read_block)) break;
    }
    PRINT_INFO("Read %lX bytes from flash", i);
    return crc;
}

/*
    Read bitstream from flash and calc its CRC
*/
uint32_t fpga_flash_calc_crc32(uint32_t addr)
{
    spi_flash_read(addr, TEST_RD_BUF, 256);
    uint32_t bs_len = get_bitstream_length(TEST_RD_BUF, 256);
    if (!bs_len) {
        PRINT_ERR("No bitstream in flash");
        return 0;
    }

    PRINT_INFO("Reading %lX bytes from flash", bs_len);

    uint32_t crc = crc32c(0x00, TEST_RD_BUF, 256);
    uint32_t i = 256;
    while (i < bs_len) {
        uint32_t read_len = ((i + 256) > bs_len) ? (bs_len - i) : 256;
        spi_flash_read(addr + i, TEST_RD_BUF, read_len);
        crc = crc32c(crc, TEST_RD_BUF, read_len);
        i += read_len;
    }

    PRINT_INFO("Read %lX bytes from flash", i);
    return crc;
}

/*
    Handle the case where we get a UF2 block
*/
int handle_firmware_program(uint32_t lba, uint8_t *buffer, uint32_t bufsize)
{
    firmware_init_spi(CONFIG.flash_prog_speed);
    struct UF2_Block *start_blk = (struct UF2_Block *)(buffer);

    // If this is the first block of firmware, reset all addr stuff to 0
    if (!start_blk->blockNo) {
        PRINT_INFO("Starting firmware program of %lX blocks", start_blk->numBlocks);
        FIRMWARE_BLOCK_COUNTER = 0;
        FIRMWARE_FLASH_ADDR = 0;
        FIRMWARE_CRC32 = 0;

        // NOTE: could set block_counter to >64k to trigger erase
        PRINT_INFO("Erasing @ %lX", FIRMWARE_FLASH_ADDR);
        while (spi_flash_is_busy());
        if (spi_flash_64k_erase_nonblocking(FIRMWARE_FLASH_ADDR)) { // todo add different index options to flash
            PRINT_ERR("FW Erase error @ %lX", FIRMWARE_FLASH_ADDR);
        }
        while (spi_flash_is_busy());
    }

    // Max erase is 64kB, so do an erase everytime we've programmed 64k
    if (FIRMWARE_BLOCK_COUNTER >= CONST_64k) {
        PRINT_INFO("Erasing @ %lX", FIRMWARE_FLASH_ADDR);
        while (spi_flash_is_busy());
        if (spi_flash_64k_erase_nonblocking(FIRMWARE_FLASH_ADDR)) { // todo add different index options to flash
            PRINT_ERR("FW Erase error @ %lX", FIRMWARE_FLASH_ADDR);
        }
        while (spi_flash_is_busy());
        FIRMWARE_BLOCK_COUNTER = 0;
    }

    // program data
    for (uint32_t i = 0; i < bufsize; i += 512) {
        struct UF2_Block *cur_blk = (struct UF2_Block *)(buffer + i);
        if (is_uf2_block(cur_blk)) {
            if (spi_flash_write_buffer(FIRMWARE_FLASH_ADDR, buffer + i, 512)) {
                PRINT_ERR("FW prog err @ %lX", FIRMWARE_FLASH_ADDR + i);
            }
            FIRMWARE_CRC32 = crc32c(FIRMWARE_CRC32, buffer + i, 512);

            spi_flash_read(FIRMWARE_FLASH_ADDR, TEST_RD_BUF, 512);
            if (memcmp(buffer + i, TEST_RD_BUF, 512)) {
                PRINT_ERR("Verify error @ %lX", FIRMWARE_FLASH_ADDR + i);
            }

            FIRMWARE_BLOCK_COUNTER += 512;
            FIRMWARE_FLASH_ADDR += 512;

            // TODO: do we want to record number of blocks at the start, then decrement as we program?
            if (uf2_is_last_block(cur_blk)) {
                FIRMWARE_BLOCK_COUNTER = 0;
                FIRMWARE_FLASH_ADDR = 0;
                PRINT_INFO("Finished programming of %lX blocks, CRC = %lX, verifying...", 
                    cur_blk->blockNo, FIRMWARE_CRC32);
                uint32_t read_crc = firmware_calc_crc(0x00);

                #ifdef TESTING_BUILD
                PRINT_TEST(read_crc == FIRMWARE_CRC32, "firmware flash CRC check");
                #endif

                if (read_crc != FIRMWARE_CRC32) {
                    PRINT_ERR("CRC mismatch %lX on flash, %lX prog'd", read_crc, FIRMWARE_CRC32);
                } else {
                    PRINT_INFO("CRC matched");
                }
                break;
            }
        }
    }
    return 0;
}

/*
    Handle the case where we don't get a UF2 block and it's not a known file (i.e. OPTIONS.txt, LOG.txt, etc)

    If at the start of a file, parses the header and erases the first 64k

    Puts all data into a 64kB buffer
*/
int handle_bitstream_program(uint32_t lba, uint8_t *buffer, uint32_t bufsize)
{
    // if we're at the start of the file
    if (!FPGA_PROG_BYTES_LEFT) {
        // Parse Xilinx header to get file length
        FPGA_PROG_BYTES_LEFT = get_bitstream_length(buffer, bufsize);

        // If the parse fails, FPGA_PROG_BYTES_LEFT = 0
        if (FPGA_PROG_BYTES_LEFT) {
            fpga_program_init(CONFIG.fpga_prog_speed);
            fpga_program_setup1(); // nprog low to erase
            // delay, board_millis() seems not to work
            for (volatile uint32_t i = 0; i < 5000; i++);
            fpga_program_setup2(); // nprog back high
            for (volatile uint32_t i = 0; i < 5000; i++); // need to wait a bit after this before we start programming, 5ms from datasheet info

            PRINT_INFO("Programming %lX bytes", FPGA_PROG_BYTES_LEFT);
            FPGA_TOTAL_PROG_BYTES = FPGA_PROG_BYTES_LEFT; // keep track of file size for logging purposes

            if (CONFIG.prog_flash) {
                bitstream_init_spi(CONFIG.flash_prog_speed);

                // erase 64k of flash
                FPGA_FLASH_BLOCK_COUNTER = 0;
                FLASH_CURRENT_OFFSET = 0; // offset in actual flash
                FLASH_PROG_BYTES_LEFT = FPGA_PROG_BYTES_LEFT;

                FLASH_BASE_OFFSET = flash_get_bitstream_offset();
                PRINT_INFO("Using offset %lX", FLASH_BASE_OFFSET);
                while (spi_flash_is_busy());
                spi_flash_64k_erase_nonblocking(FLASH_BASE_OFFSET); // todo add different index options to flash

                BITSTREAM_CRC32 = 0x00;
                FLASH_TOTAL_PROG_BYTES = 0;
            }
        }

    }

    // repeat the check to go into this block after running the above header parse
    if (FPGA_PROG_BYTES_LEFT) {
        // maybe simplify this to not write to a buffer? will have to check speed
        bufsize = min(bufsize, FPGA_PROG_BYTES_LEFT);
        if (CONFIG.prog_flash && FLASH_PROG_BYTES_LEFT) {
            int err = 0;
            bitstream_init_spi(CONFIG.flash_prog_speed);

            // if at the beginning of our buffer, erase 64k of flash
            // todo just keep track of flash length separate from fpga length
            if (FPGA_FLASH_BLOCK_COUNTER >= 0x10000) {
                while (spi_flash_is_busy());
                if (spi_flash_64k_erase_nonblocking(FLASH_CURRENT_OFFSET + FLASH_BASE_OFFSET)) {
                    PRINT_ERR("Erase error @ %lX", FLASH_CURRENT_OFFSET);
                }
                // while (spi_flash_is_busy());
                PRINT_INFO("Erasing %lX", FLASH_CURRENT_OFFSET);
                FPGA_FLASH_BLOCK_COUNTER = 0;
            }
            while (spi_flash_is_busy());

            // write all of buffer
            if (err = spi_flash_write_buffer(FLASH_BASE_OFFSET + FLASH_CURRENT_OFFSET, buffer, bufsize)) {
                PRINT_ERR("BSFL WR err %d @ %lX", err, FLASH_BASE_OFFSET + FLASH_CURRENT_OFFSET);
            }

            if (err = spi_flash_read(FLASH_BASE_OFFSET + FLASH_CURRENT_OFFSET, TEST_RD_BUF, bufsize)) {
                PRINT_ERR("BSFL RD err %d @ %lX", err, FLASH_BASE_OFFSET + FLASH_CURRENT_OFFSET);
            }

            if (memcmp(buffer, TEST_RD_BUF, bufsize)) {
                PRINT_ERR("BSFL verif err @ %lX", FLASH_BASE_OFFSET + FLASH_CURRENT_OFFSET);
            }

            FPGA_FLASH_BLOCK_COUNTER += bufsize;
            FLASH_CURRENT_OFFSET += bufsize;
            FLASH_TOTAL_PROG_BYTES += bufsize;
            FLASH_PROG_BYTES_LEFT -= bufsize;

            // update CRC with data written
            BITSTREAM_CRC32 = crc32c(BITSTREAM_CRC32, buffer, bufsize);
            PRINT_DEBUG("Prog %lX bytes, CRC = %lX, %lX bytes left, total prog = %lX", bufsize, BITSTREAM_CRC32, FLASH_PROG_BYTES_LEFT, FLASH_TOTAL_PROG_BYTES);
        }
        fpga_program_sendchunk(buffer, bufsize);
        FPGA_PROG_BYTES_LEFT -= bufsize;

        if (!FPGA_PROG_BYTES_LEFT) {
            PRINT_INFO("Finished programming %lX bytes, verifying CRC", FPGA_TOTAL_PROG_BYTES);
            if (CONFIG.prog_flash) {
                bitstream_init_spi(CONFIG.flash_prog_speed);
                uint32_t read_crc = fpga_flash_calc_crc32(FLASH_BASE_OFFSET);
                if (read_crc != BITSTREAM_CRC32) {
                    PRINT_ERR("CRC mismatch %lX on flash, %lX prog'd", read_crc, BITSTREAM_CRC32);
                } else {
                    PRINT_INFO("CRC matched %lX", read_crc);
                }
                #ifdef TESTING_BUILD
                PRINT_TEST(read_crc == BITSTREAM_CRC32, "flash CRC check");
                #endif

            }
            if (!FPGA_ISDONE()) {
                PRINT_ERR("FPGA Done pin failed to go high. Is your bitstream valid?");
            }
            #ifdef TESTING_BUILD
            test_done_program(0);
            #endif
        }
    }
    return 0;
}

/*
    Check if the cluster pointed to by LBA is reserved (i.e. it's LOG.txt, OPTIONS.txt, etc., or part of the FAT, etc)
*/
int is_reserved_cluster(uint32_t lba)
{
    int32_t reserved_clusters[] = {0, 1, get_file_cluster(get_filesystem(), 0, "README"), 
        get_file_cluster(get_filesystem(), 0, "OPTIONS"), get_file_cluster(get_filesystem(), 0, "LOG")};

    int32_t opt_cluster = get_file_cluster(get_filesystem(), 0, "OPTIONS");
    int rtn = 0;

    if (opt_cluster > 0) {
        if (sector_to_cluster(lba) == opt_cluster) {
            CONFIG.dirty = 1; // if we touched OPTIONS.txt, mark it as dirty
            rtn = 1;
        }
    }

    for (uint16_t i = 0; i < ARR_LEN(reserved_clusters); i++) {
        if (reserved_clusters[i] < 0) continue;
        if (sector_to_cluster(lba) == reserved_clusters[i]) {
            rtn = 1;
        }
    }
    return rtn;
}

// callback when PC wants to write to our filesystem
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    // out of ramdisk
    if (!bufsize) return 0; //???

    // TODO func this
    // cluster 2 is readme, cluster 3 is fpga folder, cluster 4 is flash folder
    uint8_t known_file = is_reserved_cluster(lba); // should change to "is known file/folder"

    // cap bufsize at max of size of buffer (4096 bytes). This should be done already, but just in case
    bufsize = min(bufsize, CFG_TUD_MSC_EP_BUFSIZE);


    if (!known_file) {
        // TODO func this
        // TODO add programming of different flash slots
        if (is_uf2_block((void *)buffer)) {
            handle_firmware_program(lba, buffer, bufsize);
        } else {
            // TODO func this
            handle_bitstream_program(lba, buffer, bufsize);
        }
    }

    if (lba >= DISK_REAL_SECTOR_NUM)
        return bufsize; // fake it
    struct fat_filesystem *fs = get_filesystem();
    uint8_t *addr = fs->raw_sectors[lba] + offset;

    // Windows tries to mess up the file table...
    // uint16_t bad_seq[] = {0xFFF0, 0xFFF};
    // if (!memcmp(buffer + 10, bad_seq, sizeof(bad_seq))) {
    //     PRINT_DEBUG("Sys info invalid FAT, fixing...");
    //     memset(buffer + 10, 0xFF, sizeof(bad_seq));
    // }
    memcpy(addr, buffer, bufsize);
    write_file_info(fs, 0, "LOG", &err_file_entry); // make sure PC doesn't overwrite our err file info


    /* Update config immediately
        Not really required for proper operation, but makes doing test easier
     */
    if (CONFIG.dirty) {
        if (parse_config(get_filesystem(), &CONFIG)) {
            // if config parse fails, set everything back to default
            set_default_config(&CONFIG);
            #ifdef TESTING_BUILD
            PRINT_TEST(0, "config parse");
            #endif
        } else {
            #ifdef TESTING_BUILD
            PRINT_TEST(1, "config parse");
            test_config(0);
            #endif

        }

    }
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