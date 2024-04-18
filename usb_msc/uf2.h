#pragma once
#include <stdint.h>

enum uf2_flags {
    UF2_NOT_MAIN_FLASH_FLAG = 0x01,
    UF2_FILE_CONTAINER_FLAG = 0x1000,
    UF2_FAMILYID_PRESENT_FLAG = 0x2000,
    UF2_MD5_CHECKSUM_PRESENT_FLAG = 0x4000,
    UF2_EXTENSION_TAGS_PRESENT_FLAG = 0x8000,
    UF2_EXTENSION_TAG_SHA_2 = 0xB46DB0, // SHA-2 checksum
};
#define UF2_MAGIC_START_0 0xA324655
#define UF2_MAGIC_START_1 0x9E5D5157
#define UF2_MAGIC_END 0xAB16F30

#pragma pack(push, 1)
struct UF2_Block {
    // 32 byte header
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

struct extension_tag {
    uint8_t total_size;
    uint8_t ext_type[3];
    uint8_t data[256-4];
};
#pragma pack(pop)

int is_uf2_block(struct UF2_Block *block);
int uf2_get_sha(struct UF2_Block *block, struct extension_tag *sha);