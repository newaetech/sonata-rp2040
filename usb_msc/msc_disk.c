
#include "bsp/board.h"
#include "tusb.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "fpga_program.h"
#include "flash_util.h"
#include "fat_util.h"
#include "util.h"
#include "main.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define FF_BITSTREAM_LOCATION

uint8_t FLASH_READ_BUF[4096];

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

uint8_t FPGA_PROG_IN_PROCESS = 0;
uint64_t FPGA_PROG_BYTES_LEFT = 0;
uint32_t FPGA_ERASED_START = 0;

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes

uint8_t led_state_a = 0;
uint8_t led_state_b = 0;

uint8_t FPGA_ERASED = 0;
uint32_t BITSTREAM_CLUSTER = 0;


int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    // out of ramdisk
    if (!bufsize) return 0; //wtf?

    // cluster 2 is readme, cluster 3 is fpga folder, cluster 4 is flash folder
    uint16_t num_root_files = 0;
    uint16_t num_folder_files = 0;
    struct directory_entry root_files[NUM_ROOT_DIR_ENTRIES];
    struct directory_entry folder_files[NUM_ROOT_DIR_ENTRIES];
    uint8_t is_reserved_cluster = 0; // should change to "is known file/folder"

    int32_t reserved_clusters[] = {0, 1, get_file_cluster(get_filesystem(), 0, "README"), 
        get_file_cluster(get_filesystem(), 0, "OPTIONS")};
    for (uint16_t i = 0; i < ARR_LEN(reserved_clusters); i++) {
        if (sector_to_cluster(lba) == reserved_clusters[i]) {
            is_reserved_cluster = 1;
        }
    }

    if (!is_reserved_cluster) {
        if (!FPGA_PROG_BYTES_LEFT) {
            int len_offset = find_bitstream_len_offset(buffer, bufsize);
            if (len_offset >= 0) {
                gpio_put(26, 1); // LED off
                fpga_program_setup1(); // nprog low to erase
                FPGA_ERASED_START = board_millis();
                // while ((cur_time - FPGA_ERASED_START) < 2) cur_time = board_millis(); // T_program says >250ns, so this should be safe
                for (volatile uint32_t i = 0; i < 5000; i++);
                uint32_t cur_time = board_millis();
                fpga_program_setup2();
                FPGA_PROG_BYTES_LEFT = BE_4U8_TO_U32(buffer + len_offset);
                FPGA_PROG_BYTES_LEFT += len_offset;

                bufsize = min(bufsize, FPGA_PROG_BYTES_LEFT);
                fpga_program_sendchunk(buffer, bufsize);
                FPGA_PROG_BYTES_LEFT -= bufsize;
            }
        } else {
            // this should be okay for now, may rework to follow file table to handle
            // writing to multiple files at a time
            bufsize = min(bufsize, FPGA_PROG_BYTES_LEFT);
            fpga_program_sendchunk(buffer, bufsize);
            FPGA_PROG_BYTES_LEFT -= bufsize;
        }
    }

    if (lba >= DISK_REAL_SECTOR_NUM)
        return bufsize; // fake it
    struct fat_filesystem *fs = get_filesystem();
    uint8_t *addr = fs->raw_sectors[lba] + offset;
    memcpy(addr, buffer, bufsize);
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