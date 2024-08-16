#pragma once
#include <stdarg.h>
#include "fat_util.h"

int print_err_file(struct fat_filesystem *fs, const char *fmt, ...);

extern struct directory_entry err_file_entry;
#define xstr(s) str(s)
#define str(s) #s
#define ERR_FILE_NUM_CLUSTER 1
#define ERR_FILE_SIZE ERR_FILE_NUM_CLUSTER * DISK_CLUSTER_SIZE

// this stuff can take up a lot of our limited file room
#ifdef PRINT_FILE
#define COMPILER_FILEINFO __FILE__ ":" xstr(__LINE__)
#else
#define COMPILER_FILEINFO
#endif


#if DEBUG_LEVEL >= 1
    #define PRINT_CRIT(FMT, ...) print_err_file(get_filesystem(), COMPILER_FILEINFO  "CRIT: " FMT "\r\n", ##__VA_ARGS__)
#else
    #define PRINT_CRIT(FMT, ...)
#endif

#if DEBUG_LEVEL >= 2
    #define PRINT_ERR(FMT, ...) print_err_file(get_filesystem(), COMPILER_FILEINFO  "ERR: " FMT "\r\n", ##__VA_ARGS__)
#else
    #define PRINT_ERR(FMT, ...)
#endif

#if DEBUG_LEVEL >= 3
    #define PRINT_WARN(FMT, ...) print_err_file(get_filesystem(), COMPILER_FILEINFO  "WARN: " FMT "\r\n", ##__VA_ARGS__)
#else
    #define PRINT_WARN(FMT, ...)
#endif

#if DEBUG_LEVEL >= 4
    #define PRINT_INFO(FMT, ...) print_err_file(get_filesystem(), COMPILER_FILEINFO  "INFO: " FMT "\r\n", ##__VA_ARGS__)
#else
    #define PRINT_INFO(FMT, ...)
#endif

#if DEBUG_LEVEL >= 5
    #define PRINT_DEBUG(FMT, ...) print_err_file(get_filesystem(), COMPILER_FILEINFO  "DEBUG: " FMT "\r\n", ##__VA_ARGS__)
#else
    #define PRINT_DEBUG(FMT, ...)
#endif

#ifdef TESTING_BUILD
    extern volatile uint32_t TESTING_COUNTER;
    #define PRINT_TEST(PASSED, NAME, FMT, ...) print_err_file(get_filesystem(), "TEST %.10s %4s " FMT "\r\n", NAME, PASSED ? "PASS" : "FAIL", ##__VA_ARGS__)
#else
    #define PRINT_TEST(PASSED, NAME, FMT, ...)
#endif
