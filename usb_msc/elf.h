#pragma once
#include <stdint.h>

//TODO check endianness
#define ELF_MAGIC 0x464c457f

struct elf_header32 {
    uint32_t mag;
    uint8_t eclass; // 1 for 32-bit, 2 for 64-bit
    uint8_t data;
    uint8_t version;
    uint8_t osabi;
    uint8_t abiversion;
    uint8_t pad[7];
    uint8_t type;
    uint16_t machine;
    uint32_t version2;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

// needed?
struct elf_header64 {
    uint32_t mag;
    uint8_t eclass; // 1 for 32-bit, 2 for 64-bit
    uint8_t data;
    uint8_t version;
    uint8_t osabi;
    uint8_t abiversion;
    uint8_t pad[7];
    uint8_t type;
    uint16_t machine;
    uint32_t version2;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct elf_section_header32 {
    uint32_t name;
    uint32_t type;

    uint32_t flags;
    uint32_t addr;
    uint32_t offset;
    uint32_t size;

    uint32_t link;
    uint32_t info;

    uint32_t addralign;
    uint32_t entsize;
};

struct elf_section_header64 {
    uint32_t name;
    uint32_t type;

    uint64_t flags;
    uint64_t addr;
    uint64_t offset;
    uint64_t size;

    uint32_t link;
    uint32_t info;

    uint64_t addralign;
    uint64_t entsize;
};

static inline int is_elf(struct elf_header32 *hdr)
{
    return hdr->mag == ELF_MAGIC;
}