#pragma once


#include "bsp/board.h"
#include "tusb.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "fpga_program.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

enum
{
    DISK_REAL_CLUSTER_NUM = 0x10,
    DISK_REPORT_SECTOR_NUM = 0xFFFF,
    DISK_SECTOR_PER_CLUSTER = 1, // 0x04
    DISK_SECTOR_SIZE = 4096, // 512 // sector size
    DISK_CLUSTER_SIZE = DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER,
    DISK_SECTOR_PER_FAT = 0x04, //0x40,
    NUM_FAT = 1,
    DISK_REAL_SECTOR_NUM = (1 + NUM_FAT * DISK_SECTOR_PER_FAT + 1 + (DISK_REAL_CLUSTER_NUM * DISK_SECTOR_PER_CLUSTER)),
    DISK_BLOCK_NUM = DISK_SECTOR_SIZE + (DISK_SECTOR_PER_FAT * DISK_SECTOR_SIZE) + (DISK_SECTOR_SIZE) + (DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER * DISK_REAL_CLUSTER_NUM),
    NUM_ROOT_DIR_ENTRIES = DISK_SECTOR_SIZE / 32,
};

#define FAT_NAME_SZ 8
#define FAT_EXT_SZ 3

static uint32_t cluster_to_sector(uint32_t cluster)
{
    if (cluster <= 2)
        return 0;
    uint32_t rtn = 0;
    rtn += 1;                   // add boot sector
    rtn += DISK_SECTOR_PER_FAT * NUM_FAT; // add FAT
    rtn += 1;                   // add root directory
    rtn += (cluster - 2) * 4;
    return rtn;
}

static uint32_t sector_to_cluster(uint32_t sector)
{
    uint32_t cluster_start = 1 + (NUM_FAT) * (DISK_SECTOR_PER_FAT) + 1;
    if (sector < cluster_start) return 0;
    sector -= cluster_start;
    sector /= DISK_SECTOR_PER_CLUSTER;
    sector += 2;
    return sector;
}

// NOTE: keep this under 512 bytes as using multiple blocks is kind of a pain
#define README_CONTENTS                                            \
    "See the following notes:\r\n" \
    "\t1. Copy bitstreams or firmware into this directory to program\r\n" \
    "\t2. Firmware must be in the Intel hex format\r\n" \
    "\t3. If the transferred file begins with ':' and the checksum of the first line matches, the file is assumed to be firmware\r\n" \
    "\t4. Otherwise, if the transferred file contains the magic sequence 0x000000BB, 0x11220044, it is assumed to be a bitstream\r\n" \
    "\t5. If neither is true, nothing will be written\r\n" \
    "\t6. Programming options can be modified by changing options.txt\r\n" \
    ""

#define OPTIONS_CONTENTS \
    "PROG_SPI_FLASH=NO\r\n" \
    "SPI_FLASH_SPEED_KHZ=5000\r\n" \
    "SPI_FPGA_SPEED_KHZ=5000\r\n" \
    ""

#pragma pack(push, 1)
struct boot_sector {
    union {
        struct {
            uint8_t jmp_cmd[3]; // 0xEB, 0x3C, 0x90
            uint8_t oem_name[8];
            uint8_t bytes_per_sector[2];     // usually 0x200
            uint8_t sectors_per_cluster;     // https://support.microsoft.com/en-us/topic/default-cluster-size-for-ntfs-fat-and-exfat-9772e6f1-e31a-00d7-e18f-73169155af95
            uint8_t reserved_sectors[2];     // sectors before FAT, usually 1
            uint8_t num_fat;                 // number of File Allocation Tables. Often 2, but in our case using 1 saves a lot of space
            uint8_t max_root_dir_entries[2]; // Max number of entries in the root dir. Each entry takes up 32 bytes
            uint8_t total_sectors[2];        // Total number of sectors
            uint8_t media_descriptor;        // 0xF8 for removable media
            uint8_t sectors_per_fat[2];      // Needs to be long enough to hold all data on disk
            uint8_t sectors_per_head[2];     // ignored
            uint8_t heads_per_cylinder[2];   // ignored
            uint8_t hidden_sectors[4];       // set to 0
            uint8_t big_num_sectors[4];      // FAT32 only
            uint8_t bios_drive_number; // 0x80
            uint8_t reserved;          // 0x00
            uint8_t ext_boot_sig;      // 0x29
            uint8_t serial_number[4];
            uint8_t volume_label[11];
            uint8_t sys_identifier[8]; // FAT12, FAT16 or FAT32

        };
        uint8_t mem_b4_sig[DISK_SECTOR_SIZE - 2];

    };

    uint8_t sig[2]; // 0x55, 0xAA
};

struct directory_entry
{
    uint8_t filename[8];
    uint8_t extension[3];
    uint8_t attribute;
    uint8_t uppercase;
    uint8_t creation_time_ms;
    uint8_t creation_time[2];
    uint8_t creation_date[2];
    uint8_t last_access_date[2];
    uint8_t fat32_high_word_cluster[2];
    uint8_t time_stamp[2];
    uint8_t date_stamp[2];
    uint8_t starting_cluster[2];
    uint8_t file_size[4];
};

struct fat_filesystem
{
    union
    {
        struct
        {
            union
            {
                struct boot_sector boot_sec;
                // uint8_t boot_sec_raw[DISK_SECTOR_SIZE];
            };
            union {
                uint8_t fat[NUM_FAT][DISK_SECTOR_PER_FAT * DISK_SECTOR_SIZE];
                uint16_t fat16[NUM_FAT][DISK_SECTOR_PER_FAT * DISK_SECTOR_SIZE / 2];
            };
            // union {
            struct directory_entry root_dir[NUM_ROOT_DIR_ENTRIES];
            // uint8_t root_dir_raw[DISK_SECTOR_SIZE];
            // };
            union
            {
                uint8_t clusters[DISK_REAL_CLUSTER_NUM][DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER];
                struct directory_entry directories[DISK_REAL_CLUSTER_NUM][(DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER) / sizeof(struct directory_entry)];
            };
        };
        uint8_t raw_sectors[DISK_REAL_SECTOR_NUM][DISK_SECTOR_SIZE];
    };
};

#pragma pack(pop)

enum fat_directory_bit_flags {
    FAT_DIR_READ_ONLY = 0x01,
    FAT_DIR_HIDDEN = 0x02,
    FAT_DIR_SYSTEM = 0x04,
    FAT_DIR_VOL_LABEL = 0x08,
    FAT_DIR_DIRECTORY = 0x10,
    FAT_DIR_ARCHIVE = 0x20
};

struct fat_filesystem *get_filesystem(void);

int32_t get_file_cluster(struct fat_filesystem *fs, uint16_t parent_cluster, char *filename);

int is_cluster_in_chain(struct fat_filesystem *fs, uint16_t starting_cluster, uint16_t ciq);

uint16_t cluster_to_fat_table_val(struct fat_filesystem *fs, uint16_t cluster_num);

int get_first_file_in_dir(struct fat_filesystem *fs, uint16_t parent_cluster, struct directory_entry *info);

void dir_fill_req_entries(uint16_t cluster_num, uint16_t parent_cluster);

int get_files_in_directory(uint16_t dir_cluster, struct directory_entry *files, uint16_t max_num_files);

int is_folder(struct directory_entry *entry);

uint32_t get_file_length(struct fat_filesystem *fs, uint16_t parent_cluster, char *filename);

int32_t get_file_info(struct fat_filesystem *fs, uint16_t parent_cluster, char *filename, struct directory_entry *file_info);