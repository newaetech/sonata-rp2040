from test_common import xorshift, xor_fill_buf, gen_xorshift_crc32
from crc32c import crc32c

sums = gen_xorshift_crc32()
print("xorshift gen crc32: " + ", ".join("0x{:04X}".format(sum) for sum in sums))

files = ["sonata_top_lrlcd.bit", "sonata-exceptions.bit", "sonata.bit", "usb_msc.uf2"]
for file in files:
    with open(file, "rb") as f:
        data = f.read()
        crc = crc32c(data)
        print("Crc for {} is 0x{:04X}".format(file, crc))