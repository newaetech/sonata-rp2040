
#include "bsp/board.h"
#include "tusb.h"

extern uint32_t blink_interval_ms;

enum
{
  DISK_REAL_CLUSTER_NUM = 0x10,
  DISK_REPORT_SECTOR_NUM = 0xFFFF,
  DISK_SECTOR_PER_CLUSTER = 4,
  DISK_SECTOR_SIZE = 512, // sector size
  DISK_CLUSTER_SIZE = DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER,
  DISK_SECTOR_PER_FAT = 0x40,
  NUM_FAT = 2,
  DISK_REAL_SECTOR_NUM = (1 + NUM_FAT * DISK_SECTOR_PER_FAT + 1 + (DISK_REAL_CLUSTER_NUM*DISK_SECTOR_PER_CLUSTER)),
  DISK_BLOCK_NUM = DISK_SECTOR_SIZE + (DISK_SECTOR_PER_FAT * DISK_SECTOR_SIZE) + (DISK_SECTOR_SIZE) + (DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER * DISK_REAL_CLUSTER_NUM),
  NUM_ROOT_DIR_ENTRIES = 512 / 32,
};

#define U16_TO_2U8(X) ((X) & 0xFF), (((X) >> 8) & 0xFF)
#define U32_TO_4U8(X) ((X) & 0xFF), (((X) >> 8) & 0xFF), (((X) >> 16) & 0xFF), (((X) >> 24) & 0xFF)

uint32_t cluster_to_sector(uint32_t cluster)
{
  if (cluster <= 2) return 0;
  uint32_t rtn = 0;
  rtn += 1; // add boot sector
  rtn += DISK_SECTOR_PER_FAT; // add FAT
  rtn += 1; // add root directory
  rtn += (cluster - 2) * 4;
  return rtn;
}


//--------------------------------------------------------------------+
// LUN 0
//--------------------------------------------------------------------+
// #define README0_CONTENTS 
// "LUN0: This is tinyusb's MassStorage Class demo.\r\n\r\n
// If you find any bugs or get any questions, feel free to file an\r\n
// issue at github.com/hathach/tinyusbV"
//  

/*
NOTES:

Idea: search root directory for cluster location of flash and sram folders
*/

// NOTE: keep this under 512 bytes as using multiple blocks is kind of a pain
#define README_CONTENTS \
"Place bitstreams in the FPGA folder to only program FPGA\r\n" \
"Place bitstreams in the FLASH folder to only program SPI Flash\r\n"

#define README0_CONTENTS \
"Hi"

