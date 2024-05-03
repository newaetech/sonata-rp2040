import numpy as np
from crc32c import crc32c
import subprocess
import shutil
from time import sleep
import logging

xorshift_state = np.uint32(0x00)
crc_buf = np.array([0]*32, dtype=np.uint32)

def xorshift():
    global xorshift_state
    xorshift_state ^= np.uint32(xorshift_state << 13)
    xorshift_state ^= np.uint32(xorshift_state >> 17)
    xorshift_state ^= np.uint32(xorshift_state << 5)
    return xorshift_state

def xor_fill_buf(buf, seed):
    global xorshift_state
    if (seed):
        xorshift_state = np.uint32(seed)
    for i in range(len(crc_buf)):
        crc_buf[i] = xorshift()
        # print(hex(crc_buf[i]))

    return buf

def get_windows_drive_letter():
    import wmi
    sonata_letter = None
    c = wmi.WMI()
    for drive in c.Win32_LogicalDisk():
        logging.debug(str(drive))
        logging.debug(str(drive.Caption) + str(drive.VolumeName) + str(drive.DriveType))
        if drive.VolumeName == "SONATA":
            sonata_letter = drive.Caption
    return sonata_letter 

def ps_run(file):
    completed = subprocess.run(["powershell", file])

def gen_xorshift_crc32(seeds=[0xDEADBEEF, 0x11223344, 0xF0F0A7A7]):
    sums = []
    for seed in seeds:
        xor_fill_buf(crc_buf, seed)
        crc = crc32c(crc_buf)
        # print("0x{:08X}".format(crc))
        sums.append(crc)
    return sums

valid_options = \
"\
SPI_FPGA_SPEED=7770000\n\
SPI_FLASH_SPEED=5120000\n\
PROG_SPI_FLASH=NO\n\
"

invalid_int_options = \
"\
SPI_FPGA_SPEED=7E70000\n\
SPI_FLASH_SPEED=5120000\n\
PROG_SPI_FLASH=NO\n\
"
invalid_flash_options = \
"\
SPI_FPGA_SPEED=7770000\n\
SPI_FLASH_SPEED=5120000\n\
PROG_SPI_FLASH=N0\n\
"

def copy_sonata_bitstream(sonata_path):
    shutil.copyfile("./sonata.bit", sonata_path + "/sonata.bit")

def copy_firmware(sonata_path):
    shutil.copyfile("./usb_msc.uf2", sonata_path + "/usb_msc.uf2")

def write_options(sonata_path, string):
    option_file = open(sonata_path + "/OPTIONS.txt", "w")
    option_file.write(string)
    logging.debug("Writing")
    logging.debug(string)
    option_file.close()

def write_all_options(sonata_path):
    write_options(sonata_path, invalid_int_options)
    sleep(0.25)
    write_options(sonata_path, invalid_flash_options)
    sleep(0.25)
    write_options(sonata_path, valid_options)
    sleep(0.25)

def get_test_results(sonata_path):
    results = []
    option_file = open(sonata_path + "/LOG.txt", "r")
    string = option_file.read()
    option_file.close()

    for line in string.split('\n'):
        line.strip()
        if "TEST" in line:
            passed = test_passed(line)
            name = get_test_name(line)
            results.append({"string": line, "passed": passed, "name": name})
    return results, string

def win_eject_drive():
    ps_run("./eject.ps1")

def test_passed(test_string):
    if "PASS" in test_string:
        return 1
    else:
        return 0

def get_test_name(test_string):
    strs = test_string.split(':')
    return strs[-1]