#include <stdio.h>
#include "config.h"
#include "util.h"

#define FPGA_PROG_SPEED_STR "SPI_FPGA_SPEED"
#define FLASH_PROG_SPEED_STR "SPI_FLASH_SPEED"
#define PROG_FLASH_STR "PROG_SPI_FLASH"

// simplify operations
// static uint8_t conf_buf[DISK_SECTOR_SIZE + 1];

uint8_t *get_config_opt_int(uint8_t *str, enum config_options_int *opt)
{
    if (!str || !opt) return NULL;
    int cmp = 0;

    for (; *str; str++) {
        if ((*str == ' ') || (*str == '\n') || (*str == '\r')) continue; // skip spaces or newlines

        // NOTE: need sizeof() - 1, since string size includes null terminator, which won't be there in str
        if (cmp = strncmp(str, FPGA_PROG_SPEED_STR, sizeof(FPGA_PROG_SPEED_STR) - 1), !cmp) {
            *opt = CONF_FPGA_PROG_SPEED;
            // str += sizeof(FPGA_PROG_SPEED_STR); //advance past 
            return strchr(str, '='); // return pointer str after =
        }
        if (cmp = strncmp(str, FLASH_PROG_SPEED_STR, sizeof(FLASH_PROG_SPEED_STR) - 1), !cmp) {
            *opt = CONF_FLASH_PROG_SPEED;
            // str += sizeof(FLASH_PROG_SPEED_STR);
            return strchr(str, '=');
        }
        if (cmp = strncmp(str, PROG_FLASH_STR, sizeof(PROG_FLASH_STR) - 1), !cmp) {
            *opt = CONF_PROG_FLASH;
            // str += sizeof(PROG_FLASH_STR);
            return strchr(str, '=');
        }
    }

    // didn't find anything, return NULL
    return NULL;
}

int write_config_to_file(struct fat_filesystem *fs, struct config_options *opts)
{
    struct directory_entry info;
    int32_t err = get_file_info(fs, 0, "OPTIONS", &info);
    if (err) return -1;

    uint16_t file_cluster = LE_2U8_TO_U16(info.starting_cluster);
    // uint32_t file_size = LE_4U8_TO_U32(info.file_size);
    if (file_cluster < 2) return -1;
    uint8_t *data = fs->clusters[file_cluster - 2];
    char flash_str_opts[][4] = {"NO", "YES"};

    // TODO: maybe make this more automated in the future?
    int file_size = snprintf(data, DISK_SECTOR_SIZE - 1, 
        "%s=%lu\r\n"\
        "%s=%lu\r\n"\
        "%s=%s\r\n",
        FPGA_PROG_SPEED_STR, opts->fpga_prog_speed,
        FLASH_PROG_SPEED_STR, opts->flash_prog_speed,
        PROG_FLASH_STR, flash_str_opts[opts->prog_flash]
    );

    uint8_t file_size_arr[] = {LE_U32_TO_4U8(file_size)};
    memcpy(info.file_size, file_size_arr, sizeof(file_size_arr));
    err = write_file_info(fs, 0, "OPTIONS", &info);
    if (err) return -1;
    return 0;
}

/*
    Parses config file in root directory of fs

    Max size of this config file currently the size of one cluster - 1, will return -1 if it is larger
*/
int parse_config(struct fat_filesystem *fs, struct config_options *opts)
{
    struct directory_entry info;
    int32_t err = get_file_info(fs, 0, "OPTIONS", &info);
    if (err) return -1;

    uint16_t file_cluster = LE_2U8_TO_U16(info.starting_cluster);
    uint32_t file_size = LE_4U8_TO_U32(info.file_size);

    enum config_options_int cur_opt;

    // only handle one cluster at a time for now
    if ((file_cluster < 2) || (!file_size) || (file_size >= DISK_SECTOR_SIZE)) return -1;

    // memcpy(conf_buf, fs->clusters[file_cluster - 2], file_size);
    // conf_buf[file_size] = '\0'; // make this a null terminated string

    uint8_t *cur_line = fs->clusters[file_cluster - 2];
    cur_line[file_size] = '\0'; //ensure buffer is null terminated, should be okay if len < DISK_SECTOR_SIZE
    // char *next_line = strchr(confbuf, '\n');

    while (cur_line) {
        cur_line = get_config_opt_int(cur_line, &cur_opt);
        if (!cur_line) break; // all done
        cur_line++; //advance beyond '='
        switch (cur_opt) {
            case CONF_FPGA_PROG_SPEED:
                opts->fpga_prog_speed = strtoul(cur_line, NULL, 0);
                break;
            case CONF_FLASH_PROG_SPEED:
                opts->flash_prog_speed = strtoul(cur_line, NULL, 0);
                break;
            case CONF_PROG_FLASH:
                while (*cur_line == ' ') cur_line++; // skip spaces
                if (!memcmp(cur_line, "YES", sizeof("YES") - 1))    opts->prog_flash = true;
                else if (!memcmp(cur_line, "NO", sizeof("NO") - 1)) opts->prog_flash = false;
                break;
            default:
                return -1;

        }
        cur_line = strpbrk(cur_line, "\r\n"); // get to next line, or end things

    }
    opts->dirty = 0;
    return 0;
}

void set_default_config(struct config_options *opts)
{
    opts->dirty = 1;
    opts->flash_prog_speed = CONF_DEFAULT_FLASH_PROG_SPEED;
    opts->fpga_prog_speed = CONF_DEFAULT_FPGA_PROG_SPEED;
    opts->prog_flash = CONF_DEFAULT_PROG_FLASH;
}