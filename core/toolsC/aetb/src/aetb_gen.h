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

/* src/compiler/aetb_gen.h */
#ifndef _AETB_GEN_H_
#define _AETB_GEN_H_

#include <stdio.h>
#include <stdint.h>

/* AETB 头部结构（128 字节）*/
typedef struct {
    uint32_t magic;              /* 0x00: Magic: 0x42544541 ("AETB" host-read value) */
    uint32_t format_version;     /* 0x04: 格式版本: 1 */
    uint32_t compiler_version;   /* 0x08: 编译器版本 */
    uint64_t build_time;         /* 0x0C: 构建时间戳 */
    uint32_t code_section_size;  /* 0x14: 代码段大小 */
    uint32_t data_section_size;  /* 0x18: 数据段大小 */
    uint32_t symbol_table_size;  /* 0x1C: 符号表大小 */
    uint64_t entry_point;        /* 0x20: 入口点地址 */
    uint32_t relocation_count;   /* 0x28: 重定位数量 */
    uint32_t import_count;       /* 0x2C: 导入符号数量 */
    uint32_t export_count;       /* 0x30: 导出符号数量 */
    uint8_t optimization_level;  /* 0x34: 优化级别 (0-3) */
    uint8_t debug_info;          /* 0x35: 调试信息标志 */
    uint16_t target_arch;        /* 0x36: 目标架构 (1=x86-64) */
    uint32_t header_checksum;    /* 0x38: 头部 CRC32 校验和 */
    uint32_t string_table_size;  /* 0x3C: 字符串表大小 (新增) */
    uint8_t reserved[76];        /* 0x40: 预留字段 (80 -> 76) */
} AETBHeader;

/* AETB 符号条目（32 字节）*/
typedef struct {
    uint32_t name_offset;        /* 符号名字符串的偏移 */
    uint32_t name_length;        /* 符号名长度 */
    uint8_t type;                /* 符号类型（函数、数据等） */
    uint8_t binding;             /* 符号绑定（本地、全局、弱） */
    uint16_t section_idx;        /* 段索引 */
    uint64_t address;            /* 符号地址 */
    uint64_t size;               /* 符号大小 */
    uint32_t flags;              /* 符号标志 */
} AETBSymbol;

/* AETB 重定位条目（24 字节）*/
typedef struct {
    uint64_t offset;             /* 需要重定位的偏移 */
    uint32_t symbol_idx;         /* 符号索引 */
    uint16_t relocation_type;    /* 重定位类型 (R_X86_64_*) */
    uint16_t addend_type;        /* Addend 类型 */
} AETBRelocation;

/* AETB 生成器上下文 */
typedef struct {
    FILE *output;
    uint8_t *code_section;
    uint32_t code_size;
    uint32_t code_capacity;
    uint8_t *data_section;
    uint32_t data_size;
    uint32_t data_capacity;
    AETBSymbol *symbols;
    uint32_t symbol_count;
    uint32_t symbol_capacity;
    uint8_t *string_pool;
    uint32_t string_pool_size;
    uint32_t string_pool_capacity;
    AETBRelocation *relocations;
    uint32_t relocation_count;
    uint32_t relocation_capacity;
    int optimization_level;
    int debug_info;
    uint64_t entry_point;
} AETBGenerator;

/* 函数声明 */
AETBGenerator* aetb_gen_create(FILE *output, int opt_level, int debug);
void aetb_gen_destroy(AETBGenerator *gen);
void aetb_gen_emit_code(AETBGenerator *gen, const uint8_t *code, uint32_t size);
void aetb_gen_emit_data(AETBGenerator *gen, const uint8_t *data, uint32_t size);
uint32_t aetb_gen_add_symbol(AETBGenerator *gen, const char *name,
                             uint8_t type, uint8_t binding,
                             uint16_t section_idx, uint64_t address, uint64_t size);
uint32_t aetb_gen_add_string(AETBGenerator *gen, const char *str);
void aetb_gen_add_relocation(AETBGenerator *gen, uint64_t offset,
                             uint32_t symbol_idx, uint16_t reloc_type);
void aetb_gen_add_relocation_ex(AETBGenerator *gen, uint64_t offset,
                                uint32_t symbol_idx, uint16_t reloc_type,
                                int16_t addend);
void aetb_gen_set_entry_point(AETBGenerator *gen, uint64_t entry_point);
int aetb_gen_finalize(AETBGenerator *gen);

#endif /* _AETB_GEN_H_ */
