# FAT Filesystem

## Introduction

The File Allocation Table (FAT) filesystem is a filesystem developed in the late 70's and
most notable for both being the primary filesystem of older Microsoft OS's, before the introduction 
of NTFS, as well as the most common filesystem used on portable media such as flash drives. This filesystem, besides 
being used by storage media, can also be used a user friendly interface for a bootloader, allowing users to 
program a device by simply dragging a firmware file into a mounted drive.

Variants of FAT FS include FAT12, FAT16, and FAT32, with the latter being the most common variant
used today. The number here indicates the size of each entry in the FAT. 
Which variant to use will largely depend on the storage needs of devices, as each has a valid range of partition 
sizes that they support. This document will focus on FAT16, as it is the most relevant for storage sizes 
common on microcontrollers and FPGAs. Note that FAT32 is a bit different from FAT12/16, such as not having a
root directory section, as well as having additional fields in the boot sector.

## Important Terms/Concepts

* Sector: The minimum storage unit of a disk. All disk sizes and sections are multiples of this number. Usually
512
* Cluster: The basic data storage unit of the filesystem. Each file and folder is allocated at least one
cluster. Each cluster is a fixed size of some multiple of the sector size. A larger cluster size will
reduce the minimum spaced used for each file, but also require a larger FAT
* File Allocation Table (FAT): A table of each cluster that incdicates if a cluster is free, where the next
cluster for that file/folder is, or if that is the final cluster for a file/folder
* Data is stored in little endian format, meaning `0xABCD` is stored as `{0xCD, 0xAB}`.
* Strings are fixed size and should be padded with ASCII spaces (`' '`).


## Top Level Overview

A FAT filesystem can be broken down into four major sections: the boot sector, the file allocation table (FAT),
the root directory, and the data clusters. In short, the boot sector includes important information
about the drive, such as the filesystem type, the size of data clusters, how large the FAT is, etc. The FAT
is a linked list laying out the data clusters. The root directory includes file and folder information
for the top level of a drive. Each data cluster can contain subdirectories or raw data.

The boot sector spans 1 sector. The number of sectors occupied by the FAT is indicated in the boot sector.
The root directory is the minimum number of sectors required to hold the maximum number of entries. The data clusters
occupy the rest of the disk.

## More Detail

### Boot Sector

The boot sector is layed out as follows for a drive with 512 byte sectors:

```C

struct boot_sector {
  uint8_t jmp_cmd[3]; // 0xEB, 0x3C, 0x90
  uint8_t oem_name[8]; 
  uint8_t bytes_per_sector[2]; // usually 0x200
  uint8_t sectors_per_cluster; // https://support.microsoft.com/en-us/topic/default-cluster-size-for-ntfs-fat-and-exfat-9772e6f1-e31a-00d7-e18f-73169155af95
  uint8_t reserved_sectors[2]; // sectors before FAT, usually 1
  uint8_t num_fat; // number of File Allocation Tables. Often 2 to backup the first FAT, but in our case using 1 saves a lot of space
  uint8_t max_root_dir_entries[2]; // Max number of entries in the root dir. Each entry takes up 32 bytes
  uint8_t total_sectors[2]; // Total number of sectors
  uint8_t media_descriptor; // 0xF8 for removable media
  uint8_t sectors_per_fat[2]; // Needs to be long enough to hold all data on disk
  uint8_t sectors_per_head[2]; // ignored
  uint8_t heads_per_cylinder[2]; // ignored
  uint8_t hidden_sectors[4]; // set to 0
  uint8_t big_num_sectors[4]; // FAT32 only
  uint8_t bios_drive_number; // 0x80
  uint8_t reserved; // 0x00
  uint8_t ext_boot_sig; // 0x29
  uint8_t serial_number[4];
  uint8_t volume_label[11];
  uint8_t sys_identifier[8]; // FAT12, FAT16 or FAT32
  uint8_t empty[448];
  uint8_t sig[2]; // 0x55, 0xAA
};
```

The following fields are important:

* `bytes_per_sector`: From what I've read, this can technically vary, but is almost always 512 bytes. Larger sizes
result in more wasted space in the boot sector and root directories, but also allow for larger drive sizes
* `sectors_per_cluster`: How many sectors each cluster takes up. Larger values result in more wasted space per file,
but also a smaller FAT
* `max_root_dir_entries`: The maximum number of entries in the root directory. 
* `total_sectors`: This number times `bytes_per_sector` indicates the maximum size of the drive
* `sectors_per_fat`: How many sectors the FAT takes up. The fat must be large enough to hold information on every cluster
on the device.

