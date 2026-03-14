/*
 * Copyright (C) 2024-2026 Aethel-Systems. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * ROM Firmware Image Generation (Raw pflash)
 *
 * Goals:
 * - Flashable raw ROM (filled to size, default 8MB, typically 0xFF)
 * - Reset-window trampoline (last 64KB) that jumps to ActFlow entry
 * - Industrial layout: keep ActFlow->MirrorState->ConstantTruth contiguous
 *   to match the compiler's baked ConstantTruth fixups.
 *
 * Reset Vector (x86 reset state):
 * - CPU starts at physical 0xFFFFFFF0 (CS selector 0xF000, IP 0xFFF0, hidden CS base 0xFFFF0000)
 * - The last 16 bytes contain a short JMP to 0xFFFF:0000 (within the hidden base window)
 * - The reset window (last 64KB) contains the trampoline at its start
 */

#include "rom.h"
#include "../common/format_common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ROM_DEFAULT_SIZE_BYTES (8ULL * 1024ULL * 1024ULL)
#define ROM_RESET_VECTOR_SIZE 16u
#define ROM_WINDOW_SIZE (64u * 1024u)
#define ROM_LOAD_BASE 0xFFFF0000u
#define ROM_PHYS_TOP_4G 0x100000000ull
#define ROM_STACK32 0x0009FC00u
#define ROM_STACK64 0x000000000009FC00ull

static int append_u8(uint8_t *buf, size_t cap, size_t *pos, uint8_t v) {
    if (!buf || !pos || *pos >= cap) return -1;
    buf[(*pos)++] = v;
    return 0;
}

static int append_u16(uint8_t *buf, size_t cap, size_t *pos, uint16_t v) {
    if (append_u8(buf, cap, pos, (uint8_t)(v & 0xFF)) != 0) return -1;
    return append_u8(buf, cap, pos, (uint8_t)((v >> 8) & 0xFF));
}

static int append_u32(uint8_t *buf, size_t cap, size_t *pos, uint32_t v) {
    if (append_u16(buf, cap, pos, (uint16_t)(v & 0xFFFFu)) != 0) return -1;
    return append_u16(buf, cap, pos, (uint16_t)((v >> 16) & 0xFFFFu));
}

