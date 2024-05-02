#include "uf2.h"
#include <stdlib.h>
#include <string.h>

int is_uf2_block(struct UF2_Block *block)
{
    if (block->magicStart0 == UF2_MAGIC_START_0 &&
        block->magicStart1 == UF2_MAGIC_START_1 &&
        block->magicEnd == UF2_MAGIC_END) {
            return 1;
    }
    return 0;
}
int uf2_get_sha(struct UF2_Block *block, struct extension_tag *sha)
{
    // check that an extension is present
    if (!(block->flags & UF2_EXTENSION_TAGS_PRESENT_FLAG)) {
        return -1;
    }

    // navigate to the tag extension past the end of the data (4 byte aligned)
    uint8_t *tag_loc = &block->data[block->payloadSize + (block->payloadSize & 0b11)];
    struct extension_tag *tag_loc_typed = (struct extension_tag *)tag_loc;
    while (tag_loc_typed->ext_type != UF2_EXTENSION_TAG_SHA_2) {
        if (tag_loc_typed->ext_type == 0 && tag_loc_typed->total_size == 0) return -1; // it's over...
        tag_loc = tag_loc + tag_loc_typed->total_size;
        tag_loc_typed = (struct extension_tag *)tag_loc;
    }
    memcpy(sha, tag_loc_typed, tag_loc_typed->total_size);
    return 0;
}

int uf2_is_last_block(struct UF2_Block *block)
{
    if (!is_uf2_block(block)) return 1;
    if (block->blockNo >= (block->numBlocks - 1)) return 1;
    return 0;
}