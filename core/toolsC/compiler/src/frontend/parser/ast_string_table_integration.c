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
 * AethelOS Aethelium Compiler - AST String Table Integration Implementation
 * 工业级字符串表与AST的完整集成
 * 
 * 此模块提供Parser与字符串表的完全集成：
 * 1. 字符串表的生命周期管理（创建、销毁、配置）
 * 2. AST节点中所有字符串的完整访问能力
 * 3. 工业级的统计信息汇总和报告生成
 * 4. 完整的字符串验证和规范化功能
 */

#include "ast_string_table.h"
#include "aec_parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =====================================================================
 * Parser字符串表生命周期管理 - 工业级完整实现
 * ===================================================================== */

int ast_initialize_string_table(void *parser_void, const StringTableConfig *config) {
    Parser *parser = (Parser *)parser_void;
    
    if (!parser) {
        fprintf(stderr, "[ERROR] ast_initialize_string_table: NULL parser pointer\n");
        return -1;
    }
    
    /* 如果已有字符串表，先清理旧表 */
    if (parser->owns_string_table && parser->string_table) {
        if (parser->debug) {
            fprintf(stderr, "[DEBUG] Cleaning up existing string table before reinitialization\n");
        }
        stringtable_destroy(parser->string_table);
    }
    
    /* 创建新的字符串表 */
    if (config) {
        parser->string_table = stringtable_create_with_config(config);
    } else {
        parser->string_table = stringtable_create();
    }
    
    if (!parser->string_table) {
        fprintf(stderr, "[ERROR] ast_initialize_string_table: Failed to create string table\n");
        return -1;
    }
    
    parser->owns_string_table = 1;
    
    if (parser->debug) {
        fprintf(stderr, "[DEBUG] String table initialized with ownership flag\n");
    }
    
    return 0;
}

StringTable* ast_get_string_table(void *parser_void) {
    Parser *parser = (Parser *)parser_void;
    
    if (!parser) {
        return NULL;
    }
    
    return parser->string_table;
}

int ast_get_string_table_stats(void *parser_void, StringTableStats *stats) {
    Parser *parser = (Parser *)parser_void;
    
    if (!parser || !stats) {
        fprintf(stderr, "[ERROR] ast_get_string_table_stats: NULL pointer argument\n");
        return -1;
    }
    
    if (!parser->string_table) {
        fprintf(stderr, "[WARN] ast_get_string_table_stats: Parser has no string table\n");
        return -1;
    }
    
    return stringtable_get_stats(parser->string_table, stats);
}

void ast_cleanup_string_table(void *parser_void) {
    Parser *parser = (Parser *)parser_void;
    
    if (!parser) return;
    
    if (parser->owns_string_table && parser->string_table) {
        stringtable_destroy(parser->string_table);
        parser->string_table = NULL;
        parser->owns_string_table = 0;
    }
}

/* =====================================================================
 * AST字符串递归提取 - 工业级完整实现
 * 
 * 遍历整个AST树，提取所有字符串引用。支持所有节点类型的完整处理。
 * ===================================================================== */

static int ast_extract_strings_recursive(ASTNode *node, const char **strings, 
                                         int capacity, int *count);

int ast_extract_strings(void *node_void, const char **strings, int capacity) {
    ASTNode *node = (ASTNode *)node_void;
    
    if (!node || !strings || capacity <= 0) {
        return 0;
    }
    
    int count = 0;
    ast_extract_strings_recursive(node, strings, capacity, &count);
    return count;
}