static int append_u64(uint8_t *buf, size_t cap, size_t *pos, uint64_t v) {
    if (append_u32(buf, cap, pos, (uint32_t)(v & 0xFFFFFFFFu)) != 0) return -1;
    return append_u32(buf, cap, pos, (uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

static int append_padding(uint8_t *buf, size_t cap, size_t *pos, size_t count, uint8_t fill) {
    size_t i;
    if (!buf || !pos) return -1;
    for (i = 0; i < count; i++) {
        if (append_u8(buf, cap, pos, fill) != 0) return -1;
    }
    return 0;
}

static int align_to(uint8_t *buf, size_t cap, size_t *pos, size_t align, uint8_t fill) {
    size_t pad;
    if (!pos || align == 0) return -1;
    pad = (align - (*pos % align)) % align;
    return append_padding(buf, cap, pos, pad, fill);
}

static int build_trampoline32(uint8_t *buf, size_t cap, uint64_t entry_offset, size_t *out_size) {
    size_t pos = 0;
    size_t lgdt_disp_off = 0;
    size_t far_pm32_off = 0;
    size_t pm32_off = 0;
    size_t entry32_off = 0;
    size_t gdtr_off = 0;
    size_t gdt_off = 0;
    uint32_t entry_addr;

    if (!buf || !out_size) return -1;

    if (append_u8(buf, cap, &pos, 0xFA) != 0) return -1; /* cli */
    /* Enable A20 via port 0x92 */
    if (append_u8(buf, cap, &pos, 0xE4) != 0) return -1; /* in al,0x92 */
    if (append_u8(buf, cap, &pos, 0x92) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x0C) != 0) return -1; /* or al,0x02 */
    if (append_u8(buf, cap, &pos, 0x02) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xE6) != 0) return -1; /* out 0x92,al */
    if (append_u8(buf, cap, &pos, 0x92) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x66) != 0) return -1; /* 32-bit base */
    if (append_u8(buf, cap, &pos, 0x2E) != 0) return -1; /* cs: */
    if (append_u8(buf, cap, &pos, 0x0F) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x01) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x16) != 0) return -1; /* lgdt [disp16] */
    lgdt_disp_off = pos;
    if (append_u16(buf, cap, &pos, 0) != 0) return -1;

    if (append_u8(buf, cap, &pos, 0x66) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x0F) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x20) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xC0) != 0) return -1; /* mov eax, cr0 */
    if (append_u8(buf, cap, &pos, 0x66) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x83) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xC8) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x01) != 0) return -1; /* or eax,1 */
    if (append_u8(buf, cap, &pos, 0x66) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x0F) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x22) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xC0) != 0) return -1; /* mov cr0,eax */

    if (append_u8(buf, cap, &pos, 0x66) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xEA) != 0) return -1; /* jmp ptr16:32 */
    far_pm32_off = pos;
    if (append_u32(buf, cap, &pos, 0) != 0) return -1;
    if (append_u16(buf, cap, &pos, 0x0008) != 0) return -1;

    pm32_off = pos;
    if (append_u8(buf, cap, &pos, 0x66) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xB8) != 0) return -1;
    if (append_u16(buf, cap, &pos, 0x0010) != 0) return -1; /* mov ax,0x10 */
    if (append_u8(buf, cap, &pos, 0x8E) != 0 || append_u8(buf, cap, &pos, 0xD8) != 0) return -1; /* mov ds,ax */
    if (append_u8(buf, cap, &pos, 0x8E) != 0 || append_u8(buf, cap, &pos, 0xC0) != 0) return -1; /* mov es,ax */
    if (append_u8(buf, cap, &pos, 0x8E) != 0 || append_u8(buf, cap, &pos, 0xD0) != 0) return -1; /* mov ss,ax */
    if (append_u8(buf, cap, &pos, 0xBC) != 0) return -1;
    if (append_u32(buf, cap, &pos, ROM_STACK32) != 0) return -1; /* mov esp,stack */
    if (append_u8(buf, cap, &pos, 0xB8) != 0) return -1;
    entry32_off = pos;
    if (append_u32(buf, cap, &pos, 0) != 0) return -1; /* mov eax, entry */
    if (append_u8(buf, cap, &pos, 0xFF) != 0 || append_u8(buf, cap, &pos, 0xE0) != 0) return -1; /* jmp eax */

    gdtr_off = pos;
    if (append_u16(buf, cap, &pos, (uint16_t)(3u * 8u - 1u)) != 0) return -1;
    if (append_u32(buf, cap, &pos, 0) != 0) return -1; /* gdtr base */

    gdt_off = pos;
    if (append_u64(buf, cap, &pos, 0x0000000000000000ull) != 0) return -1;
    if (append_u64(buf, cap, &pos, 0x00CF9A000000FFFFull) != 0) return -1; /* 32-bit code */
    if (append_u64(buf, cap, &pos, 0x00CF92000000FFFFull) != 0) return -1; /* data */

    *out_size = pos;

    if (lgdt_disp_off + 1 >= cap) return -1;
    buf[lgdt_disp_off] = (uint8_t)(gdtr_off & 0xFF);
    buf[lgdt_disp_off + 1] = (uint8_t)((gdtr_off >> 8) & 0xFF);

    {
        uint32_t pm32_addr = ROM_LOAD_BASE + (uint32_t)pm32_off;
        buf[far_pm32_off + 0] = (uint8_t)(pm32_addr & 0xFF);
        buf[far_pm32_off + 1] = (uint8_t)((pm32_addr >> 8) & 0xFF);
        buf[far_pm32_off + 2] = (uint8_t)((pm32_addr >> 16) & 0xFF);
        buf[far_pm32_off + 3] = (uint8_t)((pm32_addr >> 24) & 0xFF);
    }

    {
        uint32_t gdt_base = ROM_LOAD_BASE + (uint32_t)gdt_off;
        buf[gdtr_off + 2] = (uint8_t)(gdt_base & 0xFF);
        buf[gdtr_off + 3] = (uint8_t)((gdt_base >> 8) & 0xFF);
        buf[gdtr_off + 4] = (uint8_t)((gdt_base >> 16) & 0xFF);
        buf[gdtr_off + 5] = (uint8_t)((gdt_base >> 24) & 0xFF);
    }

    /* entry_offset is interpreted as a 32-bit linear entry address (not an offset). */
    entry_addr = (uint32_t)entry_offset;
    buf[entry32_off + 0] = (uint8_t)(entry_addr & 0xFF);
    buf[entry32_off + 1] = (uint8_t)((entry_addr >> 8) & 0xFF);
    buf[entry32_off + 2] = (uint8_t)((entry_addr >> 16) & 0xFF);
    buf[entry32_off + 3] = (uint8_t)((entry_addr >> 24) & 0xFF);

    return 0;
}

