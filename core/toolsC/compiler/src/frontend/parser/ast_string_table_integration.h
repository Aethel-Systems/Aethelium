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
 * AethelOS Aethelium Compiler - AST String Table Integration Header
 * AST与字符串表的高效集成层
 *
 * 这个模块提供：
 * 1. Parser与字符串表的无缝集成
 * 2. 对现有代码的最小侵入性修改
 * 3. 工业级的内存管理和性能优化
 */

#ifndef AST_STRING_TABLE_INTEGRATION_H
#define AST_STRING_TABLE_INTEGRATION_H

#include "ast_string_table.h"
/* aec_parser.h会在实现中包含，避免forward declaration冲突 */

/* =====================================================================
 * AST字符串管理接口
 * ===================================================================== */

/**
 * 为Parser初始化字符串表
 * 必须在创建Parser之前或之后立即调用
 * 
 * @param parser    Parser指针（void*）
 * @param config    字符串表配置（NULL使用默认）
 * @return          成功返回0，失败返回-1
 */
int ast_initialize_string_table(void *parser, const StringTableConfig *config);

/**
 * 获取Parser关联的字符串表
 * 
 * @param parser    Parser指针（void*）
 * @return          字符串表指针，失败或未初始化返回NULL
 */
StringTable* ast_get_string_table(void *parser);

/**
 * 从Parser获取字符串表的统计信息
 * 
 * @param parser    Parser指针（void*）
 * @param stats     统计信息输出指针
 * @return          成功返回0，失败返回-1
 */
int ast_get_string_table_stats(void *parser, StringTableStats *stats);

/**
 * 清理Parser中的字符串表
 * 
 * @param parser    Parser指针（void*）
 */
void ast_cleanup_string_table(void *parser);

/* =====================================================================
 * AST访问辅助函数
 * 
 * 这些函数提供了与AST中字符串相关字段的交互接口
 * 支持ID管理和字符串转换
 * ===================================================================== */

/**
 * 从AST节点中提取所有字符串字段
 * 用于字符串表迁移和分析
 * 
 * @param node      AST节点指针
 * @param strings   字符串指针数组（输出）
 * @param capacity  数组容量
 * @return          实际提取的字符串数量
 */
int ast_extract_strings(ASTNode *node, const char **strings, int capacity);

/**
 * 验证AST中所有字符串指针的有效性
 * 
 * @param node      AST根节点指针
 * @param table     字符串表指针
 * @return          有效返回0，无效返回-1，并输出错误信息
 */
int ast_validate_strings(ASTNode *node, StringTable *table);

/**
 * 生成AST中使用的字符串统计报告
 * 
 * @param node      AST根节点指针
 * @param report    输出缓冲区
 * @param capacity  缓冲区容量
 * @return          输出字符数，失败返回-1
 */
int ast_generate_string_report(ASTNode *node, char *report, int capacity);

/* =====================================================================
 * 对内存管理优化的支持
 * ===================================================================== */

/**
 * 对AST中的所有字符串指针进行规范化
 * 替换重复的指针为同一字符串表中的引用
 * 
 * @param node      AST根节点指针
 * @param table     字符串表指针
 * @return          规范化字符串数量，失败返回-1
 */
int ast_normalize_string_references(ASTNode *node, StringTable *table);

/**
 * 计算AST中字符串平均重复率
 * 用于验证去重效制效能
 * 
 * @param node      AST根节点指针
 * @return          重复率（0.0-1.0），失败返回-1.0
 */
double ast_calculate_string_duplication_ratio(ASTNode *node);

/* =====================================================================
 * 调试和诊断
 * ===================================================================== */

/**
 * 生成AST字符串使用的详细报告文件
 * 
 * @param node      AST根节点指针
 * @param table     字符串表指针
 * @param filename  输出文件路径
 * @return          成功返回0，失败返回-1
 */
int ast_dump_string_usage(ASTNode *node, StringTable *table, const char *filename);

#endif /* AST_STRING_TABLE_INTEGRATION_H */
