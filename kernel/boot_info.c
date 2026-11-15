/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/boot_info.h>

/* GNU Multiboot v1 fields through boot_loader_name (offset 64). */
struct multiboot_info {
    uint32_t flags, mem_lower, mem_upper, boot_device, cmdline;
    uint32_t mods_count, mods_addr;
    uint32_t symbols[4];
    uint32_t mmap_length, mmap_addr, drives_length, drives_addr;
    uint32_t config_table, boot_loader_name;
};
struct multiboot_module {
    uint32_t begin, end, string, reserved;
};
_Static_assert(offsetof(struct multiboot_info, mmap_length) == 44, "Multiboot layout");
_Static_assert(sizeof(struct multiboot_module) == 16, "Multiboot module layout");

static bool valid_range(uint32_t address, uint32_t length)
{
    return address >= PAGE_SIZE && address < PHYSICAL_LIMIT && length != 0
        && length <= PHYSICAL_LIMIT - address;
}

static bool reserve(struct boot_context *boot, uint32_t address, uint32_t length)
{
    if (!valid_range(address, length) || boot->reserved_count == BOOT_RANGE_MAX
        || !memory_map_contains_ram(&boot->map, address, (uint64_t)address + length)) {
        return false;
    }
    struct physical_range *range = &boot->reserved[boot->reserved_count++];
    range->begin = address;
    range->end = address + length;
    return true;
}

static bool read_string(struct boot_context *boot, uint32_t address, char *copy)
{
    for (uint32_t i = 0; i < BOOT_STRING_MAX; ++i) {
        if (!valid_range(address, i + 1)
            || !memory_map_contains_ram(&boot->map, address, (uint64_t)address + i + 1)) {
            return false;
        }
        char character = *(const char *)(uintptr_t)(address + i);
        if (copy != NULL) {
            copy[i] = character;
        }
        if (character == '\0') {
            return reserve(boot, address, i + 1);
        }
    }
    return false; /* Reject unterminated/oversized strings rather than truncate. */
}

bool boot_info_read(uint32_t magic, uint32_t address, struct boot_context *boot)
{
    if (boot == NULL) {
        return false;
    }
    boot->map.count = 0;
    boot->module_count = 0;
    boot->reserved_count = 0;
    boot->commandline[0] = '\0';
    if (magic != UINT32_C(0x2badb002) || (address & 3) != 0
        || !valid_range(address, 116)) {
        return false;
    }
    const struct multiboot_info *info = (const void *)(uintptr_t)address;
    /* Fail closed for optional pointer-bearing formats not yet supported:
     * symbols, drives, config, APM, VBE and framebuffer. Never silently free them. */
    const uint32_t supported_flags = 0x24f;
    if (!(info->flags & (1u << 6)) || (info->flags & ~supported_flags) != 0
        || !valid_range(info->mmap_addr, info->mmap_length)
        || !memory_map_parse((const void *)(uintptr_t)info->mmap_addr,
                             info->mmap_length, &boot->map)) {
        return false;
    }
    if (!reserve(boot, address, 116)
        || !reserve(boot, info->mmap_addr, info->mmap_length)) {
        return false;
    }
    if ((info->flags & (1u << 2)) && !read_string(boot, info->cmdline, boot->commandline)) {
        return false;
    }
    if ((info->flags & (1u << 9)) && !read_string(boot, info->boot_loader_name, NULL)) {
        return false;
    }
    if (info->flags & (1u << 3)) {
        if (info->mods_count > 8) {
            return false;
        }
        if (info->mods_count != 0) {
            if ((info->mods_addr & 3) != 0
                || !reserve(boot, info->mods_addr, info->mods_count * 16)) {
                return false;
            }
            const struct multiboot_module *modules = (const void *)(uintptr_t)info->mods_addr;
            for (uint32_t i = 0; i < info->mods_count; ++i) {
                if (modules[i].end <= modules[i].begin
                    || !reserve(boot, modules[i].begin, modules[i].end - modules[i].begin)
                    || (modules[i].string != 0 && !read_string(boot, modules[i].string, NULL))) {
                    return false;
                }
                boot->modules[boot->module_count++] = (struct physical_range){modules[i].begin, modules[i].end};
            }
        }
    }
    return true;
}

bool boot_has_option(const char *commandline, const char *wanted)
{
    while (*commandline != '\0') {
        while (*commandline == ' ') {
            ++commandline;
        }
        const char *token = commandline;
        const char *expected = wanted;
        while (*expected != '\0' && *token == *expected) {
            ++token;
            ++expected;
        }
        if (*expected == '\0' && (*token == ' ' || *token == '\0')) {
            return true;
        }
        while (*commandline != '\0' && *commandline != ' ') {
            ++commandline;
        }
    }
    return false;
}