The others can mostly be left as default.

### The FAT

The FAT is a big table with entries for each data cluster indicating if the cluster is free, where the next cluster for the file/folder is,
or if this is the last cluster for the file/folder. On FAT16, each table entry takes up 16 bits (12 bits for FAT12, 32 for FAT32). The first entry
should be the media descriptor, with the rest being 1's (`0xFFF8`) in our case. The next entry must be `0xFFFF`.

For each remaining cluster, the entry should be `0x0000` if the cluster is free, `0xFFFF` if the cluster is the end of a file, or a pointer
to another entry in the table if the file spans more clusters. For example the following drive has a file/folder using 2 cluster and another
using 1:

```C
uint8_t FAT[bytes_per_sector * sectors_per_fat] = {
    0xF8, 0xFF, // first entry must be 0xFFF8 // cluster 0x00
    0xFF, 0xFF, // fixed as 0xFFFF // cluster 0x01
    0x03, 0x00, // cluster in use, file also uses cluster 0x03 // cluster 0x02
    0xFF, 0xFF, // this is the end of the above file // cluster 0x03
    0xFF, 0xFF  // this file uses only one cluster // cluster 0x04
    // rest of the drive is free, so this is all 0's
};
```

Note that files don't need to occupy contiguous clusters. The example above could have had the first file occupying
clusters 0x02 and 0x04 and the second file occupying 0x03. In that case, the entry at 0x02 would be 0x04 instead.

### Directory

Directories consist of one or more 32 byte entries. The layout is as follows:

```C
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
```

Important attributes are:

* 0x01: Read only
* 0x02: Hidden
* 0x04: System
* 0x08: Volume Label
* 0x10: Directory

Starting cluster should point to the first cluster of the file/folder. For example, if the file starts in cluster 0x05, then set `starting_cluster = 0x05`.

Volume label indicates that this entry has the name of the drive. This entry should be the first one in the root directory. Its
starting cluster and file_size should both be 0.

Subdirectories have the same layout as the root directory, except they can span a variable amount of clusters. A directory entry has a
`file_size` of 0.

Subdirectories' first two entries must be `'.'` and `'..'` pointing to the subdirectory's cluster and 
its parent directory's cluster, respectively. If the parent directory is the root directory, `'..'` should point
to cluster 0.

## Full Struct For FAT Filesystem

```C
#pragma pack(push, 1)
struct boot_sector {
  uint8_t jmp_cmd[3]; // 0xEB, 0x3C, 0x90
  uint8_t oem_name[8]; 
  uint8_t bytes_per_sector[2]; // usually 0x200
  uint8_t sectors_per_cluster; // https://support.microsoft.com/en-us/topic/default-cluster-size-for-ntfs-fat-and-exfat-9772e6f1-e31a-00d7-e18f-73169155af95
  uint8_t reserved_sectors[2]; // sectors before FAT, usually 1
  uint8_t num_fat; // number of File Allocation Tables. Usually 2, but can save space by using 1
  uint8_t max_root_dir_entries[2]; // Max number of entries in the root dir. Each entry takes up 32 bytes
  uint8_t total_sectors[2]; // Total number of sectors
  uint8_t media_descriptor; // 0xF8 for removable media
  uint8_t sectors_per_fat[2]; // Needs to be long enough to hold all data on disk
  uint8_t sectors_per_head[2]; // ignored
  uint8_t heads_per_cylinder[2]; // ignored
  uint8_t hidden_sectors[4]; // set to 0
  uint8_t big_num_sectors[4]; // FAT32 only
  uint8_t bios_drive_number; // 0x80
  uint8_t reserved; // 0x00
  uint8_t ext_boot_sig; // 0x29
  uint8_t serial_number[4];
  uint8_t volume_label[11];
  uint8_t sys_identifier[8]; // FAT12, FAT16 or FAT32
  uint8_t empty[448];
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
#pragma pack(pop)
};
```

## Useful Tools

On Windows, `chkdsk` can be used to check the vaility of the filesystem. `fsck.fat` can be used for the
same use on Linux.

### Common Issues

If these tools talk about the sectors where your subdirectories are being reclaimed or free, one cause
can be that these folders are missing the `'.'` and `'..'` subdirectories.

### Useful Links

https://averstak.tripod.com/fatdox/00dindex.htm