# RP2040 Firmware for Sonata Board

## Usage

### Copying Bitstreams/Firmware

Once plugged in, the Sonata board should enumerate as a USB mass storage device. Bitstreams
and Sonata firmware can both be programmed by copying their respective files to the mass
storage device. By default, bitstreams will be programmed into both flash memory
and the FPGA, while firmware will be programmed into flash memory. Note that
Sonata firmware must be in UF2 format to be detected and programmed into flash.

### Flash Slots

Bitstream and firmware both have 3 flash slots, which can be selected via the 3 position
Bitstream select switch by the USB port on the device. This controls both where
bitstream/firmware are written to when programming, as well as where bitstreams
are loaded from on device boot.

### Logging/Options

Important things such as firmware version, which slots have bitstreams/firmware, etc.
is logged in LOG.TXT. Various options, such as programming speed and whether or not
to write bitstreams to flash can be modified in OPTIONS.TXT.

### Reprogramming/Updating Sonata

The Sonata's firmware can be erased by holding down SW9 while plugging in the USB, after
which the board should enumerate as a different USB mass storage device. New firmware is 
avilable in UF2 format and can be programmed by copying the UF2 file into the drive, similar
to how bitstream and firmware programming works on the Sonata board.

## Getting started

### Linux Setup

TLDR; to setup pico-sdk and build, the quickest way is currently:

```bash
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
```

```bash
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
```

### Windows Setup

Grab the installer from https://github.com/raspberrypi/pico-setup-windows. All the paths
should be setup, so you can omit the `PICO_SDK_PATH` part of the cmake command. Note that
Windows uses ninja instead of make by default.

### Building

```bash
mkdir build
cd build
PICO_SDK_PATH=/path-to/pico-sdk cmake ..
make # or ninja if on Windows
```
## Logging

Different logging levels are available and can be selected with `-DDEBUG_LEVEL=N` where `N` is
between 1 and 5. The levels available are:

1. Critical
1. Error
1. Warning
1. Info
1. Debug

with Info (4) being the default. Logs are written to `LOG.txt`.

## Debugging

### GDB/OpenOCD

Follow the `Building` instructions above, but add `-DCMAKE_BUILD_TYPE=Debug` to the `cmake` command

Start OpenOCD by running:

```bash
openocd -f DEBUG_CONF.cfg -f target/rp2040.cfg
```

Start GDB from the `build` directory:

```bash
cd build
arm-none-eabi-gdb usb_msc/usb_msc.elf -ex "target extended-remote localhost:3333" -ex "load" -ex "monitor reset init"
```

## Testing

Some basic tests can be run by creating testing firmware with cmake: `-DTESTING_BUILD=ON`,
then running either `tests/test_linux.py` or `tests/test_windows.py`, depending on your platform.

Tests should be run after a fresh boot and nothing should be uploaded to the Sonata before running the tests.

## Notes

### Windows Quirks

Windows is pretty lazy with reading new information from disk. This doesn't cause any issues
with programming, but `ERROR.txt` won't be updated until you "eject" the disk by right clicking
the drive in Explorer and clicking `eject`.

### Linux Quirks

Writes to Linux drives are nonblocking by default, meaning copying things to disk returns before
the copy is done.

### Early PCB Rev Notes

Early PCB revisions have a few mistakes that affect running/debugging:

1. On revisions before v0.3, DIN isn't routed to the RP2040, so FPGA programming is unavailable unless a flywire is soldered onto the PCB: https://github.com/newaetech/sonata-pcb/issues/4
1. On revisions before v0.3, a nonstandard SPI flash is used for the RP2040. On these revisions,
you must pass `PICO_DEFAULT_BOOT_STAGE2=boot2_generic_03h` to `cmake` and debuggers are unable
to program SPI flash.