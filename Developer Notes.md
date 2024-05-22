## Things This Firmware Does

### Handling Bitstreams/Firmware

This firmware enumerates as a FAT16 USB mass storage device and allows programming of both
bitstreams to the onboard FPGA/SPI flash and UF2 files into the second SPI flash.

The most important function for that is `tud_msc_write10_cb()` in `msc_disk.c`, which is called
when the PC writes data to the disk. Most of the drive is "fake" (i.e. the file can't be read after
being written).

In practice, writes are typically the smaller of `CFG_TUD_MSC_EP_BUFSIZE` and the length of the remaining file.
Writes to folder clusters (the file information), the file table, and the actual file data can be done in any order
and this varies by operation system. For example, Windows will write file information before file data, but this
is the opposite on Linux. Technically, data writes can be out of order, but this doesn't appear to happen in practice.

Various FAT utility functions (such as writing to files, getting file info, etc) are in `fat_util.c`
and flash functions are in `flash_util.c`. A CRC32C is used to verify the data written to flash.

See `FAT Filesystem.md` for more information about the filesystem.

### Config File

This firmware includes a config file (`CONFIG.txt`) that appears in the root directory of the filesystem. Code for parsing
this file is in `config.c`. Current options include SPI frequency and whether or not to save bitstreams to SPI flash.

### Logging

A logging file (`LOG.txt`) is also included.
Different logging levels are available and can be selected with `-DDEBUG_LEVEL=N` where `N` is between 1 and 5. The levels available are:

1. Critical
1. Error
1. Warning
1. Info
1. Debug

with Info (4) being the default. Handling of the log file is done in `error.c` and `error.h`.

###  Tests

Some basic tests can be run by creating testing firmware with cmake: `-DTESTING_BUILD=ON`,
then running either `tests/test_linux.py` or `tests/test_windows.py`, depending on your platform.

Tests should be run after a fresh boot and nothing should be uploaded to the Sonata before running the tests.

Some tests are done via functions in `tests.c`, but others are done inline in the code. Tests will be recorded
in `LOG.txt` and in the form `"TEST %lu %4s: " + STR` where `%lu` is an incrementing integer, `%4s` is either
`"PASS"` or `"FAIL"` and `STR` gives additional context for the test.