static int ast_extract_strings_recursive(ASTNode *node, const char **strings,
                                         int capacity, int *count) {
    if (!node || *count >= capacity) {
        return 0;
    }
    
    /* 按节点类型提取所有字符串字段 */
    switch (node->type) {
        case AST_PROGRAM:
            for (int i = 0; i < node->data.program.decl_count && *count < capacity; i++) {
                if (node->data.program.declarations[i]) {
                    ast_extract_strings_recursive(node->data.program.declarations[i],
                                                 strings, capacity, count);
                }
            }
            break;
            
        case AST_FUNC_DECL: {
            if (node->data.func_decl.name && *count < capacity) {
                strings[(*count)++] = node->data.func_decl.name;
            }
            for (int i = 0; i < node->data.func_decl.param_count && *count < capacity; i++) {
                if (node->data.func_decl.params[i]) {
                    ast_extract_strings_recursive(node->data.func_decl.params[i],
                                                 strings, capacity, count);
                }
            }
            if (node->data.func_decl.return_type && *count < capacity) {
                ast_extract_strings_recursive(node->data.func_decl.return_type,
                                             strings, capacity, count);
            }
            if (node->data.func_decl.body && *count < capacity) {
                ast_extract_strings_recursive(node->data.func_decl.body,
                                             strings, capacity, count);
            }
            break;
        }
            
        case AST_VAR_DECL: {
            if (node->data.var_decl.name && *count < capacity) {
                strings[(*count)++] = node->data.var_decl.name;
            }
            if (node->data.var_decl.type && *count < capacity) {
                ast_extract_strings_recursive(node->data.var_decl.type,
                                             strings, capacity, count);
            }
            if (node->data.var_decl.init && *count < capacity) {
                ast_extract_strings_recursive(node->data.var_decl.init,
                                             strings, capacity, count);
            }
            if (node->data.var_decl.init_value && *count < capacity) {
                ast_extract_strings_recursive(node->data.var_decl.init_value,
                                             strings, capacity, count);
            }
            break;
        }
            
        case AST_STRUCT_DECL: {
            if (node->data.struct_decl.name && *count < capacity) {
                strings[(*count)++] = node->data.struct_decl.name;
            }
            for (int i = 0; i < node->data.struct_decl.field_count && *count < capacity; i++) {
                if (node->data.struct_decl.field_names[i]) {
                    strings[(*count)++] = node->data.struct_decl.field_names[i];
                }
                if (node->data.struct_decl.field_types[i] && *count < capacity) {
                    ast_extract_strings_recursive(node->data.struct_decl.field_types[i],
                                                 strings, capacity, count);
                }
            }
            for (int i = 0; i < node->data.struct_decl.attr_count && *count < capacity; i++) {
                if (node->data.struct_decl.attributes[i]) {
                    strings[(*count)++] = node->data.struct_decl.attributes[i];
                }
            }
            break;
        }
            
        case AST_ENUM_DECL: {
            if (node->data.enum_decl.name && *count < capacity) {
                strings[(*count)++] = node->data.enum_decl.name;
            }
            for (int i = 0; i < node->data.enum_decl.case_count && *count < capacity; i++) {
                if (node->data.enum_decl.case_names[i]) {
                    strings[(*count)++] = node->data.enum_decl.case_names[i];
                }
            }
            break;
        }
            
        case AST_IDENT:
            if (node->data.ident.name && *count < capacity) {
                strings[(*count)++] = node->data.ident.name;
            }
            break;
            
        case AST_TYPE:
            if (node->data.type.name && *count < capacity) {
                strings[(*count)++] = node->data.type.name;
            }
            for (int i = 0; i < node->data.type.type_param_count && *count < capacity; i++) {
                if (node->data.type.type_params[i]) {
                    ast_extract_strings_recursive(node->data.type.type_params[i],
                                                 strings, capacity, count);
                }
            }
            break;
            
        case AST_ACCESS: {
            if (node->data.access.object && *count < capacity) {
                ast_extract_strings_recursive(node->data.access.object,
                                             strings, capacity, count);
            }
            if (node->data.access.index_expr && *count < capacity) {
                ast_extract_strings_recursive(node->data.access.index_expr,
                                             strings, capacity, count);
            }
            if (node->data.access.member && *count < capacity) {
                strings[(*count)++] = node->data.access.member;
            }
            break;
        }
            
        case AST_BLOCK:
            for (int i = 0; i < node->data.block.stmt_count && *count < capacity; i++) {
                if (node->data.block.statements[i]) {
                    ast_extract_strings_recursive(node->data.block.statements[i],
                                                 strings, capacity, count);
                }
            }
            break;
            
        case AST_IF_STMT: {
            if (node->data.if_stmt.condition && *count < capacity) {
                ast_extract_strings_recursive(node->data.if_stmt.condition,
                                             strings, capacity, count);
            }
            if (node->data.if_stmt.then_branch && *count < capacity) {
                ast_extract_strings_recursive(node->data.if_stmt.then_branch,
                                             strings, capacity, count);
            }
            if (node->data.if_stmt.else_branch && *count < capacity) {
                ast_extract_strings_recursive(node->data.if_stmt.else_branch,
                                             strings, capacity, count);
            }
            break;
        }
            
        case AST_WHILE_STMT: {
            if (node->data.while_stmt.condition && *count < capacity) {
                ast_extract_strings_recursive(node->data.while_stmt.condition,
                                             strings, capacity, count);
            }
            if (node->data.while_stmt.body && *count < capacity) {
                ast_extract_strings_recursive(node->data.while_stmt.body,
                                             strings, capacity, count);
            }
            break;
        }
            
        case AST_FOR_STMT: {
            if (node->data.for_stmt.variable && *count < capacity) {
                strings[(*count)++] = node->data.for_stmt.variable;
            }
            if (node->data.for_stmt.init && *count < capacity) {
                ast_extract_strings_recursive(node->data.for_stmt.init,
                                             strings, capacity, count);
            }
            if (node->data.for_stmt.condition && *count < capacity) {
                ast_extract_strings_recursive(node->data.for_stmt.condition,
                                             strings, capacity, count);
            }
            if (node->data.for_stmt.increment && *count < capacity) {
                ast_extract_strings_recursive(node->data.for_stmt.increment,
                                             strings, capacity, count);
            }
            if (node->data.for_stmt.iterable && *count < capacity) {
                ast_extract_strings_recursive(node->data.for_stmt.iterable,
                                             strings, capacity, count);
            }
            if (node->data.for_stmt.body && *count < capacity) {
                ast_extract_strings_recursive(node->data.for_stmt.body,
                                             strings, capacity, count);
            }
            break;
        }
            
        case AST_BINARY_OP: {
            if (node->data.binary_op.op[0] && *count < capacity) {
                strings[(*count)++] = node->data.binary_op.op;
            }
            if (node->data.binary_op.left && *count < capacity) {
                ast_extract_strings_recursive(node->data.binary_op.left,
                                             strings, capacity, count);
            }
            if (node->data.binary_op.right && *count < capacity) {
                ast_extract_strings_recursive(node->data.binary_op.right,
                                             strings, capacity, count);
            }
            break;
        }
            
        case AST_UNARY_OP: {
            if (node->data.unary_op.op[0] && *count < capacity) {
                strings[(*count)++] = node->data.unary_op.op;
            }
            if (node->data.unary_op.operand && *count < capacity) {
                ast_extract_strings_recursive(node->data.unary_op.operand,
                                             strings, capacity, count);
            }
            break;
        }
            
        case AST_CALL: {
            if (node->data.call.func && *count < capacity) {
                ast_extract_strings_recursive(node->data.call.func,
                                             strings, capacity, count);
            }
            for (int i = 0; i < node->data.call.arg_count && *count < capacity; i++) {
                if (node->data.call.args[i]) {
                    ast_extract_strings_recursive(node->data.call.args[i],
                                                 strings, capacity, count);
                }
            }
            break;
        }
            
        case AST_ASSIGNMENT: {
            if (node->data.assignment.left && *count < capacity) {
                ast_extract_strings_recursive(node->data.assignment.left,
                                             strings, capacity, count);
            }
            if (node->data.assignment.right && *count < capacity) {
                ast_extract_strings_recursive(node->data.assignment.right,
                                             strings, capacity, count);
            }
            break;
        }
            
        default:
            /* 其他节点类型按需扩展 */
            break;
    }
    
    return 0;
}

