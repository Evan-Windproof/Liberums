/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ABI-only loader fixture: exercised multi-page RX, R/O, initialized data/BSS. */
#include <abi/elf.h>
__attribute__((section(".note.liberums"),used,aligned(4)))
static const struct liberums_abi_note requirement={9,12,LIBERUMS_NOTE_TYPE,"LIBERUMS",1,2,LIBERUMS_FEATURE_ELF32};
const uint8_t layout_ro[8193]={1,[8192]=3};
volatile uint8_t layout_data[4097]={5,[4096]=7};
volatile uint8_t layout_bss[8193];
__attribute__((section(".text.second"),noinline)) uint32_t layout_second(void)
{
    for (uint32_t i=0;i<8193;++i) {
        if (layout_bss[i]!=0 || ((const volatile uint8_t *)layout_ro)[i]!=(i==0 ? 1 : i==8192 ? 3 : 0)) { return 1; }
        layout_bss[i]=(uint8_t)i;
    }
    return layout_data[0]!=5 || layout_data[4096]!=7;
}
__attribute__((section(".text.entry"))) _Noreturn void _start(void) { liberums_exit(layout_second()); }
