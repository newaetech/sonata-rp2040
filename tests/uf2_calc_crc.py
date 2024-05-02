from crc32c import crc32c

with open("usb_msc.uf2", "rb") as f:
    data = f.read()
    # print(data)
    crc = crc32c(data)
    print(hex(crc))