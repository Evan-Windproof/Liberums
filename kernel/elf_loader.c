/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/elf_loader.h>

static uint16_t read16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}
static uint32_t read32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}
static int32_t validate_note(const uint8_t *note, uint32_t length)
{
    static const char expected_name[12] = "LIBERUMS";
    if (length != 36 || read32(note) != 9 || read32(note + 4) != 12
        || read32(note + 8) != LIBERUMS_NOTE_TYPE) {
        return RES_ENOEXEC;
    }
    for (uint32_t i = 0; i < 12; ++i) {
        if (note[12 + i] != (uint8_t)expected_name[i]) {
            return RES_ENOEXEC;
        }
    }
    if (!liberums_abi_compatible(LIBERUMS_ABI_VERSION, LIBERUMS_FEATURES,
                                 read32(note + 24), read32(note + 28), read32(note + 32))) {
        return RES_EPROTONOSUPPORT;
    }
    return 0;
}

static int32_t add_segment(const uint8_t *header, size_t length, struct elf_plan *plan)
{
    uint32_t offset = read32(header + 4);
    uint32_t address = read32(header + 8);
    uint32_t file_size = read32(header + 16);
    uint32_t memory_size = read32(header + 20);
    uint32_t flags = read32(header + 24);
    uint32_t alignment = read32(header + 28);
    if (plan->segment_count == ELF_SEGMENT_LIMIT || memory_size == 0 || file_size > memory_size
        || offset > length || file_size > length - offset
        || address < LIBERUMS_IMAGE_BASE || address >= LIBERUMS_IMAGE_END
        || memory_size > LIBERUMS_IMAGE_END - address) {
        return RES_ENOEXEC;
    }
    /* Strict profile: readable segments, no unknown permissions or explicit W+X.
     * Page-rounded overlap is rejected rather than silently merging permissions. */
    if (!(flags & 4) || (flags & ~7u) != 0 || (flags & 3) == 3
        || alignment > 4096 || (alignment > 1 && (alignment & (alignment - 1)) != 0)
        || (offset & 4095) != (address & 4095)
        || (alignment > 1 && (offset % alignment) != (address % alignment))) {
        return RES_ENOEXEC;
    }
    uint32_t begin = address & ~4095u;
    uint32_t end = (address + memory_size + 4095) & ~4095u;
    for (uint32_t i = 0; i < plan->segment_count; ++i) {
        const struct elf_segment *other = &plan->segments[i];
        uint32_t other_begin = other->address & ~4095u;
        uint32_t other_end = (other->address + other->memory_size + 4095) & ~4095u;
        if (begin < other_end && other_begin < end) {
            return RES_ENOEXEC;
        }
    }
    uint32_t pages = (end - begin) / 4096;
    if (pages > ELF_IMAGE_PAGE_LIMIT - plan->page_count) {
        return RES_ENOEXEC;
    }
    plan->segments[plan->segment_count++] = (struct elf_segment){address, offset, file_size, memory_size, flags};
    plan->page_count += pages;
    return 0;
}

int32_t elf_validate(const uint8_t *image, size_t length, struct elf_plan *plan)
{
    if (plan == NULL) {
        return RES_ENOEXEC;
    }
    plan->segment_count = plan->page_count = plan->entry = 0;
    if (image == NULL || length < 52 || length > LIBERUMS_ELF_FILE_LIMIT) {
        return RES_ENOEXEC;
    }
    if (image[0] != 0x7f || image[1] != 'E' || image[2] != 'L' || image[3] != 'F'
        || image[4] != 1 || image[5] != 1 || image[6] != 1 || image[7] != 0 || image[8] != 0
        || read16(image + 16) != 2 || read16(image + 18) != 3 || read32(image + 20) != 1
        || read32(image + 36) != 0 || read16(image + 40) != 52 || read16(image + 42) != 32) {
        return RES_ENOEXEC;
    }
    uint32_t phoff = read32(image + 28);
    uint32_t count = read16(image + 44);
    if (phoff < 52 || phoff > length || (phoff & 3) != 0 || count == 0 || count > 16
        || count > (length - phoff) / 32) {
        return RES_ENOEXEC;
    }
    /* No unchecked casts to packed or potentially unaligned input. */
    uint32_t notes = 0;
    int32_t error = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *header = image + phoff + i * 32;
        uint32_t type = read32(header);
        if (type == 1) {
            error = add_segment(header, length, plan);
        } else if (type == 4) {
            uint32_t offset = read32(header + 4), size = read32(header + 16);
            if (++notes != 1 || offset > length || size > length - offset) {
                error = RES_ENOEXEC;
            } else {
                error = validate_note(image + offset, size);
            }
        } else if (type != 0) {
            error = RES_ENOEXEC; /* No INTERP, DYNAMIC, TLS or other implicit features. */
        }
        if (error != 0) {
            goto invalid;
        }
    }
    uint32_t entry = read32(image + 24);
    if (notes != 1 || plan->segment_count == 0) {
        error = RES_ENOEXEC;
        goto invalid;
    }
    for (uint32_t i = 0; i < plan->segment_count; ++i) {
        const struct elf_segment *segment = &plan->segments[i];
        if ((segment->flags & 1) && entry >= segment->address
            && entry - segment->address < segment->file_size) {
            plan->entry = entry;
            return 0;
        }
    }
    error = RES_ENOEXEC;
invalid:
    plan->segment_count = plan->page_count = plan->entry = 0;
    return error;
}
