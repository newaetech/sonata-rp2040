#include "fat_util.h"
#include "util.h"

static struct fat_filesystem FILESYSTEM = {
    .boot_sec = {
        .jmp_cmd = {0xEB, 0x3C, 0x90},
        .oem_name = {0x4D, 0x53, 0x44, 0x4F, 0x53, 0x35, 0x2E, 0x30}, // OEM name
        .bytes_per_sector = {LE_U16_TO_2U8(DISK_CLUSTER_SIZE)},
        .sectors_per_cluster = DISK_SECTOR_PER_CLUSTER,
        .reserved_sectors = {LE_U16_TO_2U8(0x01)},
        .num_fat = NUM_FAT,
        .max_root_dir_entries = {LE_U16_TO_2U8(NUM_ROOT_DIR_ENTRIES)},
        .total_sectors = {LE_U16_TO_2U8(0xFFFF)},
        .media_descriptor = 0xF8,
        .sectors_per_fat = {LE_U16_TO_2U8(DISK_SECTOR_PER_FAT)},
        .bios_drive_number = 0x80,
        .ext_boot_sig = 0x29,
        .serial_number = {LE_U32_TO_4U8(0x1234)},
        .volume_label = {'T', 'i', 'n', 'y', 'U', 'S', 'B', ' ', '0', ' ', ' '},
        .sys_identifier = {'F', 'A', 'T', '1', '6', ' ', ' ', ' '},
        .sig = {0x55, 0xAA},
        .sectors_per_head = {32, 0x00},
        .heads_per_cylinder = {0x02, 0x00},
    },
    .fat = {{
                0xF8, 0xFF, // 16 bits for FAT ID , 12 for FAT12, etc. (0xFFF8 for partitioned disk)
                0xFF, 0xFF, // End of chain indicator (reserved cluster?)
                0xFF, 0xFF, // cluster 2 (README.txt in our case)
                0xFF, 0xFF, // cluster 2 (README.txt in our case)
                0xFF, 0xFF, // cluster 2 (README.txt in our case)
                            // 0xFF, 0xFF, // cluster 3 (FPGA folder in our case)
                            // 0xFF, 0xFF, // cluster 4 (SRAM folder in our case)
            }
            },
    .root_dir = {{.filename = {'T', 'i', 'n', 'y', 'U', 'S', 'B', ' '}, // name
                  .extension = {'0', ' ', ' '},
                  .attribute = 0x08, // volume label
                  .time_stamp = {0x4F, 0x6D},
                  .date_stamp = {0x65, 0x43}},
                 {.filename = {'R', 'E', 'A', 'D', 'M', 'E', ' ', ' '}, 
                 .extension = {'T', 'X', 'T'}, .creation_time = {0x52, 0x6D},
                  .creation_date = {0x65, 0x43}, .last_access_date = {0x65, 0x43}, 
                  .time_stamp = {0x88, 0x6D}, .date_stamp = {0x65, 0x43}, 
                  .starting_cluster = {0x02, 0x00}, .file_size = {LE_U32_TO_4U8(sizeof(README_CONTENTS) - 1)}},
                 {
                     .filename = {'F', 'P', 'G', 'A', ' ', ' ', ' ', ' '},
                     .extension = {' ', ' ', ' '}, // extension
                     .attribute = 0x10,            // directory
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
                     .attribute = 0x10,            // directory
                     .creation_time = {0x52, 0x6D},
                     .creation_date = {0x65, 0x43},
                     .last_access_date = {0x65, 0x43},
                     .time_stamp = {0x88, 0x6D},
                     .date_stamp = {0x65, 0x43},
                     .starting_cluster = {0x04, 0x00},
                 }},
    .clusters = {{README_CONTENTS}, {}, {}}};

// int validate_filesystem(void *memory, uint32_t memlen)
// {
//     if (memlen < sizeof(FILESYSTEM)) return -1; // didn't read enough from flash
//     struct fat_filesystem *flash_filesystem = memory;

//     if (memcmp(&flash_filesystem->boot_sec, &FILESYSTEM.boot_sec, sizeof(FILESYSTEM.boot_sec))) {
//         return -2; // boot sec doesn't match
//     }

//     uint8_t file_table_start[] = {0xF8, 0xFF, 0xFF, 0xFF};
//     if (memcmp(&flash_filesystem->fat, file_table_start, sizeof(file_table_start))) {
//         return -3; // FAT probably corrupted
//     }
// }

int cstr_to_fatstr(char *cstr, uint8_t *fatstr)
{
    if (!fatstr) return -1;
    if (!cstr) return -1;
    strncpy(fatstr, cstr, 8);
    // convert nulls to spaces
    for (uint8_t i = 0; i < 8; i++) {
        if (!fatstr[i]) {
            fatstr[i] = ' ';
        }
    }
    return 0;
}

struct fat_filesystem *get_filesystem(void)
{
    return &FILESYSTEM;
}

