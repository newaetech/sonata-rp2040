#pragma once
#define min(X, Y) (((X) > (Y)) ? (Y) : (X))
#define max(X, Y) (((X) > (Y)) ? (X) : (Y))

#define LE_U16_TO_2U8(X) ((X) & 0xFF), (((X) >> 8) & 0xFF)
#define LE_U32_TO_4U8(X) ((X) & 0xFF), (((X) >> 8) & 0xFF), (((X) >> 16) & 0xFF), (((X) >> 24) & 0xFF)

#define BE_U16_TO_2U8(X) (((X) >> 8) & 0xFF), ((X) & 0xFF)
#define BE_U32_TO_4U8(X) (((X) >> 24) & 0xFF), (((X) >> 16) & 0xFF), (((X) >> 8) & 0xFF), ((X) & 0xFF)

#define LE_4U8_TO_U32(X) ((X)[0] | ((X)[1] << 8) | ((X)[2] << 16) | ((X)[3] << 24))
#define LE_2U8_TO_U16(X) ((X)[0] | ((X)[1] << 8))

#define BE_4U8_TO_U32(X) ((X)[3] | ((X)[2] << 8) | ((X)[1] << 16) | ((X)[0] << 24))
#define BE_2U8_TO_U16(X) ((X)[1] | ((X)[0] << 8))