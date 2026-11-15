/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_ELF_LOADER_H
#define LIBERUMS_ELF_LOADER_H
#include <stddef.h>
#include <stdint.h>
#include <abi/elf.h>
#define ELF_SEGMENT_LIMIT LIBERUMS_ELF_SEGMENTS_MAX
#define ELF_IMAGE_PAGE_LIMIT LIBERUMS_IMAGE_PAGES_MAX
struct elf_segment {
    uint32_t address, offset, file_size, memory_size, flags;
};
struct elf_plan {
    uint32_t entry, segment_count, page_count;
    struct elf_segment segments[ELF_SEGMENT_LIMIT];
};
/* Pure bounded parser: does not allocate, execute or trust section headers. */
int32_t elf_validate_features(const uint8_t *image, size_t length, struct elf_plan *plan, uint32_t features);
int32_t elf_validate(const uint8_t *image, size_t length, struct elf_plan *plan);
#endif
