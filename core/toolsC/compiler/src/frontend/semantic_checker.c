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

#include "semantic_checker.h"

#include "parser/aec_parser.h"
#include "lexer/unix_strike.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    SemanticResult *result;
    const char *source_file;
    int architect_mode;
    char *error_buf;
    size_t error_buf_size;
} SemanticContext;

static void set_error(SemanticContext *ctx, const char *fmt, ...) {
    va_list args;

    ctx->result->error_count++;

    va_start(args, fmt);
    if (ctx->error_buf && ctx->error_buf_size > 0 && ctx->error_buf[0] == '\0') {
        vsnprintf(ctx->error_buf, ctx->error_buf_size, fmt, args);
    }
    va_end(args);

    va_start(args, fmt);
    fprintf(stderr, "[SEMANTIC] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static int is_intent_import(const char *module) {
    size_t n;
    if (!module) return 0;
    n = strlen(module);
    if (n < 4) return 0;
    if (module[0] != '[' || module[n - 1] != ']') return 0;
    return strstr(module, ":") != NULL;
}

static int validate_identifier(const char *name) {
    size_t i;
    if (!name || !name[0]) return 1;
    if (name[0] == '_' && name[1] == '_') {
        size_t n = strlen(name);
        if (n >= 4 && name[n - 1] == '_' && name[n - 2] == '_') {
            return 1;
        }
    }
    for (i = 0; name[i] != '\0'; i++) {
        if (name[i] == '_') {
            return 0;
        }
    }
    return 1;
}

static int validate_aethel_path_identifier(const char *name, int require_slash) {
    size_t i;
    int has_slash = 0;
    if (!name || !name[0]) return 0;
    if (name[0] == '/' || name[strlen(name) - 1] == '/') return 0;
    for (i = 0; name[i] != '\0'; i++) {
        char c = name[i];
        if (c == '_') return 0;
        if (c == '/') {
            has_slash = 1;
            if (name[i + 1] == '/' || name[i + 1] == '\0') return 0;
            continue;
        }
        if (!(isalnum((unsigned char)c) || c == '-')) {
            return 0;
        }
    }
    if (require_slash && !has_slash) return 0;
    return 1;
}

static int validate_branch_import(const char *module) {
    /* AethelOS 分支式导入：仅允许使用 '-' 作为层级分隔符，严格拒绝 '/' 和 Unix 相对路径。 */
    size_t i = 0;
    int segment_len = 0;

    if (!module || !module[0]) return 0;

    if (module[0] == '.') {
        /* 禁止以 '.' 起始的相对路径写法（避免 Unix 化） */
        return 0;
    }

    for (; module[i] != '\0'; i++) {
        char c = module[i];

        if (c == '/') return 0;      /* 彻底禁止正斜杠 */
        if (c == '.') return 0;      /* 禁止 Unix 相对片段 */
        if (c == '_') return 0;      /* 禁止下划线命名 */

        if (c == '-') {
            if (segment_len == 0) return 0; /* 空片段 */
            segment_len = 0;
            continue;
        }

        if (!isalnum((unsigned char)c)) {
            return 0;
        }

        segment_len++;
    }

    return segment_len > 0;
}

static int classify_import(const char *module, int is_rimport, SemanticImportKind *kind) {
    size_t i;
    int gt_count = 0;
    int dash_count = 0;

    if (!module || !module[0]) return 0;

    if (is_rimport) {
        *kind = SEM_IMPORT_RIMPORT;
        return 1;
    }

    if (is_intent_import(module)) {
        *kind = SEM_IMPORT_INTENT;
        return 1;
    }

    for (i = 0; module[i] != '\0'; i++) {
        if (module[i] == '>') gt_count++;
        else if (module[i] == '-') dash_count++;
        else if (module[i] == '/') return 0; /* 绝不接受正斜杠 */
    }

    if (gt_count > 0 && dash_count > 0) return 0;

    if (gt_count > 0) {
        int separators = 0;
        int non_empty = 0;
        int in_token = 0;
        for (i = 0; module[i] != '\0'; i++) {
            if (module[i] == '>') {
                separators++;
                in_token = 0;
            } else {
                if (!in_token) {
                    non_empty++;
                    in_token = 1;
                }
            }
        }
        if (separators != 2 || non_empty != 3) {
            return 0;
        }
        *kind = SEM_IMPORT_LIBA;
        return 1;
    }

    /* Aethel 分支式导入：仅允许 '-' 连接的层级名 */
    if (dash_count > 0) {
        if (!validate_branch_import(module)) {
            return 0;
        }
        *kind = SEM_IMPORT_PATH;
        return 1;
    }

    return 0;
}

static void add_import_record(SemanticContext *ctx, SemanticImportKind kind, const char *module) {
    size_t idx;
    if (ctx->result->import_count >= 256) {
        set_error(ctx, "Import list overflow in '%s'", ctx->source_file);
        return;
    }
    idx = ctx->result->import_count++;
    ctx->result->imports[idx].kind = kind;
    snprintf(ctx->result->imports[idx].module, sizeof(ctx->result->imports[idx].module), "%s", module ? module : "");
}

static void analyze_node(const ASTNode *node, SemanticContext *ctx);

static void analyze_list(ASTNode **nodes, int count, SemanticContext *ctx) {
    int i;
    if (!nodes || count <= 0) return;
    for (i = 0; i < count; i++) {
        analyze_node(nodes[i], ctx);
    }
}

static void analyze_import(const ASTNode *node, SemanticContext *ctx) {
    SemanticImportKind kind;
    const char *module = node->data.import_stmt.module;

    if (!classify_import(module, node->data.import_stmt.is_rimport, &kind)) {
        set_error(ctx,
                  "Invalid import syntax '%s' at line %d. Allowed: [intent:xx], dir>file>symbol, dir-sub-file, dir/branch",
                  module ? module : "",
                  node->line);
        return;
    }

    if (strike_detect_forbidden_identifier(module, node->line, 1)) {
        set_error(ctx,
                  "[STRIKE] Unix naming is forbidden in import '%s' at line %d",
                  module ? module : "",
                  node->line);
        return;
    }

    add_import_record(ctx, kind, module);

    if (kind == SEM_IMPORT_INTENT) {
        set_error(ctx,
                  "Host environment incompatible for intent import '%s' at line %d",
                  module,
                  node->line);
        return;
    }

    if (kind == SEM_IMPORT_RIMPORT) {
        ctx->result->has_rimport = 1;
        if (!ctx->architect_mode) {
            set_error(ctx,
                      "[STRIKE] Rimport '%s' requires architect mode, but current mode is sandbox (line %d)",
                      module ? module : "",
                      node->line);
        }
    }
}

static void analyze_node(const ASTNode *node, SemanticContext *ctx) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
            analyze_list(node->data.program.declarations, node->data.program.decl_count, ctx);
            break;

        case AST_FUNC_DECL:
            /* [TODO-06] Main 函数检测 - 在语义分析阶段罢工 */
            if (node->data.func_decl.name && strike_detect_main_function(node->data.func_decl.name)) {
                set_error(ctx,
                          "[STRIKE] 编译器找不到入口(main函数被禁止) at line %d",
                          node->line);
                ctx->result->error_count++; /* 确保错误被计数 */
            }
            if (strike_detect_forbidden_identifier(node->data.func_decl.name, node->line, 1)) {
                set_error(ctx,
                          "[STRIKE] Unix naming is forbidden in identifier '%s' at line %d",
                          node->data.func_decl.name ? node->data.func_decl.name : "",
                          node->line);
            }
            if (!validate_identifier(node->data.func_decl.name)) {
                set_error(ctx,
                          "Invalid identifier '%s' at line %d: '_' is forbidden",
                          node->data.func_decl.name,
                          node->line);
            }
            analyze_list(node->data.func_decl.params, node->data.func_decl.param_count, ctx);
            analyze_node(node->data.func_decl.return_type, ctx);
            analyze_node(node->data.func_decl.body, ctx);
            break;

        case AST_VAR_DECL:
            if (strike_detect_forbidden_identifier(node->data.var_decl.name, node->line, 1)) {
                set_error(ctx,
                          "[STRIKE] Unix naming is forbidden in identifier '%s' at line %d",
                          node->data.var_decl.name ? node->data.var_decl.name : "",
                          node->line);
            }
            if (!validate_identifier(node->data.var_decl.name)) {
                set_error(ctx,
                          "Invalid identifier '%s' at line %d: '_' is forbidden",
                          node->data.var_decl.name,
                          node->line);
            }
            analyze_node(node->data.var_decl.type, ctx);
            analyze_node(node->data.var_decl.init, ctx);
            analyze_node(node->data.var_decl.init_value, ctx);
            break;

        case AST_BLOCK:
            analyze_list(node->data.block.statements, node->data.block.stmt_count, ctx);
            break;

        case AST_IF_STMT:
            analyze_node(node->data.if_stmt.condition, ctx);
            analyze_node(node->data.if_stmt.then_branch, ctx);
            analyze_node(node->data.if_stmt.else_branch, ctx);
            break;

        case AST_WHILE_STMT:
            analyze_node(node->data.while_stmt.condition, ctx);
            analyze_node(node->data.while_stmt.body, ctx);
            break;

        case AST_FOR_STMT:
            analyze_node(node->data.for_stmt.init, ctx);
            analyze_node(node->data.for_stmt.condition, ctx);
            analyze_node(node->data.for_stmt.increment, ctx);
            analyze_node(node->data.for_stmt.iterable, ctx);
            analyze_node(node->data.for_stmt.body, ctx);
            break;

        case AST_MATCH_STMT:
            analyze_node(node->data.match_stmt.expr, ctx);
            analyze_list(node->data.match_stmt.cases, node->data.match_stmt.case_count, ctx);
            analyze_node(node->data.match_stmt.default_case, ctx);
            break;

        case AST_CASE_CLAUSE:
            analyze_node(node->data.case_clause.pattern, ctx);
            analyze_list(node->data.case_clause.statements, node->data.case_clause.stmt_count, ctx);
            break;

        case AST_SWITCH_STMT:
            analyze_node(node->data.switch_stmt.expr, ctx);
            analyze_list(node->data.switch_stmt.cases, node->data.switch_stmt.case_count, ctx);
            analyze_node(node->data.switch_stmt.default_case, ctx);
            break;

        case AST_SWITCH_CASE:
            analyze_list(node->data.switch_case.values, node->data.switch_case.value_count, ctx);
            analyze_list(node->data.switch_case.statements, node->data.switch_case.stmt_count, ctx);
            break;

        case AST_GUARD_STMT:
            analyze_node(node->data.guard_stmt.condition, ctx);
            analyze_node(node->data.guard_stmt.binding_expr, ctx);
            analyze_node(node->data.guard_stmt.else_branch, ctx);
            break;

        case AST_DEFER_STMT:
            analyze_node(node->data.defer_stmt.body, ctx);
            break;

        case AST_RETURN_STMT:
            analyze_node(node->data.return_stmt.value, ctx);
            break;

        case AST_ASSIGNMENT:
            analyze_node(node->data.assignment.left, ctx);
            analyze_node(node->data.assignment.right, ctx);
            break;

        case AST_BINARY_OP:
            if (strcmp(node->data.binary_op.op, "/") == 0) {
                set_error(ctx,
                          "[STRIKE] Invalid arithmetic operator '/' at line %d. Use '÷' for division",
                          node->line);
            }
            analyze_node(node->data.binary_op.left, ctx);
            analyze_node(node->data.binary_op.right, ctx);
            break;

        case AST_UNARY_OP:
            if (strcmp(node->data.unary_op.op, "*") == 0) {
                ctx->result->trap_hint_count++;
            }
            analyze_node(node->data.unary_op.operand, ctx);
            break;

        case AST_CALL:
            ctx->result->reloc_dna_count++;
            analyze_node(node->data.call.func, ctx);
            analyze_list(node->data.call.args, node->data.call.arg_count, ctx);
            break;

        case AST_ACCESS:
            if (node->data.access.member && strcmp(node->data.access.member, "[index]") == 0) {
                ctx->result->trap_hint_count++;
            }
            analyze_node(node->data.access.object, ctx);
            break;

        case AST_IDENT:
            if (strike_detect_forbidden_identifier(node->data.ident.name, node->line, 1)) {
                set_error(ctx,
                          "[STRIKE] Unix naming is forbidden in identifier '%s' at line %d",
                          node->data.ident.name ? node->data.ident.name : "",
                          node->line);
            }
            if (!validate_identifier(node->data.ident.name)) {
                set_error(ctx,
                          "Invalid identifier '%s' at line %d: '_' is forbidden",
                          node->data.ident.name,
                          node->line);
            }
            break;

        case AST_ARRAY_LITERAL:
            analyze_list(node->data.array_literal.elements, node->data.array_literal.element_count, ctx);
            break;

        case AST_RANGE:
            analyze_node(node->data.range.start, ctx);
            analyze_node(node->data.range.end, ctx);
            break;

        case AST_CLOSURE:
            analyze_node(node->data.closure.body, ctx);
            break;

        case AST_FORCE_UNWRAP:
            analyze_node(node->data.force_unwrap.operand, ctx);
            break;

        case AST_TUPLE_LITERAL:
            analyze_list(node->data.tuple_literal.elements, node->data.tuple_literal.element_count, ctx);
            break;

        case AST_REFERENCE:
            analyze_node(node->data.reference.operand, ctx);
            break;

        case AST_STRUCT_DECL:
            if (strike_detect_forbidden_identifier(node->data.struct_decl.name, node->line, 1)) {
                set_error(ctx,
                          "[STRIKE] Unix naming is forbidden in struct identifier '%s' at line %d",
                          node->data.struct_decl.name ? node->data.struct_decl.name : "",
                          node->line);
            }
            if (!validate_identifier(node->data.struct_decl.name)) {
                set_error(ctx,
                          "Invalid struct identifier '%s' at line %d: '_' is forbidden",
                          node->data.struct_decl.name,
                          node->line);
            }
            analyze_list(node->data.struct_decl.field_types, node->data.struct_decl.field_count, ctx);
            break;

        case AST_ENUM_DECL:
            if (strike_detect_forbidden_identifier(node->data.enum_decl.name, node->line, 1)) {
                set_error(ctx,
                          "[STRIKE] Unix naming is forbidden in enum identifier '%s' at line %d",
                          node->data.enum_decl.name ? node->data.enum_decl.name : "",
                          node->line);
            }
            if (!validate_identifier(node->data.enum_decl.name)) {
                set_error(ctx,
                          "Invalid enum identifier '%s' at line %d: '_' is forbidden",
                          node->data.enum_decl.name,
                          node->line);
            }
            break;

        case AST_METAL_BLOCK:
            ctx->result->has_metal_block = 1;
            ctx->result->requires_architect_mode = 1;
            if (ctx->result->identity_contract_min_sip < 2) {
                ctx->result->identity_contract_min_sip = 2;
            }
            analyze_list(node->data.metal_block.statements, node->data.metal_block.stmt_count, ctx);
            break;
        
        /* ==================== 硅基语义节点 ==================== */
        case AST_SILICON_BLOCK:
            /* 硅基语义块 - 必须在Rimport后 */
            ctx->result->has_metal_block = 1;  /* 硅基语义隐含了metal能力 */
            ctx->result->requires_architect_mode = 1;  /* 需要架构师模式 */
            if (ctx->result->identity_contract_min_sip < 3) {
                ctx->result->identity_contract_min_sip = 3;  /* 硅基语义需要更高权限 */
            }
            
            /* 检查Rimport权限 */
            if (!ctx->architect_mode) {
                set_error(ctx, "Silicon semantics requires Rimport permission at line %d", node->line);
            }
            
            /* 分析硅基块内的语句 */
            if (node->data.silicon_block.statements) {
                for (int i = 0; i < node->data.silicon_block.stmt_count; i++) {
                    analyze_node(node->data.silicon_block.statements[i], ctx);
                }
            }
            break;
        
        case AST_MICROARCH_CONFIG:
            /* 微架构配置 */
            ctx->result->requires_architect_mode = 1;
            if (ctx->result->identity_contract_min_sip < 3) {
                ctx->result->identity_contract_min_sip = 3;
            }
            break;
        
        case AST_PIPELINE_BLOCK:
            /* 流水线编排块 */
            ctx->result->requires_architect_mode = 1;
            if (ctx->result->identity_contract_min_sip < 3) {
                ctx->result->identity_contract_min_sip = 3;
            }
            
            /* 分析流水线块内的语句 */
            if (node->data.pipeline_block.statements) {
                for (int i = 0; i < node->data.pipeline_block.stmt_count; i++) {
                    analyze_node(node->data.pipeline_block.statements[i], ctx);
                }
            }
            break;
        
        case AST_PIPELINE_BARRIER:
            /* 管线屏障 */
            ctx->result->requires_architect_mode = 1;
            if (ctx->result->identity_contract_min_sip < 3) {
                ctx->result->identity_contract_min_sip = 3;
            }
            break;
        
        case AST_PIPELINE_HINT:
            /* 管线提示 */
            ctx->result->requires_architect_mode = 1;
            break;
        
        case AST_CACHE_OPERATION:
            /* 缓存操作 */
            ctx->result->requires_architect_mode = 1;
            if (ctx->result->identity_contract_min_sip < 3) {
                ctx->result->identity_contract_min_sip = 3;
            }
            
            /* 缓存操作的目标必须是有效的表达式 */
            if (node->data.cache_operation.target) {
                analyze_node(node->data.cache_operation.target, ctx);
            }
            break;
        
        case AST_PREFETCH_OPERATION:
            /* 预取操作 */
            ctx->result->requires_architect_mode = 1;
            
            /* 预取地址必须是有效的表达式 */
            if (node->data.prefetch_operation.address) {
                analyze_node(node->data.prefetch_operation.address, ctx);
            }
            break;
        
        case AST_SYNTAX_OPCODE_DEF:
            /* 操作码定义 - 暗物质指令注入 */
            ctx->result->requires_architect_mode = 1;
            if (ctx->result->identity_contract_min_sip < 3) {
                ctx->result->identity_contract_min_sip = 3;
            }
            
            /* 验证操作码定义的有效性 */
            if (!node->data.syntax_opcode_def.opcode_name || 
                !node->data.syntax_opcode_def.opcode_hex) {
                set_error(ctx, "Invalid syntax/opcode definition at line %d", node->line);
            }
            break;
        
        case AST_PHYS_TYPE:
            /* 物理硬连线类型 */
            ctx->result->requires_architect_mode = 1;
            if (ctx->result->identity_contract_min_sip < 2) {
                ctx->result->identity_contract_min_sip = 2;
            }
            break;

        case AST_MAP_DEF: {
            /* 原初层：内存拓扑 ASCII 映射定义 */
            if (strike_detect_forbidden_identifier(node->data.map_def.name, node->line, 1)) {
                set_error(ctx,
                          "[STRIKE] Unix naming is forbidden in map identifier '%s' at line %d",
                          node->data.map_def.name ? node->data.map_def.name : "",
                          node->line);
            }
            
            /* 验证 map 名称有效性 */
            if (!validate_aethel_path_identifier(node->data.map_def.name, 1)) {
                set_error(ctx,
                          "Invalid map identifier '%s' at line %d: use AethelOS slash path (no '_', e.g. Root/Table)",
                          node->data.map_def.name,
                          node->line);
            }
            
            /* 注意：基地址可以为 0（动态位置），允许为零 */
            
            /* 验证字段定义 */
            for (int i = 0; i < node->data.map_def.field_count; i++) {
                if (!node->data.map_def.field_names[i] || 
                    !node->data.map_def.field_types[i]) {
                    set_error(ctx,
                              "Map '%s' at line %d: field %d has invalid definition",
                              node->data.map_def.name,
                              node->line,
                              i);
                    continue;
                }
                
                /* 检查字段名有效性 */
                if (!validate_aethel_path_identifier(node->data.map_def.field_names[i], 1)) {
                    set_error(ctx,
                              "Map '%s' at line %d: invalid field name '%s' (must be slash path and no '_')",
                              node->data.map_def.name,
                              node->line,
                              node->data.map_def.field_names[i]);
                }
                
                /* 检查字段大小合理性 */
                if (node->data.map_def.field_sizes[i] <= 0 || 
                    node->data.map_def.field_sizes[i] > 8192) {
                    set_error(ctx,
                              "Map '%s' at line %d: field '%s' has invalid size %d",
                              node->data.map_def.name,
                              node->line,
                              node->data.map_def.field_names[i],
                              node->data.map_def.field_sizes[i]);
                }
            }
            
            /* 要求 Architect 模式用于内存拓扑定义 */
            ctx->result->requires_architect_mode = 1;
            if (ctx->result->identity_contract_min_sip < 2) {
                ctx->result->identity_contract_min_sip = 2;
            }
            break;
        }

        case AST_SYNTAX_DEF: {
            /* 原初层：自定义语法映射定义 */
            if (strike_detect_forbidden_identifier(node->data.syntax_def.name, node->line, 1)) {
                set_error(ctx,
                          "[STRIKE] Unix naming is forbidden in syntax identifier '%s' at line %d",
                          node->data.syntax_def.name ? node->data.syntax_def.name : "",
                          node->line);
            }
            
            /* 验证 syntax 名称有效性 */
            if (!validate_identifier(node->data.syntax_def.name)) {
                set_error(ctx,
                          "Invalid syntax identifier '%s' at line %d: '_' is forbidden",
                          node->data.syntax_def.name,
                          node->line);
            }
            
            /* 验证模式-动作对 */
            for (int i = 0; i < node->data.syntax_def.pattern_count; i++) {
                if (!node->data.syntax_def.patterns[i] || 
                    !node->data.syntax_def.actions[i]) {
                    set_error(ctx,
                              "Syntax '%s' at line %d: pattern-action pair %d is incomplete",
                              node->data.syntax_def.name,
                              node->line,
                              i);
                    continue;
                }
                
                /* 检查是否为空模式或动作 */
                if (node->data.syntax_def.patterns[i][0] == '\0') {
                    set_error(ctx,
                              "Syntax '%s' at line %d: pattern %d is empty",
                              node->data.syntax_def.name,
                              node->line,
                              i);
                }
                
                if (node->data.syntax_def.actions[i][0] == '\0') {
                    set_error(ctx,
                              "Syntax '%s' at line %d: action %d is empty",
                              node->data.syntax_def.name,
                              node->line,
                              i);
                }
                
                /* 检查重复的模式 */
                for (int j = i + 1; j < node->data.syntax_def.pattern_count; j++) {
                    if (strcmp(node->data.syntax_def.patterns[i],
                              node->data.syntax_def.patterns[j]) == 0) {
                        set_error(ctx,
                                  "Syntax '%s' at line %d: duplicate pattern '%s'",
                                  node->data.syntax_def.name,
                                  node->line,
                                  node->data.syntax_def.patterns[i]);
                    }
                }
            }
            
            /* 如果没有任何模式定义，这是一个警告 */
            if (node->data.syntax_def.pattern_count == 0) {
                fprintf(stderr, "警告：Syntax '%s' at line %d: no patterns defined\n",
                        node->data.syntax_def.name, node->line);
            }
            
            /* 允许在任何模式下定义 syntax，但记录统计 */
            ctx->result->requires_architect_mode = 1;
            if (ctx->result->identity_contract_min_sip < 2) {
                ctx->result->identity_contract_min_sip = 2;
            }
            break;
        }

        case AST_IMPORT_STMT:
            analyze_import(node, ctx);
            break;

        case AST_TYPECAST:
            analyze_node(node->data.typecast.expr, ctx);
            analyze_node(node->data.typecast.target_type, ctx);
            break;

        default:
            break;
    }
}

