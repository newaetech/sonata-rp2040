#include "config.h"
#include "util.h"

#define FPGA_PROG_SPEED_STR "SPI_FPGA_SPEED_KHZ"
#define FLASH_PROG_SPEED_STR "SPI_FLASH_SPEED_KHZ"
#define PROG_FLASH_STR "PROG_SPI_FLASH"

// simplify operations
// static uint8_t conf_buf[DISK_SECTOR_SIZE + 1];

uint8_t *get_config_opt_int(uint8_t *str, enum config_options_int *opt)
{
    if (!str || !opt) return NULL;

    for (; *str; str++) {
        if ((*str == ' ') || (*str == '\n')) continue; // skip spaces or newlines
        if (!strncmp(str, FPGA_PROG_SPEED_STR, sizeof(FPGA_PROG_SPEED_STR))) {
            *opt = CONF_FPGA_PROG_SPEED;
            // str += sizeof(FPGA_PROG_SPEED_STR); //advance past 
            return strchr(str, '='); // return pointer str after =
        }
        if (!strncmp(str, FLASH_PROG_SPEED_STR, sizeof(FLASH_PROG_SPEED_STR))) {
            *opt = CONF_FLASH_PROG_SPEED;
            // str += sizeof(FLASH_PROG_SPEED_STR);
            return strchr(str, '=');
        }
        if (!strncmp(str, PROG_FLASH_STR, sizeof(PROG_FLASH_STR))) {
            *opt = CONF_PROG_FLASH;
            // str += sizeof(PROG_FLASH_STR);
            return strchr(str, '=');
        }
    }

    // didn't find anything, return NULL
    return NULL;
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
        switch (cur_opt) {
            case CONF_FPGA_PROG_SPEED:
                opts->fpga_prog_speed = strtoul(cur_line, NULL, 0);
                break;
            case CONF_FLASH_PROG_SPEED:
                opts->flash_prog_speed = strtoul(cur_line, NULL, 0);
                break;
            case CONF_PROG_FLASH:
                while (*cur_line == ' ') cur_line++; // skip spaces
                if (!memcmp(cur_line, "YES", sizeof("YES")))    opts->prog_flash = true;
                else if (!memcmp(cur_line, "NO", sizeof("NO"))) opts->prog_flash = false;
                break;
            default:
                return -1;

        }
        cur_line = strchr(cur_line, '\n'); // get to next line, or end things
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