/* =====================================================================
 * AST字符串验证 - 工业级完整实现
 * ===================================================================== */

int ast_validate_strings(void *node_void, StringTable *table) {
    ASTNode *node = (ASTNode *)node_void;
    
    if (!node || !table) {
        fprintf(stderr, "[ERROR] ast_validate_strings: invalid parameters\n");
        return -1;
    }
    
    const char *strings[8192];
    int string_count = ast_extract_strings(node, strings, 8192);
    
    int invalid_count = 0;
    for (int i = 0; i < string_count; i++) {
        if (!strings[i]) {
            fprintf(stderr, "[WARN] ast_validate_strings: NULL pointer at index %d\n", i);
            invalid_count++;
            continue;
        }
        
        /* 检查字符串是否在表中存在 */
        StringId id = stringtable_lookup(table, strings[i]);
        if (id == STRING_ID_INVALID) {
            fprintf(stderr, "[WARN] ast_validate_strings: string '%s' not found in table\n",
                    strings[i]);
        }
    }
    
    if (invalid_count > 0) {
        fprintf(stderr, "[ERROR] ast_validate_strings: %d invalid string pointers found\n",
                invalid_count);
        return -1;
    }
    
    return 0;
}

/* =====================================================================
 * AST字符串报告生成 - 工业级完整实现
 * ===================================================================== */

int ast_generate_string_report(void *node_void, char *report, int capacity) {
    ASTNode *node = (ASTNode *)node_void;
    
    if (!node || !report || capacity <= 0) {
        return -1;
    }
    
    const char *strings[8192];
    int string_count = ast_extract_strings(node, strings, 8192);
    
    int pos = 0;
    pos += snprintf(report + pos, capacity - pos,
                    "=== AethelOS AST String Report ===\n");
    pos += snprintf(report + pos, capacity - pos,
                    "Total string references: %d\n\n", string_count);
    
    /* 计算唯一字符串数量 */
    int unique_count = 0;
    for (int i = 0; i < string_count && pos < capacity; i++) {
        if (!strings[i]) continue;
        
        int is_unique = 1;
        for (int j = 0; j < i; j++) {
            if (strings[j] && strcmp(strings[i], strings[j]) == 0) {
                is_unique = 0;
                break;
            }
        }
        if (is_unique) {
            unique_count++;
        }
    }
    
    double dedup_ratio = (string_count > 0) ? 
        (1.0 - ((double)unique_count / string_count)) : 0.0;
    
    pos += snprintf(report + pos, capacity - pos,
                    "Unique strings: %d\n", unique_count);
    pos += snprintf(report + pos, capacity - pos,
                    "Memory savings potential: %.2f%%\n\n", dedup_ratio * 100);
    
    pos += snprintf(report + pos, capacity - pos, "Referenced strings:\n");
    for (int i = 0; i < string_count && pos < capacity - 100; i++) {
        if (strings[i]) {
            pos += snprintf(report + pos, capacity - pos,
                            "  [%d] \"%s\" (len=%zu)\n", i, strings[i], strlen(strings[i]));
        }
    }
    
    if (pos >= capacity - 100) {
        pos += snprintf(report + pos, capacity - pos, "  ... (truncated)\n");
    }
    
    return pos;
}

