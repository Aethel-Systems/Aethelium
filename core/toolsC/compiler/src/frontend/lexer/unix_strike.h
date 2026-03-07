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
 * AethelOS "范式清洗" (Paradigm Purge) - Unix 特征罢工检测模块
 * ================================================================
 * 目标：检测并拒绝所有 Unix 遗留特征
 * 返回码：1010 (0x3F2) 表示编译器范式冲突
 * ================================================================
 */

#ifndef UNIX_STRIKE_H
#define UNIX_STRIKE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 罢工返回码 */
#define COMPILER_STRIKE_CODE 1010

/* 罢工诊断信息 */
typedef struct {
    const char *violation_type;     /* 违规类型 */
    const char *detected_token;     /* 检测到的词汇 */
    const char *reason;             /* 理由 */
    const char *correction;         /* 纠正建议 */
    int line;
    int column;
} UnixViolation;

/* 符号罢工清单 */

/* [TODO-01] 点号罢工检测 */
int strike_detect_dot_access(const char *lexeme, int line, int column, 
                              int is_in_float_literal);

/* [TODO-02] 箭头罢工检测 */
int strike_detect_arrow(const char *lexeme, int line, int column);

/* [TODO-04] 下划线在标识符中的罢工 */
int strike_detect_underscore_identifier(const char *identifier, int line, int column);


/* [TODO-03] 禁忌头文件罢工 */
int strike_detect_forbidden_include(const char *include_path);

/* [TODO-05] 禁咒符号检测 */
int strike_detect_forbidden_symbol(const char *symbol_name, int line, int column);
int strike_detect_forbidden_identifier(const char *identifier, int line, int column);

/* [TODO-06] Main 函数检测 */
int strike_detect_main_function(const char *function_name);

/* [TODO-07] GNU 扩展检测 */
int strike_detect_gnu_extension(const char *extension_syntax);

/* 通用罢工输出函数 */
void print_strike_message(UnixViolation *violation);
void compiler_strike_and_exit(UnixViolation *violation);

/* 路径字面量检测：禁止 / ./ .. ~ 等 */
int strike_detect_path_literals(const char *string_literal);

#endif /* UNIX_STRIKE_H */
