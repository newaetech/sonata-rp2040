#pragma once


#include "bsp/board.h"
#include "tusb.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "fpga_program.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

enum
{
    DISK_REAL_CLUSTER_NUM = 0x10,
    DISK_REPORT_SECTOR_NUM = 0xFFFF,
    DISK_SECTOR_PER_CLUSTER = 4,
    DISK_SECTOR_SIZE = 512, // sector size
    DISK_CLUSTER_SIZE = DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER,
    DISK_SECTOR_PER_FAT = 0x40,
    NUM_FAT = 1,
    DISK_REAL_SECTOR_NUM = (1 + NUM_FAT * DISK_SECTOR_PER_FAT + 1 + (DISK_REAL_CLUSTER_NUM * DISK_SECTOR_PER_CLUSTER)),
    DISK_BLOCK_NUM = DISK_SECTOR_SIZE + (DISK_SECTOR_PER_FAT * DISK_SECTOR_SIZE) + (DISK_SECTOR_SIZE) + (DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER * DISK_REAL_CLUSTER_NUM),
    NUM_ROOT_DIR_ENTRIES = 512 / 32,
};

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
    "Place bitstreams in the FPGA folder to only program FPGA\r\n" \
    "Place bitstreams in the FLASH folder to only program SPI Flash\r\n" \
    "To reprogram, delete file and resent\r\n"

#pragma pack(push, 1)
struct boot_sector
{
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
    union
    {
        struct
        {
            uint8_t bios_drive_number; // 0x80
            uint8_t reserved;          // 0x00
            uint8_t ext_boot_sig;      // 0x29
            uint8_t serial_number[4];
            uint8_t volume_label[11];
            uint8_t sys_identifier[8]; // FAT12, FAT16 or FAT32
            uint8_t empty[448];
        };
        // partially complete FAT32 repr
        struct
        {
            uint8_t big_sectors_per_fat[4]; // FAT32 only
            uint8_t ext_flags[2];
            uint8_t fs_version[2];
            uint8_t root_dir_start[4];
            uint8_t fs_info_sector[2];
            uint8_t backup_boot_sector[2];
        };
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
            uint8_t fat[NUM_FAT][DISK_SECTOR_PER_FAT * DISK_SECTOR_SIZE];
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
struct fat_filesystem *get_filesystem(void);
int32_t get_file_cluster(uint16_t parent_cluster, char *filename);
int is_cluster_in_chain(uint16_t starting_cluster, uint16_t ciq);
uint16_t cluster_to_fat_table_val(uint16_t cluster_num);
int get_first_file_in_dir(uint16_t parent_cluster, struct directory_entry *info);
void dir_fill_req_entries(uint16_t cluster_num, uint16_t parent_cluster);