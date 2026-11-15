/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/memory_map.h>

/* Byte reads avoid alignment assumptions about firmware records. */
static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64(const uint8_t *bytes)
{
    return read_u32(bytes) | ((uint64_t)read_u32(bytes + 4) << 32);
}

bool memory_map_parse(const void *bytes, size_t length, struct memory_map *map)
{
    if (map == NULL) {
        return false;
    }
    map->count = 0;
    if (bytes == NULL || length == 0) {
        return false;
    }
    const uint8_t *cursor = bytes;
    size_t remaining = length;
    while (remaining != 0) {
        if (remaining < 4) {
            goto invalid;
        }
        uint32_t payload_size = read_u32(cursor);
        /* The length excludes its own four bytes. Subtraction avoids wrapping. */
        if (payload_size < 20 || payload_size > remaining - 4
            || map->count == MEMORY_REGION_MAX) {
            goto invalid;
        }
        uint64_t begin = read_u64(cursor + 4);
        uint64_t size = read_u64(cursor + 12);
        if (size == 0 || size > UINT64_MAX - begin) {
            goto invalid;
        }
        struct memory_region *region = &map->regions[map->count++];
        region->begin = begin;
        region->end = begin + size;
        region->type = read_u32(cursor + 20);
        size_t record_size = (size_t)payload_size + 4;
        cursor += record_size;
        remaining -= record_size;
    }
    return true;
invalid:
    map->count = 0; /* Never expose a partially accepted map. */
    return false;
}

bool memory_map_contains_ram(const struct memory_map *map, uint64_t begin, uint64_t end)
{
    if (map == NULL || map->count > MEMORY_REGION_MAX || begin >= end) {
        return false;
    }
    /* A reserved overlap always wins, regardless of record order. */
    for (size_t i = 0; i < map->count; ++i) {
        const struct memory_region *r = &map->regions[i];
        if (r->type != 1 && begin < r->end && r->begin < end) {
            return false;
        }
    }
    uint64_t covered = begin;
    while (covered < end) {
        uint64_t next = covered;
        for (size_t i = 0; i < map->count; ++i) {
            const struct memory_region *r = &map->regions[i];
            if (r->type == 1 && r->begin <= covered && r->end > next) {
                next = r->end;
            }
        }
        if (next == covered) {
            return false;
        }
        covered = next;
    }
    return true;
}