static int build_trampoline16(uint8_t *buf, size_t cap, uint64_t entry_offset, size_t *out_size) {
    size_t pos = 0;
    size_t disp_off = 0;
    size_t target_word_off = 0;
    uint32_t target_off;
    if (!buf || !out_size) return -1;

    /* Stay on reset CS base (0xFFFF0000). Do not reload CS. */
    if (append_u8(buf, cap, &pos, 0xFA) != 0) return -1; /* cli */
    if (append_u8(buf, cap, &pos, 0x31) != 0 || append_u8(buf, cap, &pos, 0xC0) != 0) return -1; /* xor ax,ax */
    if (append_u8(buf, cap, &pos, 0x8E) != 0 || append_u8(buf, cap, &pos, 0xD0) != 0) return -1; /* mov ss,ax */
    if (append_u8(buf, cap, &pos, 0xBC) != 0) return -1; /* mov sp,imm16 */
    if (append_u16(buf, cap, &pos, 0x7C00u) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x8E) != 0 || append_u8(buf, cap, &pos, 0xD8) != 0) return -1; /* mov ds,ax */

    /* jmp word [cs:disp16] to absolute offset within same CS base */
    if (append_u8(buf, cap, &pos, 0x2E) != 0) return -1; /* cs: */
    if (append_u8(buf, cap, &pos, 0xFF) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x26) != 0) return -1; /* jmp word [disp16] */
    disp_off = pos;
    if (append_u16(buf, cap, &pos, 0x0000u) != 0) return -1;

    target_word_off = pos;
    if (append_u16(buf, cap, &pos, 0x0000u) != 0) return -1;

    *out_size = pos;
    target_off = (uint32_t)(*out_size + entry_offset);
    if (target_off > 0xFFFFu) return -1;
    buf[disp_off + 0] = (uint8_t)(target_word_off & 0xFF);
    buf[disp_off + 1] = (uint8_t)((target_word_off >> 8) & 0xFF);
    buf[target_word_off + 0] = (uint8_t)(target_off & 0xFF);
    buf[target_word_off + 1] = (uint8_t)((target_off >> 8) & 0xFF);
    return 0;
}

