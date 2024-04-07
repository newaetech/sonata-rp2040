import numpy as np
from crc32c import crc32c

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
