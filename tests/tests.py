import numpy as np
from crc32c import crc32c
import subprocess
import shutil
import wmi
from time import sleep

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

for seed in [0xDEADBEEF, 0x11223344, 0xF0F0A7A7]:
    xor_fill_buf(crc_buf, seed)
    crc = crc32c(crc_buf)
    print("0x{:08X}".format(crc))


def ps_run(file):
    completed = subprocess.run(["powershell", file])

# shutil.copyfile("")
# ps_run("./eject.ps1")
sonata_letter = None
c = wmi.WMI()
for drive in c.Win32_LogicalDisk():
    print(drive)
    print(drive.Caption, drive.VolumeName, drive.DriveType)
    if drive.VolumeName == "SONATA":
        sonata_letter = drive.Caption

if not sonata_letter:
    print("Sonata drive not found")
    exit(1)

print("Sonata at " + sonata_letter)
print("Copying sonata.bit...")
shutil.copyfile("./sonata.bit", sonata_letter + "\sonata.bit")

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

def write_options(string):
    option_file = open(sonata_letter + "\OPTIONS.txt", "w")
    option_file.write(string)
    print("Writing")
    print(string)
    option_file.close()

print ("Done copy. Testinging config write...")
write_options(invalid_int_options)
sleep(0.25)
write_options(invalid_flash_options)
sleep(0.25)
write_options(valid_options)
sleep(0.25)

print("Done. Ejecting drive...")
sleep(0.25)
ps_run("./eject.ps1")

def get_test_results():
    test_lines = []
    option_file = open(sonata_letter + "\ERROR.txt", "r")
    string = option_file.read()
    option_file.close()

    for line in string.split('\n'):
        line.strip()
        if "TEST" in line:
            test_lines.append(line)
    return test_lines, string

results, full_file = get_test_results()
print(results)
print(full_file)