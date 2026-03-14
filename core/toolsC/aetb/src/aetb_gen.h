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

#ifndef _AETB_GEN_H_
#define _AETB_GEN_H_

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/* 引入全局二进制格式规范，绝不在此重复定义 AethelID 和 aetb_header_t */
#include "../../include/binary_format.h"

/* AETB 符号条目 (32 bytes) */
typedef struct {
    uint32_t name_offset;
    uint32_t name_length;
    uint8_t  type;
    uint8_t  binding;
    uint16_t section_idx;
    uint64_t address;
    uint64_t size;
    uint32_t flags;
} __attribute__((packed)) AETBSymbol;

/* AETB 重定位条目 (16 bytes) */
typedef struct {
    uint64_t offset;
    uint32_t symbol_idx;
    uint16_t relocation_type;
    uint16_t addend_type;
} __attribute__((packed)) AETBRelocation;

/* AETB 工业级生成器上下文 */
typedef struct {
    FILE *output;
    int optimization_level;
    int debug_info;
    uint64_t entry_point;
    
    /* 严格域隔离 */
    uint8_t *act_flow;           /* 存放纯机器码 */
    uint32_t act_flow_size;
    uint32_t act_flow_capacity;
    
    uint8_t *mirror_state;       /* 存放全局可变变量 */
    uint32_t mirror_state_size;
    uint32_t mirror_state_capacity;
    
    uint8_t *constant_truth;     /* 存放字符串、常量 */
    uint32_t constant_truth_size;
    uint32_t constant_truth_capacity;
    
    /* 元数据 */
    AETBSymbol *symbols;
    uint32_t symbol_count;
    uint32_t symbol_capacity;
    
    uint8_t *strtab;
    uint32_t strtab_size;
    uint32_t strtab_capacity;
    
    AETBRelocation *relocs;
    uint32_t reloc_count;
    uint32_t reloc_capacity;
} AETBGenerator;

/* 函数声明 */
AETBGenerator* aetb_gen_create(FILE *output, int opt_level, int debug);
void aetb_gen_destroy(AETBGenerator *gen);

void aetb_gen_emit_act_flow(AETBGenerator *gen, const uint8_t *code, uint32_t size);
void aetb_gen_emit_mirror_state(AETBGenerator *gen, const uint8_t *data, uint32_t size);
void aetb_gen_emit_constant_truth(AETBGenerator *gen, const uint8_t *data, uint32_t size);

uint32_t aetb_gen_add_symbol(AETBGenerator *gen, const char *name, uint8_t type, uint8_t binding, uint16_t section_idx, uint64_t address, uint64_t size);
uint32_t aetb_gen_add_string(AETBGenerator *gen, const char *str);
void aetb_gen_add_relocation_ex(AETBGenerator *gen, uint64_t offset, uint32_t symbol_idx, uint16_t reloc_type, int16_t addend);
void aetb_gen_set_entry_point(AETBGenerator *gen, uint64_t entry_point);
int aetb_gen_finalize(AETBGenerator *gen);

#endif /* _AETB_GEN_H_ */