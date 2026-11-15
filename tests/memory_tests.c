/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/memory_map.h>
#include <kernel/physical_pages.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition) do { ++checks; assert(condition); } while (0)

static void put32(uint8_t *data, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        data[i] = (uint8_t)(value >> (8 * i));
    }
}

static void record(uint8_t *data, uint32_t size, uint64_t begin, uint64_t length,
                   uint32_t type)
{
    put32(data, size);
    put32(data + 4, (uint32_t)begin);
    put32(data + 8, (uint32_t)(begin >> 32));
    put32(data + 12, (uint32_t)length);
    put32(data + 16, (uint32_t)(length >> 32));
    put32(data + 20, type);
}

static void test_map(void)
{
    struct memory_map map;
    uint8_t data[28 + 24 * (MEMORY_REGION_MAX + 1)] = {0};
    record(data, 24, 0x100000, 0x200000, 1); /* Includes four extension bytes. */
    record(data + 28, 20, UINT64_C(0x100000000), 0x2000, 7);
    CHECK(memory_map_parse(data, 52, &map));
    CHECK(map.count == 2 && map.regions[1].begin == UINT64_C(0x100000000));
    CHECK(map.regions[1].type == 7); /* Unknown types are not usable RAM. */
    CHECK(memory_map_contains_ram(&map, 0x100000, 0x300000));
    CHECK(!memory_map_contains_ram(&map, 0x100000, 0x300001));
    CHECK(!memory_map_parse(data, 51, &map) && map.count == 0);
    CHECK(!memory_map_parse(data, 3, &map));
    CHECK(!memory_map_parse(NULL, 24, &map));
    CHECK(!memory_map_parse(data, 0, &map));
    CHECK(!memory_map_parse(data, 24, NULL));
    record(data, 19, 0, 0x1000, 1);
    CHECK(!memory_map_parse(data, 24, &map));
    record(data, UINT32_MAX, 0, 0x1000, 1);
    CHECK(!memory_map_parse(data, 24, &map));
    record(data, 20, UINT64_MAX - 0x100, 0x200, 1);
    CHECK(!memory_map_parse(data, 24, &map));
    record(data, 20, 0, 0, 1);
    CHECK(!memory_map_parse(data, 24, &map));
    for (unsigned i = 0; i <= MEMORY_REGION_MAX; ++i) {
        record(data + i * 24, 20, i * 0x1000, 0x1000, 1);
    }
    CHECK(memory_map_parse(data, 24 * MEMORY_REGION_MAX, &map));
    CHECK(!memory_map_parse(data, 24 * (MEMORY_REGION_MAX + 1), &map));
    CHECK(map.count == 0);
    map.count = 3;
    map.regions[0] = (struct memory_region){0x2000, 0x4000, 1};
    map.regions[1] = (struct memory_region){0x1000, 0x2000, 1};
    map.regions[2] = (struct memory_region){0x2800, 0x2900, 2};
    CHECK(memory_map_contains_ram(&map, 0x1000, 0x2800));
    CHECK(!memory_map_contains_ram(&map, 0x1000, 0x3000));
    puts("PASS: variable-size map, bounds, overflow, capacity, gaps and reserved overlaps");
}

static void test_pages(void)
{
    struct memory_map map;
    map.count = 5;
    map.regions[0] = (struct memory_region){0, 0x800000, 1};
    map.regions[1] = (struct memory_region){0x310001, 0x311001, 2};
    map.regions[2] = (struct memory_region){0x900001, 0x9fffff, 1};
    map.regions[3] = (struct memory_region){UINT64_C(0x100000000), UINT64_C(0x100010000), 1};
    map.regions[4] = (struct memory_region){0x3f00000, 0x4400000, 1};
    const struct physical_range reserved[] = {{0x20000c, 0x20001e}, {0x380000, 0x382000}};
    uint32_t address;
    CHECK(!physical_page_alloc(&address));
    CHECK(!physical_pages_init(&map, reserved, 2, 0x100000, PHYSICAL_LIMIT + 1));
    CHECK(physical_pages_init(&map, reserved, 2, 0x100000, 0x180000));
    CHECK(!physical_pages_init(&map, reserved, 2, 0x100000, 0x180000));
    CHECK(physical_page_state(0x500) == PAGE_RESERVED);
    CHECK(physical_page_state(0xa0000) == PAGE_RESERVED);
    CHECK(physical_page_state(0x100000) == PAGE_KERNEL);
    CHECK(physical_page_state(0x200000) == PAGE_RESERVED);
    CHECK(physical_page_state(0x310000) == PAGE_RESERVED);
    CHECK(physical_page_state(0x311000) == PAGE_RESERVED);
    CHECK(physical_page_state(0x380000) == PAGE_RESERVED);
    CHECK(physical_page_state(0x800000) == PAGE_UNAVAILABLE);
    CHECK(physical_page_state(0x900000) == PAGE_UNAVAILABLE);
    CHECK(physical_page_state(0x9ff000) == PAGE_UNAVAILABLE);
    CHECK(physical_page_state(PHYSICAL_LIMIT) == PAGE_UNAVAILABLE);
    CHECK(!physical_page_free(0x100000));
    CHECK(!physical_page_free(0x200000));
    CHECK(!physical_page_alloc(NULL));
    static bool seen[PHYSICAL_LIMIT / PAGE_SIZE];
    uint32_t initial = physical_pages_free_count();
    uint32_t allocated = 0;
    while (physical_page_alloc(&address)) {
        CHECK(address % PAGE_SIZE == 0 && address < PHYSICAL_LIMIT);
        CHECK(!seen[address / PAGE_SIZE]);
        CHECK(memory_map_contains_ram(&map, address, (uint64_t)address + PAGE_SIZE));
        CHECK(physical_page_state(address) == PAGE_ALLOCATED);
        seen[address / PAGE_SIZE] = true;
        ++allocated;
    }
    CHECK(allocated == initial && initial != 0 && physical_pages_free_count() == 0);
    CHECK(!physical_page_alloc(&address));
    CHECK(!physical_page_free(0x180001));
    CHECK(physical_page_free(0x180000));
    CHECK(!physical_page_free(0x180000));
    CHECK(physical_page_alloc(&address) && address == 0x180000);
    CHECK(physical_pages_free_count() == 0);
    for (uint32_t page = 0; page < PHYSICAL_LIMIT / PAGE_SIZE; ++page) {
        if (seen[page]) {
            CHECK(physical_page_free(page * PAGE_SIZE));
        }
    }
    CHECK(physical_pages_free_count() == initial);
    printf("PASS: exhausted %u pages without duplicates/reserved pages; free/reallocate/double-free checks\n", allocated);
}

int main(void)
{
    test_map();
    test_pages();
    printf("PASS: %u host assertions\n", checks);
    return 0;
}
