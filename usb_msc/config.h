#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <fat_util.h>

enum config_defaults {
    CONF_DEFAULT_FPGA_PROG_SPEED = (int)5E6,
    CONF_DEFAULT_FLASH_PROG_SPEED = (int)5E6,
    CONF_DEFAULT_PROG_FLASH = false
};

#define MAX_CONFIG_NAME_LEN 32

struct config_options {
    uint32_t fpga_prog_speed;
    uint32_t flash_prog_speed;
    bool prog_flash;
    bool dirty; // note think about how to do this
};

enum config_options_int {
    CONF_UNKNOWN,
    CONF_FPGA_PROG_SPEED,
    CONF_FLASH_PROG_SPEED,
    CONF_PROG_FLASH
};

int parse_config(struct fat_filesystem *fs, struct config_options *opts);
void set_default_config(struct config_options *opts);
int write_config_to_file(struct fat_filesystem *fs, struct config_options *opts);
// int read_config(struct fat_filesystem *fs, void *mem, uint32_t len);