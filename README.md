# RP2040 Firmware for Sonata Board

## Getting started with CMAKE

TLDR; to setup pico-sdk and build, the quickest way is currently:

``
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
``

``
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
``

Currently you need to pass `PICO_DEFAULT_BOOT_STAGE2=boot2_generic_03h` to work with the QSPI on 0.1 rev boards.

``
mkdir build2
cd build2
PICO_SDK_PATH=/path-to/pico-sdk cmake -DPICO_DEFAULT_BOOT_STAGE2=boot2_generic_03h ..
make
``

## With Ninja

NB - the ninja file requires changes to build on other systems.

To build, navigate to the `build` folder and run `ninja`.
