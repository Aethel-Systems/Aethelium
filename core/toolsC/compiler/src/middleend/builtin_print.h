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
 * AethelOS Aethelium Compiler - Built-in print() Function Handler
 * 工业级print()函数编译支持
 *
 * 功能：
 * - 编译时检测print()调用
 * - 生成多格式支持的机器码：PE (UEFI), AETB, AKI, SRV, HDA, BIN
 * - 支持字符串、整数、浮点数、指针等所有基础类型
 * - 动态长度字符串的高效处理
 * - 格式统一，无printf风格脆弱性
 *
 * 架构：
 * - print(String) 的编译分三条路：
 *   1. PE/UEFI: 调用EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL.OutputString
 *   2. AethelOS应用 (AETB/AKI/SRV/HDA): 通过syscall进行输出
 *   3. 内核/驱动铁层 (LET/BIN): 原始机器码VT100终端直接写入
 */

#ifndef BUILTIN_PRINT_H
#define BUILTIN_PRINT_H

#include "../frontend/parser/aec_parser.h"
#include "codegen/aec_codegen.h"
#include <stdio.h>
#include <stdint.h>

/* 编译上下文标志 */
typedef struct {
    int target_format;           /* FORMAT_PE, FORMAT_AETB, FORMAT_BIN 等 */
    int machine_bits;            /* 16/32/64 */
    const char *target_isa;      /* "x86-64", "x86", 等 */
    int use_uefi;                /* PE输出时为1 */
    int use_syscall;             /* AETB/AKI输出时为1 */
    int raw_metal;               /* 纯机器码输出时为1 */
} PrintCompileContext;

/* 打印参数类型 */
typedef enum {
    PRINT_TYPE_STRING,           /* [*]u8 或 String */
    PRINT_TYPE_INT,              /* i8, i16, i32, i64, u64 等 */
    PRINT_TYPE_FLOAT,            /* f32, f64 */
    PRINT_TYPE_POINTER,          /* ptr<T> */
    PRINT_TYPE_UNKNOWN
} PrintArgType;

/**
 * 检测是否为print()函数调用
 */
int is_print_function_call(ASTNode *call_expr);

/**
 * 检测是否为comp()函数调用（ROM串口输出）
 */
int is_comp_function_call(ASTNode *call_expr);

/**
 * 推导print()参数的类型
 */
PrintArgType infer_print_arg_type(ASTNode *arg_expr);

/**
 * 生成print()调用的机器码
 * 
 * 工业级实现特性：
 * - 生成完整的字符串处理代码（null终止检测）
 * - 根据编译上下文选择正确的输出路径
 * - 无任何printf导入污染
 * - 完全独立的实现
 *
 * @param ctx 工业级编译上下文
 * @param call AST_CALL节点，表示print(arg)
 * @param gen 代码生成器
 * @return 0成功，-1失败
 */
int codegen_print_call(PrintCompileContext *ctx, ASTNode *call, CodeGenerator *gen);

/**
 * 生成comp()调用的机器码
 * - ROM: 串口（COM1）输出
 * - 其它目标：拒绝（编译期错误）
 */
int codegen_comp_call(PrintCompileContext *ctx, ASTNode *call, CodeGenerator *gen);

/**
 * 生成UEFI格式的print()
 * - 流程：构造UTF-16字符串 → 调用EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL.OutputString
 * - 编译期支持：将utf-8字符串编码为UTF-16
 */
int codegen_print_uefi(PrintCompileContext *ctx, ASTNode *arg, CodeGenerator *gen);

/**
 * 生成AethelOS系统调用格式的print()
 * - 流程：构造请求 → syscall(SYS_PRINT, msg_addr, msg_len)
 * - 响应模型：IPC到Aura Shell或调试输出服务
 */
int codegen_print_syscall(PrintCompileContext *ctx, ASTNode *arg, CodeGenerator *gen);

/**
 * 生成纯机器码的print()（完全铁层实现）
 * - 流程：字符逐个输出到VT100终端（通过COM口或显存）
 * - 机制：直接调用显存或I/O端口操作
 * - 用途：内核引导阶段、驱动调试
 */
int codegen_print_raw_metal(PrintCompileContext *ctx, ASTNode *arg, CodeGenerator *gen);

/**
 * 生成ROM实模式print()
 * - 16位：BIOS INT 10h + VGA/COM
 * - 32/64位：VGA/COM 直写
 */
int codegen_print_rom_real(PrintCompileContext *ctx, ASTNode *arg, CodeGenerator *gen);

/**
 * 生成ROM comp()：只做串口输出（不做任何初始化）
 */
int codegen_comp_rom_serial(PrintCompileContext *ctx, ASTNode *arg, CodeGenerator *gen);

/**
 * 字符串字面量预处理
 * - 将UTF-8字符串编码到目标格式（UTF-16 for UEFI, UTF-8 for syscall等）
 * - 返回编码后的位置和长度
 */
int prepare_string_constant(const char *utf8_str, int target_format,
                           uint8_t **encoded_out, size_t *size_out);

/**
 * 验证print()语法合法性
 * - 确保只使用print(expr)形式，无printf风格参数
 */
int validate_print_syntax(ASTNode *call_expr);

#endif /* BUILTIN_PRINT_H */
