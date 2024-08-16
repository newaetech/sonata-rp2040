import cstruct
import sys
from pathlib import Path

UF2_MAGIC_START_0 = 0xA324655
UF2_MAGIC_START_1 = 0x9E5D5157
UF2_MAGIC_END = 0xAB16F30

class uf2_block(cstruct.MemCStruct):
    __def__ = """
        struct UF2_Block {
            uint32_t magicStart0;
            uint32_t magicStart1;
            uint32_t flags;
            uint32_t targetAddr;
            uint32_t payloadSize;
            uint32_t blockNo;
            uint32_t numBlocks;
            uint32_t fileSize; // or familyID;
            uint8_t data[476];
            uint32_t magicEnd;
        };
    """

if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise ValueError("File path required")
    filepath = Path(sys.argv[1])
    if filepath.suffix != "uf2":
        raise ValueError("File doesn't end in uf2")
    with open(filepath, "rb") as uf2_file:

        uf2_full = uf2_file.read()
        for i in range(len(uf2_file), step=512):
            blk = uf2_block()
            blk.unpack(uf2_full[i:i+512])
            assert blk.magicStart0 == UF2_MAGIC_START_0
            assert blk.magicStart1 == UF2_MAGIC_START_1
            assert blk.magicEnd == UF2_MAGIC_END