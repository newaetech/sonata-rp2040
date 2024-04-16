#pragma once
// int test_program_bitstream(int interation);
int test_done_program(int iteration);
int test_program_flash(int iteration);
int test_config(int iteration);
int test_crc(int iteration);
int test_basic_flash(int iteration);
// arm-none-eabi-gdb usb_msc\usb_msc.elf -ex "target extended-remote localhost:3333" -ex "load" -ex "monitor reset init" -ex "continue"