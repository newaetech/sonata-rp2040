# RP2040 Firmware for Sonata Board

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