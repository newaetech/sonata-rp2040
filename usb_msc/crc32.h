#pragma once
#include <stdint.h>
#include <stdlib.h>
// uint32_t crc32c(uint32_t crc, const void *buf, size_t size);
uint32_t crc32c(uint32_t crc, const uint8_t *data, unsigned int length);