static int build_trampoline64(uint8_t *buf, size_t cap, uint64_t entry_offset, size_t *out_size) {
    size_t pos = 0;
    size_t lgdt_disp_off = 0;
    size_t far_pm32_off = 0;
    size_t pm32_off = 0;
    size_t mov_cr3_off = 0;
    size_t far_lm64_off = 0;
    size_t lm64_off = 0;
    size_t entry64_off = 0;
    size_t gdtr_off = 0;
    size_t gdt_off = 0;
    size_t pml4_off = 0;
    size_t pdpt_off = 0;
    size_t pd_off[4] = {0, 0, 0, 0};
    uint64_t entry_addr;
    int i;

    if (!buf || !out_size) return -1;

    if (append_u8(buf, cap, &pos, 0xFA) != 0) return -1; /* cli */
    /* Enable A20 via port 0x92 */
    if (append_u8(buf, cap, &pos, 0xE4) != 0) return -1; /* in al,0x92 */
    if (append_u8(buf, cap, &pos, 0x92) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x0C) != 0) return -1; /* or al,0x02 */
    if (append_u8(buf, cap, &pos, 0x02) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xE6) != 0) return -1; /* out 0x92,al */
    if (append_u8(buf, cap, &pos, 0x92) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x66) != 0) return -1; /* 32-bit base */
    if (append_u8(buf, cap, &pos, 0x2E) != 0) return -1; /* cs: */
    if (append_u8(buf, cap, &pos, 0x0F) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x01) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x16) != 0) return -1; /* lgdt [disp16] */
    lgdt_disp_off = pos;
    if (append_u16(buf, cap, &pos, 0) != 0) return -1;

    if (append_u8(buf, cap, &pos, 0x66) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x0F) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x20) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xC0) != 0) return -1; /* mov eax, cr0 */
    if (append_u8(buf, cap, &pos, 0x66) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x83) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xC8) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x01) != 0) return -1; /* or eax,1 */
    if (append_u8(buf, cap, &pos, 0x66) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x0F) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x22) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xC0) != 0) return -1; /* mov cr0,eax */

    if (append_u8(buf, cap, &pos, 0x66) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xEA) != 0) return -1; /* jmp ptr16:32 */
    far_pm32_off = pos;
    if (append_u32(buf, cap, &pos, 0) != 0) return -1;
    if (append_u16(buf, cap, &pos, 0x0008) != 0) return -1;

    pm32_off = pos;
    if (append_u8(buf, cap, &pos, 0x66) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xB8) != 0) return -1;
    if (append_u16(buf, cap, &pos, 0x0010) != 0) return -1; /* mov ax,0x10 */
    if (append_u8(buf, cap, &pos, 0x8E) != 0 || append_u8(buf, cap, &pos, 0xD8) != 0) return -1; /* mov ds,ax */
    if (append_u8(buf, cap, &pos, 0x8E) != 0 || append_u8(buf, cap, &pos, 0xC0) != 0) return -1; /* mov es,ax */
    if (append_u8(buf, cap, &pos, 0x8E) != 0 || append_u8(buf, cap, &pos, 0xD0) != 0) return -1; /* mov ss,ax */
    if (append_u8(buf, cap, &pos, 0xBC) != 0) return -1;
    if (append_u32(buf, cap, &pos, ROM_STACK32) != 0) return -1; /* mov esp,stack */

    if (append_u8(buf, cap, &pos, 0x0F) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x20) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xE0) != 0) return -1; /* mov eax,cr4 */
    if (append_u8(buf, cap, &pos, 0x83) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xC8) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x20) != 0) return -1; /* or eax,0x20 */
    if (append_u8(buf, cap, &pos, 0x0F) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x22) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xE0) != 0) return -1; /* mov cr4,eax */

    if (append_u8(buf, cap, &pos, 0xB9) != 0) return -1;
    if (append_u32(buf, cap, &pos, 0xC0000080u) != 0) return -1; /* ecx=EFER */
    if (append_u8(buf, cap, &pos, 0x0F) != 0 || append_u8(buf, cap, &pos, 0x32) != 0) return -1; /* rdmsr */
    if (append_u8(buf, cap, &pos, 0x0D) != 0) return -1;
    if (append_u32(buf, cap, &pos, 0x00000100u) != 0) return -1; /* or eax,LME */
    if (append_u8(buf, cap, &pos, 0x0F) != 0 || append_u8(buf, cap, &pos, 0x30) != 0) return -1; /* wrmsr */

    if (append_u8(buf, cap, &pos, 0xB8) != 0) return -1;
    mov_cr3_off = pos;
    if (append_u32(buf, cap, &pos, 0) != 0) return -1; /* mov eax, pml4 */
    if (append_u8(buf, cap, &pos, 0x0F) != 0 || append_u8(buf, cap, &pos, 0x22) != 0 || append_u8(buf, cap, &pos, 0xD8) != 0) return -1; /* mov cr3,eax */

    if (append_u8(buf, cap, &pos, 0x0F) != 0 || append_u8(buf, cap, &pos, 0x20) != 0 || append_u8(buf, cap, &pos, 0xC0) != 0) return -1; /* mov eax,cr0 */
    if (append_u8(buf, cap, &pos, 0x0D) != 0) return -1;
    if (append_u32(buf, cap, &pos, 0x80000001u) != 0) return -1; /* or eax,PG|PE */
    if (append_u8(buf, cap, &pos, 0x0F) != 0 || append_u8(buf, cap, &pos, 0x22) != 0 || append_u8(buf, cap, &pos, 0xC0) != 0) return -1; /* mov cr0,eax */

    if (append_u8(buf, cap, &pos, 0xEA) != 0) return -1; /* jmp ptr16:32 */
    far_lm64_off = pos;
    if (append_u32(buf, cap, &pos, 0) != 0) return -1;
    if (append_u16(buf, cap, &pos, 0x0018) != 0) return -1;

    lm64_off = pos;
    if (append_u8(buf, cap, &pos, 0x66) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xB8) != 0) return -1;
    if (append_u16(buf, cap, &pos, 0x0010) != 0) return -1; /* mov ax,0x10 */
    if (append_u8(buf, cap, &pos, 0x8E) != 0 || append_u8(buf, cap, &pos, 0xD8) != 0) return -1; /* mov ds,ax */
    if (append_u8(buf, cap, &pos, 0x8E) != 0 || append_u8(buf, cap, &pos, 0xC0) != 0) return -1; /* mov es,ax */
    if (append_u8(buf, cap, &pos, 0x8E) != 0 || append_u8(buf, cap, &pos, 0xD0) != 0) return -1; /* mov ss,ax */
    if (append_u8(buf, cap, &pos, 0x48) != 0 || append_u8(buf, cap, &pos, 0xBC) != 0) return -1; /* mov rsp,imm64 */
    if (append_u64(buf, cap, &pos, ROM_STACK64) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0x48) != 0 || append_u8(buf, cap, &pos, 0xB8) != 0) return -1; /* mov rax,entry */
    entry64_off = pos;
    if (append_u64(buf, cap, &pos, 0) != 0) return -1;
    if (append_u8(buf, cap, &pos, 0xFF) != 0 || append_u8(buf, cap, &pos, 0xE0) != 0) return -1; /* jmp rax */

    gdtr_off = pos;
    if (append_u16(buf, cap, &pos, (uint16_t)(4u * 8u - 1u)) != 0) return -1;
    if (append_u32(buf, cap, &pos, 0) != 0) return -1; /* gdtr base */

    gdt_off = pos;
    if (append_u64(buf, cap, &pos, 0x0000000000000000ull) != 0) return -1;
    if (append_u64(buf, cap, &pos, 0x00CF9A000000FFFFull) != 0) return -1; /* 32-bit code */
    if (append_u64(buf, cap, &pos, 0x00CF92000000FFFFull) != 0) return -1; /* data */
    if (append_u64(buf, cap, &pos, 0x00AF9A000000FFFFull) != 0) return -1; /* 64-bit code */

    if (align_to(buf, cap, &pos, 4096, 0x00) != 0) return -1;

    pml4_off = pos;
    for (i = 0; i < 512; i++) {
        if (append_u64(buf, cap, &pos, 0x0000000000000000ull) != 0) return -1;
    }

    pdpt_off = pos;
    for (i = 0; i < 512; i++) {
        if (append_u64(buf, cap, &pos, 0x0000000000000000ull) != 0) return -1;
    }

    for (i = 0; i < 4; i++) {
        int j;
        pd_off[i] = pos;
        for (j = 0; j < 512; j++) {
            uint64_t pde = ((uint64_t)i * 0x40000000ull) + ((uint64_t)j * 0x200000ull);
            pde |= 0x83ull; /* present|rw|2MB */
            if (append_u64(buf, cap, &pos, pde) != 0) return -1;
        }
    }

    *out_size = pos;

    if (lgdt_disp_off + 1 >= cap) return -1;
    buf[lgdt_disp_off] = (uint8_t)(gdtr_off & 0xFF);
    buf[lgdt_disp_off + 1] = (uint8_t)((gdtr_off >> 8) & 0xFF);

    {
        uint32_t pm32_addr = ROM_LOAD_BASE + (uint32_t)pm32_off;
        buf[far_pm32_off + 0] = (uint8_t)(pm32_addr & 0xFF);
        buf[far_pm32_off + 1] = (uint8_t)((pm32_addr >> 8) & 0xFF);
        buf[far_pm32_off + 2] = (uint8_t)((pm32_addr >> 16) & 0xFF);
        buf[far_pm32_off + 3] = (uint8_t)((pm32_addr >> 24) & 0xFF);
    }

    {
        uint32_t lm64_addr = ROM_LOAD_BASE + (uint32_t)lm64_off;
        buf[far_lm64_off + 0] = (uint8_t)(lm64_addr & 0xFF);
        buf[far_lm64_off + 1] = (uint8_t)((lm64_addr >> 8) & 0xFF);
        buf[far_lm64_off + 2] = (uint8_t)((lm64_addr >> 16) & 0xFF);
        buf[far_lm64_off + 3] = (uint8_t)((lm64_addr >> 24) & 0xFF);
    }

    {
        uint32_t gdt_base = ROM_LOAD_BASE + (uint32_t)gdt_off;
        buf[gdtr_off + 2] = (uint8_t)(gdt_base & 0xFF);
        buf[gdtr_off + 3] = (uint8_t)((gdt_base >> 8) & 0xFF);
        buf[gdtr_off + 4] = (uint8_t)((gdt_base >> 16) & 0xFF);
        buf[gdtr_off + 5] = (uint8_t)((gdt_base >> 24) & 0xFF);
    }

    {
        uint32_t pml4_addr = ROM_LOAD_BASE + (uint32_t)pml4_off;
        buf[mov_cr3_off + 0] = (uint8_t)(pml4_addr & 0xFF);
        buf[mov_cr3_off + 1] = (uint8_t)((pml4_addr >> 8) & 0xFF);
        buf[mov_cr3_off + 2] = (uint8_t)((pml4_addr >> 16) & 0xFF);
        buf[mov_cr3_off + 3] = (uint8_t)((pml4_addr >> 24) & 0xFF);
    }

    /* entry_offset is interpreted as a 64-bit linear entry address (not an offset). */
    entry_addr = entry_offset;
    buf[entry64_off + 0] = (uint8_t)(entry_addr & 0xFF);
    buf[entry64_off + 1] = (uint8_t)((entry_addr >> 8) & 0xFF);
    buf[entry64_off + 2] = (uint8_t)((entry_addr >> 16) & 0xFF);
    buf[entry64_off + 3] = (uint8_t)((entry_addr >> 24) & 0xFF);
    buf[entry64_off + 4] = (uint8_t)((entry_addr >> 32) & 0xFF);
    buf[entry64_off + 5] = (uint8_t)((entry_addr >> 40) & 0xFF);
    buf[entry64_off + 6] = (uint8_t)((entry_addr >> 48) & 0xFF);
    buf[entry64_off + 7] = (uint8_t)((entry_addr >> 56) & 0xFF);

    {
        uint64_t pdpt_addr = (uint64_t)ROM_LOAD_BASE + (uint64_t)pdpt_off;
        uint64_t entry0 = pdpt_addr | 0x3ull;
        size_t pml4e_off = pml4_off;
        buf[pml4e_off + 0] = (uint8_t)(entry0 & 0xFF);
        buf[pml4e_off + 1] = (uint8_t)((entry0 >> 8) & 0xFF);
        buf[pml4e_off + 2] = (uint8_t)((entry0 >> 16) & 0xFF);
        buf[pml4e_off + 3] = (uint8_t)((entry0 >> 24) & 0xFF);
        buf[pml4e_off + 4] = (uint8_t)((entry0 >> 32) & 0xFF);
        buf[pml4e_off + 5] = (uint8_t)((entry0 >> 40) & 0xFF);
        buf[pml4e_off + 6] = (uint8_t)((entry0 >> 48) & 0xFF);
        buf[pml4e_off + 7] = (uint8_t)((entry0 >> 56) & 0xFF);
    }

    for (i = 0; i < 4; i++) {
        uint64_t pd_addr = (uint64_t)ROM_LOAD_BASE + (uint64_t)pd_off[i];
        uint64_t pdpte = pd_addr | 0x3ull;
        size_t off = pdpt_off + (size_t)i * 8u;
        buf[off + 0] = (uint8_t)(pdpte & 0xFF);
        buf[off + 1] = (uint8_t)((pdpte >> 8) & 0xFF);
        buf[off + 2] = (uint8_t)((pdpte >> 16) & 0xFF);
        buf[off + 3] = (uint8_t)((pdpte >> 24) & 0xFF);
        buf[off + 4] = (uint8_t)((pdpte >> 32) & 0xFF);
        buf[off + 5] = (uint8_t)((pdpte >> 40) & 0xFF);
        buf[off + 6] = (uint8_t)((pdpte >> 48) & 0xFF);
        buf[off + 7] = (uint8_t)((pdpte >> 56) & 0xFF);
    }

    return 0;
}