void semantic_result_init(SemanticResult *result) {
    if (!result) return;
    memset(result, 0, sizeof(*result));
    result->identity_contract_min_sip = 1;
}

int semantic_analyze_program(const ASTNode *ast,
                            const char *source_file,
                            const char *compile_mode,
                            SemanticResult *result,
                            char *error_buf,
                            size_t error_buf_size) {
    SemanticContext ctx;

    if (!ast || !result) {
        return -1;
    }

    semantic_result_init(result);
    if (error_buf && error_buf_size > 0) {
        error_buf[0] = '\0';
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.result = result;
    ctx.source_file = source_file ? source_file : "<unknown>";
    ctx.architect_mode = (compile_mode && strcmp(compile_mode, "architect") == 0);
    ctx.error_buf = error_buf;
    ctx.error_buf_size = error_buf_size;

    analyze_node(ast, &ctx);

    if ((result->requires_architect_mode || result->has_rimport) && !ctx.architect_mode) {
        set_error(&ctx,
                  "[STRIKE] Identity contract requires architect mode for '%s'",
                  ctx.source_file);
    }

    return result->error_count == 0 ? 0 : -1;
}

/* =====================================================================
 * 工业级类型转换检查（原初层地址强类型安全）
 * =====================================================================
 *
 * 此函数实现AethelOS原初层的核心类型安全机制：PhysAddr和VirtAddr
 * 是强类型的地址，它们之间不能隐式转换。这是防止内核级内存错误的
 * 第一道防线。
 */
int ae_type_conversion_allowed(const char *from_type, const char *to_type) {
    /* 同类型转换总是允许 */
    if (from_type && to_type && strcmp(from_type, to_type) == 0) {
        return 1;  /* 允许 */
    }
    
    /* 物理地址和虚拟地址不能相互转换（絕對禁止） */
    if (from_type && to_type) {
        int from_is_phys = (strstr(from_type, "PhysAddr") != NULL);
        int from_is_virt = (strstr(from_type, "VirtAddr") != NULL);
        int to_is_phys = (strstr(to_type, "PhysAddr") != NULL);
        int to_is_virt = (strstr(to_type, "VirtAddr") != NULL);
        
        /* 物理地址 <-> 虚拟地址 转换绝对禁止 */
        if ((from_is_phys && to_is_virt) || (from_is_virt && to_is_phys)) {
            return 0;  /* 禁止 */
        }
        
        /* 地址 -> 整数：允许但需显式cast */
        if ((from_is_phys || from_is_virt) && strcmp(to_type, "u64") == 0) {
            return 0;  /* 需显式 as u64 */
        }
        
        /* 整数 -> 地址：允许但需显式cast */
        if (strcmp(from_type, "u64") == 0 && (to_is_phys || to_is_virt)) {
            return 0;  /* 需显式 as PhysAddr/VirtAddr */
        }
    }
    
    /* 默认允许（应该在后续实现完整的类型系统后扩展） */
    return 1;
}