#pragma pack(push, 1)
struct boot_sector {
  uint8_t jmp_cmd[3]; // 0xEB, 0x3C, 0x90
  uint8_t oem_name[8]; 
  uint8_t bytes_per_sector[2]; // usually 0x200
  uint8_t sectors_per_cluster; // https://support.microsoft.com/en-us/topic/default-cluster-size-for-ntfs-fat-and-exfat-9772e6f1-e31a-00d7-e18f-73169155af95
  uint8_t reserved_sectors[2]; // sectors before FAT, usually 1
  uint8_t num_fat; // number of File Allocation Tables. Often 2, but in our case using 1 saves a lot of space
  uint8_t max_root_dir_entries[2]; // Max number of entries in the root dir. Each entry takes up 32 bytes
  uint8_t total_sectors[2]; // Total number of sectors
  uint8_t media_descriptor; // 0xF8 for removable media
  uint8_t sectors_per_fat[2]; // Needs to be long enough to hold all data on disk
  uint8_t sectors_per_head[2]; // ignored
  uint8_t heads_per_cylinder[2]; // ignored
  uint8_t hidden_sectors[4]; // set to 0
  uint8_t big_num_sectors[4]; // FAT32 only
  union {
    struct {
      uint8_t bios_drive_number; // 0x80
      uint8_t reserved; // 0x00
      uint8_t ext_boot_sig; // 0x29
      uint8_t serial_number[4];
      uint8_t volume_label[11];
      uint8_t sys_identifier[8]; // FAT12, FAT16 or FAT32
      uint8_t empty[448];
    };
    struct {
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

struct directory_entry {
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

struct fat_filesystem {
  union {
    struct {
      union {
        struct boot_sector boot_sec;
        // uint8_t boot_sec_raw[DISK_SECTOR_SIZE];
      };
      uint8_t fat[NUM_FAT][DISK_SECTOR_PER_FAT * DISK_SECTOR_SIZE];
      // union {
        struct directory_entry root_dir[NUM_ROOT_DIR_ENTRIES];
        // uint8_t root_dir_raw[DISK_SECTOR_SIZE];
      // };
      union {
        uint8_t clusters[DISK_REAL_CLUSTER_NUM][DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER];
        struct directory_entry directories[DISK_REAL_CLUSTER_NUM][(DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER)/sizeof(struct directory_entry)];
      };
      
    };
    uint8_t raw_sectors[DISK_REAL_SECTOR_NUM][DISK_SECTOR_SIZE];
  };
};

struct fat_filesystem FILESYSTEM = {
  .boot_sec =  {
      .jmp_cmd = {0xEB, 0x3C, 0x90},
      .oem_name = {0x4D, 0x53, 0x44, 0x4F, 0x53, 0x35, 0x2E, 0x30}, // OEM name
      .bytes_per_sector = {U16_TO_2U8(0x200)},
      .sectors_per_cluster = DISK_SECTOR_PER_CLUSTER,
      .reserved_sectors = {U16_TO_2U8(0x01)},
      .num_fat = 2,
      .max_root_dir_entries = {U16_TO_2U8(NUM_ROOT_DIR_ENTRIES)},
      .total_sectors = {U16_TO_2U8(0xFFFF)},
      .media_descriptor = 0xF8,
      .sectors_per_fat = {U16_TO_2U8(DISK_SECTOR_PER_FAT)},
      .bios_drive_number = 0x80,
      .ext_boot_sig = 0x29,
      .serial_number = {U32_TO_4U8(0x1234)},
      .volume_label = {'T' , 'i' , 'n' , 'y' , 'U' , 'S' , 'B' , ' ' , '0' , ' ' , ' '},
      .sys_identifier = {'F', 'A', 'T', '1', '6', ' ', ' ', ' '},
      .sig = {0x55, 0xAA},
      .sectors_per_head = {32, 0x00},
      .heads_per_cylinder = {0x02, 0x00},
  },
  .fat = {
      {
      0xF8, 0xFF, // 16 bits for FAT ID , 12 for FAT12, etc. (0xFFF8 for partitioned disk)
      0xFF, 0xFF, //End of chain indicator (reserved cluster?)
      0xFF, 0xFF, // cluster 2 (README.txt in our case)
      0xFF, 0xFF, // cluster 2 (README.txt in our case)
      0xFF, 0xFF, // cluster 2 (README.txt in our case)
      // 0xFF, 0xFF, // cluster 3 (FPGA folder in our case)
      // 0xFF, 0xFF, // cluster 4 (SRAM folder in our case)
    },
      {
      0xF8, 0xFF, // 16 bits for FAT ID , 12 for FAT12, etc. (0xFFF8 for partitioned disk)
      0xFF, 0xFF, //End of chain indicator (reserved cluster?)
      0xFF, 0xFF, // cluster 2 (README.txt in our case)
      0xFF, 0xFF, // cluster 2 (README.txt in our case)
      0xFF, 0xFF, // cluster 2 (README.txt in our case)
      // 0xFF, 0xFF, // cluster 3 (FPGA folder in our case)
      // 0xFF, 0xFF, // cluster 4 (SRAM folder in our case)
    }
  },
  .root_dir = {
      {
        .filename = {'T' , 'i' , 'n' , 'y' , 'U' , 'S' , 'B' , ' ' }, // name
        .extension = {'0', ' ', ' '},
        .attribute = 0x08, // volume label
        .time_stamp = {0x4F, 0x6D},
        .date_stamp = {0x65, 0x43}
      },
      {
        .filename = {'R' , 'E' , 'A' , 'D' , 'M' , 'E' , ' ' , ' '},
        .extension = {'T' , 'X' , 'T'},
        .creation_time = {0x52, 0x6D},
        .creation_date = {0x65, 0x43},
        .last_access_date = {0x65, 0x43},
        .time_stamp = {0x88, 0x6D},
        .date_stamp = {0x65, 0x43},
        .starting_cluster = {0x02, 0x00},
        .file_size = {U32_TO_4U8(sizeof(README_CONTENTS) - 1)}
      },
      {
        .filename = {'F', 'P', 'G', 'A', ' ', ' ', ' ', ' '},
        .extension = {' ', ' ', ' '}, // extension
        .attribute = 0x10, // directory
        .creation_time = {0x52, 0x6D},
        .creation_date = {0x65, 0x43},
        .last_access_date = {0x65, 0x43},
        .time_stamp = {0x88, 0x6D},
        .date_stamp = {0x65, 0x43},
        .starting_cluster = {0x03, 0x00},
      },
      {
        .filename = {'F', 'L', 'A', 'S', 'H', ' ', ' ', ' '},
        .extension = {' ', ' ', ' '}, // extension
        .attribute = 0x10, // directory
        .creation_time = {0x52, 0x6D},
        .creation_date = {0x65, 0x43},
        .last_access_date = {0x65, 0x43},
        .time_stamp = {0x88, 0x6D},
        .date_stamp = {0x65, 0x43},
        .starting_cluster = {0x04, 0x00},
      }
  },
  .clusters = {
    {README_CONTENTS},
    {},
    {}
  }
};
#pragma pack(pop)

// This seems kinda sketch
uint32_t sector_to_cluster(uint32_t sector)
{
  void *cluster_base_addr = FILESYSTEM.clusters;
  void *sector_addr = FILESYSTEM.raw_sectors + sector;
  if (sector_addr < cluster_base_addr) return 0; // NOTE: pointer comparison like this only okay since they're part of the same struct

  for (uint32_t i = 0; i < DISK_REPORT_SECTOR_NUM / DISK_SECTOR_PER_CLUSTER; i++) {
    if ((void *)(FILESYSTEM.clusters + i) > sector_addr) {
      return i;
    }
  }
  return 0;
}

void dir_fill_req_entries(uint16_t cluster_num, uint16_t parent_cluster)
{
  if (cluster_num < 2) {
    return;
  }
  // '.' file
  FILESYSTEM.directories[cluster_num - 2][0] = (struct directory_entry){
      .filename = {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
      .extension = {' ', ' ', ' '}, // extension
      .attribute = 0x10, // directory
      .creation_time = {0x52, 0x6D},
      .creation_date = {0x65, 0x43},
      .last_access_date = {0x65, 0x43},
      .time_stamp = {0x88, 0x6D},
      .date_stamp = {0x65, 0x43},
      .starting_cluster = U16_TO_2U8(cluster_num)
  };
  // '..' file
  FILESYSTEM.directories[cluster_num - 2][1] = (struct directory_entry){
      .filename = {'.', '.', ' ', ' ', ' ', ' ', ' ', ' '},
      .extension = {' ', ' ', ' '}, // extension
      .attribute = 0x10, // directory
      .creation_time = {0x52, 0x6D},
      .creation_date = {0x65, 0x43},
      .last_access_date = {0x65, 0x43},
      .time_stamp = {0x88, 0x6D},
      .date_stamp = {0x65, 0x43},
      .starting_cluster = U16_TO_2U8(parent_cluster)
  };
}

// Invoked to determine max LUN
uint8_t tud_msc_get_maxlun_cb(void)
{
  return 1; // dual LUN
}

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
  (void) lun; // use same ID for both LUNs

  const char vid[] = "TinyUSB";
  const char pid[] = "Mass Storage";
  const char rev[] = "1.0";

  memcpy(vendor_id  , vid, strlen(vid));
  memcpy(product_id , pid, strlen(pid));
  memcpy(product_rev, rev, strlen(rev));
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
  if ( lun == 1 && board_button_read() ) return false;

  return true; // RAM disk is always ready
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)
{
  (void) lun;

  // *block_count = DISK_BLOCK_NUM;
  // *block_size  = DISK_BLOCK_SIZE;
  *block_count = DISK_REPORT_SECTOR_NUM; // bytes per sector?// DISK_BLOCK_NUM;
  *block_size  = DISK_SECTOR_SIZE; // number of sectors? //DISK_BLOCK_SIZE;
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
  (void) lun;
  (void) power_condition;

  if ( load_eject )
  {
    if (start)
    {
      // load disk storage
    }else
    {
      // unload disk storage
    }
  }

  return true;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
  // out of ramdisk
  if ( lba >= DISK_REAL_SECTOR_NUM) {
    // fake it?
    memset(buffer, 0x00, bufsize);
    return (int32_t) bufsize;
    // return -1;
  // return -1;
  }

  // uint8_t const* addr = msc_disk0[lba] + offset;
  uint8_t const *addr = FILESYSTEM.raw_sectors[lba] + offset;
  memcpy(buffer, addr, bufsize);

  return (int32_t) bufsize;
}

bool tud_msc_is_writable_cb (uint8_t lun)
{
  (void) lun;

#ifdef CFG_EXAMPLE_MSC_READONLY
  return false;
#else
  return true;
#endif
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
  // out of ramdisk
  if ( lba >= DISK_REAL_SECTOR_NUM ) return -1;
  // if ( offset >= (sizeof(FILESYSTEM) - 1) ) {
  //   return (int32_t) bufsize; // ignore write outside of bounds
  // }

#ifndef CFG_EXAMPLE_MSC_READONLY
  // uint8_t* addr = msc_disk0[lba]  + offset;
  if (sector_to_cluster(lba) == 2) { // in FPGA folder
    blink_interval_ms = 2500;
  } else if (sector_to_cluster(lba) == 3) { // in SRAM folder
    blink_interval_ms = 100;
  }
  uint8_t *addr = FILESYSTEM.raw_sectors[lba] + offset;
  memcpy(addr, buffer, bufsize);
#else
  (void) lun; (void) lba; (void) offset; (void) buffer;
#endif

  return (int32_t) bufsize;
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb (uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize)
{
  // read10 & write10 has their own callback and MUST not be handled here

  void const* response = NULL;
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
  if ( resplen > bufsize ) resplen = bufsize;

  if ( response && (resplen > 0) )
  {
    if(in_xfer)
    {
      memcpy(buffer, response, (size_t) resplen);
    }else
    {
      // SCSI output
    }
  }

  return resplen;
}