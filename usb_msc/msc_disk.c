
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
    // if (lba >= DISK_REAL_SECTOR_NUM)
    // {
    //     // fake it?
    //     memset(buffer, 0x00, bufsize);
    //     return (int32_t)bufsize;
    //     // return -1;
    //     // return -1;
    // }

    // // uint8_t const* addr = msc_disk0[lba] + offset;
    // struct fat_filesystem *fs = get_filesystem();
    // uint8_t const *addr = fs->raw_sectors[lba] + offset;
    // memcpy(buffer, addr, bufsize);
    if (spi_flash_read(lba * DISK_SECTOR_SIZE, buffer, bufsize)) return -1;

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

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    if (!bufsize) return 0;
    if (bufsize > DISK_SECTOR_SIZE) return -1;
    

    // bitstream_init_spi();
    if ((offset) || (bufsize != DISK_SECTOR_SIZE)) {
        if (spi_flash_read((lba * DISK_SECTOR_SIZE), FLASH_READ_BUF, DISK_SECTOR_SIZE)) return -1;
        memcpy(FLASH_READ_BUF + offset, buffer, bufsize);
        buffer = FLASH_READ_BUF;
    }

    if (spi_flash_sector_erase(lba)) return -1;

    for (uint8_t i = 0; i < (bufsize / 256); i++) {
        if (spi_flash_page_program((lba * DISK_SECTOR_SIZE), buffer + (256 * i))) return -1;
    }

    return bufsize;
}

#if 0
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    // out of ramdisk
    if (!bufsize) return 0; //wtf?

    // cluster 2 is readme, cluster 3 is fpga folder, cluster 4 is flash folder
    if (sector_to_cluster(lba) > 4) {
        int32_t fpga_folder_cluster = get_file_cluster(0, "FPGA");
        if (fpga_folder_cluster < 0) return -1; // error
        struct directory_entry bitstream_info = {0};
        if (!get_first_file_in_dir(fpga_folder_cluster, &bitstream_info)) {
            uint16_t bitstream_start_cluster = LE_2U8_TO_U16(bitstream_info.starting_cluster);
            if ((sector_to_cluster(lba) == bitstream_start_cluster) && !FPGA_PROG_BYTES_LEFT) {
                if ((!FPGA_ERASED) && !FPGA_ERASED_START) {
                    // set NPROG low
                    gpio_put(26, 1); // LED off
                    fpga_program_setup1(); // nprog low to erase
                    FPGA_ERASED_START = board_millis(); // get start of erase time
                    return 0; // busy
                } else if (FPGA_ERASED_START) {
                    if ((board_millis() - FPGA_ERASED_START) < 100) {
                        return 0; // still waiting on erase
                    } else {
                        FPGA_ERASED = 1;
                        FPGA_ERASED_START = 0;
                        gpio_put(24, 1);
                        fpga_program_setup2();
                    }
                }

                if (FPGA_ERASED) {
                    // uint16_t header_end_loc = 0;
                    // uint8_t match_pattern[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                    // for (; header_end_loc < (bufsize - sizeof(match_pattern)); header_end_loc++) {
                    //     if (buffer[header_end_loc] != 0xFF) continue;
                    //     if (!memcmp(buffer + header_end_loc, match_pattern, sizeof(match_pattern))) break; // found
                    // }
                    int len_loc = find_bitstream_len_offset(buffer, bufsize);
                    if (len_loc < 0) {
                        set_err_led(1);
                    }
                    return -1;

                    FPGA_PROG_BYTES_LEFT = BE_4U8_TO_U32(buffer + len_loc);
                    FPGA_PROG_BYTES_LEFT += header_end_loc; // TODO: check this shouldn't be +1 or -1 or something
                    fpga_init_dma();
                    FPGA_ERASED = 0;

                }
            }
            if (is_cluster_in_chain(bitstream_start_cluster, sector_to_cluster(lba))) {
                if (FPGA_PROG_BYTES_LEFT) {
                    if (!is_fpga_dma_ready()) {
                        gpio_put(26, led_state_a); // blink every time we get here
                        led_state_a ^= 1;
                        // start transfer
                        // bufsize = min(bufsize, sizeof(FPGA_WRITE_BUF));
                        bufsize = min(bufsize, FPGA_PROG_BYTES_LEFT);
                        // memcpy(FPGA_WRITE_BUF, buffer, bufsize);
                        // dma_channel_configure(fpga_dma, &fpga_dma_config, &spi_get_hw(spi1)->dr,
                        //     FPGA_WRITE_BUF, bufsize, true);

                        int written_bytes = fpga_send_dma(buffer, bufsize);
                        // spi_write_blocking(spi1, buffer, bufsize);
                        // for (uint32_t i = 0; i < bufsize; i++) {
                        //     fpga_program_sendbyte(buffer[i]);
                        // }
                        FPGA_PROG_BYTES_LEFT -= written_bytes;
                        if (!FPGA_PROG_BYTES_LEFT) {
                            FPGA_PROG_IN_PROCESS = 0;
                        }
                        // return bufsize;
                    } else {
                        return 0;
                    }
                }
            }
        }
    }
    if (lba >= DISK_REAL_SECTOR_NUM)
        return bufsize; // fake it
    struct fat_filesystem *fs = get_filesystem();
    uint8_t *addr = fs->raw_sectors[lba] + offset;
    memcpy(addr, buffer, bufsize);
    return bufsize;
}
#endif

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