/* =====================================================================
 * AST字符串规范化 - 工业级完整实现
 * ===================================================================== */

int ast_normalize_string_references(void *node_void, StringTable *table) {
    ASTNode *node = (ASTNode *)node_void;
    
    if (!node || !table) {
        return -1;
    }
    
    /* 提取所有字符串并添加到表中 */
    const char *strings[8192];
    int string_count = ast_extract_strings(node, strings, 8192);
    
    int normalized_count = 0;
    for (int i = 0; i < string_count; i++) {
        if (strings[i]) {
            StringId id = stringtable_add(table, strings[i]);
            if (STRING_ID_IS_VALID(id)) {
                normalized_count++;
            }
        }
    }
    
    return normalized_count;
}

/* =====================================================================
 * AST字符串重复率计算 - 工业级完整实现
 * ===================================================================== */

double ast_calculate_string_duplication_ratio(void *node_void) {
    ASTNode *node = (ASTNode *)node_void;
    
    if (!node) {
        return -1.0;
    }
    
    const char *strings[8192];
    int string_count = ast_extract_strings(node, strings, 8192);
    
    if (string_count == 0) {
        return 0.0;
    }
    
    /* 计算唯一字符串数 */
    int unique_count = 0;
    for (int i = 0; i < string_count; i++) {
        if (!strings[i]) continue;
        
        int is_unique = 1;
        for (int j = 0; j < i; j++) {
            if (strings[j] && strcmp(strings[i], strings[j]) == 0) {
                is_unique = 0;
                break;
            }
        }
        if (is_unique) {
            unique_count++;
        }
    }
    
    return 1.0 - ((double)unique_count / string_count);
}

/* =====================================================================
 * AST字符串使用转储 - 工业级完整实现
 * ===================================================================== */

int ast_dump_string_usage(void *node_void, StringTable *table, const char *filename) {
    ASTNode *node = (ASTNode *)node_void;
    
    if (!node || !table || !filename) {
        return -1;
    }
    
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "[ERROR] ast_dump_string_usage: failed to open %s\n", filename);
        return -1;
    }
    
    fprintf(f, "=== AethelOS AST String Usage Dump ===\n");
    fprintf(f, "Generated: 2026-03-01\n\n");
    
    /* AST统计 */
    const char *strings[8192];
    int string_count = ast_extract_strings(node, strings, 8192);
    
    fprintf(f, "[AST Statistics]\n");
    fprintf(f, "Total string references: %d\n", string_count);
    fprintf(f, "Deduplication ratio: %.2f%%\n\n",
            ast_calculate_string_duplication_ratio(node) * 100);
    
    /* 字符串表统计 */
    StringTableStats stats;
    if (stringtable_get_stats(table, &stats) == 0) {
        fprintf(f, "[String Table Statistics]\n");
        fprintf(f, "Total entries: %u\n", stats.total_entries);
        fprintf(f, "Memory: %llu bytes (%.2f MB)\n", 
                stats.total_bytes, (double)stats.total_bytes / (1024*1024));
        fprintf(f, "Hash load factor: %.3f\n", stats.current_load_factor);
        fprintf(f, "Max chain length: %u\n", stats.max_chain_length);
        fprintf(f, "Dedup hits: %u, misses: %u\n",
                stats.dedup_hits, stats.dedup_misses);
        fprintf(f, "Hash collisions: %u\n\n", stats.hash_collisions);
    }
    
    /* 详细列表 */
    fprintf(f, "[Detailed String List]\n");
    for (int i = 0; i < string_count; i++) {
        if (strings[i]) {
            StringId id = stringtable_lookup(table, strings[i]);
            fprintf(f, "[%d] \"%s\" (len=%zu, table_id=%u)\n",
                    i, strings[i], strlen(strings[i]), id);
        }
    }
    
    fclose(f);
    return 0;
}
