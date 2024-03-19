#include "fat_util.h"
#include <stdint.h>
#include "util.h"
int is_valid_file(struct directory_entry *entry);

static struct fat_filesystem FILESYSTEM = {
    .boot_sec = {
        .jmp_cmd = {0xEB, 0x3C, 0x90},
        .oem_name = {0x4D, 0x53, 0x44, 0x4F, 0x53, 0x35, 0x2E, 0x30}, // OEM name
        .bytes_per_sector = {LE_U16_TO_2U8(DISK_SECTOR_SIZE)},
        .sectors_per_cluster = DISK_SECTOR_PER_CLUSTER,
        .reserved_sectors = {LE_U16_TO_2U8(0x01)},
        .num_fat = NUM_FAT,
        .max_root_dir_entries = {LE_U16_TO_2U8(NUM_ROOT_DIR_ENTRIES)},
        .total_sectors = {LE_U16_TO_2U8(DISK_REPORT_SECTOR_NUM)},
        .media_descriptor = 0xF8,
        .sectors_per_fat = {LE_U16_TO_2U8(DISK_SECTOR_PER_FAT)},
        .bios_drive_number = 0x80,
        .ext_boot_sig = 0x29,
        .serial_number = {LE_U32_TO_4U8(0x1234)},
        .volume_label = {'S', 'O', 'N', 'A', 'T', 'A', ' ', ' ', ' ', ' ', ' '},
        .sys_identifier = {'F', 'A', 'T', '1', '6', ' ', ' ', ' '},
        .sig = {0x55, 0xAA},
        .sectors_per_head = {32, 0x00},
        .heads_per_cylinder = {0x02, 0x00},
    },
    .fat = {{
                0xF8, 0xFF, // 16 bits for FAT ID , 12 for FAT12, etc. (0xFFF8 for partitioned disk)
                0xFF, 0xFF, // End of chain indicator (reserved cluster?)
                0xFF, 0xFF, // cluster 2 (README.txt in our case)
                0xFF, 0xFF, // cluster 3 (OPTIONS.txt in our case)
                0xFF, 0xFF, // cluster 4 (ERROR.txt in our case)
            }},
    .root_dir = {{
                    .filename = {'S', 'O', 'N', 'A', 'T', 'A', ' ', ' '}, // name
                    .extension = {' ', ' ', ' '},
                    .attribute = FAT_DIR_VOL_LABEL, // volume label
                    .time_stamp = {0x4F, 0x6D},
                    .date_stamp = {0x65, 0x43}
                },
                {
                    .filename = {'R', 'E', 'A', 'D', 'M', 'E', ' ', ' '}, 
                    .extension = {'T', 'X', 'T'}, .creation_time = {0x52, 0x6D},
                    .creation_date = {0x65, 0x43}, .last_access_date = {0x65, 0x43}, 
                    .time_stamp = {0x88, 0x6D}, .date_stamp = {0x65, 0x43}, 
                    .starting_cluster = {0x02, 0x00}, .file_size = {LE_U32_TO_4U8(sizeof(README_CONTENTS) - 1)}
                },
                {
                    .filename = {'O', 'P', 'T', 'I', 'O', 'N', 'S', ' '}, 
                    .extension = {'T', 'X', 'T'}, .creation_time = {0x52, 0x6D},
                    .creation_date = {0x65, 0x43}, .last_access_date = {0x65, 0x43}, 
                    .time_stamp = {0x88, 0x6D}, .date_stamp = {0x65, 0x43}, 
                    .starting_cluster = {0x03, 0x00}, .file_size = {LE_U32_TO_4U8(sizeof(OPTIONS_CONTENTS) - 1)}
                },
                {
                    .filename = {'E', 'R', 'R', 'O', 'R', ' ', ' ', ' '}, 
                    .extension = {'T', 'X', 'T'}, .creation_time = {0x52, 0x6D},
                    .creation_date = {0x65, 0x43}, .last_access_date = {0x65, 0x43}, 
                    .time_stamp = {0x88, 0x6D}, .date_stamp = {0x65, 0x43}, 
                    .attribute = FAT_DIR_READ_ONLY,
                    .starting_cluster = {0x04, 0x00}, .file_size = {LE_U32_TO_4U8(DISK_CLUSTER_SIZE)}
                }
                },
    .clusters = {{README_CONTENTS}, {OPTIONS_CONTENTS}, {}}
};

