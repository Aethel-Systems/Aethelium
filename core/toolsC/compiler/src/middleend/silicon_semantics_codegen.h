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
 * AethelOS Aethelium Compiler - Silicon Semantics Code Generation Header
 * 硅基语义代码生成公共函数声明
 * 
 * 版本：1.0
 * 状态：工业级实现
 */

#ifndef SILICON_SEMANTICS_CODEGEN_H
#define SILICON_SEMANTICS_CODEGEN_H

#include "../frontend/parser/aec_parser.h"
#include "../frontend/silicon_semantics.h"
#include <stdio.h>

/* =====================================================================
 * 硅基语义代码生成函数声明
 * ===================================================================== */

/**
 * 生成硅基语义块的汇编代码
 */
void silicon_codegen_silicon_block(FILE *out, ASTNode *silicon_block);

/**
 * 生成微架构配置代码
 * 处理MSR/CR寄存器的读写操作
 */
void silicon_codegen_microarch_config(FILE *out, ASTNode *config_node);

/**
 * 生成流水线块的汇编代码
 * 处理序列化执行、推测执行阻止等
 */
void silicon_codegen_pipeline_block(FILE *out, ASTNode *pipeline_node);

/**
 * 生成管线屏障指令
 * 支持: lfence (load barrier), sfence (store barrier), mfence (full barrier)
 */
void silicon_codegen_pipeline_barrier(FILE *out, ASTNode *barrier_node);

/**
 * 生成管线提示指令
 * 用于分支预测和预取提示
 */
void silicon_codegen_pipeline_hint(FILE *out, ASTNode *hint_node);

/**
 * 生成缓存操作指令
 * 支持: clflush, clflushopt, clflush_invalidate
 */
void silicon_codegen_cache_operation(FILE *out, ASTNode *cache_node);

/**
 * 生成预取操作指令
 * 支持: prefetcht0, prefetcht1, prefetcht2, prefetchnta
 */
void silicon_codegen_prefetch_operation(FILE *out, ASTNode *prefetch_node);

/**
 * 生成操作码定义（暗物质指令注入）
 */
void silicon_codegen_syntax_opcode(FILE *out, ASTNode *opcode_node);

/**
 * 生成物理硬连线类型操作
 */
void silicon_codegen_phys_type(FILE *out, ASTNode *phys_node);

#endif /* SILICON_SEMANTICS_CODEGEN_H */