static uint32_t rom_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0;
    size_t i;
    int j;
    if (!data && size != 0) return 0;
    for (i = 0; i < size; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return crc;
}

static int write_padding(FILE *out, uint64_t count, uint8_t fill_byte) {
    uint8_t buffer[4096];
    if (!out) return -1;
    memset(buffer, fill_byte, sizeof(buffer));
    while (count > 0) {
        size_t chunk = (count > sizeof(buffer)) ? sizeof(buffer) : (size_t)count;
        if (fwrite(buffer, 1, chunk, out) != chunk) {
            return -1;
        }
        count -= chunk;
    }
    return 0;
}

static int fill_file(FILE *out, uint64_t size, uint8_t fill_byte) {
    if (!out) return -1;
    if (fseek(out, 0, SEEK_SET) != 0) return -1;
    if (write_padding(out, size, fill_byte) != 0) return -1;
    return 0;
}

static int write_at(FILE *out, uint64_t offset, const void *data, size_t size) {
    if (!out || (!data && size != 0)) return -1;
    if (fseek(out, (long)offset, SEEK_SET) != 0) return -1;
    if (size > 0 && fwrite(data, 1, size, out) != size) return -1;
    return 0;
}

static int write_reset_vector(FILE *out, uint64_t rom_size) {
    uint8_t vec[ROM_RESET_VECTOR_SIZE];
    uint16_t disp = 0x000Du; /* from IP=0xFFF0 to 0x0000 */

    memset(vec, 0x90, sizeof(vec));
    vec[0] = 0xE9; /* JMP rel16 */
    vec[1] = (uint8_t)(disp & 0xFF);
    vec[2] = (uint8_t)((disp >> 8) & 0xFF);

    return write_at(out, rom_size - ROM_RESET_VECTOR_SIZE, vec, sizeof(vec));
}