/*

    Converts a cstring to a fat filesystem string by converting nulls to spaces

    TODO: should memzero fatstr?

*/
int cstr_to_fatstr(char *cstr, uint8_t *fatstr)
{
    if (!fatstr) return -1;
    if (!cstr) return -1;
    strncpy(fatstr, cstr, FAT_NAME_SZ); // copies all 8 characters, may not include NULL

    // convert nulls to spaces
    for (uint8_t i = 0; i < FAT_NAME_SZ; i++) {
        if (!fatstr[i]) {
            fatstr[i] = ' ';
        }
    }
    return 0;
}

/*
    Get a pointer to the filesystem in RAM
*/
struct fat_filesystem *get_filesystem(void)
{
    return &FILESYSTEM;
}

/*
    Get the number of directory entries for a particular cluster

    This is a fixed number for the root cluster and determined by the size of clusters for other clusters
*/
uint16_t get_num_dir_entries(uint16_t cluster)
{
    if (!cluster) return NUM_ROOT_DIR_ENTRIES;
    else          return (DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER) / sizeof(struct directory_entry);

}

/*
    Get a pointer to the directory entries for a given cluster

    If the cluster number is < 2, a pointer to the root dir will be returned

    If the cluster is unallocated, returns NULL
*/
struct directory_entry *cluster_to_dir(struct fat_filesystem *fs, uint16_t cluster)
{
    struct directory_entry *rtn = NULL;
    if (cluster < 2) { // parent cluster being root dir is a special case
        rtn = fs->root_dir;
    }
    if (!rtn) {
        if (!cluster_to_fat_table_val(fs, cluster)) return NULL; // if parent cluster is unallocated, abort
        rtn = fs->directories[cluster - 2];
    }
    return rtn;
}

/*
    Searches a cluster for a file with a given filename

    WARNING: Does not check file extensions
*/
int32_t get_file_index(struct fat_filesystem *fs, uint16_t parent_cluster, char *filename)
{
    uint8_t name_cpy[8];

    struct directory_entry *parent_dir = cluster_to_dir(fs, parent_cluster);
    uint16_t dir_entries = get_num_dir_entries(parent_cluster);
    if (!parent_dir) return -1; // 

    cstr_to_fatstr(filename, name_cpy); // make sure the filename is in FAT str format

    // search for the filename
    int file_index = -1;
    for (uint8_t i = 0; i < dir_entries; i++) {
        if (!memcmp(name_cpy, parent_dir[i].filename, FAT_NAME_SZ)) {
            file_index = i;
            break;
        }
    }
    if (file_index < 0) return -1; // couldn't find that folder

    return file_index;
}

/*
    Get cluster number for a file with name filename. Return -1 if it couldn't be found

    Gives the FAT cluster index instead of the entry in FILESYSTEM.clusters, the latter of which is 2 lower (i.e. it starts at 0 instead of 2)

    Does not check extensions

    Returns the file cluster or -1 if the cluster can't be found
*/
int32_t get_file_cluster(struct fat_filesystem *fs, uint16_t parent_cluster, char *filename)
{
    int32_t file_index = get_file_index(fs, parent_cluster, filename);
    struct directory_entry *parent_dir = cluster_to_dir(fs, parent_cluster);
    if (file_index < 0) return -1; // couldn't find that folder

    // now get the cluster where that folder's at
    uint16_t cluster_num = LE_2U8_TO_U16(parent_dir[file_index].starting_cluster);
    if (cluster_num < 2) return -1; // what....
    if (!cluster_to_fat_table_val(fs, cluster_num)) return -1; // if the file we're looking at is unallocated
    return cluster_num;
}


/*
    Gets the file information for a file with a given filename

    This information is stored in *file_info

    Returns -1 upon error
*/
int32_t get_file_info(struct fat_filesystem *fs, uint16_t parent_cluster, char *filename, struct directory_entry *file_info)
{
    int32_t file_index = get_file_index(fs, parent_cluster, filename);
    struct directory_entry *dir = cluster_to_dir(fs, parent_cluster);
    if ((file_index < 0) || !dir) return -1; // couldn't find that folder
    memcpy(file_info, &dir[file_index], sizeof(struct directory_entry));
    return 0;
}

/*
    Writes file information to a file matching filename in the directory of parent_cluster

    This function does not:
        * Create a file if it does not exist
        * Touch the file table in any way
        * Handle files that span multiple clusters
        * Check that the information in *file_info is valid
*/
int32_t write_file_info(struct fat_filesystem *fs, uint16_t parent_cluster, char *filename, struct directory_entry *file_info)
{
    int32_t file_index = get_file_index(fs, parent_cluster, filename);
    struct directory_entry *dir = cluster_to_dir(fs, parent_cluster);
    if ((file_index < 0) || !dir) return -1; // couldn't find that folder
    memcpy(&dir[file_index], file_info, sizeof(struct directory_entry));
    return 0;
}

