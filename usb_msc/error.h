#pragma once
#include <stdarg.h>
#include "fat_util.h"

int print_err_file(struct fat_filesystem *fs, const char *fmt, ...);

extern struct directory_entry err_file_entry;