int rom_generate_image(const char *output_file,
                       const uint8_t *code, size_t code_size,
                       const uint8_t *mirror_data, size_t mirror_size,
                       const uint8_t *constant_data, size_t constant_size,
                       uint64_t entry_offset,
                       uint16_t machine_bits,
                       uint16_t target_isa,
                       const uint8_t *aethel_id,
                       uint64_t rom_size,
                       uint8_t fill_byte) {
    FILE *out = NULL;
    uint64_t reset_window_off;
    uint64_t reset_vector_off;
    uint64_t rom_phys_base;
    uint64_t act_flow_off;
    uint64_t mirror_off;
    uint64_t truth_off;
    uint64_t payload_end;
    uint8_t *trampoline = NULL;
    size_t tramp_size = 0;
    int tramp_rc = 0;
    AethelBinaryHeader hdr;
    uint32_t crc_act;
    uint32_t crc_mirror;
    uint32_t crc_truth;
    uint32_t crc_payload;

    if (!output_file || (!code && code_size != 0)) {
        fprintf(stderr, "[ROM] Error: Invalid parameters\n");
        return -1;
    }

    if (rom_size == 0) {
        rom_size = ROM_DEFAULT_SIZE_BYTES;
    }
    if (rom_size < ROM_WINDOW_SIZE) {
        fprintf(stderr, "[ROM] Error: ROM size too small (<64KB)\n");
        return -1;
    }
    if (rom_size > ROM_PHYS_TOP_4G) {
        fprintf(stderr, "[ROM] Error: ROM size too large (>4GB)\n");
        return -1;
    }
    if (entry_offset > (uint64_t)code_size) {
        fprintf(stderr, "[ROM] Error: Entry offset 0x%llx out of ActFlow bounds (size=%zu)\n",
                (unsigned long long)entry_offset, code_size);
        return -1;
    }

    reset_window_off = rom_size - ROM_WINDOW_SIZE;
    reset_vector_off = rom_size - ROM_RESET_VECTOR_SIZE;
    rom_phys_base = ROM_PHYS_TOP_4G - rom_size;

    crc_act = rom_crc32(code, code_size);
    crc_mirror = rom_crc32(mirror_data, mirror_size);
    crc_truth = rom_crc32(constant_data, constant_size);
    crc_payload = crc_act ^ crc_mirror ^ crc_truth;

    trampoline = (uint8_t *)malloc(ROM_WINDOW_SIZE);
    if (!trampoline) {
        fprintf(stderr, "[ROM] Error: Out of memory for ROM trampoline\n");
        return -1;
    }

    /* Determine the payload placement and build the reset-window trampoline. */
    if (machine_bits == 16) {
        /* Real-mode ROM firmware must fit entirely within the reset window. */
        tramp_rc = build_trampoline16(trampoline, ROM_WINDOW_SIZE, entry_offset, &tramp_size);
        if (tramp_rc != 0) {
            fprintf(stderr, "[ROM] Error: Failed to build 16-bit ROM trampoline\n");
            free(trampoline);
            return -1;
        }
        act_flow_off = reset_window_off + (uint64_t)tramp_size;
        mirror_off = act_flow_off + (uint64_t)code_size;
        truth_off = mirror_off + (uint64_t)mirror_size;
        payload_end = truth_off + (uint64_t)constant_size;
        if (payload_end > reset_vector_off) {
            fprintf(stderr, "[ROM] Error: 16-bit payload too large for reset window (need=%llu, avail=%llu)\n",
                    (unsigned long long)(payload_end - reset_window_off),
                    (unsigned long long)(reset_vector_off - reset_window_off));
            free(trampoline);
            return -1;
        }
    } else if (machine_bits == 32) {
        uint32_t entry_phys32;
        act_flow_off = (uint64_t)AETHEL_HEADER_SIZE;
        mirror_off = act_flow_off + (uint64_t)code_size;
        truth_off = mirror_off + (uint64_t)mirror_size;
        payload_end = truth_off + (uint64_t)constant_size;
        if (payload_end > reset_window_off) {
            fprintf(stderr, "[ROM] Error: Payload overlaps reset window (payload_end=0x%llx, reset_window_off=0x%llx)\n",
                    (unsigned long long)payload_end,
                    (unsigned long long)reset_window_off);
            free(trampoline);
            return -1;
        }
        entry_phys32 = (uint32_t)(rom_phys_base + act_flow_off + entry_offset);
        tramp_rc = build_trampoline32(trampoline, ROM_WINDOW_SIZE, (uint64_t)entry_phys32, &tramp_size);
        if (tramp_rc != 0) {
            fprintf(stderr, "[ROM] Error: Failed to build 32-bit ROM trampoline\n");
            free(trampoline);
            return -1;
        }
    } else if (machine_bits == 64) {
        uint64_t entry_phys64;
        act_flow_off = (uint64_t)AETHEL_HEADER_SIZE;
        mirror_off = act_flow_off + (uint64_t)code_size;
        truth_off = mirror_off + (uint64_t)mirror_size;
        payload_end = truth_off + (uint64_t)constant_size;
        if (payload_end > reset_window_off) {
            fprintf(stderr, "[ROM] Error: Payload overlaps reset window (payload_end=0x%llx, reset_window_off=0x%llx)\n",
                    (unsigned long long)payload_end,
                    (unsigned long long)reset_window_off);
            free(trampoline);
            return -1;
        }
        entry_phys64 = rom_phys_base + act_flow_off + entry_offset;
        tramp_rc = build_trampoline64(trampoline, ROM_WINDOW_SIZE, entry_phys64, &tramp_size);
        if (tramp_rc != 0) {
            fprintf(stderr, "[ROM] Error: Failed to build 64-bit ROM trampoline\n");
            free(trampoline);
            return -1;
        }
    } else {
        fprintf(stderr, "[ROM] Error: Unsupported machine bits for ROM: %u\n", (unsigned)machine_bits);
        free(trampoline);
        return -1;
    }

    /* Build ROM header (always at file offset 0). */
    aethel_header_init(&hdr, MAGIC_ROM);
    if (aethel_id) {
        memcpy(hdr.aethel_id, aethel_id, AETHEL_ID_SIZE);
    }
    hdr.genesis_point = entry_offset;
    hdr.act_flow_offset = act_flow_off;
    hdr.act_flow_size = code_size;
    hdr.mirror_state_offset = mirror_off;
    hdr.mirror_state_size = mirror_size;
    hdr.constant_truth_offset = truth_off;
    hdr.constant_truth_size = constant_size;

    memset(hdr.format_specific, 0, sizeof(hdr.format_specific));
    hdr.format_specific[0] = (uint8_t)machine_bits;
    hdr.format_specific[1] = (uint8_t)target_isa;
    hdr.format_specific[2] = fill_byte;
    hdr.format_specific[3] = (machine_bits == 16) ? 1u : 0u; /* layout_kind */
    memcpy(&hdr.format_specific[4], &crc_act, sizeof(crc_act));
    memcpy(&hdr.format_specific[8], &crc_payload, sizeof(crc_payload));

    memset(hdr.extended_metadata, 0, sizeof(hdr.extended_metadata));
    {
        uint32_t tramp_size_u32 = (uint32_t)tramp_size;
        memcpy(&hdr.extended_metadata[32], &tramp_size_u32, sizeof(tramp_size_u32));
    }
    memcpy(&hdr.extended_metadata[0], &rom_size, sizeof(rom_size));
    memcpy(&hdr.extended_metadata[8], &reset_window_off, sizeof(reset_window_off));
    memcpy(&hdr.extended_metadata[16], &reset_vector_off, sizeof(reset_vector_off));
    memcpy(&hdr.extended_metadata[24], &rom_phys_base, sizeof(rom_phys_base));
    memcpy(&hdr.extended_metadata[40], &crc_mirror, sizeof(crc_mirror));
    memcpy(&hdr.extended_metadata[44], &crc_truth, sizeof(crc_truth));

    (void)aethel_header_calculate_crc(&hdr);

    out = fopen(output_file, "wb+");
    if (!out) {
        fprintf(stderr, "[ROM] Error: Cannot create output file '%s'\n", output_file);
        free(trampoline);
        return -1;
    }

    if (fill_file(out, rom_size, fill_byte) != 0) {
        fprintf(stderr, "[ROM] Error: Failed to initialize ROM with padding\n");
        free(trampoline);
        fclose(out);
        return -1;
    }

    if (write_at(out, 0, &hdr, sizeof(hdr)) != 0) {
        fprintf(stderr, "[ROM] Error: Failed to write ROM header\n");
        free(trampoline);
        fclose(out);
        return -1;
    }

    /* Trampoline must be at the start of the reset window. */
    if (tramp_size > 0) {
        if (write_at(out, reset_window_off, trampoline, tramp_size) != 0) {
            fprintf(stderr, "[ROM] Error: Failed to write ROM trampoline\n");
            free(trampoline);
            fclose(out);
            return -1;
        }
    }

    /* Write zones in the exact order expected by codegen fixups. */
    if (code_size > 0) {
        if (write_at(out, act_flow_off, code, code_size) != 0) {
            fprintf(stderr, "[ROM] Error: Failed to write ActFlow\n");
            free(trampoline);
            fclose(out);
            return -1;
        }
    }
    if (mirror_data && mirror_size > 0) {
        if (write_at(out, mirror_off, mirror_data, mirror_size) != 0) {
            fprintf(stderr, "[ROM] Error: Failed to write MirrorState\n");
            free(trampoline);
            fclose(out);
            return -1;
        }
    }
    if (constant_data && constant_size > 0) {
        if (write_at(out, truth_off, constant_data, constant_size) != 0) {
            fprintf(stderr, "[ROM] Error: Failed to write ConstantTruth\n");
            free(trampoline);
            fclose(out);
            return -1;
        }
    }

    if (write_reset_vector(out, rom_size) != 0) {
        fprintf(stderr, "[ROM] Error: Failed to write reset vector\n");
        free(trampoline);
        fclose(out);
        return -1;
    }

    free(trampoline);
    fclose(out);
    return 0;
}
