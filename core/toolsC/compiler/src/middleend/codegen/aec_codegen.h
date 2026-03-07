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
 * AethelOS Aethelium Compiler - Code Generator Header
 * 代码生成器：AST → x86-64 汇编
 */

#ifndef AEC_CODEGEN_H
#define AEC_CODEGEN_H

#include "aec_parser.h"
#include <stdio.h>

typedef struct {
    FILE *output;
    FILE *binary_output;     /* 原始二进制输出（硬件层使用） */
    int label_count;
    int var_offset;
    int machine_bits;
    const char *target_isa;
    int target_format;       /* TARGET_FORMAT_*: PE, AETB, BIN等 */
    int use_uefi;            /* PE/UEFI时为1 */
    int use_syscall;         /* AETB/AKI/SRV/HDA时为1 */
    int in_hardware_block;   /* 是否在硬件层块中（直接二进制输出） */
    int emit_assembly;       /* 是否输出汇编文本（0=二进制，1=汇编） */
    char error[1024];        /* 扩展错误缓冲区 - 工业级别 */
    char errors[64][256];    /* 错误历史记录 */
    int error_count;         /* 错误计数 */
    /* [TODO-06] 入口点管理 */
    const char *entry_point_name;  /* 从命令行或@entry装饰器指定的入口点名称 */
} CodeGenerator;

/**
 * 创建代码生成器
 * @param output 输出文件指针
 * @return 初始化的代码生成器
 */
CodeGenerator* codegen_create(FILE *output);

int codegen_set_target(CodeGenerator *gen, const char *isa, int machine_bits);

/**
 * 析构代码生成器
 * @param gen 代码生成器
 */
void codegen_destroy(CodeGenerator *gen);

/**
 * 生成代码
 * @param gen 代码生成器
 * @param ast 抽象语法树
 * @return 成功返回 0
 */
int codegen_generate(CodeGenerator *gen, ASTNode *ast);

/**
 * 获取错误信息
 * @param gen 代码生成器
 * @return 错误字符串
 */
const char* codegen_get_error(CodeGenerator *gen);

#endif /* AEC_CODEGEN_H */
