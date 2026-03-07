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
 * AethelOS Aethelium Compiler - Syntax Version Definitions
 * 语法版本 v0.3：当前实施版本
 * 
 * 特点：
 * - 点访问成员语法 (a.b)
 * - snake_case 命名约定
 * - 旧式 import 语义
 */

#ifndef AE_V0_3_GRAMMAR_H
#define AE_V0_3_GRAMMAR_H

/* =====================================================================
 * 语法版本标识
 * ===================================================================== */

#if !defined(AE_SYNTAX_VERSION_MAJOR)
#define AE_SYNTAX_VERSION_MAJOR 0
#endif

#if !defined(AE_SYNTAX_VERSION_MINOR)
#define AE_SYNTAX_VERSION_MINOR 3
#endif

#if !defined(AE_SYNTAX_VERSION_PATCH)
#define AE_SYNTAX_VERSION_PATCH 0
#endif

/* =====================================================================
 * v0.3 语法特性开关
 * ===================================================================== */

/* 成员访问语法 */
#define AE_V0_3_DOT_MEMBER_ACCESS    1   /* a.b 访问 */
#define AE_V0_3_SNAKE_CASE_NAMES     1   /* 推荐 snake_case */
#define AE_V0_3_OLD_IMPORT_SEMANTICS 1   /* 旧式 import/rimport */

/* 基本特性 */
#define AE_V0_3_BASIC_TYPES          1   /* i8, i16, i32, i64, f32, f64 */
#define AE_V0_3_STRUCT_TYPES         1   /* struct 定义 */
#define AE_V0_3_ENUM_TYPES           1   /* enum 定义 */
#define AE_V0_3_FUNCTION_DECL        1   /* 函数声明 */
#define AE_V0_3_VARIABLE_DECL        1   /* let/var 声明 */

/* 控制流 */
#define AE_V0_3_IF_ELSE              1   /* if/else 语句 */
#define AE_V0_3_WHILE_LOOP           1   /* while 循环 */
#define AE_V0_3_FOR_LOOP             1   /* for 循环（范围迭代） */
#define AE_V0_3_MATCH_STMT           1   /* match 语句 */
#define AE_V0_3_SWITCH_STMT          0   /* switch 不在 v0.3 */

/* 表达式 */
#define AE_V0_3_BINARY_OPS           1   /* +, -, *, /, %, &&, ||, etc */
#define AE_V0_3_UNARY_OPS            1   /* !, -, ~, & (取地址) */
#define AE_V0_3_FUNCTION_CALLS       1   /* 函数调用 */
#define AE_V0_3_FIELD_ACCESS         1   /* 字段访问 a.b */
#define AE_V0_3_ARRAY_INDEX          1   /* 数组索引 a[i] */

/* 高级特性 */
#define AE_V0_3_OPTIONAL_TYPE        1   /* T? Optional 类型 */
#define AE_V0_3_GENERIC_TYPES        1   /* 泛型参数 */
#define AE_V0_3_CLOSURES             0   /* 闭包在 v0.3 禁用 */
#define AE_V0_3_ASYNC_AWAIT          0   /* async/await 在 v0.3 禁用 */

/* 系统层特性 */
#define AE_V0_3_METAL_BLOCK          0   /* metal { } 禁用 */
#define AE_V0_3_ASM_BLOCK            0   /* asm { } 禁用 */
#define AE_V0_3_BYTES_BLOCK          0   /* bytes { } 禁用 */
#define AE_V0_3_EXTERN_FUNCTION      1   /* extern 函数声明 */

/* =====================================================================
 * v0.3 BNF 语法定义（注释形式）
 * ===================================================================== */

/*
GRAMMAR ae_v0_3 {
    // 顶级：程序结构
    program        = (function_decl | var_decl | struct_decl | enum_decl | import_stmt)*
    
    // 导入
    import_stmt    = ("import" | "rimport") IDENT ("from" STRING)? ";"
    
    // 函数声明
    function_decl  = "func" IDENT "(" params ")" type_spec block
    params         = (IDENT ":" type_spec ("," IDENT ":" type_spec)*)?
    
    // 变量声明
    var_decl       = ("let" | "var") IDENT (":" type_spec)? ("=" expr)? ";"
    
    // 类型说明
    type_spec      = IDENT ("*" | "[" INT "]")*
                   | IDENT "?"  // Optional
    
    // 结构体声明
    struct_decl    = "struct" IDENT "{" struct_fields "}"
    struct_fields  = (IDENT ":" type_spec ";")*
    
    // 语句
    statement      = block
                   | if_stmt
                   | while_stmt
                   | for_stmt
                   | match_stmt
                   | defer_stmt
                   | guard_stmt
                   | return_stmt
                   | expr_stmt ";"
                   | break_stmt ";"
                   | continue_stmt ";"
    
    block          = "{" statement* "}"
    
    if_stmt        = "if" "(" expr ")" block ("else" block)?
    while_stmt     = "while" "(" expr ")" block
    for_stmt       = "for" IDENT "in" expr block
    match_stmt     = "match" expr "{" match_case+ "}"
    match_case     = pattern "=>" expr ("," | ";")
    
    return_stmt    = "return" expr?
    break_stmt     = "break"
    continue_stmt  = "continue"
    
    // 表达式
    expr           = or_expr
    or_expr        = and_expr ("||" and_expr)*
    and_expr       = eq_expr ("&&" eq_expr)*
    eq_expr        = rel_expr (("==" | "!=") rel_expr)*
    rel_expr       = add_expr (("<" | "<=" | ">" | ">=") add_expr)*
    add_expr       = mul_expr (("+" | "-") mul_expr)*
    mul_expr       = unary_expr (("*" | "/" | "%") unary_expr)*
    unary_expr     = ("!" | "-" | "~" | "&") unary_expr
                   | postfix_expr
    postfix_expr   = primary_expr ("." IDENT | "[" expr "]" | "(" args ")")*
    
    primary_expr   = IDENT
                   | INT | FLOAT | STRING | "true" | "false" | "nil"
                   | "(" expr ")"
                   | "[" expr ("," expr)* "]"  // Array literal
    
    // 字面量
    args           = (expr ("," expr)*)?
}
*/

#endif /* AE_V0_3_GRAMMAR_H */
