/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/elf_loader.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    assert(argc == 3);
    FILE *file = fopen(argv[1], "rb");
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size >= 0 && (unsigned long)size <= LIBERUMS_ELF_FILE_LIMIT + 1);
    rewind(file);
    uint8_t *bytes = malloc((size_t)size + 1);
    assert(bytes != NULL && fread(bytes, 1, (size_t)size, file) == (size_t)size);
    fclose(file);
    struct elf_plan plan;
    int32_t result = elf_validate(bytes, (size_t)size, &plan);
    assert(result == strtol(argv[2], NULL, 10));
    if (result == 0) {
        assert(plan.entry >= LIBERUMS_IMAGE_BASE && plan.entry < LIBERUMS_IMAGE_END);
        assert(plan.page_count > 0 && plan.page_count <= ELF_IMAGE_PAGE_LIMIT);
        assert(plan.segment_count > 0 && plan.segment_count <= ELF_SEGMENT_LIMIT);
    } else {
        assert(plan.entry == 0 && plan.page_count == 0 && plan.segment_count == 0);
    }
    free(bytes);
    printf("PASS: bounded ELF parser %s (%d)\n", argv[1], result);
    return 0;
}