/*
    Find out if the cluster in question is in the cluster chain started by starting_cluster

    Returns 1 if the cluster is in the chain, 0 if it isn't, and -1 upon error
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

/*

    Gets the file table entry for a given cluster

*/
uint16_t cluster_to_fat_table_val(struct fat_filesystem *fs, uint16_t cluster_num)
{
    return LE_2U8_TO_U16(fs->fat[0] + cluster_num*2);
}


/*
    get file information for all files in dir_cluster (0 for root directory)

    stores the info in *files, up to a maximum of max_num_files

    returns the number of files found (up to max_num_files) or negative values for errors

    Does not include the . and .. directories

*/
int get_files_in_directory(uint16_t dir_cluster, struct directory_entry *files, uint16_t max_num_files)
{
    struct directory_entry *search_dir = NULL;
    uint16_t max_search = max_num_files;
    uint16_t file_index = 0;
    if (dir_cluster < 2) {
        search_dir = FILESYSTEM.root_dir;
        max_search = NUM_ROOT_DIR_ENTRIES;
    } else {
        search_dir = FILESYSTEM.directories[dir_cluster - 2];
        max_search = sizeof(FILESYSTEM.directories[0]) / sizeof(FILESYSTEM.directories[0][0]);
    }

    for (uint16_t i = 0; i < max_search; i++) {
        if (is_valid_file(search_dir + i)) {
            memcpy(files + file_index, search_dir + i, sizeof(struct directory_entry));
            file_index++;
        }

        if (file_index >= max_num_files)
            break;
    }

    return file_index;

}

/*
    Checks if a given file is valid by checking if it isn't a reserved file (. or ..)
    or if it has a non-zero staring cluster
*/
int is_valid_file(struct directory_entry *entry)
{
    // now we need to check the folder for files
    // these two entries are reserved, so ignore them
    uint8_t reserved_file0[8] = {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    uint8_t reserved_file1[8] = {'.', '.', ' ', ' ', ' ', ' ', ' ', ' '};
    uint8_t null_file[8] = {0}; //empty file entry
    if (!memcmp(entry->filename, reserved_file0, 8)) return 0; // isn't .
    if (!memcmp(entry->filename, reserved_file1, 8)) return 0; // isn't ..
    if (!memcmp(entry->filename, null_file, 8)) return 0; // isn't all null
    if (!LE_2U8_TO_U16(entry->starting_cluster)) return 0; // cluster 0 isn't valid
    // if (!LE_4U8_TO_U32(entry->file_size)) return 0; // filesize of 0 isn't valid

    return 1;
}


/*
    Checks if a given directory_entry is for a folder
*/
int is_folder(struct directory_entry *entry)
{
    return (entry->attribute & FAT_DIR_DIRECTORY) == FAT_DIR_DIRECTORY;
}

/*
    gets the info for the first (non reserved) file or folder in a given folder

    returns 0 for success, -1 if directory is empty
*/
int get_first_file_in_dir(struct fat_filesystem *fs, uint16_t parent_cluster, struct directory_entry *info)
{
    int32_t folder_cluster = parent_cluster;
    if (!fs) return -1;
    if (folder_cluster < 0) return -1;
    if (!cluster_to_fat_table_val(fs, folder_cluster)) return -1; // this cluster is free

    // struct directory_entry *folderptr = fs->directories[folder_cluster - 2];
    struct directory_entry *folderptr = cluster_to_dir(fs, parent_cluster);

    // now we need to check the folder for files
    // these two entries are reserved, so ignore them
    // uint8_t reserved_file0[8] = {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    // uint8_t reserved_file1[8] = {'.', '.', ' ', ' ', ' ', ' ', ' ', ' '};
    // uint8_t null_file[8] = {0}; //empty file entry

    // now parse the entries
    int16_t bitstream_entry = -1;
    for (int16_t i = 0; i < (DISK_SECTOR_SIZE * DISK_SECTOR_PER_CLUSTER) / sizeof(struct directory_entry); i++) {
        if (!is_valid_file(folderptr + i)) continue;
        if (folderptr[i].attribute & FAT_DIR_SYSTEM) continue; // system file...
        if (!LE_4U8_TO_U32(folderptr[i].file_size)) return 0; // filesize of 0 isn't valid
        bitstream_entry = i;
        break;
    }
    if (bitstream_entry == -1) return -1; // if -1, no file is in fpga folder

    memcpy(info, &folderptr[bitstream_entry], sizeof(struct directory_entry));
    return 0;
}

/*
    Fill a directory at cluster_num with the . and .. directories
*/
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