/*
    Get cluster number for a file with name filename. Return -1 if it couldn't be found

    Gives the FAT cluster index instead of the entry in FILESYSTEM.clusters, the latter of which is 2 lower (i.e. it starts at 0 instead of 2)
*/
int32_t get_file_cluster(struct fat_filesystem *fs, uint16_t parent_cluster, char *filename)
{
    uint8_t name_cpy[8];
    cstr_to_fatstr(filename, name_cpy); // make sure the filename is in FAT str format
    uint8_t dir_entries = (DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER) / sizeof(struct directory_entry);
    struct directory_entry *parent_dir = NULL;
    if (!parent_cluster) { // parent cluster being root dir is a special case
        parent_dir = fs->root_dir;
        dir_entries = NUM_ROOT_DIR_ENTRIES;
    }
    if (!parent_dir) {
        if (!cluster_to_fat_table_val(fs, parent_cluster)) return -1; // if parent cluster is unallocated, abort
        parent_dir = fs->directories[parent_cluster - 2];
    }

    // search for the filename
    int file_index = -1;
    for (uint8_t i = 0; i < dir_entries; i++) {
        if (!memcmp(name_cpy, parent_dir[i].filename, 8)) {
            file_index = i;
            break;
        }
    }
    if (file_index < 0) return -1; // couldn't find that folder

    // now get the cluster where that folder's at
    uint16_t cluster_num = LE_2U8_TO_U16(parent_dir[file_index].starting_cluster);
    if (cluster_num < 2) return -1; // what....
    if (!cluster_to_fat_table_val(fs, cluster_num)) return -1; // if the file we're looking at is unallocated
    return cluster_num;
}

/*
    Find out if the cluster in question is in the cluster chain started by starting_cluster
*/
int is_cluster_in_chain(struct fat_filesystem *fs, uint16_t starting_cluster, uint16_t ciq)
{
    if (!fs) return -1;
    if (starting_cluster < 2) return 0; // first two cluster are reserved
    while (starting_cluster != 0xFFFF) {
        if (ciq == starting_cluster) return 1;
        starting_cluster = cluster_to_fat_table_val(fs, starting_cluster);
    }
    return 0;
}

uint16_t cluster_to_fat_table_val(struct fat_filesystem *fs, uint16_t cluster_num)
{
    return LE_2U8_TO_U16(fs->fat[0] + cluster_num*2);
}

/*
    gets the info for the first (non reserved) file or folder in a given folder
*/
int get_first_file_in_dir(struct fat_filesystem *fs, uint16_t parent_cluster, struct directory_entry *info)
{
    int32_t folder_cluster = parent_cluster;
    if (!fs) return -1;
    if (folder_cluster < 0) return -1;
    if (!cluster_to_fat_table_val(fs, folder_cluster)) return -1; // this cluster is free

    struct directory_entry *folderptr = fs->directories[folder_cluster - 2];

    // now we need to check the folder for files
    // these two entries are reserved, so ignore them
    uint8_t reserved_file0[8] = {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    uint8_t reserved_file1[8] = {'.', '.', ' ', ' ', ' ', ' ', ' ', ' '};
    uint8_t null_file[8] = {0}; //empty file entry

    // now parse the entries
    int16_t bitstream_entry = -1;
    for (int16_t i = 0; i < (DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER) / sizeof(struct directory_entry); i++) {
        if (!memcmp(folderptr[i].filename, reserved_file0, 8)) continue; // isn't .
        if (!memcmp(folderptr[i].filename, reserved_file1, 8)) continue; // isn't ..
        if (!memcmp(folderptr[i].filename, null_file, 8)) continue; // isn't all null
        if (!LE_2U8_TO_U16(folderptr[i].starting_cluster)) continue; // cluster 0 isn't valid
        if (!LE_4U8_TO_U32(folderptr[i].file_size)) continue; // filesize of 0 isn't valid
        if (folderptr[i].attribute & 0x04) continue; // system file...
        bitstream_entry = i;
        break;
    }
    if (bitstream_entry == -1) return -1; // if -1, no file is in fpga folder

    memcpy(info, &folderptr[bitstream_entry], sizeof(struct directory_entry));
    return 0;
}

void dir_fill_req_entries(uint16_t cluster_num, uint16_t parent_cluster)
{
    if (cluster_num < 2)
    {
        return;
    }
    // '.' file
    FILESYSTEM.directories[cluster_num - 2][0] = (struct directory_entry){
        .filename = {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
        .extension = {' ', ' ', ' '}, // extension
        .attribute = 0x10,            // directory
        .creation_time = {0x52, 0x6D},
        .creation_date = {0x65, 0x43},
        .last_access_date = {0x65, 0x43},
        .time_stamp = {0x88, 0x6D},
        .date_stamp = {0x65, 0x43},
        .starting_cluster = LE_U16_TO_2U8(cluster_num)};
    // '..' file
    FILESYSTEM.directories[cluster_num - 2][1] = (struct directory_entry){
        .filename = {'.', '.', ' ', ' ', ' ', ' ', ' ', ' '},
        .extension = {' ', ' ', ' '}, // extension
        .attribute = 0x10,            // directory
        .creation_time = {0x52, 0x6D},
        .creation_date = {0x65, 0x43},
        .last_access_date = {0x65, 0x43},
        .time_stamp = {0x88, 0x6D},
        .date_stamp = {0x65, 0x43},
        .starting_cluster = LE_U16_TO_2U8(parent_cluster)};
}