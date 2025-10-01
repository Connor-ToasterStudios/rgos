#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

typedef struct {
    uint32_t *base;
    uint64_t width;
    uint64_t height;
    uint64_t pixelsPerScanLine;
} Framebuffer;

#endif
