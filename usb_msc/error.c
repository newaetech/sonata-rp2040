#include "error.h"
#include "fat_util.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "util.h"

struct directory_entry err_file_entry = {};
// gotta keep track of the file size here, since PCs don't recheck the file size
uint32_t err_file_size = 0;

// todo, CRC for bitstream

int print_err_file(struct fat_filesystem *fs, const char *fmt, ...)
{
    int32_t err = get_file_info(fs, 0, "ERROR", &err_file_entry);
    if (err) return -1;

    uint16_t file_cluster = LE_2U8_TO_U16(err_file_entry.starting_cluster);
    if ((err_file_size >= DISK_CLUSTER_SIZE) || (file_cluster < 2)) return -1;

    uint32_t space_left = DISK_CLUSTER_SIZE - err_file_size;

    uint8_t *data = fs->clusters[file_cluster - 2] + err_file_size;

    va_list args;
    va_start(args, fmt);
    int data_written = vsnprintf(data, space_left, fmt, args);
    va_end(args);
    data_written = min(data_written, space_left - 1);

    err_file_size += data_written;
    memset(data + data_written, ' ', DISK_CLUSTER_SIZE - data_written); // pad file with spaces
    // uint8_t file_size_arr[] = {LE_U32_TO_4U8(file_size)};
    // memcpy(err_file_entry.file_size, file_size_arr, sizeof(file_size_arr));
    // err = write_file_info(fs, 0, "ERROR", &err_file_entry);
    // if (err) return -1;
    return data_written;
}