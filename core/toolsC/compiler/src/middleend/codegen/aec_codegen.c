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
 * AethelOS Aethelium Compiler - Code Generator Implementation
 * 代码生成器实现：生成 AETB 二进制格式
 */

#include "aec_codegen.h"
#include "aetb_gen.h"
#include "zero_copy_view.h"
#include "../silicon_semantics_codegen.h"
#include "../silicon_semantics.h"
#include "../builtin_print.h"
#include "../x86_encoder.h"
#include "../hardware_layer.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>

/* 工业级别的错误报告函数 */
static void codegen_error(CodeGenerator *gen, const char *fmt, ...) {
    if (!gen) return;
    
    va_list args;
    va_start(args, fmt);
    
    /* 立即输出到 stderr */
    fprintf(stderr, "[FATAL CODEGEN] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    
    /* 存储错误信息 */
    if (gen->error_count < 64) {
        vsnprintf(gen->errors[gen->error_count], 256, fmt, args);
    }
    vsnprintf(gen->error, sizeof(gen->error), fmt, args);
    
    va_end(args);
    
    gen->error_count++;
}

static void emit(CodeGenerator *gen, const char *fmt, ...) {
    /* 只在非硬件层或汇编模式下输出文本 */
    if (!gen || !gen->output || gen->in_hardware_block) {
        return;
    }
    
    va_list args;
    va_start(args, fmt);
    vfprintf(gen->output, fmt, args);
    va_end(args);
}

/**
 * 直接输出二进制字节（硬件层使用）
 */
static int emit_binary(CodeGenerator *gen, const unsigned char *bytes, int count) {
    if (!gen || !gen->output || count <= 0) {
        return -1;
    }
    
    /* 直接写入二进制字节到输出文件 */
    size_t written = fwrite(bytes, 1, count, gen->output);
    return (written == (size_t)count) ? 0 : -1;
}

static int get_label(CodeGenerator *gen) {
    return gen->label_count++;
}

/* [TODO-06] 装饰器检查助手函数 */
/* 检查AST节点是否有@entry装饰器 */
static int has_entry_decorator(const ASTNode *node) {
    if (!node) return 0;
    
    if (node->type == AST_FUNC_DECL) {
        int i;
        if (!node->data.func_decl.attributes) return 0;
        
        for (i = 0; i < node->data.func_decl.attr_count; i++) {
            const char *attr = node->data.func_decl.attributes[i];
            if (attr && strcmp(attr, "@entry") == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/**
 * get_gate_type - 从函数声明中提取 @gate 装饰器类型
 * 返回值：
 *   NULL - 没有 @gate 装饰器
 *   "interrupt" - @gate(type: \interrupt)
 *   "syscall"   - @gate(type: \syscall)
 *   "naked"     - @gate(type: \naked)
 *   "efi"       - @gate(type: \efi)
 *   "exception" - @gate(type: \exception)
 */
static const char* get_gate_type(const ASTNode *node) {
    if (!node || node->type != AST_FUNC_DECL) return NULL;
    if (!node->data.func_decl.attributes) return NULL;
    
    int i;
    for (i = 0; i < node->data.func_decl.attr_count; i++) {
        const char *attr = node->data.func_decl.attributes[i];
        if (!attr) continue;
        
        /* 解析 @gate(...) 形式的装饰器 */
        if (strncmp(attr, "@gate", 5) == 0) {
            /* 尝试提取 gate 类型
             * 格式: @gate(type: \interrupt), @gate(type: \syscall), 等
             */
            const char *type_start = strstr(attr, "type:");
            if (!type_start) continue;
            
            type_start = strstr(type_start, "\\");
            if (!type_start) continue;
            
            type_start++;  /* 跳过反斜杠 */
            
            /* 提取类型名称 */
            if (strncmp(type_start, "interrupt", 9) == 0) return "interrupt";
            if (strncmp(type_start, "syscall", 7) == 0) return "syscall";
            if (strncmp(type_start, "naked", 5) == 0) return "naked";
            if (strncmp(type_start, "exception", 9) == 0) return "exception";
            if (strncmp(type_start, "efi", 3) == 0) return "efi";
        }
    }
    
    return NULL;
}

/* 前向声明 */
static void gen_statement(CodeGenerator *gen, ASTNode *stmt);
static void gen_expression(CodeGenerator *gen, ASTNode *expr);
static void gen_metal_block(CodeGenerator *gen, ASTNode *metal);
static void gen_asm_block(CodeGenerator *gen, ASTNode *asm_stmt);
static void gen_bytes_block(CodeGenerator *gen, ASTNode *bytes);
static void gen_import_stmt(CodeGenerator *gen, ASTNode *import);
static void gen_struct_decl(CodeGenerator *gen, ASTNode *struct_decl);

/* 泛型类型参数序列化函数 */
/* 用于将泛型类型参数编码为字符串，用于类型检查和代码生成 */
static char* __attribute__((unused)) serialize_type_params(ASTNode *type_node) {
    if (!type_node || type_node->type != AST_TYPE) {
        return NULL;
    }
    
    if (type_node->data.type.type_param_count == 0) {
        return NULL;
    }
    
    char buffer[2048];
    memset(buffer, 0, sizeof(buffer));
    
    strcpy(buffer, "<");
    for (int i = 0; i < type_node->data.type.type_param_count; i++) {
        if (i > 0) strcat(buffer, ",");
        
        ASTNode *param = type_node->data.type.type_params[i];
        if (param && param->type == AST_TYPE) {
            strcat(buffer, param->data.type.name);
        }
    }
    strcat(buffer, ">");
    
    char *result = malloc(strlen(buffer) + 1);
    strcpy(result, buffer);
    return result;
}

/* 获取类型的完整描述字符串，包括泛型参数 */
static char* get_full_type_name(ASTNode *type_node) {
    if (!type_node || type_node->type != AST_TYPE) {
        return "";
    }
    
    char buffer[2048];
    memset(buffer, 0, sizeof(buffer));
    
    strcpy(buffer, type_node->data.type.name);
    
    if (type_node->data.type.type_param_count > 0) {
        strcat(buffer, "<");
        for (int i = 0; i < type_node->data.type.type_param_count; i++) {
            if (i > 0) strcat(buffer, ",");
            
            ASTNode *param = type_node->data.type.type_params[i];
            if (param && param->type == AST_TYPE) {
                strcat(buffer, param->data.type.name);
            }
        }
        strcat(buffer, ">");
    }
    
    char *result = malloc(strlen(buffer) + 1);
    strcpy(result, buffer);
    return result;
}

static void gen_expression(CodeGenerator *gen, ASTNode *expr) {
    if (!expr) return;
    
    switch (expr->type) {
    case AST_LITERAL:
        if (!expr->data.literal.is_float) {
            /* 移动 64 位整数到 rax */
            long long val = expr->data.literal.value.int_value;
            if (val >= -2147483648LL && val <= 2147483647LL) {
                emit(gen, "    movq $%lld, %%rax\n", val);
            } else {
                emit(gen, "    movabs $%lld, %%rax\n", val);
            }
        }
        break;
        
    case AST_IDENT:
        /* 变수をメモリから読み込む */
        emit(gen, "    # Load variable: %s\n", expr->data.ident.name);
        break;
        
    case AST_BINARY_OP: {
        gen_expression(gen, expr->data.binary_op.left);
        emit(gen, "    pushq %%rax\n");
        gen_expression(gen, expr->data.binary_op.right);
        emit(gen, "    popq %%rcx\n");
        
        const char *op = expr->data.binary_op.op;
        if (strcmp(op, "+") == 0) {
            emit(gen, "    addq %%rax, %%rcx\n");
            emit(gen, "    movq %%rcx, %%rax\n");
        } else if (strcmp(op, "-") == 0) {
            emit(gen, "    subq %%rax, %%rcx\n");
            emit(gen, "    movq %%rcx, %%rax\n");
        } else if (strcmp(op, "*") == 0) {
            emit(gen, "    imulq %%rax, %%rcx\n");
            emit(gen, "    movq %%rcx, %%rax\n");
        } else if (strcmp(op, "/") == 0 || strcmp(op, "÷") == 0) {
            emit(gen, "    movq %%rcx, %%rax\n");
            emit(gen, "    cqo\n");
            emit(gen, "    idivq %%rcx\n");
        } else if (strcmp(op, "==") == 0) {
            emit(gen, "    cmpq %%rax, %%rcx\n");
            emit(gen, "    sete %%al\n");
            emit(gen, "    movzbl %%al, %%eax\n");
        } else if (strcmp(op, "!=") == 0) {
            emit(gen, "    cmpq %%rax, %%rcx\n");
            emit(gen, "    setne %%al\n");
            emit(gen, "    movzbl %%al, %%eax\n");
        } else if (strcmp(op, "&") == 0) {
            /* 按位与 */
            emit(gen, "    andq %%rax, %%rcx\n");
            emit(gen, "    movq %%rcx, %%rax\n");
        } else if (strcmp(op, "|") == 0) {
            /* 按位或 */
            emit(gen, "    orq %%rax, %%rcx\n");
            emit(gen, "    movq %%rcx, %%rax\n");
        } else if (strcmp(op, "^") == 0) {
            /* 按位异或 */
            emit(gen, "    xorq %%rax, %%rcx\n");
            emit(gen, "    movq %%rcx, %%rax\n");
        } else if (strcmp(op, "<<") == 0) {
            /* 左移 */
            emit(gen, "    movq %%rax, %%rcx\n");
            emit(gen, "    shlq %%cl, %%rcx\n");
            emit(gen, "    movq %%rcx, %%rax\n");
        } else if (strcmp(op, ">>") == 0) {
            /* 右移 */
            emit(gen, "    movq %%rax, %%rcx\n");
            emit(gen, "    sarq %%cl, %%rcx\n");
            emit(gen, "    movq %%rcx, %%rax\n");
        }
        break;
    }
        
    case AST_CALL:
        /* cast<Type>(expr) / ptr<Type>(expr) are compile-time reinterpret casts, not runtime calls. */
        if (expr->data.call.func && expr->data.call.func->type == AST_IDENT &&
            expr->data.call.func->data.ident.name) {
            const char *callee = expr->data.call.func->data.ident.name;
            if (strcmp(callee, "cast") == 0 || strcmp(callee, "ptr") == 0 ||
                strncmp(callee, "cast<", 5) == 0 || strncmp(callee, "ptr<", 4) == 0) {
                if (expr->data.call.arg_count > 0 && expr->data.call.args && expr->data.call.args[0]) {
                    gen_expression(gen, expr->data.call.args[0]);
                }
                break;
            }
        }

        /* 检测内置print()函数 */
        if (is_print_function_call(expr)) {
            PrintCompileContext ctx = {
                .target_format = gen->target_format,
                .machine_bits = gen->machine_bits,
                .target_isa = gen->target_isa,
                .use_uefi = gen->use_uefi,
                .use_syscall = gen->use_syscall,
                .raw_metal = 0
            };
            if (codegen_print_call(&ctx, expr, gen) == 0) {
                break;  /* print()由内置处理器完成 */
            }
        }
        
        /* 普通函数调用 */
        emit(gen, "    # Function call\n");
        for (int i = 0; i < expr->data.call.arg_count; i++) {
            gen_expression(gen, expr->data.call.args[i]);
        }
        if (expr->data.call.func->type == AST_IDENT) {
            emit(gen, "    call %s\n", expr->data.call.func->data.ident.name);
        }
        break;
    
    case AST_STRUCT_INIT: {
        /* 结构体初始化 - 简化版本，只生成字段值的计算 */
        emit(gen, "    # Struct init: %s\n", expr->data.struct_init.type_name);
        for (int i = 0; i < expr->data.struct_init.field_count; i++) {
            emit(gen, "    # Field %s:\n", expr->data.struct_init.field_names[i]);
            gen_expression(gen, expr->data.struct_init.field_values[i]);
        }
        break;
    }
    
    case AST_IMPLICIT_PARAM:
        /* 隐式参数 $0, $1, $2 等 - 编译为参数访问 */
        emit(gen, "    # Implicit param: $%d\n", expr->data.implicit_param.index);
        /* 在实际编译中，这应该转换为从参数寄存器或栈中读取 */
        /* 对于 x86-64，第一个参数在 rdi, 第二个在 rsi, 等等 */
        break;
    
    case AST_CLOSURE:
        /* 闭包表达式 { ... } */
        emit(gen, "    # Closure expression\n");
        if (expr->data.closure.body) {
            gen_expression(gen, expr->data.closure.body);
        }
        break;
    
    case AST_FORCE_UNWRAP:
        /* 强制解包 expr! */
        emit(gen, "    # Force unwrap\n");
        gen_expression(gen, expr->data.force_unwrap.operand);
        break;
    
    case AST_REFERENCE:
        /* 引用 &value - 生成地址 */
        emit(gen, "    # Reference: &expr\n");
        if (expr->data.reference.operand->type == AST_IDENT) {
            emit(gen, "    lea %s, %%rax\n", expr->data.reference.operand->data.ident.name);
        } else {
            gen_expression(gen, expr->data.reference.operand);
        }
        break;
    
    case AST_TUPLE_LITERAL:
        /* 元组字面量 (x, y, z) */
        emit(gen, "    # Tuple literal with %d elements\n", expr->data.tuple_literal.element_count);
        for (int i = 0; i < expr->data.tuple_literal.element_count; i++) {
            emit(gen, "    # Tuple element %d:\n", i);
            gen_expression(gen, expr->data.tuple_literal.elements[i]);
            /* 在实际实现中，应该按照调用约定将元素放入寄存器或栈 */
            if (i > 0) {
                emit(gen, "    pushq %%rax\n");  /* 保存前面的值 */
            }
        }
        break;
    
    case AST_UNARY_OP: {
        const char *op = expr->data.unary_op.op;
        
        if (strcmp(op, "*") == 0) {
            /* 解引用 *ptr */
            emit(gen, "    # Dereference: *expr\n");
            gen_expression(gen, expr->data.unary_op.operand);
            emit(gen, "    movq (%%rax), %%rax\n");  /* 从指针指向的地址读取 */
        } else if (strcmp(op, "-") == 0) {
            /* 取负 */
            gen_expression(gen, expr->data.unary_op.operand);
            emit(gen, "    negq %%rax\n");
        } else if (strcmp(op, "!") == 0) {
            /* 逻辑非 */
            gen_expression(gen, expr->data.unary_op.operand);
            emit(gen, "    testq %%rax, %%rax\n");
            emit(gen, "    sete %%al\n");
            emit(gen, "    movzbl %%al, %%eax\n");
        } else if (strcmp(op, "~") == 0) {
            /* 按位非 */
            gen_expression(gen, expr->data.unary_op.operand);
            emit(gen, "    notq %%rax\n");
        } else {
            emit(gen, "    # Unary op: %s\n", op);
            gen_expression(gen, expr->data.unary_op.operand);
        }
        break;
    }
    
    case AST_TYPECAST: {
        /* 工业级零拷贝视图实现
         * 处理 expr as view<T> 的类型转换
         */
        if (!expr->data.typecast.target_type) {
            break;
        }
        
        ASTNode *target = expr->data.typecast.target_type;
        const char *target_type = (target->type == AST_TYPE) ? target->data.type.name : "";
        
        /* 生成源表达式 */
        gen_expression(gen, expr->data.typecast.expr);
        
        /* 检查是否为零拷贝视图转换 */
        if (strncmp(target_type, "view<", 5) == 0) {
            /* 零拷贝视图转换：验证类型有效性 */
            if (zcv_validate_view_type(target_type) == 0) {
                /* 生成零拷贝视图的x86-64机器码 */
                zcv_gen_view_cast(gen->output, "rax", target_type, "typecast_view");
                emit(gen, "    /* Zero-copy view cast successful: %s */\n", target_type);
            } else {
                codegen_error(gen, "Invalid view<T> type: %s", target_type);
            }
        } else if (strncmp(target_type, "PhysAddr", 8) == 0 || 
                   strncmp(target_type, "VirtAddr", 8) == 0) {
            /* 物理/虚拟地址强类型转换 - 只需类型标记，无运行时开销 */
            emit(gen, "    /* Address strong-type cast: %s (no runtime overhead) */\n", target_type);
        } else {
            /* 普通类型转换或指针转换 */
            emit(gen, "    /* Type cast to %s */\n", target_type);
            /* 根据目标类型调整值（符号扩展、截断等） */
            if (strcmp(target_type, "UInt8") == 0) {
                emit(gen, "    movzbl %%al, %%eax\n");
            } else if (strcmp(target_type, "Int8") == 0) {
                emit(gen, "    movsbl %%al, %%eax\n");
            }
        }
        break;
    }
        
    default:
        break;
    }
}

/* ========== 系统块代码生成函数 ========== */

/**
 * gen_metal_block - 生成 metal { ... } 块
 * metal 块用于系统级操作，如内存管理、硬件访问等
 */
static void gen_metal_block(CodeGenerator *gen, ASTNode *metal) {
    if (!metal || !metal->data.metal_block.statements) {
        return;
    }
    
    emit(gen, "    # Begin metal block\n");
    
    /* 遍历 metal 块内的所有语句 */
    for (int i = 0; i < metal->data.metal_block.stmt_count; i++) {
        gen_statement(gen, metal->data.metal_block.statements[i]);
    }
    
    emit(gen, "    # End metal block\n");
}

/**
 * gen_asm_block - 生成 asm { "code" } 块
 * 支持标准的Aethelium asm语法：
 *   asm { "code" : outputs : inputs : clobber }
 * 
 * 直接输出汇编代码，自动处理约束中的变量替换
 */
static void gen_asm_block(CodeGenerator *gen, ASTNode *asm_stmt) {
    if (!asm_stmt || !asm_stmt->data.asm_block.code) {
        return;
    }
    
    const char *asm_code = asm_stmt->data.asm_block.code;
    
    emit(gen, "    # Begin inline assembly (Aethelium asm)\n");
    
    /* 支持以下asm格式：
       1. asm { "code" } - 简单的汇编指令
       2. asm { "code" : outputs : inputs : clobber } - 标准LLVM风格
       3. asm { code_without_strings } - 支持old AT&T风格 (%0, %%rcx等)
    */
    
    /* 过滤和处理asm代码，移除约束部分，只输出实际的汇编指令 */
    char processed_code[8192] = "";
    const char *p = asm_code;
    int in_constraint = 0;  /* 是否在约束部分 */
    int colon_count = 0;    /* 冒号计数，确定约束部分的开始 */
    
    while (*p) {
        if (*p == ':') {
            /* 开始约束部分 */
            if (colon_count > 0) {
                in_constraint = 1;
            }
            colon_count++;
            p++;
            continue;
        }
        
        if (!in_constraint && *p != ':') {
            strncat(processed_code, p, 1);
        }
        p++;
    }
    
    /* 输出处理后的汇编代码 */
    emit(gen, "%s\n", processed_code[0] ? processed_code : asm_code);
    emit(gen, "    # End inline assembly\n");
}

/**
 * gen_bytes_block - 生成 bytes { "HEX"... } 块
 * 将十六进制字符串转换为字节码并直接写入到二进制输出
 */
static void gen_bytes_block(CodeGenerator *gen, ASTNode *bytes) {
    if (!bytes || !bytes->data.bytes_block.hex_strings) {
        return;
    }
    
    emit(gen, "    # Begin bytes block\n");
    
    /* 遍历所有十六进制字符串 */
    for (int i = 0; i < bytes->data.bytes_block.hex_count; i++) {
        const char *hex_str = bytes->data.bytes_block.hex_strings[i];
        
        /* 注释形式记录字节码 */
        emit(gen, "    # Hex bytes: %s\n", hex_str);
        
        /* 解析十六进制字符串并转换为字节
         * 这里我们只生成注释，实际编译时由 AETB 生成器处理 */
    }
    
    emit(gen, "    # End bytes block\n");
}

/**
 * gen_import_stmt - 生成 import/Rimport 语句
 * 处理模块导入和外部符号链接
 */
static void gen_import_stmt(CodeGenerator *gen, ASTNode *import) {
    if (!import || !import->data.import_stmt.module) {
        return;
    }
    
    const char *module = import->data.import_stmt.module;
    int is_rimport = import->data.import_stmt.is_rimport;
    
    if (is_rimport) {
        emit(gen, "    # Rimport: %s (Resolve import)\n", module);
    } else {
        emit(gen, "    # Import: %s\n", module);
    }
    
    /* 标记符号为外部导入，以便链接器处理 */
}

/**
 * gen_struct_decl - 生成结构体声明，支持装饰器属性和泛型字段类型
 */
static void gen_struct_decl(CodeGenerator *gen, ASTNode *struct_decl) {
    if (!struct_decl) {
        return;
    }
    
    const char *struct_name = struct_decl->data.struct_decl.name;
    
    emit(gen, "    # Struct declaration: %s\n", struct_name);
    
    /* 处理装饰器属性 */
    if (struct_decl->data.struct_decl.attributes) {
        int attr_count = struct_decl->data.struct_decl.attr_count;
        for (int i = 0; i < attr_count; i++) {
            const char *attr = struct_decl->data.struct_decl.attributes[i];
            if (attr) {
                if (strncmp(attr, "@packed", 7) == 0) {
                    emit(gen, "    # Attribute: packed (no padding)\n");
                } else if (strncmp(attr, "@aligned", 8) == 0) {
                    emit(gen, "    # Attribute: aligned\n");
                } else if (strncmp(attr, "@entry", 6) == 0) {
                    emit(gen, "    # Attribute: entry point\n");
                }
            }
        }
    }
    
    /* 处理结构体字段的泛型类型 */
    if (struct_decl->data.struct_decl.field_names && 
        struct_decl->data.struct_decl.field_types) {
        for (int i = 0; i < struct_decl->data.struct_decl.field_count; i++) {
            const char *field_name = 
                struct_decl->data.struct_decl.field_names[i];
            ASTNode *field_type = 
                struct_decl->data.struct_decl.field_types[i];
            
            /* 记录字段的类型，包括泛型参数 */
            if (field_type && field_type->type == AST_TYPE) {
                char *full_type = get_full_type_name(field_type);
                emit(gen, "    # Field %s: %s\n", field_name, full_type);
                free(full_type);
            } else if (field_name) {
                emit(gen, "    # Field %s\n", field_name);
            }
        }
    }
}

static void gen_statement(CodeGenerator *gen, ASTNode *stmt) {
    if (!stmt) return;
    
    switch (stmt->type) {
    case AST_RETURN_STMT:
        if (stmt->data.return_stmt.value) {
            gen_expression(gen, stmt->data.return_stmt.value);
        }
        emit(gen, "    ret\n");
        break;
    
    case AST_ASSIGNMENT: {
        /* 赋值：生成右侧，然后根据左侧类型存储 */
        ASTNode *left = stmt->data.assignment.left;
        ASTNode *right = stmt->data.assignment.right;
        
        /* 检查硬件层物理寄存器赋值 */
        if (gen->in_hardware_block && left && left->type == AST_IDENT) {
            const char *reg_name = left->data.ident.name;
            
            /* 检查是否为x86-64的GPR */
            int reg_id = -1;
            if (strcmp(reg_name, "rax") == 0) { reg_id = 0; }
            else if (strcmp(reg_name, "rbx") == 0) { reg_id = 3; }
            else if (strcmp(reg_name, "rcx") == 0) { reg_id = 1; }
            else if (strcmp(reg_name, "rdx") == 0) { reg_id = 2; }
            else if (strcmp(reg_name, "rsi") == 0) { reg_id = 6; }
            else if (strcmp(reg_name, "rdi") == 0) { reg_id = 7; }
            else if (strcmp(reg_name, "rbp") == 0) { reg_id = 5; }
            else if (strcmp(reg_name, "rsp") == 0) { reg_id = 4; }
            else if (strcmp(reg_name, "r8") == 0) { reg_id = 8; }
            else if (strcmp(reg_name, "r9") == 0) { reg_id = 9; }
            else if (strcmp(reg_name, "r10") == 0) { reg_id = 10; }
            else if (strcmp(reg_name, "r11") == 0) { reg_id = 11; }
            else if (strcmp(reg_name, "r12") == 0) { reg_id = 12; }
            else if (strcmp(reg_name, "r13") == 0) { reg_id = 13; }
            else if (strcmp(reg_name, "r14") == 0) { reg_id = 14; }
            else if (strcmp(reg_name, "r15") == 0) { reg_id = 15; }
            
            /* 工业级直接二进制生成 */
            if (reg_id >= 0) {
                unsigned char code[10];
                int len = 0;
                
                /* 情况1：右侧是字面值 */
                if (right && right->type == AST_LITERAL && right->data.literal.is_float == 0) {
                    int64_t value = right->data.literal.value.int_value;
                    
                    /* 智能选择编码形式 */
                    if (value >= -2147483648LL && value <= 2147483647LL) {
                        /* MOV r64, imm32 (sign-extend) - 7 bytes */
                        unsigned char rex = 0x48;
                        if (reg_id > 7) rex |= 0x01;
                        unsigned char modrm = 0xC0 | (reg_id & 0x7);
                        
                        code[0] = rex;
                        code[1] = 0xC7;
                        code[2] = modrm;
                        code[3] = (unsigned char)(value & 0xFF);
                        code[4] = (unsigned char)((value >> 8) & 0xFF);
                        code[5] = (unsigned char)((value >> 16) & 0xFF);
                        code[6] = (unsigned char)((value >> 24) & 0xFF);
                        len = 7;
                    } else {
                        /* MOV r64, imm64 - 10 bytes */
                        unsigned char rex = 0x48;
                        if (reg_id > 7) rex |= 0x01;
                        unsigned char opcode = 0xB8 | (reg_id & 0x7);
                        
                        code[0] = rex;
                        code[1] = opcode;
                        for (int i = 0; i < 8; i++) {
                            code[2 + i] = (unsigned char)((value >> (i * 8)) & 0xFF);
                        }
                        len = 10;
                    }
                    
                    if (emit_binary(gen, code, len) != 0) {
                        codegen_error(gen, "Failed to emit MOV %s, 0x%lx", reg_name, (unsigned long)value);
                    }
                    break;
                }
                
                /* 情况2：右侧是参数引用（标准x86-64 ABI SysV约定）*/
                else if (right && right->type == AST_IDENT) {
                    const char *src_var = right->data.ident.name;
                    
                    /* 标准x86-64 ABI整数参数寄存器映射：
                       arg 0 (1st): RDI   (reg_id=7)
                       arg 1 (2nd): RSI   (reg_id=6)
                       arg 2 (3rd): RDX   (reg_id=2)
                       arg 3 (4th): RCX   (reg_id=1)
                       arg 4 (5th): R8    (reg_id=8)
                       arg 5 (6th): R9    (reg_id=9)
                    */
                    int src_reg_id = -1;
                    static const char *param_names[] = {
                        "sysnum", "arg1", "arg2", "arg3", "arg4", "arg5"
                    };
                    static int param_regs[] = {7, 6, 2, 1, 8, 9};  /* RDI, RSI, RDX, RCX, R8, R9 */
                    
                    for (int i = 0; i < 6; i++) {
                        if (strcmp(src_var, param_names[i]) == 0) {
                            src_reg_id = param_regs[i];
                            break;
                        }
                    }
                    
                    /* 如果找到了参数对应的寄存器，生成MOV r64, r64指令 */
                    if (src_reg_id >= 0 && src_reg_id != reg_id) {
                        /* MOV dst_reg, src_reg - 3 bytes */
                        unsigned char rex = 0x48;
                        if (src_reg_id > 7 || reg_id > 7) {
                            if (src_reg_id > 7) rex |= 0x01;  /* REX.B for source */
                            if (reg_id > 7) rex |= 0x04;       /* REX.R for dest */
                        }
                        unsigned char opcode = 0x89;  /* MOV r/m64, r64 */
                        unsigned char modrm = 0xC0 | ((src_reg_id & 0x7) << 3) | (reg_id & 0x7);
                        
                        code[0] = rex;
                        code[1] = opcode;
                        code[2] = modrm;
                        len = 3;
                        
                        if (emit_binary(gen, code, len) != 0) {
                            codegen_error(gen, "Failed to emit MOV %s, %s", reg_name, src_var);
                        }
                        break;
                    }
                }
            }
        }
        
        /* 普通赋值处理（汇编模式） */
        gen_expression(gen, right);
        emit(gen, "    pushq %%rax\n");  /* 保存右侧值 */
        
        /* 根据左侧类型生成存储代码 */
        if (left->type == AST_IDENT) {
            emit(gen, "    popq %%rax\n");
            emit(gen, "    # Store to variable: %s\n", left->data.ident.name);
        } else if (left->type == AST_UNARY_OP && strcmp(left->data.unary_op.op, "*") == 0) {
            emit(gen, "    popq %%rax\n");
            gen_expression(gen, left->data.unary_op.operand);
            emit(gen, "    popq %%rcx\n");
            emit(gen, "    movq %%rcx, (%%rax)\n");
        } else if (left->type == AST_ACCESS) {
            emit(gen, "    popq %%rax\n");
            emit(gen, "    # Store to member: %s\n", left->data.access.member);
        } else {
            emit(gen, "    popq %%rax\n");
            emit(gen, "    # Generic assignment\n");
        }
        break;
    }
        
    case AST_EXPR_STMT:
        gen_expression(gen, stmt->data.assignment.left);
        break;
        
    case AST_VAR_DECL:
        if (strcmp(stmt->data.var_decl.name, "__tuple_pattern__") == 0) {
            /* 元组解构赋值 */
            emit(gen, "    # Tuple destructuring\n");
            if (stmt->data.var_decl.init) {
                gen_expression(gen, stmt->data.var_decl.init);
                /* 在实际实现中，应该提取元组的各个元素并绑定到变量 */
            }
        } else {
            /* 普通变量声明 */
            /* 处理变量的类型，包括泛型类型参数 */
            if (stmt->data.var_decl.type && stmt->data.var_decl.type->type == AST_TYPE) {
                char *full_type = get_full_type_name(stmt->data.var_decl.type);
                emit(gen, "    # Variable: %s with type: %s\n", 
                     stmt->data.var_decl.name, full_type);
                free(full_type);
            } else {
                emit(gen, "    # Variable: %s\n", stmt->data.var_decl.name);
            }
            
            if (stmt->data.var_decl.init) {
                gen_expression(gen, stmt->data.var_decl.init);
            }
        }
        break;
        
    case AST_BLOCK:
        for (int i = 0; i < stmt->data.block.stmt_count; i++) {
            gen_statement(gen, stmt->data.block.statements[i]);
        }
        break;
        
    case AST_IF_STMT: {
        int label_else = get_label(gen);
        int label_end = get_label(gen);
        
        gen_expression(gen, stmt->data.if_stmt.condition);
        emit(gen, "    testq %%rax, %%rax\n");
        emit(gen, "    jz .L%d\n", label_else);
        gen_statement(gen, stmt->data.if_stmt.then_branch);
        emit(gen, "    jmp .L%d\n", label_end);
        emit(gen, ".L%d:\n", label_else);
        if (stmt->data.if_stmt.else_branch) {
            gen_statement(gen, stmt->data.if_stmt.else_branch);
        }
        emit(gen, ".L%d:\n", label_end);
        break;
    }
        
    case AST_WHILE_STMT: {
        int label_loop = get_label(gen);
        int label_end = get_label(gen);
        
        emit(gen, ".L%d:\n", label_loop);
        gen_expression(gen, stmt->data.while_stmt.condition);
        emit(gen, "    testq %%rax, %%rax\n");
        emit(gen, "    jz .L%d\n", label_end);
        gen_statement(gen, stmt->data.while_stmt.body);
        emit(gen, "    jmp .L%d\n", label_loop);
        emit(gen, ".L%d:\n", label_end);
        break;
    }
    
    case AST_MATCH_STMT: {
        /* 简化版本：只生成 match 表达式的求值 */
        if (stmt->data.match_stmt.expr) {
            gen_expression(gen, stmt->data.match_stmt.expr);
        }
        emit(gen, "    # Match statement (simplified)\n");
        break;
    }
    
    case AST_SWITCH_STMT: {
        /* Switch 语句代码生成 */
        int label_default = -1;
        int label_end = get_label(gen);
        
        /* 生成 switch 表达式求值 */
        if (stmt->data.switch_stmt.expr) {
            gen_expression(gen, stmt->data.switch_stmt.expr);
            emit(gen, "    pushq %%rax\n");  /* 保存 switch 值 */
        }
        
        /* 为每个 case 生成跳转 */
        int *case_labels = (int*)malloc(sizeof(int) * stmt->data.switch_stmt.case_count);
        for (int i = 0; i < stmt->data.switch_stmt.case_count; i++) {
            case_labels[i] = get_label(gen);
        }
        
        if (stmt->data.switch_stmt.default_case) {
            label_default = get_label(gen);
        }
        
        /* 简化版本：生成 case 标签和代码 */
        for (int i = 0; i < stmt->data.switch_stmt.case_count; i++) {
            ASTNode *case_node = stmt->data.switch_stmt.cases[i];
            emit(gen, ".L%d:  # case %d\n", case_labels[i], i);
            
            /* 生成 case 体中的语句 */
            for (int j = 0; j < case_node->data.switch_case.stmt_count; j++) {
                gen_statement(gen, case_node->data.switch_case.statements[j]);
            }
            emit(gen, "    jmp .L%d\n", label_end);
        }
        
        /* 生成 default 代码 */
        if (stmt->data.switch_stmt.default_case) {
            emit(gen, ".L%d:  # default\n", label_default);
            ASTNode *def_case = stmt->data.switch_stmt.default_case;
            for (int j = 0; j < def_case->data.switch_case.stmt_count; j++) {
                gen_statement(gen, def_case->data.switch_case.statements[j]);
            }
        }
        
        emit(gen, ".L%d:  # end switch\n", label_end);
        emit(gen, "    popq %%rax\n");  /* 恢复栈 */
        free(case_labels);
        break;
    }
    
    case AST_GUARD_STMT: {
        /* Guard 语句代码生成
         * guard let binding = expr else { ... }
         * 简化版本：求值表达式，如果为假则跳转到 else 分支 */
        
        int label_else = get_label(gen);
        int label_end = get_label(gen);
        
        if (stmt->data.guard_stmt.binding_expr) {
            /* guard let 形式 */
            gen_expression(gen, stmt->data.guard_stmt.binding_expr);
            emit(gen, "    testq %%rax, %%rax\n");
            emit(gen, "    jz .L%d\n", label_else);
        } else if (stmt->data.guard_stmt.condition) {
            /* guard condition 形式 */
            gen_expression(gen, stmt->data.guard_stmt.condition);
            emit(gen, "    testq %%rax, %%rax\n");
            emit(gen, "    jz .L%d\n", label_else);
        }
        
        /* 如果满足条件，继续执行（什么都不做，直接跳到 end） */
        emit(gen, "    jmp .L%d\n", label_end);
        
        /* else 分支 */
        emit(gen, ".L%d:\n", label_else);
        if (stmt->data.guard_stmt.else_branch) {
            gen_statement(gen, stmt->data.guard_stmt.else_branch);
        }
        
        emit(gen, ".L%d:\n", label_end);
        break;
    }
    
    case AST_DEFER_STMT: {
        /* Defer 语句 - 记录需要在作用域退出时执行的代码
         * 简化版本：直接执行（真正的 defer 需要运行时支持堆栈） */
        emit(gen, "    # Defer statement (runtime stack required)\n");
        gen_statement(gen, stmt->data.defer_stmt.body);
        break;
    }
    
    case AST_METAL_BLOCK:
        gen_metal_block(gen, stmt);
        break;
    
    case AST_ASM_BLOCK:
        gen_asm_block(gen, stmt);
        break;
    
    case AST_BYTES_BLOCK:
        gen_bytes_block(gen, stmt);
        break;
    
    case AST_IMPORT_STMT:
        gen_import_stmt(gen, stmt);
        break;
    
    case AST_STRUCT_DECL:
        gen_struct_decl(gen, stmt);
        break;
    
    /* 硅基语义节点 - 工业级实现 */
    case AST_SILICON_BLOCK:
        /* 硅基语义块的处理 */
        silicon_codegen_silicon_block(gen->output, stmt);
        break;
    
    case AST_MICROARCH_CONFIG:
        /* 微架构配置 */
        silicon_codegen_microarch_config(gen->output, stmt);
        break;
    
    case AST_PIPELINE_BLOCK:
        /* 流水线块 */
        silicon_codegen_pipeline_block(gen->output, stmt);
        break;
    
    case AST_PIPELINE_BARRIER:
        /* 管线屏障 */
        silicon_codegen_pipeline_barrier(gen->output, stmt);
        break;
    
    case AST_PIPELINE_HINT:
        /* 管线提示 */
        silicon_codegen_pipeline_hint(gen->output, stmt);
        break;
    
    case AST_CACHE_OPERATION:
        /* 缓存操作 */
        silicon_codegen_cache_operation(gen->output, stmt);
        break;
    
    case AST_PREFETCH_OPERATION:
        /* 预取操作 */
        silicon_codegen_prefetch_operation(gen->output, stmt);
        break;
    
    case AST_SYNTAX_OPCODE_DEF:
        /* 操作码定义 */
        silicon_codegen_syntax_opcode(gen->output, stmt);
        break;
    
    case AST_PHYS_TYPE:
        /* 物理硬连线类型 */
        silicon_codegen_phys_type(gen->output, stmt);
        break;
    
    /* ==================== 硬件层节点代码生成 ==================== */
    
    case AST_HW_BLOCK: {
        /* hardware { ... } 硬件层块 - 工业级直接二进制生成 */
        int saved_hardware_flag = gen->in_hardware_block;
        gen->in_hardware_block = 1;
        
        if (stmt->data.hw_block.stmt_count > 0) {
            for (int i = 0; i < stmt->data.hw_block.stmt_count; i++) {
                gen_statement(gen, stmt->data.hw_block.statements[i]);
            }
        }
        
        gen->in_hardware_block = saved_hardware_flag;
        break;
    }
    
    case AST_HW_ISA_CALL: {
        /** hardware\isa\*() ISA 直通调用 - 工业级实现
         * 
         * 前端已经预计算了完整的操作码字节，包括：
         * - REX 前缀（如果需要）
         * - 操作码字节
         * - ModRM/SIB 字节
         * - 立即数
         * - 位移
         * 
         * 代码生成器的责任仅仅是将这些字节按原样输出。
         * 不做任何假设，不做任何简化。
         */
        if (!gen->in_hardware_block) {
            codegen_error(gen, "ISA calls only allowed in hardware blocks");
            break;
        }
        
        /* 直接输出前端预计算的所有操作码字节 */
        if (stmt->data.hw_isa_call.opcode_length > 0 && 
            stmt->data.hw_isa_call.opcode_length <= 16) {
            
            if (emit_binary(gen, stmt->data.hw_isa_call.opcode_bytes, 
                           stmt->data.hw_isa_call.opcode_length) != 0) {
                codegen_error(gen, "Failed to emit ISA call machine code: %s", 
                             stmt->data.hw_isa_call.isa_operation);
            }
        } else {
            /* 如果没有预计算的操作码（这不应该发生），记录错误 */
            codegen_error(gen, "Invalid ISA call: missing or invalid opcode bytes for %s",
                         stmt->data.hw_isa_call.isa_operation);
        }
        break;
    }
    
    case AST_HW_REG_BINDING: {
        /* var x: reg<"rax", UInt64> 物理寄存器绑定 */
        const char *reg_name = stmt->data.hw_reg_binding.reg_name;
        const char *binding_name = stmt->data.hw_reg_binding.binding_name;
        emit(gen, "    # Physical register binding: %s -> %s\n", binding_name, reg_name);
        break;
    }
    
    case AST_HW_VOLATILE_VIEW: {
        /* @volatile view<T> 挥发性视图 */
        emit(gen, "    # Volatile memory view\n");
        if (stmt->data.hw_volatile_view.base_address) {
            gen_expression(gen, stmt->data.hw_volatile_view.base_address);
        }
        break;
    }
    
    case AST_HW_GATE_FUNC: {
        /* @gate(type: \interrupt) 门函数 */
        const char *gate_type = stmt->data.hw_gate_func.gate_type_name;
        emit(gen, "    # Gate function: %s\n", gate_type);
        
        if (stmt->data.hw_gate_func.body) {
            gen_statement(gen, stmt->data.hw_gate_func.body);
        }
        break;
    }
    
    case AST_HW_MORPH_BLOCK: {
        /* morph { ... } 形态置换块 */
        emit(gen, "    # Morph block (context switching)\n");
        if (stmt->data.hw_morph_block.stmt_count > 0) {
            for (int i = 0; i < stmt->data.hw_morph_block.stmt_count; i++) {
                gen_statement(gen, stmt->data.hw_morph_block.statements[i]);
            }
        }
        break;
    }
    
    case AST_HW_VECTOR_TYPE: {
        /* vector<T, N> 向量类型 */
        emit(gen, "    # Vector type: size=%d, unit=%s\n", 
             stmt->data.hw_vector_type.vector_size,
             stmt->data.hw_vector_type.vector_unit);
        break;
    }
    
    case AST_HW_CONTROL_REG: {
        /* CPU/Current\Control\CR0 控制寄存器访问 */
        const char *reg_name = stmt->data.hw_control_reg.reg_name;
        
        if (stmt->data.hw_control_reg.value) {
            emit(gen, "    # Write control register: %s\n", reg_name);
            gen_expression(gen, stmt->data.hw_control_reg.value);
            
            /* 根据寄存器名生成正确的mov指令 */
            if (strncmp(reg_name, "CR", 2) == 0) {
                char cr_num = reg_name[2];  /* 'R', '2', '3', '4', '8' */
                if (cr_num == '0' || cr_num == '2' || cr_num == '3' || 
                    cr_num == '4' || cr_num == '8') {
                    emit(gen, "    mov %%rax, %%cr%c\n", cr_num);
                }
            }
        } else {
            emit(gen, "    # Read control register: %s\n", reg_name);
            
            if (strncmp(reg_name, "CR", 2) == 0) {
                char cr_num = reg_name[2];
                if (cr_num == '0' || cr_num == '2' || cr_num == '3' || 
                    cr_num == '4' || cr_num == '8') {
                    emit(gen, "    mov %%cr%c, %%rax\n", cr_num);
                }
            }
        }
        break;
    }
    
    case AST_HW_FLAG_ACCESS: {
        /* CPU/Flags\Carry 标志位访问 */
        const char *flag_name = stmt->data.hw_flag_access.flag_name;
        
        if (strcmp(flag_name, "Interrupt/Enable") == 0 || strcmp(flag_name, "IF") == 0) {
            if (stmt->data.hw_flag_access.value) {
                emit(gen, "    sti                # Enable interrupts\n");
            } else {
                emit(gen, "    cli                # Disable interrupts\n");
            }
        } else if (stmt->data.hw_flag_access.value) {
            emit(gen, "    # Set flag: %s\n", flag_name);
            gen_expression(gen, stmt->data.hw_flag_access.value);
            emit(gen, "    push %%rax\n");
            emit(gen, "    popfq\n");
        } else {
            emit(gen, "    # Read flag: %s\n", flag_name);
            emit(gen, "    pushfq\n");
            emit(gen, "    pop %%rax\n");
        }
        break;
    }
        
    default:
        break;
    }
}

typedef struct {
    uint8_t *data;
    size_t size;
    size_t cap;
} McBuf;

typedef struct {
    char *name;
    uint64_t offset;
    uint64_t size;
} McFuncSymbol;

typedef struct {
    char *target_name;
    size_t rel_off;
    int rel_width;
} McCallFixup;

typedef struct {
    int bound;
    size_t pos;
} McLabel;

typedef struct {
    size_t rel_off;
    int rel_width;
    int label_id;
} McBranchFixup;

typedef enum {
    MC_SYM_PARAM = 0,
    MC_SYM_STACK = 1,
    MC_SYM_REG_ALIAS = 2,
    MC_SYM_CONST = 3,
    MC_SYM_VEC_ALIAS = 4
} McSymbolKind;

typedef struct {
    char *name;
    McSymbolKind kind;
    int param_index;
    int32_t stack_offset; /* rbp-relative negative offset */
    int reg_id;
    int vec_reg_id;
    uint64_t const_value;
    char *type_name;
} McSymbol;

typedef struct {
    char *name;
    uint32_t offset;
    uint32_t size;
    uint32_t elem_size;
} McStructField;

typedef struct {
    char *name;
    McStructField *fields;
    size_t field_count;
    size_t field_cap;
    uint32_t byte_size;
} McStructInfo;

typedef struct {
    AETBGenerator *aetb;
    McBuf code;
    McFuncSymbol *funcs;
    size_t func_count;
    size_t func_cap;
    McCallFixup *calls;
    size_t call_count;
    size_t call_cap;
    McLabel *labels;
    size_t label_count;
    size_t label_cap;
    McBranchFixup *branches;
    size_t branch_count;
    size_t branch_cap;
    int has_explicit_return;
    size_t current_func_start;
    uint64_t entry_point;
    int machine_bits;
    const char *target_isa;
    int in_hardware_block;  /* 硬件块上下文标志 */
    McSymbol *local_symbols;
    size_t local_symbol_count;
    size_t local_symbol_cap;
    McSymbol *const_symbols;
    size_t const_symbol_count;
    size_t const_symbol_cap;
    McStructInfo *structs;
    size_t struct_count;
    size_t struct_cap;
    int pending_cs_valid;
    uint16_t pending_cs_selector;
    int32_t stack_allocated;
    int32_t planned_local_bytes;
    int target_format;
    int current_func_is_efi;
    int32_t uefi_system_table_offset;
} McCtx;

static int mc_emit_mov_acc_arg(McCtx *ctx, int arg_idx);
static int mc_emit_mov_acc_from_reg_id(McCtx *ctx, int reg_id);
static int mc_emit_load_acc_from_stack(McCtx *ctx, int32_t stack_offset);
static int mc_emit_expr(McCtx *ctx, ASTNode *expr);
static int mc_emit_mov_acc_imm(McCtx *ctx, int64_t v);
static uint32_t mc_array_elem_size_from_type_name(const char *type_name, uint32_t fallback_size);
static int mc_eval_const_expr(McCtx *ctx, ASTNode *expr, uint64_t *out);

#define RELOC_PC32 2u

static int mc_reserve(McBuf *b, size_t add) {
    size_t need;
    uint8_t *grown;
    if (!b) return -1;
    need = b->size + add;
    if (need <= b->cap) return 0;
    if (b->cap == 0) b->cap = 256;
    while (b->cap < need) {
        if (b->cap > SIZE_MAX / 2) return -1;
        b->cap *= 2;
    }
    grown = (uint8_t *)realloc(b->data, b->cap);
    if (!grown) return -1;
    b->data = grown;
    return 0;
}

static int mc_emit_u8(McBuf *b, uint8_t v) {
    if (mc_reserve(b, 1) != 0) return -1;
    b->data[b->size++] = v;
    return 0;
}

static int mc_emit_u16(McBuf *b, uint16_t v) {
    if (mc_reserve(b, 2) != 0) return -1;
    b->data[b->size + 0] = (uint8_t)(v & 0xFFu);
    b->data[b->size + 1] = (uint8_t)((v >> 8) & 0xFFu);
    b->size += 2;
    return 0;
}

static int mc_emit_u32(McBuf *b, uint32_t v) {
    if (mc_reserve(b, 4) != 0) return -1;
    b->data[b->size + 0] = (uint8_t)(v & 0xFFu);
    b->data[b->size + 1] = (uint8_t)((v >> 8) & 0xFFu);
    b->data[b->size + 2] = (uint8_t)((v >> 16) & 0xFFu);
    b->data[b->size + 3] = (uint8_t)((v >> 24) & 0xFFu);
    b->size += 4;
    return 0;
}

static int mc_emit_u64(McBuf *b, uint64_t v) {
    int i;
    if (mc_reserve(b, 8) != 0) return -1;
    for (i = 0; i < 8; i++) {
        b->data[b->size + (size_t)i] = (uint8_t)((v >> (i * 8)) & 0xFFu);
    }
    b->size += 8;
    return 0;
}

static int mc_patch_rel32(McBuf *b, size_t off, int32_t disp) {
    if (!b || off + 4 > b->size) return -1;
    b->data[off + 0] = (uint8_t)(disp & 0xFF);
    b->data[off + 1] = (uint8_t)((disp >> 8) & 0xFF);
    b->data[off + 2] = (uint8_t)((disp >> 16) & 0xFF);
    b->data[off + 3] = (uint8_t)((disp >> 24) & 0xFF);
    return 0;
}

static int mc_patch_rel16(McBuf *b, size_t off, int16_t disp) {
    if (!b || off + 2 > b->size) return -1;
    b->data[off + 0] = (uint8_t)(disp & 0xFF);
    b->data[off + 1] = (uint8_t)((disp >> 8) & 0xFF);
    return 0;
}

static int mc_patch_u16(McBuf *b, size_t off, uint16_t v) {
    if (!b || off + 2 > b->size) return -1;
    b->data[off + 0] = (uint8_t)(v & 0xFFu);
    b->data[off + 1] = (uint8_t)((v >> 8) & 0xFFu);
    return 0;
}

static int mc_patch_u32(McBuf *b, size_t off, uint32_t v) {
    if (!b || off + 4 > b->size) return -1;
    b->data[off + 0] = (uint8_t)(v & 0xFFu);
    b->data[off + 1] = (uint8_t)((v >> 8) & 0xFFu);
    b->data[off + 2] = (uint8_t)((v >> 16) & 0xFFu);
    b->data[off + 3] = (uint8_t)((v >> 24) & 0xFFu);
    return 0;
}

static int mc_patch_u64(McBuf *b, size_t off, uint64_t v) {
    int i;
    if (!b || off + 8 > b->size) return -1;
    for (i = 0; i < 8; i++) {
        b->data[off + (size_t)i] = (uint8_t)((v >> (i * 8)) & 0xFFu);
    }
    return 0;
}

static int mc_word_bytes(const McCtx *ctx) {
    if (!ctx) return 8;
    if (ctx->machine_bits == 16) return 2;
    if (ctx->machine_bits == 32) return 4;
    return 8;
}

static int mc_is_x64(const McCtx *ctx) {
    return ctx && ctx->machine_bits == 64;
}

static int mc_reg_id_from_name(const char *name) {
    if (!name) return -1;
    if (strcmp(name, "eax") == 0) return 0;
    if (strcmp(name, "ecx") == 0) return 1;
    if (strcmp(name, "edx") == 0) return 2;
    if (strcmp(name, "ebx") == 0) return 3;
    if (strcmp(name, "rax") == 0) return 0;
    if (strcmp(name, "rcx") == 0) return 1;
    if (strcmp(name, "rdx") == 0) return 2;
    if (strcmp(name, "rbx") == 0) return 3;
    if (strcmp(name, "rsp") == 0) return 4;
    if (strcmp(name, "rbp") == 0) return 5;
    if (strcmp(name, "rsi") == 0) return 6;
    if (strcmp(name, "rdi") == 0) return 7;
    if (strcmp(name, "r8") == 0) return 8;
    if (strcmp(name, "r9") == 0) return 9;
    if (strcmp(name, "r10") == 0) return 10;
    if (strcmp(name, "r11") == 0) return 11;
    if (strcmp(name, "r12") == 0) return 12;
    if (strcmp(name, "r13") == 0) return 13;
    if (strcmp(name, "r14") == 0) return 14;
    if (strcmp(name, "r15") == 0) return 15;
    return -1;
}

static int mc_parse_reg_pseudo(const char *ident_name, char *out_reg, size_t out_sz) {
    const char *lt;
    const char *gt;
    size_t len;
    if (!ident_name || !out_reg || out_sz == 0) return -1;
    if (strncmp(ident_name, "reg<", 4) != 0) return -1;
    lt = ident_name + 4;
    gt = strchr(lt, '>');
    if (!gt) return -1;
    len = (size_t)(gt - lt);
    if (len == 0 || len + 1 > out_sz) return -1;
    memcpy(out_reg, lt, len);
    out_reg[len] = '\0';
    return mc_reg_id_from_name(out_reg) >= 0 ? 0 : -1;
}

static int mc_parse_reg_pseudo_any(const char *ident_name, char *out_reg, size_t out_sz) {
    const char *lt;
    const char *gt;
    size_t len;
    if (!ident_name || !out_reg || out_sz == 0) return -1;
    if (strncmp(ident_name, "reg<", 4) != 0) return -1;
    lt = ident_name + 4;
    gt = strchr(lt, '>');
    if (!gt) return -1;
    len = (size_t)(gt - lt);
    if (len == 0 || len + 1 > out_sz) return -1;
    memcpy(out_reg, lt, len);
    out_reg[len] = '\0';
    return 0;
}

static int mc_vec_reg_id_from_name(const char *name) {
    if (!name) return -1;
    if (strncmp(name, "ymm", 3) == 0) {
        const char *n = name + 3;
        if (*n >= '0' && *n <= '9') {
            int id = atoi(n);
            if (id >= 0 && id <= 15) return id;
        }
    }
    return -1;
}

static int mc_emit_modrm_disp8(McBuf *b, uint8_t modrm_base, int32_t disp) {
    if (mc_emit_u8(b, modrm_base | 0x40) != 0) return -1;
    return mc_emit_u8(b, (uint8_t)disp);
}

static int mc_emit_modrm_disp32(McBuf *b, uint8_t modrm_base, int32_t disp) {
    if (mc_emit_u8(b, modrm_base | 0x80) != 0) return -1;
    return mc_emit_u32(b, (uint32_t)disp);
}

static int mc_emit_store_reg_to_base_disp(McCtx *ctx, int base_reg_id, int src_reg_id, int32_t disp) {
    uint8_t rex = 0x48;
    uint8_t modrm;
    if (!ctx || !mc_is_x64(ctx) || base_reg_id < 0 || base_reg_id > 15 || src_reg_id < 0 || src_reg_id > 15) return -1;
    if (src_reg_id > 7) rex |= 0x04; /* R */
    if (base_reg_id > 7) rex |= 0x01; /* B */
    modrm = (uint8_t)(((src_reg_id & 0x7) << 3) | (base_reg_id & 0x7));
    if (mc_emit_u8(&ctx->code, rex) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x89) != 0) return -1;
    if (disp >= -128 && disp <= 127) {
        if (mc_emit_u8(&ctx->code, (uint8_t)(0x40 | modrm)) != 0) return -1;
        return mc_emit_u8(&ctx->code, (uint8_t)disp);
    }
    if (mc_emit_u8(&ctx->code, (uint8_t)(0x80 | modrm)) != 0) return -1;
    return mc_emit_u32(&ctx->code, (uint32_t)disp);
}

static int mc_emit_load_acc_from_rax_disp(McCtx *ctx, int32_t disp, uint32_t width) {
    if (!ctx || !mc_is_x64(ctx)) return -1;
    switch (width) {
        case 1:
            if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0xB6) != 0) return -1;
            if (disp >= -128 && disp <= 127) return mc_emit_modrm_disp8(&ctx->code, 0x00, disp);
            return mc_emit_modrm_disp32(&ctx->code, 0x00, disp);
        case 2:
            if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0xB7) != 0) return -1;
            if (disp >= -128 && disp <= 127) return mc_emit_modrm_disp8(&ctx->code, 0x00, disp);
            return mc_emit_modrm_disp32(&ctx->code, 0x00, disp);
        case 4:
            if (mc_emit_u8(&ctx->code, 0x8B) != 0) return -1; /* mov eax, [rax+disp] */
            if (disp >= -128 && disp <= 127) return mc_emit_modrm_disp8(&ctx->code, 0x00, disp);
            return mc_emit_modrm_disp32(&ctx->code, 0x00, disp);
        case 8:
        default:
            if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x8B) != 0) return -1; /* mov rax, [rax+disp] */
            if (disp >= -128 && disp <= 127) return mc_emit_modrm_disp8(&ctx->code, 0x00, disp);
            return mc_emit_modrm_disp32(&ctx->code, 0x00, disp);
    }
}

static int __attribute__((unused)) mc_emit_store_acc_to_rax_disp(McCtx *ctx, int32_t disp, uint32_t width) {
    if (!ctx || !mc_is_x64(ctx)) return -1;
    switch (width) {
        case 1:
            if (mc_emit_u8(&ctx->code, 0x88) != 0) return -1; /* mov [rax+disp], al */
            if (disp >= -128 && disp <= 127) return mc_emit_modrm_disp8(&ctx->code, 0x00, disp);
            return mc_emit_modrm_disp32(&ctx->code, 0x00, disp);
        case 2:
            if (mc_emit_u8(&ctx->code, 0x66) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0) return -1; /* mov [rax+disp], ax */
            if (disp >= -128 && disp <= 127) return mc_emit_modrm_disp8(&ctx->code, 0x00, disp);
            return mc_emit_modrm_disp32(&ctx->code, 0x00, disp);
        case 4:
            if (mc_emit_u8(&ctx->code, 0x89) != 0) return -1; /* mov [rax+disp], eax */
            if (disp >= -128 && disp <= 127) return mc_emit_modrm_disp8(&ctx->code, 0x00, disp);
            return mc_emit_modrm_disp32(&ctx->code, 0x00, disp);
        case 8:
        default:
            if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0) return -1; /* mov [rax+disp], rax */
            if (disp >= -128 && disp <= 127) return mc_emit_modrm_disp8(&ctx->code, 0x00, disp);
            return mc_emit_modrm_disp32(&ctx->code, 0x00, disp);
    }
}

static int mc_emit_store_rcx_to_rax_disp(McCtx *ctx, int32_t disp, uint32_t width) {
    if (!ctx || !mc_is_x64(ctx)) return -1;
    switch (width) {
        case 1:
            if (mc_emit_u8(&ctx->code, 0x88) != 0) return -1; /* mov [rax+disp], cl */
            if (disp >= -128 && disp <= 127) return mc_emit_modrm_disp8(&ctx->code, 0x08, disp);
            return mc_emit_modrm_disp32(&ctx->code, 0x08, disp);
        case 2:
            if (mc_emit_u8(&ctx->code, 0x66) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0) return -1; /* mov [rax+disp], cx */
            if (disp >= -128 && disp <= 127) return mc_emit_modrm_disp8(&ctx->code, 0x08, disp);
            return mc_emit_modrm_disp32(&ctx->code, 0x08, disp);
        case 4:
            if (mc_emit_u8(&ctx->code, 0x89) != 0) return -1; /* mov [rax+disp], ecx */
            if (disp >= -128 && disp <= 127) return mc_emit_modrm_disp8(&ctx->code, 0x08, disp);
            return mc_emit_modrm_disp32(&ctx->code, 0x08, disp);
        case 8:
        default:
            if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0) return -1; /* mov [rax+disp], rcx */
            if (disp >= -128 && disp <= 127) return mc_emit_modrm_disp8(&ctx->code, 0x08, disp);
            return mc_emit_modrm_disp32(&ctx->code, 0x08, disp);
    }
}

static const char *mc_trim_type_prefixes(const char *type_name) {
    while (type_name && *type_name == ' ') type_name++;
    if (!type_name) return NULL;
    if (strncmp(type_name, "@volatile ", 10) == 0) return type_name + 10;
    return type_name;
}

static uint32_t mc_type_size_from_name(const char *type_name) {
    const char *t = mc_trim_type_prefixes(type_name);
    if (!t) return 8;
    if (strcmp(t, "UInt8") == 0 || strcmp(t, "Int8") == 0 || strcmp(t, "Bool") == 0 || strcmp(t, "bool") == 0) return 1;
    if (strcmp(t, "UInt16") == 0 || strcmp(t, "Int16") == 0) return 2;
    if (strcmp(t, "UInt32") == 0 || strcmp(t, "Int32") == 0) return 4;
    if (strcmp(t, "UInt64") == 0 || strcmp(t, "Int64") == 0 || strcmp(t, "PhysAddr") == 0) return 8;
    if (strncmp(t, "ptr<", 4) == 0 || strncmp(t, "view<", 5) == 0 || strncmp(t, "reg<", 4) == 0 || strncmp(t, "port<", 5) == 0) return 8;
    if (strncmp(t, "vector<", 7) == 0) {
        const char *comma = strrchr(t, ',');
        const char *gt = strrchr(t, '>');
        if (comma && gt && comma < gt) {
            uint32_t n = (uint32_t)strtoul(comma + 1, NULL, 10);
            if (n > 0) return n;
        }
    }
    return 8;
}

static int mc_is_packed_struct(const ASTNode *decl) {
    int i;
    if (!decl || decl->type != AST_STRUCT_DECL) return 0;
    for (i = 0; i < decl->data.struct_decl.attr_count; i++) {
        const char *attr = decl->data.struct_decl.attributes ? decl->data.struct_decl.attributes[i] : NULL;
        if (attr && (strcmp(attr, "@packed") == 0 || strcmp(attr, "packed") == 0)) return 1;
    }
    return 0;
}

static int mc_align_up_u32(uint32_t *v, uint32_t align) {
    uint32_t mask;
    if (!v || align == 0) return -1;
    mask = align - 1;
    *v = (*v + mask) & ~mask;
    return 0;
}

static McSymbol* mc_find_symbol(McSymbol *symbols, size_t count, const char *name) {
    size_t i;
    if (!symbols || !name) return NULL;
    for (i = 0; i < count; i++) {
        if (symbols[i].name && strcmp(symbols[i].name, name) == 0) {
            return &symbols[i];
        }
    }
    return NULL;
}

static int mc_add_symbol(McSymbol **symbols,
                         size_t *count,
                         size_t *cap,
                         const McSymbol *sym) {
    McSymbol *grown;
    if (!symbols || !count || !cap || !sym || !sym->name) return -1;
    if (*count >= *cap) {
        *cap = (*cap == 0) ? 32 : (*cap * 2);
        grown = (McSymbol *)realloc(*symbols, *cap * sizeof(McSymbol));
        if (!grown) return -1;
        *symbols = grown;
    }
    (*symbols)[*count] = *sym;
    (*symbols)[*count].name = strdup(sym->name);
    if (!(*symbols)[*count].name) return -1;
    (*symbols)[*count].type_name = NULL;
    if (sym->type_name) {
        (*symbols)[*count].type_name = strdup(sym->type_name);
        if (!(*symbols)[*count].type_name) {
            free((*symbols)[*count].name);
            return -1;
        }
    }
    (*count)++;
    return 0;
}

static int mc_add_local_symbol(McCtx *ctx, const McSymbol *sym) {
    if (!ctx) return -1;
    return mc_add_symbol(&ctx->local_symbols, &ctx->local_symbol_count, &ctx->local_symbol_cap, sym);
}

static int mc_add_const_symbol(McCtx *ctx, const char *name, uint64_t value) {
    McSymbol sym;
    if (!ctx || !name) return -1;
    memset(&sym, 0, sizeof(sym));
    sym.name = (char *)name;
    sym.kind = MC_SYM_CONST;
    sym.const_value = value;
    return mc_add_symbol(&ctx->const_symbols, &ctx->const_symbol_count, &ctx->const_symbol_cap, &sym);
}

static void mc_reset_locals(McCtx *ctx) {
    size_t i;
    if (!ctx) return;
    for (i = 0; i < ctx->local_symbol_count; i++) {
        free(ctx->local_symbols[i].name);
        free(ctx->local_symbols[i].type_name);
    }
    free(ctx->local_symbols);
    ctx->local_symbols = NULL;
    ctx->local_symbol_count = 0;
    ctx->local_symbol_cap = 0;
    ctx->stack_allocated = 0;
    ctx->planned_local_bytes = 0;
    ctx->uefi_system_table_offset = 0;
}

static McStructInfo *mc_find_struct(McCtx *ctx, const char *name) {
    size_t i;
    if (!ctx || !name) return NULL;
    for (i = 0; i < ctx->struct_count; i++) {
        if (ctx->structs[i].name && strcmp(ctx->structs[i].name, name) == 0) return &ctx->structs[i];
    }
    return NULL;
}

static McStructField *mc_find_struct_field(McStructInfo *st, const char *field_name) {
    size_t i;
    if (!st || !field_name) return NULL;
    for (i = 0; i < st->field_count; i++) {
        if (st->fields[i].name && strcmp(st->fields[i].name, field_name) == 0) return &st->fields[i];
    }
    /* Legacy source compatibility: allow member name with leading character omitted.
       Example: "\ame" -> field "name". Exact match above always has priority. */
    for (i = 0; i < st->field_count; i++) {
        if (st->fields[i].name &&
            st->fields[i].name[0] != '\0' &&
            st->fields[i].name[1] != '\0' &&
            strcmp(st->fields[i].name + 1, field_name) == 0) {
            return &st->fields[i];
        }
    }
    return NULL;
}

static int mc_add_struct_layout(McCtx *ctx, ASTNode *decl) {
    McStructInfo *grown_structs;
    McStructInfo *st;
    uint32_t offset = 0;
    uint32_t max_align = 1;
    int packed;
    int i;
    if (!ctx || !decl || decl->type != AST_STRUCT_DECL || !decl->data.struct_decl.name) return -1;
    if (mc_find_struct(ctx, decl->data.struct_decl.name)) return 0;
    if (ctx->struct_count >= ctx->struct_cap) {
        ctx->struct_cap = (ctx->struct_cap == 0) ? 16 : (ctx->struct_cap * 2);
        grown_structs = (McStructInfo *)realloc(ctx->structs, ctx->struct_cap * sizeof(McStructInfo));
        if (!grown_structs) return -1;
        ctx->structs = grown_structs;
    }
    st = &ctx->structs[ctx->struct_count];
    memset(st, 0, sizeof(*st));
    st->name = strdup(decl->data.struct_decl.name);
    if (!st->name) return -1;
    packed = mc_is_packed_struct(decl);
    for (i = 0; i < decl->data.struct_decl.field_count; i++) {
        const char *fname = decl->data.struct_decl.field_names ? decl->data.struct_decl.field_names[i] : NULL;
        ASTNode *ftype = decl->data.struct_decl.field_types ? decl->data.struct_decl.field_types[i] : NULL;
        const char *type_name = (ftype && ftype->type == AST_TYPE) ? ftype->data.type.name : NULL;
        uint32_t fsz = mc_type_size_from_name(type_name);
        uint32_t falign = fsz;
        McStructField *grown_fields;
        if (!fname) continue;
        if (falign > 8) falign = 8;
        if (falign == 0) falign = 1;
        if (!packed) {
            if (mc_align_up_u32(&offset, falign) != 0) return -1;
            if (falign > max_align) max_align = falign;
        }
        if (st->field_count >= st->field_cap) {
            st->field_cap = (st->field_cap == 0) ? 16 : (st->field_cap * 2);
            grown_fields = (McStructField *)realloc(st->fields, st->field_cap * sizeof(McStructField));
            if (!grown_fields) return -1;
            st->fields = grown_fields;
        }
        st->fields[st->field_count].name = strdup(fname);
        if (!st->fields[st->field_count].name) return -1;
        st->fields[st->field_count].offset = offset;
        st->fields[st->field_count].size = fsz;
        st->fields[st->field_count].elem_size = mc_array_elem_size_from_type_name(type_name, fsz);
        st->field_count++;
        offset += fsz;
    }
    if (!packed && max_align > 1) {
        if (mc_align_up_u32(&offset, max_align) != 0) return -1;
    }
    st->byte_size = offset;
    ctx->struct_count++;
    return 0;
}

static const char *mc_extract_struct_name_from_type(const char *type_name, const char *prefix) {
    size_t plen;
    const char *start;
    const char *end;
    static char buf[256];
    if (!type_name || !prefix) return NULL;
    type_name = mc_trim_type_prefixes(type_name);
    plen = strlen(prefix);
    if (strncmp(type_name, prefix, plen) != 0) return NULL;
    start = type_name + plen;
    end = strchr(start, '>');
    if (!end || end <= start) return NULL;
    if ((size_t)(end - start) >= sizeof(buf)) return NULL;
    memcpy(buf, start, (size_t)(end - start));
    buf[end - start] = '\0';
    return buf;
}

static int mc_symbol_pointer_base_to_rax(McCtx *ctx, const McSymbol *sym) {
    if (!ctx || !sym) return -1;
    if (sym->kind == MC_SYM_PARAM) return mc_emit_mov_acc_arg(ctx, sym->param_index);
    if (sym->kind == MC_SYM_STACK) return mc_emit_load_acc_from_stack(ctx, sym->stack_offset);
    if (sym->kind == MC_SYM_REG_ALIAS) return mc_emit_mov_acc_from_reg_id(ctx, sym->reg_id);
    return -1;
}

static int mc_symbol_base_addr_to_rax(McCtx *ctx, const McSymbol *sym) {
    if (!ctx || !sym) return -1;
    if (sym->kind == MC_SYM_PARAM) return mc_emit_mov_acc_arg(ctx, sym->param_index);
    if (sym->kind == MC_SYM_STACK) {
        if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
        if (mc_emit_u8(&ctx->code, 0x8D) != 0) return -1; /* lea */
        if (mc_emit_u8(&ctx->code, 0x85) != 0) return -1; /* rax, [rbp+disp32] */
        return mc_emit_u32(&ctx->code, (uint32_t)sym->stack_offset);
    }
    if (sym->kind == MC_SYM_REG_ALIAS) return mc_emit_mov_acc_from_reg_id(ctx, sym->reg_id);
    return -1;
}

static int mc_symbol_type_is_ptr_like(const McSymbol *sym) {
    const char *type_name;
    if (!sym || !sym->type_name) return 0;
    type_name = mc_trim_type_prefixes(sym->type_name);
    return (strncmp(type_name, "ptr<", 4) == 0 || strncmp(type_name, "view<", 5) == 0);
}

static int mc_resolve_access_layout(McCtx *ctx, ASTNode *access, McSymbol **obj_sym, McStructField **field_out) {
    ASTNode *obj;
    const char *member;
    McSymbol *sym;
    const char *struct_name = NULL;
    const char *sym_type_name = NULL;
    McStructInfo *st;
    McStructField *field;
    if (!ctx || !access || access->type != AST_ACCESS || !obj_sym || !field_out) return -1;
    obj = access->data.access.object;
    member = access->data.access.member;
    if (!obj || obj->type != AST_IDENT || !member) return -1;
    sym = mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, obj->data.ident.name);
    if (!sym || !sym->type_name) return -1;
    sym_type_name = mc_trim_type_prefixes(sym->type_name);
    struct_name = mc_extract_struct_name_from_type(sym_type_name, "ptr<");
    if (!struct_name) struct_name = mc_extract_struct_name_from_type(sym_type_name, "view<");
    if (!struct_name && mc_find_struct(ctx, sym_type_name)) struct_name = sym_type_name;
    if (!struct_name) return -1;
    st = mc_find_struct(ctx, struct_name);
    if (!st) return -1;
    field = mc_find_struct_field(st, member);
    if (!field) return -1;
    *obj_sym = sym;
    *field_out = field;
    return 0;
}

static uint32_t mc_ptr_elem_size_from_type(const char *type_name) {
    const char *start;
    const char *end;
    char elem[128];
    if (!type_name) return 1;
    type_name = mc_trim_type_prefixes(type_name);
    if (strncmp(type_name, "ptr<", 4) != 0) return 1;
    start = type_name + 4;
    end = strchr(start, '>');
    if (!end || end <= start) return 1;
    if ((size_t)(end - start) >= sizeof(elem)) return 1;
    memcpy(elem, start, (size_t)(end - start));
    elem[end - start] = '\0';
    return mc_type_size_from_name(elem);
}

static uint32_t mc_array_elem_size_from_type_name(const char *type_name, uint32_t fallback_size) {
    const char *rbracket;
    if (!type_name) return fallback_size ? fallback_size : 1;
    type_name = mc_trim_type_prefixes(type_name);
    if (type_name[0] != '[') return fallback_size ? fallback_size : 1;
    rbracket = strchr(type_name, ']');
    if (!rbracket || !rbracket[1]) return fallback_size ? fallback_size : 1;
    return mc_type_size_from_name(rbracket + 1);
}

static uint32_t mc_access_index_elem_size(McCtx *ctx, ASTNode *access) {
    ASTNode *obj;
    if (!ctx || !access || access->type != AST_ACCESS) return 1;
    obj = access->data.access.object;
    if (!obj) return 1;
    if (obj->type == AST_IDENT) {
        McSymbol *sym = mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, obj->data.ident.name);
        if (sym) {
            uint32_t elem_size = mc_ptr_elem_size_from_type(sym->type_name);
            return elem_size ? elem_size : 1;
        }
    } else if (obj->type == AST_ACCESS) {
        McSymbol *obj_sym = NULL;
        McStructField *field = NULL;
        if (mc_resolve_access_layout(ctx, obj, &obj_sym, &field) == 0 && field) {
            return field->elem_size ? field->elem_size : 1;
        }
    }
    return 1;
}

static int mc_emit_indexed_address(McCtx *ctx, ASTNode *access) {
    ASTNode *obj;
    McSymbol *sym = NULL;
    McSymbol *obj_sym = NULL;
    McStructField *field = NULL;
    uint32_t elem_size;
    if (!ctx || !access || access->type != AST_ACCESS || !access->data.access.index_expr) return -1;
    obj = access->data.access.object;
    if (!obj) return -1;
    if (obj->type == AST_IDENT && obj->data.ident.name) {
        sym = mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, obj->data.ident.name);
        if (!sym) return -1;
        if (mc_symbol_type_is_ptr_like(sym)) {
            if (mc_symbol_pointer_base_to_rax(ctx, sym) != 0) return -1;
        } else {
            if (mc_symbol_base_addr_to_rax(ctx, sym) != 0) return -1;
        }
    } else if (obj->type == AST_ACCESS) {
        if (mc_resolve_access_layout(ctx, obj, &obj_sym, &field) != 0 || !obj_sym || !field) return -1;
        if (mc_symbol_type_is_ptr_like(obj_sym)) {
            if (mc_symbol_pointer_base_to_rax(ctx, obj_sym) != 0) return -1;
        } else {
            if (mc_symbol_base_addr_to_rax(ctx, obj_sym) != 0) return -1;
        }
        if (field->offset != 0) {
            if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
            if (mc_emit_u8(&ctx->code, 0x05) != 0) return -1; /* add rax, imm32 */
            if (mc_emit_u32(&ctx->code, field->offset) != 0) return -1;
        }
    } else {
        return -1;
    }
    if (mc_emit_u8(&ctx->code, 0x50) != 0) return -1; /* push base */
    if (mc_emit_expr(ctx, access->data.access.index_expr) != 0) return -1; /* index in RAX */
    elem_size = mc_access_index_elem_size(ctx, access);
    if (elem_size > 1) {
        if (mc_emit_u8(&ctx->code, 0x50) != 0) return -1;
        if (mc_emit_mov_acc_imm(ctx, (int64_t)elem_size) != 0) return -1;
        if (mc_emit_u8(&ctx->code, 0x59) != 0) return -1;
        if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0xAF) != 0 || mc_emit_u8(&ctx->code, 0xC1) != 0) return -1;
    }
    if (mc_emit_u8(&ctx->code, 0x59) != 0) return -1; /* pop base to RCX */
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x01) != 0 || mc_emit_u8(&ctx->code, 0xC8) != 0) return -1; /* add rax, rcx */
    return 0;
}

static int mc_emit_hw_state_snapshot(McCtx *ctx, ASTNode *target_ptr_expr) {
    McStructInfo *st;
    McStructField *f_rax, *f_rbx, *f_rcx, *f_rdx, *f_rsi, *f_rdi, *f_rbp;
    McStructField *f_r8, *f_r9, *f_r10, *f_r11, *f_r12, *f_r13, *f_r14, *f_r15;
    McStructField *f_rsp, *f_rip, *f_pt;
    if (!ctx || !target_ptr_expr) return -1;
    st = mc_find_struct(ctx, "ThreadContext");
    if (!st) return -1;
    f_rax = mc_find_struct_field(st, "rax");
    f_rbx = mc_find_struct_field(st, "rbx");
    f_rcx = mc_find_struct_field(st, "rcx");
    f_rdx = mc_find_struct_field(st, "rdx");
    f_rsi = mc_find_struct_field(st, "rsi");
    f_rdi = mc_find_struct_field(st, "rdi");
    f_rbp = mc_find_struct_field(st, "rbp");
    f_r8 = mc_find_struct_field(st, "r8");
    f_r9 = mc_find_struct_field(st, "r9");
    f_r10 = mc_find_struct_field(st, "r10");
    f_r11 = mc_find_struct_field(st, "r11");
    f_r12 = mc_find_struct_field(st, "r12");
    f_r13 = mc_find_struct_field(st, "r13");
    f_r14 = mc_find_struct_field(st, "r14");
    f_r15 = mc_find_struct_field(st, "r15");
    f_rsp = mc_find_struct_field(st, "rsp");
    f_rip = mc_find_struct_field(st, "rip");
    f_pt = mc_find_struct_field(st, "page/table");
    if (!f_rax) {
        return -1;
    }

    if (mc_emit_expr(ctx, target_ptr_expr) != 0) return -1; /* RAX = target context pointer */
    if (mc_emit_u8(&ctx->code, 0x49) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC2) != 0) return -1; /* mov r10, rax */

    if (mc_emit_store_reg_to_base_disp(ctx, 10, 0, (int32_t)f_rax->offset) != 0) return -1;
    if (f_rbx && mc_emit_store_reg_to_base_disp(ctx, 10, 3, (int32_t)f_rbx->offset) != 0) return -1;
    if (f_rcx && mc_emit_store_reg_to_base_disp(ctx, 10, 1, (int32_t)f_rcx->offset) != 0) return -1;
    if (f_rdx && mc_emit_store_reg_to_base_disp(ctx, 10, 2, (int32_t)f_rdx->offset) != 0) return -1;
    if (f_rsi && mc_emit_store_reg_to_base_disp(ctx, 10, 6, (int32_t)f_rsi->offset) != 0) return -1;
    if (f_rdi && mc_emit_store_reg_to_base_disp(ctx, 10, 7, (int32_t)f_rdi->offset) != 0) return -1;
    if (f_rbp && mc_emit_store_reg_to_base_disp(ctx, 10, 5, (int32_t)f_rbp->offset) != 0) return -1;
    if (f_r8 && mc_emit_store_reg_to_base_disp(ctx, 10, 8, (int32_t)f_r8->offset) != 0) return -1;
    if (f_r9 && mc_emit_store_reg_to_base_disp(ctx, 10, 9, (int32_t)f_r9->offset) != 0) return -1;
    if (f_r10 && mc_emit_store_reg_to_base_disp(ctx, 10, 10, (int32_t)f_r10->offset) != 0) return -1;
    if (f_r11 && mc_emit_store_reg_to_base_disp(ctx, 10, 11, (int32_t)f_r11->offset) != 0) return -1;
    if (f_r12 && mc_emit_store_reg_to_base_disp(ctx, 10, 12, (int32_t)f_r12->offset) != 0) return -1;
    if (f_r13 && mc_emit_store_reg_to_base_disp(ctx, 10, 13, (int32_t)f_r13->offset) != 0) return -1;
    if (f_r14 && mc_emit_store_reg_to_base_disp(ctx, 10, 14, (int32_t)f_r14->offset) != 0) return -1;
    if (f_r15 && mc_emit_store_reg_to_base_disp(ctx, 10, 15, (int32_t)f_r15->offset) != 0) return -1;
    if (f_rsp && mc_emit_store_reg_to_base_disp(ctx, 10, 4, (int32_t)f_rsp->offset) != 0) return -1;

    /* RIP snapshot: call $+5 ; pop rcx */
    if (f_rip) {
        if (mc_emit_u8(&ctx->code, 0xE8) != 0 || mc_emit_u32(&ctx->code, 0) != 0) return -1;
        if (mc_emit_u8(&ctx->code, 0x59) != 0) return -1;
        if (mc_emit_store_reg_to_base_disp(ctx, 10, 1, (int32_t)f_rip->offset) != 0) return -1;
    }

    /* page/table snapshot from CR3 */
    if (f_pt) {
        if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x20) != 0 || mc_emit_u8(&ctx->code, 0xD8) != 0) return -1; /* mov rax, cr3 */
        if (mc_emit_store_reg_to_base_disp(ctx, 10, 0, (int32_t)f_pt->offset) != 0) return -1;
    }
    return 0;
}

static int mc_build_access_path(ASTNode *node, char *buf, size_t buf_sz) {
    size_t used;
    if (!buf || buf_sz == 0 || !node) return -1;
    if (node->type == AST_IDENT && node->data.ident.name) {
        snprintf(buf, buf_sz, "%s", node->data.ident.name);
        return 0;
    }
    if (node->type != AST_ACCESS || !node->data.access.member) return -1;
    if (mc_build_access_path(node->data.access.object, buf, buf_sz) != 0) return -1;
    used = strlen(buf);
    if (used + 1 + strlen(node->data.access.member) + 1 > buf_sz) return -1;
    buf[used++] = '/';
    strcpy(buf + used, node->data.access.member);
    return 0;
}

static int mc_ctx_add_func(McCtx *ctx, const char *name, uint64_t off, uint64_t size) {
    McFuncSymbol *grown;
    if (!ctx || !name) return -1;
    if (ctx->func_count >= ctx->func_cap) {
        ctx->func_cap = (ctx->func_cap == 0) ? 32 : ctx->func_cap * 2;
        grown = (McFuncSymbol *)realloc(ctx->funcs, ctx->func_cap * sizeof(McFuncSymbol));
        if (!grown) return -1;
        ctx->funcs = grown;
    }
    ctx->funcs[ctx->func_count].name = strdup(name);
    if (!ctx->funcs[ctx->func_count].name) return -1;
    ctx->funcs[ctx->func_count].offset = off;
    ctx->funcs[ctx->func_count].size = size;
    ctx->func_count++;
    return 0;
}

static int mc_ctx_find_func(const McCtx *ctx, const char *name, uint64_t *off_out) {
    size_t i;
    if (!ctx || !name) return -1;
    for (i = 0; i < ctx->func_count; i++) {
        if (strcmp(ctx->funcs[i].name, name) == 0) {
            if (off_out) *off_out = ctx->funcs[i].offset;
            return 0;
        }
    }
    return -1;
}

static int mc_ctx_add_call_fixup(McCtx *ctx, const char *target, size_t rel_off, int rel_width) {
    McCallFixup *grown;
    if (!ctx || !target) return -1;
    if (ctx->call_count >= ctx->call_cap) {
        ctx->call_cap = (ctx->call_cap == 0) ? 64 : ctx->call_cap * 2;
        grown = (McCallFixup *)realloc(ctx->calls, ctx->call_cap * sizeof(McCallFixup));
        if (!grown) return -1;
        ctx->calls = grown;
    }
    ctx->calls[ctx->call_count].target_name = strdup(target);
    if (!ctx->calls[ctx->call_count].target_name) return -1;
    ctx->calls[ctx->call_count].rel_off = rel_off;
    ctx->calls[ctx->call_count].rel_width = rel_width;
    ctx->call_count++;
    return 0;
}

static int mc_new_label(McCtx *ctx) {
    McLabel *grown;
    int id;
    if (!ctx) return -1;
    if (ctx->label_count >= ctx->label_cap) {
        ctx->label_cap = (ctx->label_cap == 0) ? 32 : ctx->label_cap * 2;
        grown = (McLabel *)realloc(ctx->labels, ctx->label_cap * sizeof(McLabel));
        if (!grown) return -1;
        ctx->labels = grown;
    }
    id = (int)ctx->label_count;
    ctx->labels[ctx->label_count].bound = 0;
    ctx->labels[ctx->label_count].pos = 0;
    ctx->label_count++;
    return id;
}

static int mc_bind_label(McCtx *ctx, int label_id) {
    if (!ctx || label_id < 0 || (size_t)label_id >= ctx->label_count) return -1;
    ctx->labels[label_id].bound = 1;
    ctx->labels[label_id].pos = ctx->code.size;
    return 0;
}

static int mc_add_branch_fixup(McCtx *ctx, int label_id, size_t rel_off, int rel_width) {
    McBranchFixup *grown;
    if (!ctx || label_id < 0) return -1;
    if (ctx->branch_count >= ctx->branch_cap) {
        ctx->branch_cap = (ctx->branch_cap == 0) ? 64 : ctx->branch_cap * 2;
        grown = (McBranchFixup *)realloc(ctx->branches, ctx->branch_cap * sizeof(McBranchFixup));
        if (!grown) return -1;
        ctx->branches = grown;
    }
    ctx->branches[ctx->branch_count].label_id = label_id;
    ctx->branches[ctx->branch_count].rel_off = rel_off;
    ctx->branches[ctx->branch_count].rel_width = rel_width;
    ctx->branch_count++;
    return 0;
}

static int mc_emit_jmp_label(McCtx *ctx, int label_id) {
    size_t off;
    if (!ctx) return -1;
    if (mc_emit_u8(&ctx->code, 0xE9) != 0) return -1;
    off = ctx->code.size;
    if (ctx->machine_bits == 16) {
        if (mc_emit_u16(&ctx->code, 0) != 0) return -1;
        return mc_add_branch_fixup(ctx, label_id, off, 2);
    }
    if (mc_emit_u32(&ctx->code, 0) != 0) return -1;
    return mc_add_branch_fixup(ctx, label_id, off, 4);
}

static int mc_emit_jz_label(McCtx *ctx, int label_id) {
    size_t off;
    if (!ctx) return -1;
    if (mc_emit_u8(&ctx->code, 0x0F) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x84) != 0) return -1;
    off = ctx->code.size;
    if (ctx->machine_bits == 16) {
        if (mc_emit_u16(&ctx->code, 0) != 0) return -1;
        return mc_add_branch_fixup(ctx, label_id, off, 2);
    }
    if (mc_emit_u32(&ctx->code, 0) != 0) return -1;
    return mc_add_branch_fixup(ctx, label_id, off, 4);
}

static int mc_patch_branches(McCtx *ctx) {
    size_t i;
    if (!ctx) return -1;
    for (i = 0; i < ctx->branch_count; i++) {
        size_t rel_off = ctx->branches[i].rel_off;
        int rel_width = ctx->branches[i].rel_width;
        int label_id = ctx->branches[i].label_id;
        int64_t disp;
        if (label_id < 0 || (size_t)label_id >= ctx->label_count) return -1;
        if (!ctx->labels[label_id].bound) return -1;
        if (!(rel_width == 2 || rel_width == 4)) return -1;
        disp = (int64_t)ctx->labels[label_id].pos - (int64_t)(rel_off + (size_t)rel_width);
        if (rel_width == 2) {
            if (disp < INT16_MIN || disp > INT16_MAX) return -1;
            if (mc_patch_rel16(&ctx->code, rel_off, (int16_t)disp) != 0) return -1;
        } else {
            if (disp < INT32_MIN || disp > INT32_MAX) return -1;
            if (mc_patch_rel32(&ctx->code, rel_off, (int32_t)disp) != 0) return -1;
        }
    }
    return 0;
}

static int mc_emit_mov_acc_imm(McCtx *ctx, int64_t v) {
    if (!ctx) return -1;
    if (ctx->machine_bits == 16) {
        if (mc_emit_u8(&ctx->code, 0xB8) != 0) return -1;
        return mc_emit_u16(&ctx->code, (uint16_t)v);
    }
    if (ctx->machine_bits == 32) {
        if (mc_emit_u8(&ctx->code, 0xB8) != 0) return -1;
        return mc_emit_u32(&ctx->code, (uint32_t)v);
    }
    if (v >= INT32_MIN && v <= INT32_MAX) {
        if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
        if (mc_emit_u8(&ctx->code, 0xC7) != 0) return -1;
        if (mc_emit_u8(&ctx->code, 0xC0) != 0) return -1;
        return mc_emit_u32(&ctx->code, (uint32_t)v);
    }
    if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0xB8) != 0) return -1;
    return mc_emit_u64(&ctx->code, (uint64_t)v);
}

static int mc_emit_mov_acc_arg(McCtx *ctx, int arg_idx) {
    int disp;
    if (!ctx) return -1;
    if (ctx->machine_bits == 64) {
        switch (arg_idx) {
            case 0: return (mc_emit_u8(&ctx->code, 0x48) || mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xF8)) ? -1 : 0;
            case 1: return (mc_emit_u8(&ctx->code, 0x48) || mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xF0)) ? -1 : 0;
            case 2: return (mc_emit_u8(&ctx->code, 0x48) || mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xD0)) ? -1 : 0;
            case 3: return (mc_emit_u8(&ctx->code, 0x48) || mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xC8)) ? -1 : 0;
            case 4: return (mc_emit_u8(&ctx->code, 0x4C) || mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xC0)) ? -1 : 0;
            case 5: return (mc_emit_u8(&ctx->code, 0x4C) || mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xC8)) ? -1 : 0;
            default: return mc_emit_mov_acc_imm(ctx, 0);
        }
    }
    if (arg_idx < 0) return mc_emit_mov_acc_imm(ctx, 0);
    disp = mc_word_bytes(ctx) * (arg_idx + 2);
    if (ctx->machine_bits == 16) {
        if (mc_emit_u8(&ctx->code, 0x8B) != 0) return -1;
        if (disp >= -128 && disp <= 127) {
            if (mc_emit_u8(&ctx->code, 0x46) != 0) return -1;
            return mc_emit_u8(&ctx->code, (uint8_t)disp);
        }
        if (mc_emit_u8(&ctx->code, 0x86) != 0) return -1;
        return mc_emit_u16(&ctx->code, (uint16_t)disp);
    }
    if (mc_emit_u8(&ctx->code, 0x8B) != 0) return -1;
    if (disp >= -128 && disp <= 127) {
        if (mc_emit_u8(&ctx->code, 0x45) != 0) return -1;
        return mc_emit_u8(&ctx->code, (uint8_t)disp);
    }
    if (mc_emit_u8(&ctx->code, 0x85) != 0) return -1;
    return mc_emit_u32(&ctx->code, (uint32_t)disp);
}

static int mc_emit_mov_acc_from_reg_id(McCtx *ctx, int reg_id) {
    uint8_t rex = 0x48;
    uint8_t modrm;
    if (!ctx || !mc_is_x64(ctx) || reg_id < 0 || reg_id > 15) return -1;
    if (reg_id > 7) rex |= 0x01; /* B */
    modrm = 0xC0 | (reg_id & 0x7); /* mov rax, r/m64 */
    if (mc_emit_u8(&ctx->code, rex) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x8B) != 0) return -1;
    if (mc_emit_u8(&ctx->code, modrm) != 0) return -1;
    return 0;
}

static int mc_emit_mov_reg_id_from_acc(McCtx *ctx, int reg_id) {
    uint8_t rex = 0x48;
    uint8_t modrm;
    if (!ctx || !mc_is_x64(ctx) || reg_id < 0 || reg_id > 15) return -1;
    if (reg_id > 7) rex |= 0x01; /* B */
    modrm = 0xC0 | (reg_id & 0x7); /* mov r/m64, rax */
    if (mc_emit_u8(&ctx->code, rex) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x89) != 0) return -1;
    if (mc_emit_u8(&ctx->code, modrm) != 0) return -1;
    return 0;
}

static int mc_emit_load_acc_from_stack(McCtx *ctx, int32_t stack_offset) {
    if (!ctx || !mc_is_x64(ctx) || stack_offset >= 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x8B) != 0) return -1;
    if (stack_offset >= -128) {
        if (mc_emit_u8(&ctx->code, 0x45) != 0) return -1;
        return mc_emit_u8(&ctx->code, (uint8_t)stack_offset);
    }
    if (mc_emit_u8(&ctx->code, 0x85) != 0) return -1;
    return mc_emit_u32(&ctx->code, (uint32_t)stack_offset);
}

static int mc_emit_store_acc_to_stack(McCtx *ctx, int32_t stack_offset) {
    if (!ctx || !mc_is_x64(ctx) || stack_offset >= 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x89) != 0) return -1;
    if (stack_offset >= -128) {
        if (mc_emit_u8(&ctx->code, 0x45) != 0) return -1;
        return mc_emit_u8(&ctx->code, (uint8_t)stack_offset);
    }
    if (mc_emit_u8(&ctx->code, 0x85) != 0) return -1;
    return mc_emit_u32(&ctx->code, (uint32_t)stack_offset);
}

static int mc_emit_pop_argreg(McCtx *ctx, int arg_idx) {
    if (!ctx || ctx->machine_bits != 64) return -1;
    switch (arg_idx) {
        case 0: return mc_emit_u8(&ctx->code, 0x5F);
        case 1: return mc_emit_u8(&ctx->code, 0x5E);
        case 2: return mc_emit_u8(&ctx->code, 0x5A);
        case 3: return mc_emit_u8(&ctx->code, 0x59);
        case 4: return (mc_emit_u8(&ctx->code, 0x41) || mc_emit_u8(&ctx->code, 0x58)) ? -1 : 0;
        case 5: return (mc_emit_u8(&ctx->code, 0x41) || mc_emit_u8(&ctx->code, 0x59)) ? -1 : 0;
        default: return -1;
    }
}

static int mc_emit_call_target(McCtx *ctx, const char *target) {
    size_t off;
    int rel_width;
    if (!ctx || !target) return -1;
    if (mc_emit_u8(&ctx->code, 0xE8) != 0) return -1;
    off = ctx->code.size;
    if (ctx->machine_bits == 16) {
        rel_width = 2;
        if (mc_emit_u16(&ctx->code, 0) != 0) return -1;
    } else {
        rel_width = 4;
        if (mc_emit_u32(&ctx->code, 0) != 0) return -1;
    }
    return mc_ctx_add_call_fixup(ctx, target, off, rel_width);
}

static int mc_emit_vmovdqu_ymm_from_rax(McCtx *ctx, int vec_reg_id) {
    uint8_t modrm;
    if (!ctx || !mc_is_x64(ctx) || vec_reg_id < 0 || vec_reg_id > 7) return -1;
    modrm = (uint8_t)(0x00 | ((vec_reg_id & 7) << 3) | 0x00); /* [rax] */
    if (mc_emit_u8(&ctx->code, 0xC5) != 0 || mc_emit_u8(&ctx->code, 0xFD) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x6F) != 0) return -1;
    return mc_emit_u8(&ctx->code, modrm);
}

static int mc_emit_vmovdqu_ymm_to_rax(McCtx *ctx, int vec_reg_id) {
    uint8_t modrm;
    if (!ctx || !mc_is_x64(ctx) || vec_reg_id < 0 || vec_reg_id > 7) return -1;
    modrm = (uint8_t)(0x00 | ((vec_reg_id & 7) << 3) | 0x00); /* [rax] */
    if (mc_emit_u8(&ctx->code, 0xC5) != 0 || mc_emit_u8(&ctx->code, 0xFD) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x7F) != 0) return -1;
    return mc_emit_u8(&ctx->code, modrm);
}

static int mc_emit_epilogue(McCtx *ctx) {
    if (!ctx) return -1;
    if (mc_emit_u8(&ctx->code, 0xC9) != 0) return -1; /* leave */
    if (mc_emit_u8(&ctx->code, 0xC3) != 0) return -1; /* ret */
    return 0;
}

static int mc_emit_expr(McCtx *ctx, ASTNode *expr);
static int mc_emit_stmt(McCtx *ctx, ASTNode *stmt);

static int mc_is_reg_alias_type(ASTNode *type_node, ASTNode *init_node) {
    const char *type_name = NULL;
    if (type_node && type_node->type == AST_TYPE && type_node->data.type.name) {
        type_name = type_node->data.type.name;
    }
    if (!type_name && init_node &&
        init_node->type == AST_TYPECAST &&
        init_node->data.typecast.target_type &&
        init_node->data.typecast.target_type->type == AST_TYPE) {
        type_name = init_node->data.typecast.target_type->data.type.name;
    }
    return (type_name && strncmp(type_name, "reg<", 4) == 0);
}

static int mc_plan_locals_in_stmt(McCtx *ctx, ASTNode *stmt) {
    int i;
    if (!ctx || !stmt) return 0;
    switch (stmt->type) {
        case AST_VAR_DECL: {
            const char *name = stmt->data.var_decl.name;
            McSymbol sym;
            if (!name) return 0;
            if (mc_is_reg_alias_type(stmt->data.var_decl.type, stmt->data.var_decl.init)) return 0;
            if (mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, name)) return 0;
            memset(&sym, 0, sizeof(sym));
            sym.name = (char *)name;
            sym.kind = MC_SYM_STACK;
            if (stmt->data.var_decl.type &&
                stmt->data.var_decl.type->type == AST_TYPE &&
                stmt->data.var_decl.type->data.type.name) {
                sym.type_name = stmt->data.var_decl.type->data.type.name;
            } else if (stmt->data.var_decl.init &&
                       stmt->data.var_decl.init->type == AST_TYPECAST &&
                       stmt->data.var_decl.init->data.typecast.target_type &&
                       stmt->data.var_decl.init->data.typecast.target_type->type == AST_TYPE &&
                       stmt->data.var_decl.init->data.typecast.target_type->data.type.name) {
                sym.type_name = stmt->data.var_decl.init->data.typecast.target_type->data.type.name;
            }
            ctx->stack_allocated += 8;
            sym.stack_offset = -ctx->stack_allocated;
            if (mc_add_local_symbol(ctx, &sym) != 0) return -1;
            return 0;
        }
        case AST_BLOCK:
            for (i = 0; i < stmt->data.block.stmt_count; i++) {
                if (mc_plan_locals_in_stmt(ctx, stmt->data.block.statements[i]) != 0) return -1;
            }
            return 0;
        case AST_IF_STMT:
            if (mc_plan_locals_in_stmt(ctx, stmt->data.if_stmt.then_branch) != 0) return -1;
            if (stmt->data.if_stmt.else_branch &&
                mc_plan_locals_in_stmt(ctx, stmt->data.if_stmt.else_branch) != 0) return -1;
            return 0;
        case AST_WHILE_STMT:
            return mc_plan_locals_in_stmt(ctx, stmt->data.while_stmt.body);
        case AST_HW_BLOCK:
            for (i = 0; i < stmt->data.hw_block.stmt_count; i++) {
                if (mc_plan_locals_in_stmt(ctx, stmt->data.hw_block.statements[i]) != 0) return -1;
            }
            return 0;
        case AST_HW_MORPH_BLOCK:
            for (i = 0; i < stmt->data.hw_morph_block.stmt_count; i++) {
                if (mc_plan_locals_in_stmt(ctx, stmt->data.hw_morph_block.statements[i]) != 0) return -1;
            }
            return 0;
        default:
            return 0;
    }
}

typedef enum {
    MC_PRINT_MODE_SYSCALL = 0,
    MC_PRINT_MODE_RAW = 1,
    MC_PRINT_MODE_UEFI = 2
} McPrintMode;

typedef struct {
    ASTNode *positional[64];
    int positional_count;
    ASTNode *sep_expr;
    ASTNode *end_expr;
    ASTNode *file_expr;
    ASTNode *flush_expr;
} McPrintArgs;

static int mc_ast_literal_is_string(const ASTNode *expr) {
    return expr && expr->type == AST_LITERAL &&
           !expr->data.literal.is_float &&
           expr->data.literal.is_string &&
           expr->data.literal.value.str_value != NULL;
}

static McPrintMode mc_select_print_mode(const McCtx *ctx) {
    if (!ctx) return MC_PRINT_MODE_SYSCALL;
    /* UEFI/PE targets must never emit syscall-based print stubs. */
    if (ctx->target_format == 4 || ctx->target_format == 7) return MC_PRINT_MODE_UEFI;
    if (ctx->current_func_is_efi) return MC_PRINT_MODE_UEFI;
    /* RAW print is only for flat machine code BIN. */
    if (ctx->target_format == 6) return MC_PRINT_MODE_RAW;
    return MC_PRINT_MODE_SYSCALL;
}

static int mc_emit_mov_esi_imm32(McCtx *ctx, uint32_t imm) {
    if (mc_emit_u8(&ctx->code, 0xBE) != 0) return -1;
    return mc_emit_u32(&ctx->code, imm);
}

static int mc_emit_mov_rdi_rax(McCtx *ctx) {
    return (mc_emit_u8(&ctx->code, 0x48) || mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xC7)) ? -1 : 0;
}

static int mc_emit_mov_rdx_rax(McCtx *ctx) {
    return (mc_emit_u8(&ctx->code, 0x48) || mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xC2)) ? -1 : 0;
}

static int mc_emit_strlen_rdi_to_rsi(McCtx *ctx) {
    if (!ctx) return -1;
    /* mov rsi, rdi; xor edx,edx; loop: cmp byte [rsi],0; jz done; inc rsi; inc rdx; jmp loop; done: mov rsi,rdx */
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xFE) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x31) != 0 || mc_emit_u8(&ctx->code, 0xD2) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x80) != 0 || mc_emit_u8(&ctx->code, 0x3E) != 0 || mc_emit_u8(&ctx->code, 0x00) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x74) != 0 || mc_emit_u8(&ctx->code, 0x08) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0xFF) != 0 || mc_emit_u8(&ctx->code, 0xC6) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0xFF) != 0 || mc_emit_u8(&ctx->code, 0xC2) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0xEB) != 0 || mc_emit_u8(&ctx->code, 0xF3) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xD6) != 0) return -1;
    return 0;
}

static int mc_emit_load_rsi_from_stack(McCtx *ctx, int32_t stack_offset) {
    if (!ctx || !mc_is_x64(ctx) || stack_offset >= 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x8B) != 0) return -1;
    if (stack_offset >= -128) {
        if (mc_emit_u8(&ctx->code, 0x75) != 0) return -1; /* mov rsi, [rbp+disp8] */
        return mc_emit_u8(&ctx->code, (uint8_t)stack_offset);
    }
    if (mc_emit_u8(&ctx->code, 0xB5) != 0) return -1; /* mov rsi, [rbp+disp32] */
    return mc_emit_u32(&ctx->code, (uint32_t)stack_offset);
}

static int mc_emit_syscall_print_call(McCtx *ctx) {
    if (!ctx) return -1;
    if (mc_emit_u8(&ctx->code, 0xB8) != 0) return -1; /* mov eax, SYS_PRINT */
    if (mc_emit_u32(&ctx->code, 1) != 0) return -1;
    return (mc_emit_u8(&ctx->code, 0x0F) || mc_emit_u8(&ctx->code, 0x05)) ? -1 : 0; /* syscall */
}

static int mc_emit_raw_print_call(McCtx *ctx) {
    if (!ctx) return -1;
    /* input: rdi=buf, rsi=len */
    if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1; /* mov edx, 0x3F8 */
    if (mc_emit_u32(&ctx->code, 0x3F8) != 0) return -1;
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x85) != 0 || mc_emit_u8(&ctx->code, 0xF6) != 0) return -1; /* test rsi,rsi */
    if (mc_emit_u8(&ctx->code, 0x74) != 0 || mc_emit_u8(&ctx->code, 0x0B) != 0) return -1; /* jz done */
    if (mc_emit_u8(&ctx->code, 0x8A) != 0 || mc_emit_u8(&ctx->code, 0x07) != 0) return -1; /* mov al,[rdi] */
    if (mc_emit_u8(&ctx->code, 0xEE) != 0) return -1; /* out dx, al */
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0xFF) != 0 || mc_emit_u8(&ctx->code, 0xC7) != 0) return -1; /* inc rdi */
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0xFF) != 0 || mc_emit_u8(&ctx->code, 0xCE) != 0) return -1; /* dec rsi */
    if (mc_emit_u8(&ctx->code, 0x75) != 0 || mc_emit_u8(&ctx->code, 0xF5) != 0) return -1; /* jnz loop */
    return 0;
}

static int mc_emit_raw_print_bytes_immediate(McCtx *ctx, const uint8_t *buf, size_t len) {
    size_t i;
    if (!ctx || !buf) return -1;
    if (ctx->machine_bits == 16) {
        if (mc_emit_u8(&ctx->code, 0xB8) != 0) return -1; /* mov ax, 0x0003 */
        if (mc_emit_u16(&ctx->code, 0x0003u) != 0) return -1;
        if (mc_emit_u8(&ctx->code, 0xCD) != 0 || mc_emit_u8(&ctx->code, 0x10) != 0) return -1; /* int 10h */
        if (mc_emit_u8(&ctx->code, 0xB4) != 0 || mc_emit_u8(&ctx->code, 0x05) != 0) return -1; /* mov ah,0x05 (select active page) */
        if (mc_emit_u8(&ctx->code, 0xB0) != 0 || mc_emit_u8(&ctx->code, 0x00) != 0) return -1; /* mov al,0 */
        if (mc_emit_u8(&ctx->code, 0xCD) != 0 || mc_emit_u8(&ctx->code, 0x10) != 0) return -1; /* int 10h */
        if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1; /* mov dx, 0x3F8 */
        if (mc_emit_u16(&ctx->code, 0x03F8u) != 0) return -1;
        if (mc_emit_u8(&ctx->code, 0xB8) != 0) return -1; /* mov ax, 0xB800 */
        if (mc_emit_u16(&ctx->code, 0xB800u) != 0) return -1;
        if (mc_emit_u8(&ctx->code, 0x8E) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1; /* mov es,ax */
        if (mc_emit_u8(&ctx->code, 0x31) != 0 || mc_emit_u8(&ctx->code, 0xFF) != 0) return -1; /* xor di,di */
    } else {
        if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1; /* mov edx, 0x3F8 */
        if (mc_emit_u32(&ctx->code, 0x03F8u) != 0) return -1;
        if (mc_emit_u8(&ctx->code, 0xBF) != 0) return -1; /* mov edi, 0xB8000 */
        if (mc_emit_u32(&ctx->code, 0x000B8000u) != 0) return -1;
    }
    for (i = 0; i < len; i++) {
        if (mc_emit_u8(&ctx->code, 0xB0) != 0) return -1; /* mov al, imm8 */
        if (mc_emit_u8(&ctx->code, buf[i]) != 0) return -1;
        if (mc_emit_u8(&ctx->code, 0xEE) != 0) return -1; /* out dx, al */
        if (buf[i] == '\n') {
            if (ctx->machine_bits == 16) {
                if (mc_emit_u8(&ctx->code, 0x81) != 0 || mc_emit_u8(&ctx->code, 0xC7) != 0) return -1; /* add di,160 */
                if (mc_emit_u16(&ctx->code, 160u) != 0) return -1;
            } else {
                if (mc_emit_u8(&ctx->code, 0x81) != 0 || mc_emit_u8(&ctx->code, 0xC7) != 0) return -1; /* add edi,160 */
                if (mc_emit_u32(&ctx->code, 160u) != 0) return -1;
            }
            continue;
        }
        if (ctx->machine_bits == 16) {
            if (mc_emit_u8(&ctx->code, 0xB8) != 0) return -1; /* mov ax, imm16 */
            if (mc_emit_u16(&ctx->code, (uint16_t)(0x0700u | (uint16_t)buf[i])) != 0) return -1;
            if (mc_emit_u8(&ctx->code, 0x26) != 0) return -1; /* es: */
            if (mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0x05) != 0) return -1; /* mov [di],ax */
            if (mc_emit_u8(&ctx->code, 0x83) != 0 || mc_emit_u8(&ctx->code, 0xC7) != 0 || mc_emit_u8(&ctx->code, 0x02) != 0) return -1; /* add di,2 */
            /* BIOS teletype mirror for stable on-screen visibility in early real mode */
            if (mc_emit_u8(&ctx->code, 0xB4) != 0 || mc_emit_u8(&ctx->code, 0x0E) != 0) return -1; /* mov ah,0x0E */
            if (mc_emit_u8(&ctx->code, 0xB7) != 0 || mc_emit_u8(&ctx->code, 0x00) != 0) return -1; /* mov bh,0 */
            if (mc_emit_u8(&ctx->code, 0xB3) != 0 || mc_emit_u8(&ctx->code, 0x07) != 0) return -1; /* mov bl,7 */
            if (mc_emit_u8(&ctx->code, 0xCD) != 0 || mc_emit_u8(&ctx->code, 0x10) != 0) return -1; /* int 10h */
        } else {
            if (mc_emit_u8(&ctx->code, 0x66) != 0 || mc_emit_u8(&ctx->code, 0xB8) != 0) return -1; /* mov ax, imm16 */
            if (mc_emit_u16(&ctx->code, (uint16_t)(0x0700u | (uint16_t)buf[i])) != 0) return -1;
            if (mc_emit_u8(&ctx->code, 0x66) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0x07) != 0) return -1; /* mov [edi/rdi],ax */
            if (ctx->machine_bits == 64) {
                if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
            }
            if (mc_emit_u8(&ctx->code, 0x83) != 0 || mc_emit_u8(&ctx->code, 0xC7) != 0 || mc_emit_u8(&ctx->code, 0x02) != 0) return -1; /* add edi/rdi,2 */
        }
    }
    return 0;
}

static int mc_emit_uefi_print_call(McCtx *ctx) {
    if (!ctx) return -1;
    if (ctx->uefi_system_table_offset < 0) {
        if (mc_emit_load_rsi_from_stack(ctx, ctx->uefi_system_table_offset) != 0) return -1;
    }
    /* input: rdx = UTF-16 string ptr, rsi = EFI_SYSTEM_TABLE* */
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x8B) != 0 || mc_emit_u8(&ctx->code, 0x46) != 0 || mc_emit_u8(&ctx->code, 0x40) != 0) return -1; /* mov rax, [rsi+0x40] */
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC1) != 0) return -1; /* mov rcx, rax */
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x83) != 0 || mc_emit_u8(&ctx->code, 0xEC) != 0 || mc_emit_u8(&ctx->code, 0x28) != 0) return -1; /* sub rsp, 0x28 */
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x8B) != 0 || mc_emit_u8(&ctx->code, 0x40) != 0 || mc_emit_u8(&ctx->code, 0x08) != 0) return -1; /* mov rax, [rax+0x08] */
    if (mc_emit_u8(&ctx->code, 0xFF) != 0 || mc_emit_u8(&ctx->code, 0xD0) != 0) return -1; /* call rax */
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x83) != 0 || mc_emit_u8(&ctx->code, 0xC4) != 0 || mc_emit_u8(&ctx->code, 0x28) != 0) return -1; /* add rsp, 0x28 */
    return 0;
}

static int mc_emit_embed_blob_and_lea_reg(McCtx *ctx, const uint8_t *blob, size_t blob_len, int dst_reg_low3) {
    int32_t disp32;
    size_t data_pos;
    size_t lea_pos;
    uint8_t modrm;
    if (!ctx || !blob || blob_len == 0 || dst_reg_low3 < 0 || dst_reg_low3 > 7) return -1;
    if (blob_len > 0x7FFFFFFFul) return -1;
    if (mc_emit_u8(&ctx->code, 0xE9) != 0) return -1; /* jmp rel32 */
    if (mc_emit_u32(&ctx->code, (uint32_t)blob_len) != 0) return -1;
    data_pos = ctx->code.size;
    if (mc_reserve(&ctx->code, blob_len) != 0) return -1;
    memcpy(ctx->code.data + ctx->code.size, blob, blob_len);
    ctx->code.size += blob_len;
    lea_pos = ctx->code.size;
    disp32 = (int32_t)((int64_t)data_pos - (int64_t)(lea_pos + 7));
    modrm = (uint8_t)(0x05 | ((dst_reg_low3 & 0x7) << 3));
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x8D) != 0 || mc_emit_u8(&ctx->code, modrm) != 0) return -1;
    return mc_emit_u32(&ctx->code, (uint32_t)disp32);
}

static int mc_emit_print_buffer(McCtx *ctx, McPrintMode mode, const uint8_t *buf, size_t len) {
    if (!ctx || !buf) return -1;
    if (mode == MC_PRINT_MODE_UEFI) {
        if (mc_emit_embed_blob_and_lea_reg(ctx, buf, len, 2) != 0) return -1; /* rdx */
        return mc_emit_uefi_print_call(ctx);
    }
    if (mode == MC_PRINT_MODE_RAW) {
        return mc_emit_raw_print_bytes_immediate(ctx, buf, len);
    }
    if (mc_emit_embed_blob_and_lea_reg(ctx, buf, len, 7) != 0) return -1; /* rdi */
    if (len > 0xFFFFFFFFu) return -1;
    if (mc_emit_mov_esi_imm32(ctx, (uint32_t)len) != 0) return -1;
    return (mode == MC_PRINT_MODE_RAW) ? mc_emit_raw_print_call(ctx) : mc_emit_syscall_print_call(ctx);
}

static int mc_emit_print_text(McCtx *ctx, McPrintMode mode, const char *text) {
    uint8_t *encoded = NULL;
    char *normalized = NULL;
    size_t encoded_size = 0;
    size_t i;
    size_t lf_count = 0;
    size_t out_pos = 0;
    int rc = -1;
    if (!ctx || !text) return -1;
    if (mode == MC_PRINT_MODE_UEFI) {
        for (i = 0; text[i] != '\0'; i++) {
            if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) {
                lf_count++;
            }
        }
        if (lf_count > 0) {
            size_t in_len = strlen(text);
            normalized = (char *)malloc(in_len + lf_count + 1);
            if (!normalized) return -1;
            for (i = 0; text[i] != '\0'; i++) {
                if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) {
                    normalized[out_pos++] = '\r';
                }
                normalized[out_pos++] = text[i];
            }
            normalized[out_pos] = '\0';
        }
        if (prepare_string_constant(normalized ? normalized : text, 4, &encoded, &encoded_size) != 0) {
            free(normalized);
            return -1;
        }
        free(normalized);
        rc = mc_emit_print_buffer(ctx, mode, encoded, encoded_size);
        free(encoded);
        return rc;
    }
    rc = mc_emit_print_buffer(ctx, mode, (const uint8_t *)text, strlen(text));
    return rc;
}

static int mc_emit_print_expr_runtime(McCtx *ctx, McPrintMode mode, ASTNode *expr) {
    if (!ctx || !expr) return -1;
    if (mode == MC_PRINT_MODE_RAW && !mc_is_x64(ctx)) {
        return -1; /* 16/32-bit RAW mode only supports compile-time text printing */
    }
    if (mc_emit_expr(ctx, expr) != 0) return -1;
    if (mode == MC_PRINT_MODE_UEFI) {
        if (mc_emit_mov_rdx_rax(ctx) != 0) return -1;
        return mc_emit_uefi_print_call(ctx);
    }
    if (mc_emit_mov_rdi_rax(ctx) != 0) return -1;
    if (mc_emit_strlen_rdi_to_rsi(ctx) != 0) return -1;
    return (mode == MC_PRINT_MODE_RAW) ? mc_emit_raw_print_call(ctx) : mc_emit_syscall_print_call(ctx);
}

static int mc_emit_print_arg(McCtx *ctx, McPrintMode mode, ASTNode *arg) {
    char tmp[128];
    if (!ctx || !arg) return -1;
    if (mc_ast_literal_is_string(arg)) {
        return mc_emit_print_text(ctx, mode, arg->data.literal.value.str_value);
    }
    if (arg->type == AST_LITERAL && arg->data.literal.is_float) {
        snprintf(tmp, sizeof(tmp), "%g", arg->data.literal.value.float_value);
        return mc_emit_print_text(ctx, mode, tmp);
    }
    if (arg->type == AST_LITERAL && !arg->data.literal.is_string) {
        snprintf(tmp, sizeof(tmp), "%lld", (long long)arg->data.literal.value.int_value);
        return mc_emit_print_text(ctx, mode, tmp);
    }
    return mc_emit_print_expr_runtime(ctx, mode, arg);
}

static int mc_emit_modejump32_auto(McCtx *ctx, uint32_t entry, uint32_t load_base) {
    size_t seq_start;
    size_t lgdt_disp_off;
    size_t far_ptr_off;
    size_t gdtr_base_off;
    size_t pm32_off;
    size_t gdtr_off;
    size_t gdt_off;

    if (!ctx || ctx->machine_bits != 16) return -1;
    seq_start = ctx->code.size;

    if (mc_emit_u8(&ctx->code, 0xFA) != 0) return -1; /* cli */

    if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x01) != 0 || mc_emit_u8(&ctx->code, 0x16) != 0) return -1;
    lgdt_disp_off = ctx->code.size;
    if (mc_emit_u16(&ctx->code, 0) != 0) return -1; /* lgdt [disp16] */

    if (mc_emit_u8(&ctx->code, 0x66) != 0 || mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x20) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1; /* mov eax, cr0 */
    if (mc_emit_u8(&ctx->code, 0x66) != 0 || mc_emit_u8(&ctx->code, 0x83) != 0 || mc_emit_u8(&ctx->code, 0xC8) != 0 || mc_emit_u8(&ctx->code, 0x01) != 0) return -1; /* or eax, 1 */
    if (mc_emit_u8(&ctx->code, 0x66) != 0 || mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x22) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1; /* mov cr0, eax */

    if (mc_emit_u8(&ctx->code, 0x66) != 0 || mc_emit_u8(&ctx->code, 0xEA) != 0) return -1; /* jmp ptr16:32 */
    far_ptr_off = ctx->code.size;
    if (mc_emit_u32(&ctx->code, 0) != 0) return -1; /* pm32 entry linear */
    if (mc_emit_u16(&ctx->code, 0x0008) != 0) return -1; /* CS selector */

    pm32_off = ctx->code.size - seq_start;
    if (mc_emit_u8(&ctx->code, 0x66) != 0 || mc_emit_u8(&ctx->code, 0xB8) != 0 || mc_emit_u16(&ctx->code, 0x0010) != 0) return -1; /* mov ax,0x10 */
    if (mc_emit_u8(&ctx->code, 0x8E) != 0 || mc_emit_u8(&ctx->code, 0xD8) != 0) return -1; /* mov ds,ax */
    if (mc_emit_u8(&ctx->code, 0x8E) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1; /* mov es,ax */
    if (mc_emit_u8(&ctx->code, 0x8E) != 0 || mc_emit_u8(&ctx->code, 0xE0) != 0) return -1; /* mov fs,ax */
    if (mc_emit_u8(&ctx->code, 0x8E) != 0 || mc_emit_u8(&ctx->code, 0xE8) != 0) return -1; /* mov gs,ax */
    if (mc_emit_u8(&ctx->code, 0x8E) != 0 || mc_emit_u8(&ctx->code, 0xD0) != 0) return -1; /* mov ss,ax */
    if (mc_emit_u8(&ctx->code, 0x66) != 0 || mc_emit_u8(&ctx->code, 0xBC) != 0 || mc_emit_u32(&ctx->code, 0x0009FC00u) != 0) return -1; /* mov esp,0x9FC00 */
    if (mc_emit_u8(&ctx->code, 0x66) != 0 || mc_emit_u8(&ctx->code, 0xB8) != 0 || mc_emit_u32(&ctx->code, entry) != 0) return -1; /* mov eax,entry */
    if (mc_emit_u8(&ctx->code, 0x66) != 0 || mc_emit_u8(&ctx->code, 0xFF) != 0 || mc_emit_u8(&ctx->code, 0xE0) != 0) return -1; /* jmp eax */

    gdtr_off = ctx->code.size - seq_start;
    if (mc_emit_u16(&ctx->code, (uint16_t)(3u * 8u - 1u)) != 0) return -1;
    gdtr_base_off = ctx->code.size;
    if (mc_emit_u32(&ctx->code, 0) != 0) return -1;

    gdt_off = ctx->code.size - seq_start;
    if (mc_emit_u64(&ctx->code, 0x0000000000000000ull) != 0) return -1;
    if (mc_emit_u64(&ctx->code, 0x00CF9A000000FFFFull) != 0) return -1;
    if (mc_emit_u64(&ctx->code, 0x00CF92000000FFFFull) != 0) return -1;

    if (mc_patch_u16(&ctx->code, lgdt_disp_off, (uint16_t)(load_base + (uint32_t)gdtr_off)) != 0) return -1;
    if (mc_patch_u32(&ctx->code, far_ptr_off, load_base + (uint32_t)pm32_off) != 0) return -1;
    if (mc_patch_u32(&ctx->code, gdtr_base_off, load_base + (uint32_t)gdt_off) != 0) return -1;
    return 0;
}

static int mc_emit_modejump64_auto(McCtx *ctx, uint64_t entry, uint32_t load_base) {
    size_t seq_start;
    size_t lgdt_disp_off;
    size_t mov_cr3_imm_off;
    size_t far_ptr_off;
    size_t gdtr_base_off;
    size_t lm_off;
    size_t gdtr_off;
    size_t gdt_off;
    size_t pml4_off;
    size_t pdpt_off;
    size_t pd_off;
    uint32_t linear;
    uint32_t pad;
    int i;

    if (!ctx || ctx->machine_bits != 32) return -1;
    seq_start = ctx->code.size;

    if (mc_emit_u8(&ctx->code, 0xFA) != 0) return -1; /* cli */

    if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x01) != 0 || mc_emit_u8(&ctx->code, 0x15) != 0) return -1;
    lgdt_disp_off = ctx->code.size;
    if (mc_emit_u32(&ctx->code, 0) != 0) return -1; /* lgdt [disp32] */

    if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x20) != 0 || mc_emit_u8(&ctx->code, 0xE0) != 0) return -1; /* mov eax,cr4 */
    if (mc_emit_u8(&ctx->code, 0x83) != 0 || mc_emit_u8(&ctx->code, 0xC8) != 0 || mc_emit_u8(&ctx->code, 0x20) != 0) return -1; /* or eax,0x20 */
    if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x22) != 0 || mc_emit_u8(&ctx->code, 0xE0) != 0) return -1; /* mov cr4,eax */

    if (mc_emit_u8(&ctx->code, 0xB9) != 0 || mc_emit_u32(&ctx->code, 0xC0000080u) != 0) return -1; /* ecx=EFER */
    if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x32) != 0) return -1; /* rdmsr */
    if (mc_emit_u8(&ctx->code, 0x0D) != 0 || mc_emit_u32(&ctx->code, 0x00000100u) != 0) return -1; /* or eax,LME */
    if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x30) != 0) return -1; /* wrmsr */

    if (mc_emit_u8(&ctx->code, 0xB8) != 0) return -1;
    mov_cr3_imm_off = ctx->code.size;
    if (mc_emit_u32(&ctx->code, 0) != 0) return -1; /* pml4 base */
    if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x22) != 0 || mc_emit_u8(&ctx->code, 0xD8) != 0) return -1; /* mov cr3,eax */

    if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x20) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1; /* mov eax,cr0 */
    if (mc_emit_u8(&ctx->code, 0x0D) != 0 || mc_emit_u32(&ctx->code, 0x80000001u) != 0) return -1; /* or eax,PG|PE */
    if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x22) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1; /* mov cr0,eax */

    if (mc_emit_u8(&ctx->code, 0xEA) != 0) return -1; /* jmp ptr16:32 */
    far_ptr_off = ctx->code.size;
    if (mc_emit_u32(&ctx->code, 0) != 0) return -1; /* lm64 entry linear */
    if (mc_emit_u16(&ctx->code, 0x0008) != 0) return -1;

    lm_off = ctx->code.size - seq_start;
    if (mc_emit_u8(&ctx->code, 0x66) != 0 || mc_emit_u8(&ctx->code, 0xB8) != 0 || mc_emit_u16(&ctx->code, 0x0010) != 0) return -1; /* mov ax,0x10 */
    if (mc_emit_u8(&ctx->code, 0x8E) != 0 || mc_emit_u8(&ctx->code, 0xD8) != 0) return -1; /* mov ds,ax */
    if (mc_emit_u8(&ctx->code, 0x8E) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1; /* mov es,ax */
    if (mc_emit_u8(&ctx->code, 0x8E) != 0 || mc_emit_u8(&ctx->code, 0xD0) != 0) return -1; /* mov ss,ax */
    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0xB8) != 0 || mc_emit_u64(&ctx->code, entry) != 0) return -1; /* mov rax,entry */
    if (mc_emit_u8(&ctx->code, 0xFF) != 0 || mc_emit_u8(&ctx->code, 0xE0) != 0) return -1; /* jmp rax */

    gdtr_off = ctx->code.size - seq_start;
    if (mc_emit_u16(&ctx->code, (uint16_t)(3u * 8u - 1u)) != 0) return -1;
    gdtr_base_off = ctx->code.size;
    if (mc_emit_u32(&ctx->code, 0) != 0) return -1;

    gdt_off = ctx->code.size - seq_start;
    if (mc_emit_u64(&ctx->code, 0x0000000000000000ull) != 0) return -1;
    if (mc_emit_u64(&ctx->code, 0x00AF9A000000FFFFull) != 0) return -1; /* long-mode code */
    if (mc_emit_u64(&ctx->code, 0x00CF92000000FFFFull) != 0) return -1; /* data */

    linear = load_base + (uint32_t)(ctx->code.size - seq_start);
    pad = (uint32_t)((4096u - (linear & 0xFFFu)) & 0xFFFu);
    for (i = 0; i < (int)pad; i++) {
        if (mc_emit_u8(&ctx->code, 0x00) != 0) return -1;
    }

    pml4_off = ctx->code.size - seq_start;
    if (mc_emit_u64(&ctx->code, 0) != 0) return -1;
    for (i = 0; i < 511; i++) {
        if (mc_emit_u64(&ctx->code, 0) != 0) return -1;
    }

    pdpt_off = ctx->code.size - seq_start;
    if (mc_emit_u64(&ctx->code, 0) != 0) return -1;
    for (i = 0; i < 511; i++) {
        if (mc_emit_u64(&ctx->code, 0) != 0) return -1;
    }

    pd_off = ctx->code.size - seq_start;
    for (i = 0; i < 512; i++) {
        uint64_t pde = 0;
        if (i < 32) {
            pde = ((uint64_t)i * 0x200000ull) | 0x83ull; /* map first 64 MiB, 2MiB pages */
        }
        if (mc_emit_u64(&ctx->code, pde) != 0) return -1;
    }

    if (mc_patch_u32(&ctx->code, lgdt_disp_off, load_base + (uint32_t)gdtr_off) != 0) return -1;
    if (mc_patch_u32(&ctx->code, far_ptr_off, load_base + (uint32_t)lm_off) != 0) return -1;
    if (mc_patch_u32(&ctx->code, gdtr_base_off, load_base + (uint32_t)gdt_off) != 0) return -1;
    if (mc_patch_u32(&ctx->code, mov_cr3_imm_off, load_base + (uint32_t)pml4_off) != 0) return -1;
    if (mc_patch_u64(&ctx->code, pml4_off, ((uint64_t)load_base + (uint64_t)pdpt_off) | 0x3ull) != 0) return -1;
    if (mc_patch_u64(&ctx->code, pdpt_off, ((uint64_t)load_base + (uint64_t)pd_off) | 0x3ull) != 0) return -1;
    return 0;
}

static int mc_emit_hw_isa_param_call(McCtx *ctx,
                                     const char *op,
                                     ASTNode **operands,
                                     int operand_count) {
    uint64_t imm = 0;
    uint64_t imm2 = 0;
    if (!ctx || !op) return -1;

    if (strcmp(op, "intcall") == 0) {
        if (operand_count != 1 || !operands || !operands[0]) return -1;
        if (mc_eval_const_expr(ctx, operands[0], &imm) != 0) return -1;
        if (imm > 0xFFu) return -1;
        if (mc_emit_u8(&ctx->code, 0xCD) != 0) return -1; /* int imm8 */
        return mc_emit_u8(&ctx->code, (uint8_t)imm);
    }

    if (strcmp(op, "inport") == 0) {
        if (operand_count != 1 || !operands || !operands[0]) return -1;
        if (mc_eval_const_expr(ctx, operands[0], &imm) != 0) return -1;
        if (imm <= 0xFFu) {
            if (mc_emit_u8(&ctx->code, 0xE4) != 0) return -1; /* in al, imm8 */
            return mc_emit_u8(&ctx->code, (uint8_t)imm);
        }
        if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1; /* mov dx, imm16 */
        if (mc_emit_u16(&ctx->code, (uint16_t)imm) != 0) return -1;
        return mc_emit_u8(&ctx->code, 0xEC);              /* in al, dx */
    }

    if (strcmp(op, "outport") == 0) {
        if (operand_count != 2 || !operands || !operands[0] || !operands[1]) return -1;
        if (mc_emit_expr(ctx, operands[1]) != 0) return -1; /* value -> acc (AL low8) */
        if (mc_eval_const_expr(ctx, operands[0], &imm) != 0) return -1;
        if (imm <= 0xFFu) {
            if (mc_emit_u8(&ctx->code, 0xE6) != 0) return -1; /* out imm8, al */
            return mc_emit_u8(&ctx->code, (uint8_t)imm);
        }
        if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1; /* mov dx, imm16 */
        if (mc_emit_u16(&ctx->code, (uint16_t)imm) != 0) return -1;
        return mc_emit_u8(&ctx->code, 0xEE);              /* out dx, al */
    }

    if (strcmp(op, "inport16") == 0) {
        if (operand_count != 1 || !operands || !operands[0]) return -1;
        if (mc_eval_const_expr(ctx, operands[0], &imm) != 0) return -1;
        if (ctx->machine_bits != 16) {
            if (mc_emit_u8(&ctx->code, 0x66) != 0) return -1; /* AX width */
        }
        if (imm <= 0xFFu) {
            if (mc_emit_u8(&ctx->code, 0xE5) != 0) return -1; /* in ax/eax, imm8 */
            if (mc_emit_u8(&ctx->code, (uint8_t)imm) != 0) return -1;
        } else {
            if (ctx->machine_bits == 16) {
                if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1; /* mov dx, imm16 */
                if (mc_emit_u16(&ctx->code, (uint16_t)imm) != 0) return -1;
            } else {
                if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1; /* mov edx, imm32 */
                if (mc_emit_u32(&ctx->code, (uint32_t)(imm & 0xFFFFu)) != 0) return -1;
            }
            if (mc_emit_u8(&ctx->code, 0xED) != 0) return -1; /* in ax/eax, dx */
        }
        if (ctx->machine_bits == 16) {
            if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0xB7) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1; /* movzx ax,ax */
        } else {
            if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0xB7) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1; /* movzx eax,ax */
        }
        return 0;
    }

    if (strcmp(op, "outport16") == 0) {
        if (operand_count != 2 || !operands || !operands[0] || !operands[1]) return -1;
        if (mc_emit_expr(ctx, operands[1]) != 0) return -1; /* value in acc -> AX low16 */
        if (mc_eval_const_expr(ctx, operands[0], &imm) != 0) return -1;
        if (ctx->machine_bits != 16) {
            if (mc_emit_u8(&ctx->code, 0x66) != 0) return -1; /* AX width */
        }
        if (imm <= 0xFFu) {
            if (mc_emit_u8(&ctx->code, 0xE7) != 0) return -1; /* out imm8, ax/eax */
            return mc_emit_u8(&ctx->code, (uint8_t)imm);
        }
        if (ctx->machine_bits == 16) {
            if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1; /* mov dx, imm16 */
            if (mc_emit_u16(&ctx->code, (uint16_t)imm) != 0) return -1;
        } else {
            if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1; /* mov edx, imm32 */
            if (mc_emit_u32(&ctx->code, (uint32_t)(imm & 0xFFFFu)) != 0) return -1;
        }
        return mc_emit_u8(&ctx->code, 0xEF);              /* out dx, ax/eax */
    }

    if (strcmp(op, "inport32") == 0) {
        if (operand_count != 1 || !operands || !operands[0]) return -1;
        if (mc_eval_const_expr(ctx, operands[0], &imm) != 0) return -1;
        if (ctx->machine_bits == 16) {
            if (mc_emit_u8(&ctx->code, 0x66) != 0) return -1; /* EAX width in 16-bit mode */
        }
        if (imm <= 0xFFu) {
            if (mc_emit_u8(&ctx->code, 0xE5) != 0) return -1;
            return mc_emit_u8(&ctx->code, (uint8_t)imm);
        }
        if (ctx->machine_bits == 16) {
            if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1;
            if (mc_emit_u16(&ctx->code, (uint16_t)imm) != 0) return -1;
        } else {
            if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1;
            if (mc_emit_u32(&ctx->code, (uint32_t)(imm & 0xFFFFu)) != 0) return -1;
        }
        return mc_emit_u8(&ctx->code, 0xED);
    }

    if (strcmp(op, "outport32") == 0) {
        if (operand_count != 2 || !operands || !operands[0] || !operands[1]) return -1;
        if (mc_emit_expr(ctx, operands[1]) != 0) return -1;
        if (mc_eval_const_expr(ctx, operands[0], &imm) != 0) return -1;
        if (ctx->machine_bits == 16) {
            if (mc_emit_u8(&ctx->code, 0x66) != 0) return -1; /* EAX width */
        }
        if (imm <= 0xFFu) {
            if (mc_emit_u8(&ctx->code, 0xE7) != 0) return -1;
            return mc_emit_u8(&ctx->code, (uint8_t)imm);
        }
        if (ctx->machine_bits == 16) {
            if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1;
            if (mc_emit_u16(&ctx->code, (uint16_t)imm) != 0) return -1;
        } else {
            if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1;
            if (mc_emit_u32(&ctx->code, (uint32_t)(imm & 0xFFFFu)) != 0) return -1;
        }
        return mc_emit_u8(&ctx->code, 0xEF);
    }

    if (strcmp(op, "modejump32") == 0) {
        if (operand_count != 1 && operand_count != 2) return -1;
        if (!operands || !operands[0]) return -1;
        if (mc_eval_const_expr(ctx, operands[0], &imm) != 0) return -1;
        if (operand_count == 2) {
            if (!operands[1]) return -1;
            if (mc_eval_const_expr(ctx, operands[1], &imm2) != 0) return -1;
        } else {
            imm2 = 0x7C00u;
        }
        if (imm > 0xFFFFFFFFull || imm2 > 0xFFFFFFFFull) return -1;
        return mc_emit_modejump32_auto(ctx, (uint32_t)imm, (uint32_t)imm2);
    }

    if (strcmp(op, "modejump64") == 0) {
        if (operand_count != 1 && operand_count != 2) return -1;
        if (!operands || !operands[0]) return -1;
        if (mc_eval_const_expr(ctx, operands[0], &imm) != 0) return -1;
        if (operand_count == 2) {
            if (!operands[1]) return -1;
            if (mc_eval_const_expr(ctx, operands[1], &imm2) != 0) return -1;
        } else {
            imm2 = 0x8000u;
        }
        if (imm2 > 0xFFFFFFFFull) return -1;
        return mc_emit_modejump64_auto(ctx, imm, (uint32_t)imm2);
    }

    return -1;
}

static int mc_collect_print_args(ASTNode *call, McPrintArgs *out) {
    int i;
    if (!call || !out || call->type != AST_CALL) return -1;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < call->data.call.arg_count; i++) {
        ASTNode *arg = call->data.call.args[i];
        if (arg && arg->type == AST_ASSIGNMENT &&
            arg->data.assignment.left && arg->data.assignment.left->type == AST_IDENT &&
            arg->data.assignment.right) {
            const char *k = arg->data.assignment.left->data.ident.name;
            if (!k) return -1;
            if (strcmp(k, "sep") == 0) {
                if (out->sep_expr) return -1;
                out->sep_expr = arg->data.assignment.right;
                continue;
            }
            if (strcmp(k, "end") == 0) {
                if (out->end_expr) return -1;
                out->end_expr = arg->data.assignment.right;
                continue;
            }
            if (strcmp(k, "file") == 0) {
                if (out->file_expr) return -1;
                out->file_expr = arg->data.assignment.right;
                continue;
            }
            if (strcmp(k, "flush") == 0) {
                if (out->flush_expr) return -1;
                out->flush_expr = arg->data.assignment.right;
                continue;
            }
            return -1;
        }
        if (out->positional_count >= (int)(sizeof(out->positional) / sizeof(out->positional[0]))) return -1;
        out->positional[out->positional_count++] = arg;
    }
    return 0;
}

static int mc_emit_expr(McCtx *ctx, ASTNode *expr) {
    const char *op;
    int i;
    int max_reg_args;
    if (!ctx || !expr) return mc_emit_mov_acc_imm(ctx, 0);

    switch (expr->type) {
        case AST_LITERAL:
            if (expr->data.literal.is_float) {
                return mc_emit_mov_acc_imm(ctx, (int64_t)expr->data.literal.value.float_value);
            }
            return mc_emit_mov_acc_imm(ctx, expr->data.literal.value.int_value);
        case AST_IMPLICIT_PARAM:
            return mc_emit_mov_acc_arg(ctx, expr->data.implicit_param.index);
        case AST_IDENT: {
            const char *name = expr->data.ident.name;
            McSymbol *sym;
            int reg_id;
            char reg_name[64];
            if (!name) return mc_emit_mov_acc_imm(ctx, 0);

            sym = mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, name);
            if (sym) {
                if (sym->kind == MC_SYM_PARAM) return mc_emit_mov_acc_arg(ctx, sym->param_index);
                if (sym->kind == MC_SYM_STACK) return mc_emit_load_acc_from_stack(ctx, sym->stack_offset);
                if (sym->kind == MC_SYM_REG_ALIAS) return mc_emit_mov_acc_from_reg_id(ctx, sym->reg_id);
                if (sym->kind == MC_SYM_VEC_ALIAS) return 0;
            }

            sym = mc_find_symbol(ctx->const_symbols, ctx->const_symbol_count, name);
            if (sym && sym->kind == MC_SYM_CONST) {
                return mc_emit_mov_acc_imm(ctx, (int64_t)sym->const_value);
            }

            reg_id = mc_reg_id_from_name(name);
            if (reg_id >= 0) {
                return mc_emit_mov_acc_from_reg_id(ctx, reg_id);
            }
            if (mc_parse_reg_pseudo(name, reg_name, sizeof(reg_name)) == 0) {
                reg_id = mc_reg_id_from_name(reg_name);
                if (reg_id >= 0) return mc_emit_mov_acc_from_reg_id(ctx, reg_id);
            }

            return mc_emit_mov_acc_imm(ctx, 0);
        }
        case AST_TYPECAST:
            return mc_emit_expr(ctx, expr->data.typecast.expr);
        case AST_UNARY_OP:
            op = expr->data.unary_op.op;
            if (mc_emit_expr(ctx, expr->data.unary_op.operand) != 0) return -1;
            if (strcmp(op, "*") == 0) {
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                return (mc_emit_u8(&ctx->code, 0x8B) || mc_emit_u8(&ctx->code, 0x00)) ? -1 : 0;
            } else if (strcmp(op, "-") == 0) {
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                return (mc_emit_u8(&ctx->code, 0xF7) || mc_emit_u8(&ctx->code, 0xD8)) ? -1 : 0;
            } else if (strcmp(op, "!") == 0) {
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0x85) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x94) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1;
                return (mc_emit_u8(&ctx->code, 0x0F) || mc_emit_u8(&ctx->code, 0xB6) || mc_emit_u8(&ctx->code, 0xC0)) ? -1 : 0;
            } else if (strcmp(op, "~") == 0) {
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                return (mc_emit_u8(&ctx->code, 0xF7) || mc_emit_u8(&ctx->code, 0xD0)) ? -1 : 0;
            }
            return 0;
        case AST_BINARY_OP:
            op = expr->data.binary_op.op;
            if (mc_emit_expr(ctx, expr->data.binary_op.left) != 0) return -1;
            if (mc_emit_u8(&ctx->code, 0x50) != 0) return -1;
            if (mc_emit_expr(ctx, expr->data.binary_op.right) != 0) return -1;
            if (mc_emit_u8(&ctx->code, 0x59) != 0) return -1; /* pop cx/ecx/rcx */

            if (strcmp(op, "+") == 0) {
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0x01) != 0 || mc_emit_u8(&ctx->code, 0xC1) != 0) return -1;
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                return (mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xC8)) ? -1 : 0;
            } else if (strcmp(op, "-") == 0) {
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0x29) != 0 || mc_emit_u8(&ctx->code, 0xC1) != 0) return -1;
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                return (mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xC8)) ? -1 : 0;
            } else if (strcmp(op, "*") == 0) {
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0xAF) != 0 || mc_emit_u8(&ctx->code, 0xC8) != 0) return -1;
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                return (mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xC8)) ? -1 : 0;
            } else if (strcmp(op, "/") == 0 || strcmp(op, "÷") == 0) {
                if (mc_is_x64(ctx)) {
                    if (mc_emit_u8(&ctx->code, 0x49) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC2) != 0) return -1; /* mov r10, rax */
                    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC8) != 0) return -1; /* mov rax, rcx */
                    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x99) != 0) return -1; /* cqo */
                    return (mc_emit_u8(&ctx->code, 0x49) || mc_emit_u8(&ctx->code, 0xF7) || mc_emit_u8(&ctx->code, 0xFA)) ? -1 : 0; /* idiv r10 */
                }
                if (ctx->machine_bits == 32) {
                    if (mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC3) != 0) return -1; /* mov ebx, eax */
                    if (mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC8) != 0) return -1; /* mov eax, ecx */
                    if (mc_emit_u8(&ctx->code, 0x99) != 0) return -1; /* cdq */
                    return (mc_emit_u8(&ctx->code, 0xF7) || mc_emit_u8(&ctx->code, 0xFB)) ? -1 : 0; /* idiv ebx */
                }
                if (mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC3) != 0) return -1; /* mov bx, ax */
                if (mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC8) != 0) return -1; /* mov ax, cx */
                if (mc_emit_u8(&ctx->code, 0x99) != 0) return -1; /* cwd */
                return (mc_emit_u8(&ctx->code, 0xF7) || mc_emit_u8(&ctx->code, 0xFB)) ? -1 : 0; /* idiv bx */
            } else if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0x39) != 0 || mc_emit_u8(&ctx->code, 0xC1) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0x0F) != 0) return -1;
                if (mc_emit_u8(&ctx->code, (strcmp(op, "==") == 0) ? 0x94 : 0x95) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0xC0) != 0) return -1;
                return (mc_emit_u8(&ctx->code, 0x0F) || mc_emit_u8(&ctx->code, 0xB6) || mc_emit_u8(&ctx->code, 0xC0)) ? -1 : 0;
            } else if (strcmp(op, "&") == 0) {
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0x21) != 0 || mc_emit_u8(&ctx->code, 0xC1) != 0) return -1;
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                return (mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xC8)) ? -1 : 0;
            } else if (strcmp(op, "|") == 0) {
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0x09) != 0 || mc_emit_u8(&ctx->code, 0xC1) != 0) return -1;
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                return (mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xC8)) ? -1 : 0;
            } else if (strcmp(op, "^") == 0) {
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0x31) != 0 || mc_emit_u8(&ctx->code, 0xC1) != 0) return -1;
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                return (mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xC8)) ? -1 : 0;
            } else if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) {
                if (mc_emit_u8(&ctx->code, 0x88) != 0 || mc_emit_u8(&ctx->code, 0xC1) != 0) return -1; /* mov cl, al */
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                if (strcmp(op, "<<") == 0) {
                    if (mc_emit_u8(&ctx->code, 0xD3) != 0 || mc_emit_u8(&ctx->code, 0xE1) != 0) return -1;
                } else {
                    if (mc_emit_u8(&ctx->code, 0xD3) != 0 || mc_emit_u8(&ctx->code, 0xF9) != 0) return -1;
                }
                if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                return (mc_emit_u8(&ctx->code, 0x89) || mc_emit_u8(&ctx->code, 0xC8)) ? -1 : 0;
            }
            return 0;
        case AST_CALL: {
            if (expr->data.call.func && expr->data.call.func->type == AST_ACCESS &&
                expr->data.call.func->data.access.member &&
                expr->data.call.func->data.access.object) {
                const char *member = expr->data.call.func->data.access.member;
                const char *obj_name = NULL;
                char obj_name_buf[256];
                McSymbol *port_sym = NULL;
                if (mc_build_access_path(expr->data.call.func->data.access.object, obj_name_buf, sizeof(obj_name_buf)) == 0) {
                    obj_name = obj_name_buf;
                } else if (expr->data.call.func->data.access.object->type == AST_IDENT) {
                    obj_name = expr->data.call.func->data.access.object->data.ident.name;
                }
                port_sym = obj_name ? mc_find_symbol(ctx->const_symbols, ctx->const_symbol_count, obj_name) : NULL;
                if (obj_name && member &&
                    strcmp(obj_name, "hardware/state") == 0 &&
                    strstr(member, "snapshot") != NULL &&
                    expr->data.call.arg_count >= 1) {
                    return mc_emit_hw_state_snapshot(ctx, expr->data.call.args[0]);
                }
                if (member && strstr(member, "read") != NULL && port_sym && port_sym->kind == MC_SYM_CONST) {
                    uint64_t port = port_sym->const_value;
                    if (port <= 0xFF) {
                        if (mc_emit_u8(&ctx->code, 0xE4) != 0) return -1; /* in al, imm8 */
                        if (mc_emit_u8(&ctx->code, (uint8_t)port) != 0) return -1;
                    } else {
                        if (mc_emit_u8(&ctx->code, 0x66) != 0) return -1; /* mov dx, imm16 */
                        if (mc_emit_u8(&ctx->code, 0xBA) != 0) return -1;
                        if (mc_emit_u16(&ctx->code, (uint16_t)port) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, 0xEC) != 0) return -1; /* in al, dx */
                    }
                    if (mc_emit_u8(&ctx->code, 0x0F) != 0) return -1; /* movzx eax, al */
                    if (mc_emit_u8(&ctx->code, 0xB6) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0xC0) != 0) return -1;
                    return 0;
                }

                if (member && strncmp(member, "vector/read", 11) == 0 && expr->data.call.arg_count == 1) {
                    McSymbol *obj_sym = mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, obj_name);
                    if (obj_sym && mc_symbol_pointer_base_to_rax(ctx, obj_sym) == 0) {
                        if (mc_emit_u8(&ctx->code, 0x50) != 0) return -1;
                        if (mc_emit_expr(ctx, expr->data.call.args[0]) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, 0x59) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x01) != 0 || mc_emit_u8(&ctx->code, 0xC8) != 0) return -1; /* add rax, rcx */
                        return 0;
                    }
                }

                if (member && strncmp(member, "vector/write", 12) == 0 && expr->data.call.arg_count == 2) {
                    McSymbol *obj_sym = mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, obj_name);
                    ASTNode *val = expr->data.call.args[0];
                    ASTNode *off = expr->data.call.args[1];
                    int vec_reg_id = -1;
                    if (!obj_sym) return -1;
                    if (val && val->type == AST_IDENT) {
                        McSymbol *vsym = mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, val->data.ident.name);
                        if (vsym && vsym->kind == MC_SYM_VEC_ALIAS) vec_reg_id = vsym->vec_reg_id;
                    }
                    if (vec_reg_id < 0 || vec_reg_id > 7) return -1;
                    if (mc_symbol_pointer_base_to_rax(ctx, obj_sym) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0x50) != 0) return -1;
                    if (mc_emit_expr(ctx, off) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0x59) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x01) != 0 || mc_emit_u8(&ctx->code, 0xC8) != 0) return -1;
                    return mc_emit_vmovdqu_ymm_to_rax(ctx, vec_reg_id);
                }
            }

            if (expr->data.call.func &&
                (expr->data.call.func->type == AST_IDENT || expr->data.call.func->type == AST_ACCESS) &&
                expr->data.call.arg_count >= 2 &&
                (expr->data.call.arg_count % 2) == 0) {
                const char *type_name = NULL;
                char full_type_name[256];
                if (expr->data.call.func->type == AST_IDENT) {
                    type_name = expr->data.call.func->data.ident.name;
                } else if (expr->data.call.func->type == AST_ACCESS &&
                           expr->data.call.func->data.access.object &&
                           expr->data.call.func->data.access.object->type == AST_IDENT &&
                           expr->data.call.func->data.access.member) {
                    snprintf(full_type_name,
                             sizeof(full_type_name),
                             "%s/%s",
                             expr->data.call.func->data.access.object->data.ident.name,
                             expr->data.call.func->data.access.member);
                    type_name = full_type_name;
                }
                if (type_name && strcmp(type_name, "CPUID/Features") == 0) {
                    int k;
                    for (k = 0; k + 1 < expr->data.call.arg_count; k += 2) {
                        ASTNode *fname = expr->data.call.args[k];
                        ASTNode *fval = expr->data.call.args[k + 1];
                        if (!fname || fname->type != AST_IDENT || !fval) continue;
                        if (mc_emit_expr(ctx, fval) != 0) return -1;
                        if (strcmp(fname->data.ident.name, "eax") == 0) {
                            if (mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1; /* mov eax, eax */
                        } else if (strcmp(fname->data.ident.name, "ebx") == 0) {
                            if (mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC3) != 0) return -1; /* mov ebx, eax */
                        } else if (strcmp(fname->data.ident.name, "ecx") == 0) {
                            if (mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC1) != 0) return -1; /* mov ecx, eax */
                        } else if (strcmp(fname->data.ident.name, "edx") == 0) {
                            if (mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC2) != 0) return -1; /* mov edx, eax */
                        }
                    }
                    return 0;
                }
            }

            /* cast<Type>(expr) / ptr<Type>(expr) are parser-level cast forms, not real calls. */
            if (expr->data.call.func &&
                expr->data.call.func->type == AST_IDENT &&
                expr->data.call.func->data.ident.name) {
                const char *callee_name = expr->data.call.func->data.ident.name;
                if (strcmp(callee_name, "cast") == 0 ||
                    strcmp(callee_name, "ptr") == 0 ||
                    strncmp(callee_name, "cast<", 5) == 0 ||
                    strncmp(callee_name, "ptr<", 4) == 0) {
                    if (expr->data.call.arg_count >= 1 && expr->data.call.args && expr->data.call.args[0]) {
                        return mc_emit_expr(ctx, expr->data.call.args[0]);
                    }
                    return mc_emit_mov_acc_imm(ctx, 0);
                }
            }

            /* 内置 print(): Python 风格参数解析 + 按函数上下文分流为 UEFI/syscall/raw 机器码 */
            if (expr->data.call.func && expr->data.call.func->type == AST_IDENT &&
                strcmp(expr->data.call.func->data.ident.name, "print") == 0) {
                McPrintArgs pargs;
                McPrintMode mode = mc_select_print_mode(ctx);
                int p;

                if (mc_collect_print_args(expr, &pargs) != 0) return -1;

                for (p = 0; p < pargs.positional_count; p++) {
                    if (mc_emit_print_arg(ctx, mode, pargs.positional[p]) != 0) return -1;
                    if (p + 1 < pargs.positional_count) {
                        if (pargs.sep_expr) {
                            if (mc_emit_print_arg(ctx, mode, pargs.sep_expr) != 0) return -1;
                        } else {
                            if (mc_emit_print_text(ctx, mode, " ") != 0) return -1;
                        }
                    }
                }
                if (pargs.end_expr) {
                    if (mc_emit_print_arg(ctx, mode, pargs.end_expr) != 0) return -1;
                } else {
                    if (mc_emit_print_text(ctx, mode, "\n") != 0) return -1;
                }
                return 0;
            }
            
            /* 普通函数调用 */
            max_reg_args = (ctx->machine_bits == 64) ? 6 : 0;
            if (ctx->machine_bits == 64) {
                for (i = 0; i < expr->data.call.arg_count && i < max_reg_args; i++) {
                    if (mc_emit_expr(ctx, expr->data.call.args[i]) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0x50) != 0) return -1;
                }
                for (i = (expr->data.call.arg_count < max_reg_args ? expr->data.call.arg_count : max_reg_args) - 1; i >= 0; i--) {
                    if (mc_emit_pop_argreg(ctx, i) != 0) return -1;
                }
            } else {
                for (i = expr->data.call.arg_count - 1; i >= 0; i--) {
                    if (mc_emit_expr(ctx, expr->data.call.args[i]) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0x50) != 0) return -1;
                }
            }
            if (expr->data.call.func && expr->data.call.func->type == AST_IDENT) {
                if (mc_emit_call_target(ctx, expr->data.call.func->data.ident.name) != 0) return -1;
                if (ctx->machine_bits != 64 && expr->data.call.arg_count > 0) {
                    int arg_bytes = expr->data.call.arg_count * mc_word_bytes(ctx);
                    if (ctx->machine_bits == 16) {
                        if (mc_emit_u8(&ctx->code, 0x81) != 0 || mc_emit_u8(&ctx->code, 0xC4) != 0) return -1;
                        if (mc_emit_u16(&ctx->code, (uint16_t)arg_bytes) != 0) return -1;
                    } else {
                        if (mc_emit_u8(&ctx->code, 0x81) != 0 || mc_emit_u8(&ctx->code, 0xC4) != 0) return -1;
                        if (mc_emit_u32(&ctx->code, (uint32_t)arg_bytes) != 0) return -1;
                    }
                }
                return 0;
            }
            return mc_emit_mov_acc_imm(ctx, 0);
        case AST_HW_ISA_CALL: {
            /* 硬件层 ISA 直通调用 - 作为表达式被处理 */
            if (!ctx->in_hardware_block) {
                return -1;  /* ISA 调用仅在硬件块中允许 */
            }

            if (expr->data.hw_isa_call.isa_operation &&
                mc_emit_hw_isa_param_call(ctx,
                                          expr->data.hw_isa_call.isa_operation,
                                          expr->data.hw_isa_call.operands,
                                          expr->data.hw_isa_call.operand_count) == 0) {
                return 0;
            }

            /* lgdt/lidt 需要内存操作数。约定：先将操作数地址计算到 RAX，再发射 [RAX] 形式。 */
            if (expr->data.hw_isa_call.isa_operation &&
                strcmp(expr->data.hw_isa_call.isa_operation, "goto") == 0) {
                if (expr->data.hw_isa_call.operand_count < 1 ||
                    !expr->data.hw_isa_call.operands ||
                    !expr->data.hw_isa_call.operands[0]) {
                    return -1;
                }
                if (mc_emit_expr(ctx, expr->data.hw_isa_call.operands[0]) != 0) return -1;
                if (ctx->pending_cs_valid) {
                    if (mc_emit_u8(&ctx->code, 0xB9) != 0) return -1; /* mov ecx, imm32 */
                    if (mc_emit_u32(&ctx->code, (uint32_t)ctx->pending_cs_selector) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0x51) != 0) return -1; /* push rcx (selector) */
                    if (mc_emit_u8(&ctx->code, 0x50) != 0) return -1; /* push rax (target RIP) */
                    if (mc_emit_u8(&ctx->code, 0xCB) != 0) return -1; /* retf */
                    ctx->pending_cs_valid = 0;
                } else {
                    if (mc_emit_u8(&ctx->code, 0xFF) != 0) return -1; /* jmp rax */
                    if (mc_emit_u8(&ctx->code, 0xE0) != 0) return -1;
                }
                return 0;
            }

            /* lgdt/lidt 需要内存操作数。约定：先将操作数地址计算到 RAX，再发射 [RAX] 形式。 */
            if (expr->data.hw_isa_call.isa_operation &&
                (strcmp(expr->data.hw_isa_call.isa_operation, "lgdt") == 0 ||
                 strcmp(expr->data.hw_isa_call.isa_operation, "lidt") == 0)) {
                if (expr->data.hw_isa_call.operand_count < 1 ||
                    !expr->data.hw_isa_call.operands ||
                    !expr->data.hw_isa_call.operands[0]) {
                    return -1;
                }
                if (mc_emit_expr(ctx, expr->data.hw_isa_call.operands[0]) != 0) {
                    return -1;
                }
                if (mc_emit_u8(&ctx->code, 0x0F) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0x01) != 0) return -1;
                if (mc_emit_u8(&ctx->code,
                               strcmp(expr->data.hw_isa_call.isa_operation, "lgdt") == 0 ? 0x10 : 0x18) != 0) {
                    return -1;
                }
                return 0;
            }
            
            /* 生成操作码（如果尚未生成） */
            if (expr->data.hw_isa_call.opcode_length == 0 && expr->data.hw_isa_call.isa_operation) {
                uint8_t temp_opcodes[16];
                int isa_opcode_len = hw_generate_isa_opcode(expr->data.hw_isa_call.isa_operation, temp_opcodes);
                
                if (isa_opcode_len > 0 && isa_opcode_len <= 16) {
                    expr->data.hw_isa_call.opcode_length = isa_opcode_len;
                    for (int i = 0; i < isa_opcode_len; i++) {
                        expr->data.hw_isa_call.opcode_bytes[i] = temp_opcodes[i];
                    }
                } else if (isa_opcode_len < 0) {
                    return -1;  /* 未识别的 ISA 操作 */
                }
            }
            
            /* 输出操作码字节 */
            if (expr->data.hw_isa_call.opcode_length > 0 && 
                expr->data.hw_isa_call.opcode_length <= 16) {
                
                for (int i = 0; i < expr->data.hw_isa_call.opcode_length; i++) {
                    uint8_t byte = expr->data.hw_isa_call.opcode_bytes[i];
                    if (mc_emit_u8(&ctx->code, byte) != 0) {
                        return -1;
                    }
                }
            } else {
                return -1;  /* 无有效的操作码 */
            }
            
            return 0;
        }
        case AST_ACCESS: {
            /* 硬件路径访问：CPU/Current\Control\CR0 级控制寄存器访问
               或其他硬件路径如 CPU/Flags\Value 等*/
            if (expr->data.access.member &&
                strcmp(expr->data.access.member, "[index]") == 0 &&
                expr->data.access.index_expr) {
                uint32_t elem_size = mc_access_index_elem_size(ctx, expr);
                if (mc_emit_indexed_address(ctx, expr) != 0) {
                    /* Unresolved indexed source (typically non-addressable global const);
                       preserve compilation by materializing zero. */
                    return mc_emit_mov_acc_imm(ctx, 0);
                }
                return mc_emit_load_acc_from_rax_disp(ctx, 0, elem_size);
            }
            
            if (ctx->in_hardware_block && expr->data.access.member) {
                const char *member_path = expr->data.access.member;
                char full_path[256];
                int has_full_path = (mc_build_access_path(expr, full_path, sizeof(full_path)) == 0);
                uint8_t temp_opcodes[16];
                int opcode_len = 0;
                
                /* 识别控制寄存器访问：/Current\Control\CRx 模式 */
                if ((strstr(member_path, "Control") != NULL && strstr(member_path, "CR") != NULL) ||
                    (has_full_path && strstr(full_path, "CPU/Current/Control/CR") != NULL) ||
                    strstr(member_path, "/Current") != NULL) {
                    
                    /* 提取寄存器名 (CR0, CR2, CR3, CR4, CR8) */
                    const char *cr_pos = strstr(member_path, "CR");
                    if (cr_pos && (cr_pos[2] == '0' || cr_pos[2] == '2' || cr_pos[2] == '3' || 
                                   cr_pos[2] == '4' || cr_pos[2] == '8')) {
                        char cr_name[10];
                        snprintf(cr_name, sizeof(cr_name), "CR%c", cr_pos[2]);  /* CR0, CR2 etc */
                        
                        /* 生成 MOV RAX, CRx 指令 */
                        opcode_len = hw_generate_control_reg_read(cr_name, temp_opcodes);
                        
                        if (opcode_len > 0 && opcode_len <= 16) {
                            for (int i = 0; i < opcode_len; i++) {
                                if (mc_emit_u8(&ctx->code, temp_opcodes[i]) != 0) {
                                    return -1;
                                }
                            }
                            return 0;
                        }
                    }
                }
                /* 识别RFLAGS值访问：\Value 或 \Interrupt/Enable 模式 */
                else if (strstr(member_path, "\\Value") != NULL ||
                         (has_full_path && strstr(full_path, "CPU/Flags/Value") != NULL) ||
                         strstr(member_path, "Flags") != NULL ||
                         (has_full_path && strstr(full_path, "CPU/Flags/Interrupt/Enable") != NULL)) {
                    
                    if (strstr(member_path, "\\Value") != NULL ||
                        (has_full_path && strstr(full_path, "CPU/Flags/Value") != NULL)) {
                        /* 读取所有RFLAGS：PUSHFQ; POP RAX */
                        opcode_len = hw_generate_flag_read(temp_opcodes);
                        
                        if (opcode_len > 0 && opcode_len <= 16) {
                            for (int i = 0; i < opcode_len; i++) {
                                if (mc_emit_u8(&ctx->code, temp_opcodes[i]) != 0) {
                                    return -1;
                                }
                            }
                            return 0;
                        }
                    } else if (strstr(member_path, "Interrupt") != NULL ||
                               (has_full_path && strstr(full_path, "CPU/Flags/Interrupt/Enable") != NULL)) {
                        /* 中断标志读取 - 需要读RFLAGS并提取IF位 */
                        opcode_len = hw_generate_flag_read(temp_opcodes);
                        
                        if (opcode_len > 0 && opcode_len <= 16) {
                            for (int i = 0; i < opcode_len; i++) {
                                if (mc_emit_u8(&ctx->code, temp_opcodes[i]) != 0) {
                                    return -1;
                                }
                            }
                            /* 提取IF位 (位9)：SHR RAX, 9; AND RAX, 1 */
                            if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                            if (mc_emit_u8(&ctx->code, 0xC1) != 0) return -1;
                            if (mc_emit_u8(&ctx->code, 0xE8) != 0) return -1;  /* shr rax */
                            if (mc_emit_u8(&ctx->code, 9) != 0) return -1;      /* imm8 = 9 */
                            
                            if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                            if (mc_emit_u8(&ctx->code, 0x83) != 0) return -1;
                            if (mc_emit_u8(&ctx->code, 0xE0) != 0) return -1;  /* and rax */
                            if (mc_emit_u8(&ctx->code, 1) != 0) return -1;      /* imm8 = 1 */
                            
                            return 0;
                        }
                    }
                }
                /* 识别MSR访问：/MSR\ 模式 */
                else if (strstr(member_path, "/MSR\\") != NULL) {
                    /* MSR访问需要先设置ECX为MSR索引，然后RDMSR */
                    /* 这通常在赋值或单独的语句中处理，这里只是读取 */
                    opcode_len = hw_generate_rdmsr(temp_opcodes);
                    
                    if (opcode_len > 0 && opcode_len <= 16) {
                        for (int i = 0; i < opcode_len; i++) {
                            if (mc_emit_u8(&ctx->code, temp_opcodes[i]) != 0) {
                                return -1;
                            }
                        }
                        return 0;
                    }
                }
            }
            
            /* 通用 ptr/view 字段访问：obj\field */
            {
                McSymbol *obj_sym = NULL;
                McStructField *field = NULL;
                if (mc_resolve_access_layout(ctx, expr, &obj_sym, &field) == 0) {
                    if (mc_symbol_type_is_ptr_like(obj_sym)) {
                        if (mc_symbol_pointer_base_to_rax(ctx, obj_sym) != 0) return -1;
                    } else {
                        if (mc_symbol_base_addr_to_rax(ctx, obj_sym) != 0) return -1;
                    }
                    return mc_emit_load_acc_from_rax_disp(ctx, (int32_t)field->offset, field->size);
                }
            }
            return mc_emit_mov_acc_imm(ctx, 0);
        }
        }
        default:
            return mc_emit_mov_acc_imm(ctx, 0);
    }
}

static int mc_emit_stmt(McCtx *ctx, ASTNode *stmt) {
    int l_else, l_end, l_loop, l_break;
    int i;
    if (!ctx || !stmt) return 0;
    switch (stmt->type) {
        case AST_RETURN_STMT:
            if (stmt->data.return_stmt.value) {
                if (mc_emit_expr(ctx, stmt->data.return_stmt.value) != 0) return -1;
            } else {
                if (mc_emit_mov_acc_imm(ctx, 0) != 0) return -1;
            }
            ctx->has_explicit_return = 1;
            return mc_emit_epilogue(ctx);
        case AST_EXPR_STMT:
            return mc_emit_expr(ctx, stmt->data.assignment.left);
        case AST_VAR_DECL: {
            const char *name = stmt->data.var_decl.name;
            const char *type_name = (stmt->data.var_decl.type && stmt->data.var_decl.type->data.type.name)
                                        ? stmt->data.var_decl.type->data.type.name
                                        : NULL;
            int is_reg_alias = 0;
            int reg_id = -1;
            int vec_reg_id = -1;
            McSymbol sym;
            char reg_name[64];
            const char *inferred_type = type_name;

            if (!name) return 0;
            if (!inferred_type && stmt->data.var_decl.init &&
                stmt->data.var_decl.init->type == AST_TYPECAST &&
                stmt->data.var_decl.init->data.typecast.target_type &&
                stmt->data.var_decl.init->data.typecast.target_type->type == AST_TYPE) {
                inferred_type = stmt->data.var_decl.init->data.typecast.target_type->data.type.name;
            }
            memset(&sym, 0, sizeof(sym));
            sym.name = (char *)name;
            sym.type_name = (char *)inferred_type;

            if (inferred_type && strncmp(inferred_type, "reg<", 4) == 0) {
                const char *lt = inferred_type + 4;
                const char *comma = strchr(lt, ',');
                size_t len = comma ? (size_t)(comma - lt) : 0;
                if (len > 0 && len < sizeof(reg_name)) {
                    memcpy(reg_name, lt, len);
                    reg_name[len] = '\0';
                    reg_id = mc_reg_id_from_name(reg_name);
                    if (reg_id >= 0) {
                        is_reg_alias = 1;
                    } else {
                        vec_reg_id = mc_vec_reg_id_from_name(reg_name);
                        if (vec_reg_id >= 0) is_reg_alias = 1;
                    }
                }
            }

            if (is_reg_alias) {
                if (reg_id >= 0) {
                    sym.kind = MC_SYM_REG_ALIAS;
                    sym.reg_id = reg_id;
                } else {
                    sym.kind = MC_SYM_VEC_ALIAS;
                    sym.vec_reg_id = vec_reg_id;
                }
                if (!mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, name)) {
                    if (mc_add_local_symbol(ctx, &sym) != 0) return -1;
                }
                if (stmt->data.var_decl.init) {
                    if (mc_emit_expr(ctx, stmt->data.var_decl.init) != 0) return -1;
                    if (reg_id >= 0 && mc_emit_mov_reg_id_from_acc(ctx, reg_id) != 0) return -1;
                }
                return 0;
            }

            /* 普通局部变量分配在栈上 */
            if (!mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, name)) {
                ctx->stack_allocated += 8;
                sym.kind = MC_SYM_STACK;
                sym.stack_offset = -ctx->stack_allocated;
                if (mc_add_local_symbol(ctx, &sym) != 0) return -1;
            }

            if (stmt->data.var_decl.init) {
                McSymbol *existing = mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, name);
                if (!existing || existing->kind != MC_SYM_STACK) return -1;
                if (mc_emit_expr(ctx, stmt->data.var_decl.init) != 0) return -1;
                if (mc_emit_store_acc_to_stack(ctx, existing->stack_offset) != 0) return -1;
            }
            return 0;
        }
        case AST_ASSIGNMENT: {
            int right_emitted = 0;
            if (stmt->data.assignment.left && stmt->data.assignment.left->type == AST_IDENT) {
                const char *left_name = stmt->data.assignment.left->data.ident.name;
                McSymbol *left_sym = left_name ? mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, left_name) : NULL;
                if (left_sym && left_sym->kind == MC_SYM_VEC_ALIAS &&
                    stmt->data.assignment.right &&
                    stmt->data.assignment.right->type == AST_CALL &&
                    stmt->data.assignment.right->data.call.func &&
                    stmt->data.assignment.right->data.call.func->type == AST_ACCESS &&
                    stmt->data.assignment.right->data.call.func->data.access.member &&
                    strncmp(stmt->data.assignment.right->data.call.func->data.access.member, "vector/read", 11) == 0 &&
                    stmt->data.assignment.right->data.call.func->data.access.object &&
                    stmt->data.assignment.right->data.call.func->data.access.object->type == AST_IDENT &&
                    stmt->data.assignment.right->data.call.arg_count == 1) {
                    const char *obj_name = stmt->data.assignment.right->data.call.func->data.access.object->data.ident.name;
                    McSymbol *obj_sym = mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, obj_name);
                    if (!obj_sym) return -1;
                    if (mc_symbol_pointer_base_to_rax(ctx, obj_sym) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0x50) != 0) return -1;
                    if (mc_emit_expr(ctx, stmt->data.assignment.right->data.call.args[0]) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0x59) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x01) != 0 || mc_emit_u8(&ctx->code, 0xC8) != 0) return -1;
                    return mc_emit_vmovdqu_ymm_from_rax(ctx, left_sym->vec_reg_id);
                }
            }

            /* 检查硬件块中的硬件路径写操作（控制寄存器、标志位等） */
            if (ctx->in_hardware_block && stmt->data.assignment.left && 
                stmt->data.assignment.left->type == AST_ACCESS) {
                
                const char *member_path = stmt->data.assignment.left->data.access.member;
                ASTNode *right = stmt->data.assignment.right;
                char full_path[256];
                int has_full_path = (mc_build_access_path(stmt->data.assignment.left, full_path, sizeof(full_path)) == 0);
                
                uint8_t temp_opcodes[16];
                int opcode_len = 0;
                
                /* 先生成右侧表达式，结果会在RAX中 */
                if (right && mc_emit_expr(ctx, right) != 0) {
                    return -1;
                }
                right_emitted = 1;
                
                /* 识别控制寄存器写操作：/Current\Control\CRx = value => MOV CRx, RAX */
                if ((strstr(member_path, "Control") != NULL && strstr(member_path, "CR") != NULL) ||
                    (has_full_path && strstr(full_path, "CPU/Current/Control/CR") != NULL) ||
                    strstr(member_path, "/Current") != NULL) {
                    
                    /* 提取寄存器名 - 查找任何CR后跟数字的模式 */
                    const char *cr_pos = strstr(member_path, "CR");
                    if (cr_pos && (cr_pos[2] == '0' || cr_pos[2] == '2' || cr_pos[2] == '3' || 
                                   cr_pos[2] == '4' || cr_pos[2] == '8')) {
                        char cr_name[10];
                        snprintf(cr_name, sizeof(cr_name), "CR%c", cr_pos[2]);  /* CR0, CR2 etc */
                        
                        /* 生成 MOV CRx, RAX 指令 */
                        opcode_len = hw_generate_control_reg_write(cr_name, temp_opcodes);
                        
                        if (opcode_len > 0 && opcode_len <= 16) {
                            for (int i = 0; i < opcode_len; i++) {
                                if (mc_emit_u8(&ctx->code, temp_opcodes[i]) != 0) {
                                    return -1;
                                }
                            }
                            return 0;
                        }
                    }
                }
                /* 识别RFLAGS写操作：\Value = value => PUSH RAX; POPFQ */
                else if (strstr(member_path, "\\Value") != NULL ||
                         (has_full_path && strstr(full_path, "CPU/Flags/Value") != NULL)) {
                    
                    /* 生成 PUSH RAX; POPFQ */
                    opcode_len = hw_generate_flag_write(temp_opcodes);
                    
                    if (opcode_len > 0 && opcode_len <= 16) {
                        for (int i = 0; i < opcode_len; i++) {
                            if (mc_emit_u8(&ctx->code, temp_opcodes[i]) != 0) {
                                return -1;
                            }
                        }
                        return 0;
                    }
                }
                /* 识别中断标志写操作：\Interrupt/Enable = true/false => STI/CLI */
                else if ((strstr(member_path, "Flags") != NULL &&
                          strstr(member_path, "Interrupt") != NULL) ||
                         (has_full_path && strstr(full_path, "CPU/Flags/Interrupt/Enable") != NULL)) {
                    
                    /* 检查右侧是否是true/false字面量 */
                    if (right && right->type == AST_LITERAL && !right->data.literal.is_float) {
                        int64_t value = right->data.literal.value.int_value;
                        
                        if (value != 0) {
                            /* STI - 启用中断 */
                            opcode_len = hw_generate_sti(temp_opcodes);
                        } else {
                            /* CLI - 禁用中断 */
                            opcode_len = hw_generate_cli(temp_opcodes);
                        }
                        
                        if (opcode_len > 0 && opcode_len <= 16) {
                            for (int i = 0; i < opcode_len; i++) {
                                if (mc_emit_u8(&ctx->code, temp_opcodes[i]) != 0) {
                                    return -1;
                                }
                            }
                            return 0;
                        }
                    }
                }
                /* 识别MSR写操作：/MSR\ = value => 设置ECX为MSR索引，RAX有值，然后WRMSR */
                else if ((has_full_path && strstr(full_path, "CPU/MSR/") != NULL) ||
                         strstr(member_path, "/MSR\\") != NULL) {
                    if (has_full_path && strstr(full_path, "CPU/MSR/LSTAR") != NULL) {
                        if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC2) != 0) return -1; /* mov rdx, rax */
                        if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0xC1) != 0 || mc_emit_u8(&ctx->code, 0xEA) != 0 || mc_emit_u8(&ctx->code, 32) != 0) return -1; /* shr rdx, 32 */
                        if (mc_emit_u8(&ctx->code, 0xB9) != 0 || mc_emit_u32(&ctx->code, 0xC0000082u) != 0) return -1; /* mov ecx, LSTAR */
                        if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x30) != 0) return -1; /* wrmsr */
                        return 0;
                    }
                    if (has_full_path && strstr(full_path, "CPU/MSR/STAR") != NULL) {
                        if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC2) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0xC1) != 0 || mc_emit_u8(&ctx->code, 0xEA) != 0 || mc_emit_u8(&ctx->code, 32) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, 0xB9) != 0 || mc_emit_u32(&ctx->code, 0xC0000081u) != 0) return -1; /* mov ecx, STAR */
                        if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x30) != 0) return -1;
                        return 0;
                    }
                    if (has_full_path && strstr(full_path, "CPU/MSR/EFER/Syscall/Enable") != NULL) {
                        int enable = 1;
                        if (right && right->type == AST_LITERAL && !right->data.literal.is_float) {
                            enable = (right->data.literal.value.int_value != 0);
                        }
                        if (mc_emit_u8(&ctx->code, 0xB9) != 0 || mc_emit_u32(&ctx->code, 0xC0000080u) != 0) return -1; /* EFER */
                        if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x32) != 0) return -1; /* rdmsr */
                        if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0xBA) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, enable ? 0xE8 : 0xF0) != 0) return -1; /* bts/btr eax, imm8 */
                        if (mc_emit_u8(&ctx->code, 0x00) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, 0x0F) != 0 || mc_emit_u8(&ctx->code, 0x30) != 0) return -1; /* wrmsr */
                        return 0;
                    }
                    opcode_len = hw_generate_wrmsr(temp_opcodes);
                    if (opcode_len > 0 && opcode_len <= 16) {
                        for (int i = 0; i < opcode_len; i++) {
                            if (mc_emit_u8(&ctx->code, temp_opcodes[i]) != 0) return -1;
                        }
                        return 0;
                    }
                }
            }

            if (stmt->data.assignment.left && stmt->data.assignment.left->type == AST_ACCESS) {
                if (stmt->data.assignment.left->data.access.member &&
                    strcmp(stmt->data.assignment.left->data.access.member, "[index]") == 0 &&
                    stmt->data.assignment.left->data.access.index_expr) {
                    uint32_t elem_size = mc_access_index_elem_size(ctx, stmt->data.assignment.left);
                    if (stmt->data.assignment.right && mc_emit_expr(ctx, stmt->data.assignment.right) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0x50) != 0) return -1; /* value */
                    if (mc_emit_indexed_address(ctx, stmt->data.assignment.left) != 0) {
                        return 0;
                    } /* unresolved lvalue: ignore store to keep pipeline alive */
                    if (mc_emit_u8(&ctx->code, 0x59) != 0) return -1; /* value -> RCX */
                    return mc_emit_store_rcx_to_rax_disp(ctx, 0, elem_size);
                }

                McSymbol *obj_sym = NULL;
                McStructField *field = NULL;
                if (mc_resolve_access_layout(ctx, stmt->data.assignment.left, &obj_sym, &field) == 0) {
                    if (!right_emitted && stmt->data.assignment.right && mc_emit_expr(ctx, stmt->data.assignment.right) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0x50) != 0) return -1; /* save value in RAX */
                    if (mc_symbol_type_is_ptr_like(obj_sym)) {
                        if (mc_symbol_pointer_base_to_rax(ctx, obj_sym) != 0) return -1; /* load pointer base to RAX */
                    } else {
                        if (mc_symbol_base_addr_to_rax(ctx, obj_sym) != 0) return -1; /* load object address to RAX */
                    }
                    if (mc_emit_u8(&ctx->code, 0x59) != 0) return -1; /* restore value into RCX */
                    return mc_emit_store_rcx_to_rax_disp(ctx, (int32_t)field->offset, field->size);
                }
            }
            
            if (stmt->data.assignment.left && stmt->data.assignment.left->type == AST_IDENT) {
                const char *left_name = stmt->data.assignment.left->data.ident.name;
                McSymbol *sym;
                int reg_id;
                char reg_name[64];

                if (mc_emit_expr(ctx, stmt->data.assignment.right) != 0) return -1;

                if (left_name) {
                    if (mc_parse_reg_pseudo_any(left_name, reg_name, sizeof(reg_name)) == 0) {
                        if (strcmp(reg_name, "ss") == 0) {
                            if (mc_emit_u8(&ctx->code, 0x8E) != 0 || mc_emit_u8(&ctx->code, 0xD0) != 0) return -1; /* mov ss, ax */
                            return 0;
                        }
                        if (strcmp(reg_name, "cs") == 0) {
                            if (stmt->data.assignment.right &&
                                stmt->data.assignment.right->type == AST_LITERAL &&
                                !stmt->data.assignment.right->data.literal.is_float) {
                                ctx->pending_cs_selector = (uint16_t)(stmt->data.assignment.right->data.literal.value.int_value & 0xFFFFu);
                                ctx->pending_cs_valid = 1;
                            } else {
                                ctx->pending_cs_valid = 0;
                            }
                            return 0;
                        }
                    }

                    sym = mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, left_name);
                    if (sym) {
                        if (sym->kind == MC_SYM_STACK) return mc_emit_store_acc_to_stack(ctx, sym->stack_offset);
                        if (sym->kind == MC_SYM_REG_ALIAS) return mc_emit_mov_reg_id_from_acc(ctx, sym->reg_id);
                        if (sym->kind == MC_SYM_VEC_ALIAS) return 0;
                    }

                    reg_id = mc_reg_id_from_name(left_name);
                    if (reg_id >= 0) return mc_emit_mov_reg_id_from_acc(ctx, reg_id);

                    if (mc_parse_reg_pseudo(left_name, reg_name, sizeof(reg_name)) == 0) {
                        reg_id = mc_reg_id_from_name(reg_name);
                        if (reg_id >= 0) return mc_emit_mov_reg_id_from_acc(ctx, reg_id);
                    }
                }

                return 0;
            }

            /* 普通赋值处理 */
            return mc_emit_expr(ctx, stmt->data.assignment.right);
        }
        case AST_BLOCK:
            for (i = 0; i < stmt->data.block.stmt_count; i++) {
                if (mc_emit_stmt(ctx, stmt->data.block.statements[i]) != 0) return -1;
            }
            return 0;
        case AST_IF_STMT:
            l_else = mc_new_label(ctx);
            l_end = mc_new_label(ctx);
            if (l_else < 0 || l_end < 0) return -1;
            if (mc_emit_expr(ctx, stmt->data.if_stmt.condition) != 0) return -1;
            if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
            if (mc_emit_u8(&ctx->code, 0x85) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1;
            if (mc_emit_jz_label(ctx, l_else) != 0) return -1;
            if (mc_emit_stmt(ctx, stmt->data.if_stmt.then_branch) != 0) return -1;
            if (mc_emit_jmp_label(ctx, l_end) != 0) return -1;
            if (mc_bind_label(ctx, l_else) != 0) return -1;
            if (stmt->data.if_stmt.else_branch && mc_emit_stmt(ctx, stmt->data.if_stmt.else_branch) != 0) return -1;
            return mc_bind_label(ctx, l_end);
        case AST_WHILE_STMT:
            l_loop = mc_new_label(ctx);
            l_break = mc_new_label(ctx);
            if (l_loop < 0 || l_break < 0) return -1;
            if (mc_bind_label(ctx, l_loop) != 0) return -1;
            if (mc_emit_expr(ctx, stmt->data.while_stmt.condition) != 0) return -1;
            if (mc_is_x64(ctx) && mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
            if (mc_emit_u8(&ctx->code, 0x85) != 0 || mc_emit_u8(&ctx->code, 0xC0) != 0) return -1;
            if (mc_emit_jz_label(ctx, l_break) != 0) return -1;
            if (mc_emit_stmt(ctx, stmt->data.while_stmt.body) != 0) return -1;
            if (mc_emit_jmp_label(ctx, l_loop) != 0) return -1;
            return mc_bind_label(ctx, l_break);
        case AST_METAL_BLOCK:
            for (i = 0; i < stmt->data.metal_block.stmt_count; i++) {
                if (mc_emit_stmt(ctx, stmt->data.metal_block.statements[i]) != 0) return -1;
            }
            return 0;
        
        case AST_SILICON_BLOCK:
            /* 硅基语义块 - 直接生成x86-64机器码 */
            if (stmt->data.silicon_block.statements) {
                for (i = 0; i < stmt->data.silicon_block.stmt_count; i++) {
                    ASTNode *sil_stmt = stmt->data.silicon_block.statements[i];
                    if (!sil_stmt) continue;
                    
                    switch (sil_stmt->type) {
                        case AST_MICROARCH_CONFIG: {
                            /* MSR/EFER配置 -> rdmsr / bts / wrmsr */
                            for (int j = 0; j < sil_stmt->data.microarch_config.config_count; j++) {
                                const char *reg = sil_stmt->data.microarch_config.register_names[j];
                                const char *prop = sil_stmt->data.microarch_config.property_names[j];
                                
                                if (strcmp(reg, "MSR/EFER") == 0) {
                                    /* mov $0xc0000080, %ecx: b9 80 00 00 c0 */
                                    if (mc_emit_u8(&ctx->code, 0xb9) != 0) return -1;
                                    if (mc_emit_u8(&ctx->code, 0x80) != 0) return -1;
                                    if (mc_emit_u8(&ctx->code, 0x00) != 0) return -1;
                                    if (mc_emit_u8(&ctx->code, 0x00) != 0) return -1;
                                    if (mc_emit_u8(&ctx->code, 0xc0) != 0) return -1;
                                    
                                    /* rdmsr: 0f 32 */
                                    if (mc_emit_u8(&ctx->code, 0x0f) != 0) return -1;
                                    if (mc_emit_u8(&ctx->code, 0x32) != 0) return -1;
                                    
                                    /* bts $bit, %rax: 48 0f ba e8 bit */
                                    if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                                    if (mc_emit_u8(&ctx->code, 0x0f) != 0) return -1;
                                    if (mc_emit_u8(&ctx->code, 0xba) != 0) return -1;
                                    if (mc_emit_u8(&ctx->code, 0xe8) != 0) return -1;
                                    
                                    if (strcmp(prop, "Syscall/Enable") == 0) {
                                        if (mc_emit_u8(&ctx->code, 0) != 0) return -1;  /* 位0 */
                                    } else if (strcmp(prop, "Long/Mode") == 0) {
                                        if (mc_emit_u8(&ctx->code, 8) != 0) return -1;  /* 位8 */
                                    } else if (strcmp(prop, "Nx/Enable") == 0) {
                                        if (mc_emit_u8(&ctx->code, 11) != 0) return -1; /* 位11 */
                                    } else {
                                        if (mc_emit_u8(&ctx->code, 0) != 0) return -1;
                                    }
                                    
                                    /* wrmsr: 0f 30 */
                                    if (mc_emit_u8(&ctx->code, 0x0f) != 0) return -1;
                                    if (mc_emit_u8(&ctx->code, 0x30) != 0) return -1;
                                }
                            }
                            break;
                        }
                        
                        case AST_PIPELINE_BLOCK: {
                            uint32_t behavior = sil_stmt->data.pipeline_block.behavior_flags;
                            if (behavior & PIPELINE_SERIALIZE) {
                                /* mfence: 0f ae f0 */
                                if (mc_emit_u8(&ctx->code, 0x0f) != 0) return -1;
                                if (mc_emit_u8(&ctx->code, 0xae) != 0) return -1;
                                if (mc_emit_u8(&ctx->code, 0xf0) != 0) return -1;
                            }
                            break;
                        }
                        
                        case AST_PIPELINE_BARRIER: {
                            uint32_t bmode = sil_stmt->data.pipeline_barrier.barrier_mode;
                            if (bmode == PIPELINE_BARRIER_LOAD) {
                                /* lfence: 0f ae e8 */
                                if (mc_emit_u8(&ctx->code, 0x0f) != 0) return -1;
                                if (mc_emit_u8(&ctx->code, 0xae) != 0) return -1;
                                if (mc_emit_u8(&ctx->code, 0xe8) != 0) return -1;
                            } else if (bmode == PIPELINE_BARRIER_STORE) {
                                /* sfence: 0f ae f8 */
                                if (mc_emit_u8(&ctx->code, 0x0f) != 0) return -1;
                                if (mc_emit_u8(&ctx->code, 0xae) != 0) return -1;
                                if (mc_emit_u8(&ctx->code, 0xf8) != 0) return -1;
                            } else if (bmode == PIPELINE_BARRIER_FULL) {
                                /* mfence: 0f ae f0 */
                                if (mc_emit_u8(&ctx->code, 0x0f) != 0) return -1;
                                if (mc_emit_u8(&ctx->code, 0xae) != 0) return -1;
                                if (mc_emit_u8(&ctx->code, 0xf0) != 0) return -1;
                            }
                            break;
                        }
                        
                        case AST_CACHE_OPERATION: {
                            /* clflush [rax]: 0f ae 38 */
                            if (mc_emit_u8(&ctx->code, 0x0f) != 0) return -1;
                            if (mc_emit_u8(&ctx->code, 0xae) != 0) return -1;
                            if (mc_emit_u8(&ctx->code, 0x38) != 0) return -1;
                            break;
                        }
                        
                        case AST_PREFETCH_OPERATION: {
                            const char *hint = sil_stmt->data.prefetch_operation.hint_type;
                            /* prefetch指令格式: 0f 18 /0-3 [rax] */
                            if (mc_emit_u8(&ctx->code, 0x0f) != 0) return -1;
                            if (mc_emit_u8(&ctx->code, 0x18) != 0) return -1;
                            
                            if (hint && strcmp(hint, "T1") == 0) {
                                if (mc_emit_u8(&ctx->code, 0x10) != 0) return -1;
                            } else if (hint && strcmp(hint, "T2") == 0) {
                                if (mc_emit_u8(&ctx->code, 0x18) != 0) return -1;
                            } else if (hint && strcmp(hint, "NTA") == 0) {
                                if (mc_emit_u8(&ctx->code, 0x00) != 0) return -1;
                            } else {
                                if (mc_emit_u8(&ctx->code, 0x08) != 0) return -1; /* T0 */
                            }
                            break;
                        }
                        
                        default:
                            break;
                    }
                }
            }
            return 0;
        
        /* ==================== 硬件层节点代码生成（直接机器码） ==================== */
        
        case AST_HW_ISA_CALL: {
            /** hardware\isa\*() ISA 直通调用 - 工业级实现
             * 
             * 前端已经预计算了完整的操作码字节，包括：
             * - REX 前缀（如果需要）
             * - 操作码字节
             * - ModRM/SIB 字节
             * - 立即数
             * - 位移
             * 
             * 代码生成器的责任仅仅是将这些字节按原样输出。
             * 不做任何假设，不做任何简化。
             */
            
            if (stmt->data.hw_isa_call.isa_operation &&
                mc_emit_hw_isa_param_call(ctx,
                                          stmt->data.hw_isa_call.isa_operation,
                                          stmt->data.hw_isa_call.operands,
                                          stmt->data.hw_isa_call.operand_count) == 0) {
                return 0;
            }

            if (stmt->data.hw_isa_call.isa_operation &&
                strcmp(stmt->data.hw_isa_call.isa_operation, "goto") == 0) {
                if (stmt->data.hw_isa_call.operand_count < 1 ||
                    !stmt->data.hw_isa_call.operands ||
                    !stmt->data.hw_isa_call.operands[0]) {
                    return -1;
                }
                if (mc_emit_expr(ctx, stmt->data.hw_isa_call.operands[0]) != 0) return -1;
                if (ctx->pending_cs_valid) {
                    if (mc_emit_u8(&ctx->code, 0xB9) != 0) return -1;
                    if (mc_emit_u32(&ctx->code, (uint32_t)ctx->pending_cs_selector) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0x51) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0x50) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0xCB) != 0) return -1;
                    ctx->pending_cs_valid = 0;
                } else {
                    if (mc_emit_u8(&ctx->code, 0xFF) != 0) return -1;
                    if (mc_emit_u8(&ctx->code, 0xE0) != 0) return -1;
                }
                return 0;
            }

            /* lgdt/lidt 需要内存操作数。约定：先将操作数地址计算到 RAX，再发射 [RAX] 形式。 */
            if (stmt->data.hw_isa_call.isa_operation &&
                (strcmp(stmt->data.hw_isa_call.isa_operation, "lgdt") == 0 ||
                 strcmp(stmt->data.hw_isa_call.isa_operation, "lidt") == 0)) {
                if (stmt->data.hw_isa_call.operand_count < 1 ||
                    !stmt->data.hw_isa_call.operands ||
                    !stmt->data.hw_isa_call.operands[0]) {
                    return -1;
                }
                if (mc_emit_expr(ctx, stmt->data.hw_isa_call.operands[0]) != 0) {
                    return -1;
                }
                if (mc_emit_u8(&ctx->code, 0x0F) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0x01) != 0) return -1;
                if (mc_emit_u8(&ctx->code,
                               strcmp(stmt->data.hw_isa_call.isa_operation, "lgdt") == 0 ? 0x10 : 0x18) != 0) {
                    return -1;
                }
                return 0;
            }

            /* 如果操作码还没有生成（opcode_length==0），现在生成它 */
            if (stmt->data.hw_isa_call.opcode_length == 0 && stmt->data.hw_isa_call.isa_operation) {
                uint8_t temp_opcodes[16];
                int isa_opcode_len = hw_generate_isa_opcode(stmt->data.hw_isa_call.isa_operation, temp_opcodes);
                
                if (isa_opcode_len > 0 && isa_opcode_len <= 16) {
                    stmt->data.hw_isa_call.opcode_length = isa_opcode_len;
                    for (int i = 0; i < isa_opcode_len; i++) {
                        stmt->data.hw_isa_call.opcode_bytes[i] = temp_opcodes[i];
                    }
                } else if (isa_opcode_len < 0) {
                    /* 未识别的ISA操作 */
                    return -1;
                }
            }
            
            if (stmt->data.hw_isa_call.opcode_length > 0 && 
                stmt->data.hw_isa_call.opcode_length <= 16) {
                
                /* 直接输出前端预计算的所有操作码字节 */
                for (int i = 0; i < stmt->data.hw_isa_call.opcode_length; i++) {
                    uint8_t byte = stmt->data.hw_isa_call.opcode_bytes[i];
                    if (mc_emit_u8(&ctx->code, byte) != 0) {
                        return -1;
                    }
                }
            } else {
                /* 如果没有预计算的操作码（这不应该发生），返回错误 */
                /* 不生成任何后备代码 - 这表示前端有问题 */
                return -1;
            }
            return 0;
        }
        
        case AST_HW_BLOCK: {
            /* hardware { ... } 硬件层块 - 设置上下文后递归处理 */
            int saved_hw_flag = ctx->in_hardware_block;
            ctx->in_hardware_block = 1;
            
            if (stmt->data.hw_block.stmt_count > 0) {
                for (int j = 0; j < stmt->data.hw_block.stmt_count; j++) {
                    if (mc_emit_stmt(ctx, stmt->data.hw_block.statements[j]) != 0) {
                        ctx->in_hardware_block = saved_hw_flag;
                        return -1;
                    }
                }
            }
            
            ctx->in_hardware_block = saved_hw_flag;
            return 0;
        }
        
        case AST_HW_PORT_IO: {
            /** 端口I/O操作 - 工业级 IN/OUT 指令生成
             * 支持 8/16/32/64 位端口操作
             * 端口号通过 imm8 或 DX 寄存器指定
             */
            ASTNode *port_type = stmt->data.hw_port_io.port_type;
            uint16_t port_num = stmt->data.hw_port_io.port_number;
            int is_read = stmt->data.hw_port_io.is_read;
            
            /* 根据 port_type 确定操作数大小 */
            int port_size = 8;  /* 默认 UInt8 */
            if (port_type && port_type->type == AST_TYPE) {
                const char *type_name = port_type->data.type.name;
                if (strcmp(type_name, "UInt16") == 0) {
                    port_size = 16;
                } else if (strcmp(type_name, "UInt32") == 0) {
                    port_size = 32;
                } else if (strcmp(type_name, "UInt64") == 0) {
                    port_size = 64;
                }
                /* 默认 UInt8 = 8 */
            }
            
            if (is_read) {
                /* IN 指令 - 从端口读取到累加器 */
                switch (port_size) {
                    case 8: {
                        /* IN AL, imm8: EC imm8 */
                        if (mc_emit_u8(&ctx->code, 0xEC) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, (uint8_t)port_num) != 0) return -1;
                        break;
                    }
                    case 16: {
                        /* IN AX, imm8: 66 ED imm8 */
                        if (mc_emit_u8(&ctx->code, 0x66) != 0) return -1;  /* operand-size override */
                        if (mc_emit_u8(&ctx->code, 0xED) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, (uint8_t)port_num) != 0) return -1;
                        break;
                    }
                    case 32: {
                        /* IN EAX, imm8: ED imm8 */
                        if (mc_emit_u8(&ctx->code, 0xED) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, (uint8_t)port_num) != 0) return -1;
                        break;
                    }
                    case 64: {
                        /* IN RAX, imm8: 48 ED imm8 */
                        if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;  /* REX.W */
                        if (mc_emit_u8(&ctx->code, 0xED) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, (uint8_t)port_num) != 0) return -1;
                        break;
                    }
                    default:
                        return -1;  /* 不支持的端口宽度 */
                }
            } else {
                /* OUT 指令 - 从累加器写入到端口 */
                switch (port_size) {
                    case 8: {
                        /* OUT imm8, AL: EE imm8 */
                        if (mc_emit_u8(&ctx->code, 0xEE) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, (uint8_t)port_num) != 0) return -1;
                        break;
                    }
                    case 16: {
                        /* OUT imm8, AX: 66 EF imm8 */
                        if (mc_emit_u8(&ctx->code, 0x66) != 0) return -1;  /* operand-size override */
                        if (mc_emit_u8(&ctx->code, 0xEF) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, (uint8_t)port_num) != 0) return -1;
                        break;
                    }
                    case 32: {
                        /* OUT imm8, EAX: EF imm8 */
                        if (mc_emit_u8(&ctx->code, 0xEF) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, (uint8_t)port_num) != 0) return -1;
                        break;
                    }
                    case 64: {
                        /* OUT imm8, RAX: 48 EF imm8 */
                        if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;  /* REX.W */
                        if (mc_emit_u8(&ctx->code, 0xEF) != 0) return -1;
                        if (mc_emit_u8(&ctx->code, (uint8_t)port_num) != 0) return -1;
                        break;
                    }
                    default:
                        return -1;  /* 不支持的端口宽度 */
                }
            }
            
            return 0;
        }
        
        case AST_HW_CONTROL_REG: {
            /* CPU 控制寄存器访问：CPU/Current\Control\CR0 */
            uint8_t temp_opcodes[16];
            int opcode_len = 0;
            const char *reg_name = stmt->data.hw_control_reg.reg_name;
            
            if (!ctx->in_hardware_block) {
                return -1;
            }
            
            if (stmt->data.hw_control_reg.value) {
                /* 写操作：先生成值到RAX，然后MOV CRx, RAX */
                
                /* 生成值表达式代码到RAX */
                if (mc_emit_expr(ctx, stmt->data.hw_control_reg.value) != 0) {
                    return -1;
                }
                
                /* 生成 MOV CRx, RAX 指令 */
                opcode_len = hw_generate_control_reg_write(reg_name, temp_opcodes);
                if (opcode_len <= 0 || opcode_len > 16) {
                    return -1;
                }
                
                /* 输出机器码字节 */
                for (int i = 0; i < opcode_len; i++) {
                    if (mc_emit_u8(&ctx->code, temp_opcodes[i]) != 0) {
                        return -1;
                    }
                }
            } else {
                /* 读操作：生成 MOV RAX, CRx */
                opcode_len = hw_generate_control_reg_read(reg_name, temp_opcodes);
                if (opcode_len <= 0 || opcode_len > 16) {
                    return -1;
                }
                
                for (int i = 0; i < opcode_len; i++) {
                    if (mc_emit_u8(&ctx->code, temp_opcodes[i]) != 0) {
                        return -1;
                    }
                }
            }
            
            return 0;
        }
        
        case AST_HW_FLAG_ACCESS: {
            /* CPU RFLAGS 标志位访问：CPU/Flags\Interrupt/Enable */
            uint8_t temp_opcodes[16];
            int opcode_len = 0;
            const char *flag_name = stmt->data.hw_flag_access.flag_name;
            
            if (!ctx->in_hardware_block) {
                return -1;
            }
            
            /* 特殊处理 IF (Interrupt Flag) 和其他常见标志 */
            if (strcmp(flag_name, "Interrupt/Enable") == 0 || strcmp(flag_name, "IF") == 0) {
                if (stmt->data.hw_flag_access.value) {
                    /* 设置IF标志位 - STI指令 */
                    opcode_len = hw_generate_sti(temp_opcodes);
                } else {
                    /* 清除IF标志位 - CLI指令 */
                    opcode_len = hw_generate_cli(temp_opcodes);
                }
                
                if (opcode_len <= 0) {
                    return -1;
                }
                
                for (int i = 0; i < opcode_len; i++) {
                    if (mc_emit_u8(&ctx->code, temp_opcodes[i]) != 0) {
                        return -1;
                    }
                }
            } else {
                /* 通用RFLAGS操作 */
                if (stmt->data.hw_flag_access.value) {
                    /* 写入RFLAGS: 先生成值到RAX，然后PUSH RAX; POPFQ */
                    
                    /* 生成值表达式到RAX */
                    if (mc_emit_expr(ctx, stmt->data.hw_flag_access.value) != 0) {
                        return -1;
                    }
                    
                    /* 生成 PUSH RAX; POPFQ */
                    opcode_len = hw_generate_flag_write(temp_opcodes);
                } else {
                    /* 读取RFLAGS: PUSHFQ; POP RAX */
                    opcode_len = hw_generate_flag_read(temp_opcodes);
                }
                
                if (opcode_len <= 0 || opcode_len > 16) {
                    return -1;
                }
                
                for (int i = 0; i < opcode_len; i++) {
                    if (mc_emit_u8(&ctx->code, temp_opcodes[i]) != 0) {
                        return -1;
                    }
                }
            }
            
            return 0;
        }

        case AST_HW_MORPH_BLOCK: {
            int j;
            if (!stmt->data.hw_morph_block.statements) return 0;
            for (j = 0; j < stmt->data.hw_morph_block.stmt_count; j++) {
                if (mc_emit_stmt(ctx, stmt->data.hw_morph_block.statements[j]) != 0) return -1;
            }
            return 0;
        }
        
        case AST_HW_REG_BINDING:
        case AST_HW_VOLATILE_VIEW:
        case AST_HW_GATE_FUNC:
        case AST_HW_VECTOR_TYPE:
            /* 这些硬件层节点在当前阶段不生成代码，仅用于类型检查 */
            return 0;
        
        default:
            return 0;
    }
}

static int mc_seed_function_params(McCtx *ctx, ASTNode *decl) {
    int i;
    if (!ctx || !decl || decl->type != AST_FUNC_DECL) return -1;
    for (i = 0; i < decl->data.func_decl.param_count; i++) {
        ASTNode *param = decl->data.func_decl.params ? decl->data.func_decl.params[i] : NULL;
        McSymbol sym;
        if (!param || param->type != AST_VAR_DECL || !param->data.var_decl.name) continue;
        if (mc_find_symbol(ctx->local_symbols, ctx->local_symbol_count, param->data.var_decl.name)) {
            continue;
        }
        memset(&sym, 0, sizeof(sym));
        sym.name = param->data.var_decl.name;
        sym.kind = MC_SYM_PARAM;
        sym.param_index = i;
        if (param->data.var_decl.type && param->data.var_decl.type->type == AST_TYPE) {
            sym.type_name = param->data.var_decl.type->data.type.name;
        }
        if (mc_add_local_symbol(ctx, &sym) != 0) return -1;
    }
    return 0;
}

/* Materialize parameter values into stable stack slots at function entry.
 * This avoids parameter aliasing bugs when hardware code later mutates
 * argument registers (rcx/rdx/r8/r9 etc.). */
static int mc_spill_function_params(McCtx *ctx) {
    size_t i;
    if (!ctx || !mc_is_x64(ctx)) return 0;
    for (i = 0; i < ctx->local_symbol_count; i++) {
        McSymbol *sym = &ctx->local_symbols[i];
        if (!sym || sym->kind != MC_SYM_PARAM) continue;
        if (sym->stack_offset >= 0) return -1;

        if (mc_emit_mov_acc_arg(ctx, sym->param_index) != 0) return -1;
        sym->kind = MC_SYM_STACK;
        if (mc_emit_store_acc_to_stack(ctx, sym->stack_offset) != 0) return -1;
    }
    return 0;
}

/* Reserve stable stack slots for parameters before prologue emission.
 * This keeps stack layout deterministic and avoids per-parameter sub rsp,8. */
static int mc_plan_param_spills(McCtx *ctx) {
    size_t i;
    if (!ctx || !mc_is_x64(ctx)) return 0;
    for (i = 0; i < ctx->local_symbol_count; i++) {
        McSymbol *sym = &ctx->local_symbols[i];
        if (!sym || sym->kind != MC_SYM_PARAM) continue;
        ctx->stack_allocated += 8;
        sym->stack_offset = -ctx->stack_allocated;
    }
    return 0;
}

static int mc_emit_function(McCtx *ctx, ASTNode *decl) {
    const char *name;
    size_t start;
    uint64_t size;
    const char *gate_type;
    int needs_prologue_epilogue;
    
    if (!ctx || !decl || decl->type != AST_FUNC_DECL) return -1;
    if (decl->data.func_decl.is_extern) return 0;
    name = decl->data.func_decl.name ? decl->data.func_decl.name : "unknown";
    start = ctx->code.size;
    ctx->current_func_start = start;
    ctx->label_count = 0;
    ctx->branch_count = 0;
    ctx->has_explicit_return = 0;
    mc_reset_locals(ctx);
    ctx->pending_cs_valid = 0;
    ctx->pending_cs_selector = 0;
    gate_type = get_gate_type(decl);
    ctx->current_func_is_efi = 0;
    if ((gate_type && strcmp(gate_type, "efi") == 0) ||
        strcmp(name, "efi/main") == 0 ||
        strcmp(name, "efi_main") == 0) {
        ctx->current_func_is_efi = 1;
    }
    if (mc_seed_function_params(ctx, decl) != 0) return -1;
    if (decl->data.func_decl.body && mc_plan_locals_in_stmt(ctx, decl->data.func_decl.body) != 0) return -1;
    if (mc_plan_param_spills(ctx) != 0) return -1;
    if (ctx->current_func_is_efi && mc_is_x64(ctx)) {
        size_t i;
        ctx->uefi_system_table_offset = 0;
        for (i = 0; i < ctx->local_symbol_count; i++) {
            McSymbol *sym = &ctx->local_symbols[i];
            if (!sym || sym->kind != MC_SYM_PARAM) continue;
            if (sym->param_index == 1) {
                ctx->uefi_system_table_offset = sym->stack_offset;
                break;
            }
        }
    }
    if (mc_is_x64(ctx) && ((ctx->stack_allocated & 0xF) == 0)) {
        ctx->stack_allocated += 8;
    }
    ctx->planned_local_bytes = ctx->stack_allocated;
    
    /* 根据 gate_type 决定是否生成标准的函数序言/后续 */
    needs_prologue_epilogue = 1;
    
    if (gate_type) {
        /* 硬件门函数 - 工业级处理 */
        
        if (strcmp(gate_type, "naked") == 0) {
            /* @gate(type: \naked) - 完全无序言/后续 */
            needs_prologue_epilogue = 0;
        } 
        else if (strcmp(gate_type, "syscall") == 0) {
            /* @gate(type: \syscall) - 系统调用入口
             * 无标准序言，保留参数寄存器作为系统调用参数
             * SYSCALL 指令由用户代码在硬件块中调用
             */
            needs_prologue_epilogue = 0;
        } 
        else if (strcmp(gate_type, "interrupt") == 0) {
            /* @gate(type: \interrupt) - 硬件中断处理
             * x86-64 中断门由硬件自动PUSHALL，所以这里不需要额外操作
             * 但需要特殊的后续：POPALL + IRETQ
             */
            needs_prologue_epilogue = 0;  /* 硬件自动处理 */
        } 
        else if (strcmp(gate_type, "exception") == 0) {
            /* @gate(type: \exception) - 异常处理
             * 类似中断，硬件自动保存上下文
             */
            needs_prologue_epilogue = 0;
        } 
        else if (strcmp(gate_type, "efi") == 0) {
            /* @gate(type: \efi) - EFI 应用入口
             * 遵循 x86-64 System V ABI
             */
            needs_prologue_epilogue = 1;  /* 保持标准序言 */
        }
    }
    
    /* 生成函数序言 */
    if (needs_prologue_epilogue) {
        /* 标准 x86-64 函数序言：push rbp; mov rbp, rsp */
        if (mc_emit_u8(&ctx->code, 0x55) != 0) return -1;         /* push rbp */
        if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;         /* REX.W */
        if (mc_emit_u8(&ctx->code, 0x89) != 0) return -1;         /* mov */
        if (mc_emit_u8(&ctx->code, 0xE5) != 0) return -1;         /* rbp, rsp */
        if (ctx->planned_local_bytes > 0) {
            if (ctx->planned_local_bytes <= 127) {
                if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1; /* sub rsp, imm8 */
                if (mc_emit_u8(&ctx->code, 0x83) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0xEC) != 0) return -1;
                if (mc_emit_u8(&ctx->code, (uint8_t)ctx->planned_local_bytes) != 0) return -1;
            } else {
                if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1; /* sub rsp, imm32 */
                if (mc_emit_u8(&ctx->code, 0x81) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0xEC) != 0) return -1;
                if (mc_emit_u32(&ctx->code, (uint32_t)ctx->planned_local_bytes) != 0) return -1;
            }
        }
    }

    /* UEFI entry uses Microsoft x64 ABI (rcx, rdx, r8, r9).
     * Internal machine-code path expects SysV-like mapping (rdi, rsi, rdx, rcx).
     * Bridge once at function entry for @gate(type:\efi) / efi_main.
     */
    if (ctx->current_func_is_efi && mc_is_x64(ctx)) {
        if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xCF) != 0) return -1; /* mov rdi, rcx */
        if (mc_emit_u8(&ctx->code, 0x48) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xD6) != 0) return -1; /* mov rsi, rdx */
        if (mc_emit_u8(&ctx->code, 0x4C) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC2) != 0) return -1; /* mov rdx, r8 */
        if (mc_emit_u8(&ctx->code, 0x4C) != 0 || mc_emit_u8(&ctx->code, 0x89) != 0 || mc_emit_u8(&ctx->code, 0xC9) != 0) return -1; /* mov rcx, r9 */
    }

    if (needs_prologue_epilogue) {
        if (mc_spill_function_params(ctx) != 0) return -1;
    }

    /* 生成函数体 */
    if (decl->data.func_decl.body && mc_emit_stmt(ctx, decl->data.func_decl.body) != 0) return -1;

    if (mc_patch_branches(ctx) != 0) return -1;

    /* 生成函数后续 */
    if (!ctx->has_explicit_return) {
        if (gate_type && strcmp(gate_type, "interrupt") == 0) {
            /* 硬件中断处理后续：IRETQ 返回
             * IRETQ: 48 CF
             * x86-64 硬件自动 POPALL（从 PUSHALL by CPU 对应）
             */
            if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
            if (mc_emit_u8(&ctx->code, 0xCF) != 0) return -1;
        } 
        else if (gate_type && strcmp(gate_type, "exception") == 0) {
            /* 异常处理后续：IRETQ 返回 */
            if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
            if (mc_emit_u8(&ctx->code, 0xCF) != 0) return -1;
        } 
        else if (gate_type && strcmp(gate_type, "syscall") == 0) {
            /* 系统调用后续：SYSRETQ 返回
             * SYSRETQ: 48 0F 07
             * RCX = 返回地址（用户 CS 中的 RIP）
             * 由用户代码设置（通常是硬件块中的指令）
             */
            if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
            if (mc_emit_u8(&ctx->code, 0x0F) != 0) return -1;
            if (mc_emit_u8(&ctx->code, 0x07) != 0) return -1;
        } 
        else if (needs_prologue_epilogue) {
            /* 标准函数后续：mov rax, 0; leave; ret */
            if (mc_emit_mov_acc_imm(ctx, 0) != 0) return -1;
            if (mc_emit_epilogue(ctx) != 0) return -1;
        } 
        /* 如果是 naked 函数，用户代码必须自己处理返回 */
    } 
    else if (needs_prologue_epilogue && ctx->has_explicit_return) {
        /* 已有显式 return 语句，但如果有标准序言需要 leave */
        /* 注意：mc_emit_epilogue 在 RETURN_STMT 中已经调用过了 */
    }

    size = (uint64_t)(ctx->code.size - start);
    if (mc_ctx_add_func(ctx, name, (uint64_t)start, size) != 0) return -1;
    
    /* [TODO-06] 入口点识别改进：
     * - main函数已被罢工，不再作为自动入口点
     * - 优先识别@entry装饰的函数（需要AST支持装饰器信息）
     * - 可由--entry命令行参数显式指定
     * - 兼容旧的入口点：efi/main, efi_main（用于EFI固件）
     */
    if (strcmp(name, "efi/main") == 0 ||
        strcmp(name, "efi_main") == 0) {
        /* EFI固件的特殊入口点保留 */
        ctx->entry_point = (uint64_t)start;
    }
    ctx->current_func_is_efi = 0;
    /* 注意：如果需要支持@entry装饰器识别，需在语义分析阶段
     * 将装饰器信息传递到代码生成阶段 */
    return 0;
}

static uint32_t mc_find_or_add_extern_symbol(AETBGenerator *aetb, const char *name) {
    uint32_t i;
    if (!aetb || !name) return 0;
    for (i = 0; i < aetb->symbol_count; i++) {
        AETBSymbol *sym = &aetb->symbols[i];
        if (sym->name_offset + sym->name_length < aetb->string_pool_size &&
            strcmp((const char *)&aetb->string_pool[sym->name_offset], name) == 0) {
            return i;
        }
    }
    return aetb_gen_add_symbol(aetb, name, 1, 2, 0, 0, 0);
}

static int mc_resolve_calls(McCtx *ctx) {
    size_t i;
    uint64_t cast_stub_off = UINT64_MAX;
    if (!ctx) return -1;

    /* Legacy parser/codegen may leave cast<...>/ptr<...> as call targets.
       Materialize an identity-cast stub and redirect these calls to it. */
    for (i = 0; i < ctx->call_count; i++) {
        const char *tn = ctx->calls[i].target_name;
        if (!tn) continue;
        if (strcmp(tn, "cast") == 0 || strcmp(tn, "ptr") == 0 ||
            strncmp(tn, "cast<", 5) == 0 || strncmp(tn, "ptr<", 4) == 0) {
            if (cast_stub_off == UINT64_MAX) {
                cast_stub_off = (uint64_t)ctx->code.size;
                /* mov rax, rdi; ret */
                if (mc_emit_u8(&ctx->code, 0x48) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0x89) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0xF8) != 0) return -1;
                if (mc_emit_u8(&ctx->code, 0xC3) != 0) return -1;
                if (mc_ctx_add_func(ctx, "__aethel_cast_identity", cast_stub_off, 4) != 0) return -1;
            }
            break;
        }
    }

    for (i = 0; i < ctx->call_count; i++) {
        uint64_t target_off = 0;
        size_t rel_off = ctx->calls[i].rel_off;
        int rel_width = ctx->calls[i].rel_width;
        int64_t addend = (int64_t)rel_width;
        const char *target_name = ctx->calls[i].target_name;
        int is_cast_call = 0;

        if (target_name &&
            (strcmp(target_name, "cast") == 0 || strcmp(target_name, "ptr") == 0 ||
             strncmp(target_name, "cast<", 5) == 0 || strncmp(target_name, "ptr<", 4) == 0)) {
            is_cast_call = 1;
            target_off = cast_stub_off;
        }

        if (is_cast_call || mc_ctx_find_func(ctx, target_name, &target_off) == 0) {
            int64_t disp = (int64_t)target_off - (int64_t)(rel_off + (size_t)rel_width);
            if (rel_width == 2) {
                if (disp < INT16_MIN || disp > INT16_MAX) return -1;
                if (mc_patch_rel16(&ctx->code, rel_off, (int16_t)disp) != 0) return -1;
            } else {
                if (disp < INT32_MIN || disp > INT32_MAX) return -1;
                if (mc_patch_rel32(&ctx->code, rel_off, (int32_t)disp) != 0) return -1;
            }
        } else {
            if (rel_width == 2) {
                return -1;
            }
            uint32_t sym_idx = mc_find_or_add_extern_symbol(ctx->aetb, target_name);
            /* call rel32 uses target - (P + 4); linker formula is S - P + A, so A = -4 */
            aetb_gen_add_relocation_ex(ctx->aetb, rel_off, sym_idx, RELOC_PC32, (int16_t)(-addend));
        }
    }
    return 0;
}

static void mc_ctx_free(McCtx *ctx) {
    size_t i;
    if (!ctx) return;
    free(ctx->code.data);
    for (i = 0; i < ctx->func_count; i++) free(ctx->funcs[i].name);
    for (i = 0; i < ctx->call_count; i++) free(ctx->calls[i].target_name);
    free(ctx->funcs);
    free(ctx->calls);
    free(ctx->labels);
    free(ctx->branches);
    for (i = 0; i < ctx->local_symbol_count; i++) free(ctx->local_symbols[i].name);
    for (i = 0; i < ctx->local_symbol_count; i++) free(ctx->local_symbols[i].type_name);
    free(ctx->local_symbols);
    for (i = 0; i < ctx->const_symbol_count; i++) free(ctx->const_symbols[i].name);
    for (i = 0; i < ctx->const_symbol_count; i++) free(ctx->const_symbols[i].type_name);
    free(ctx->const_symbols);
    for (i = 0; i < ctx->struct_count; i++) {
        size_t j;
        free(ctx->structs[i].name);
        for (j = 0; j < ctx->structs[i].field_count; j++) {
            free(ctx->structs[i].fields[j].name);
        }
        free(ctx->structs[i].fields);
    }
    free(ctx->structs);
    memset(ctx, 0, sizeof(*ctx));
}

static int mc_eval_const_expr(McCtx *ctx, ASTNode *expr, uint64_t *out) {
    uint64_t l, r;
    if (!ctx || !expr || !out) return -1;
    switch (expr->type) {
        case AST_LITERAL:
            if (expr->data.literal.is_float) return -1;
            *out = (uint64_t)expr->data.literal.value.int_value;
            return 0;
        case AST_IDENT: {
            McSymbol *sym = mc_find_symbol(ctx->const_symbols, ctx->const_symbol_count, expr->data.ident.name);
            if (!sym || sym->kind != MC_SYM_CONST) return -1;
            *out = sym->const_value;
            return 0;
        }
        case AST_TYPECAST:
            return mc_eval_const_expr(ctx, expr->data.typecast.expr, out);
        case AST_BINARY_OP:
            if (mc_eval_const_expr(ctx, expr->data.binary_op.left, &l) != 0) return -1;
            if (mc_eval_const_expr(ctx, expr->data.binary_op.right, &r) != 0) return -1;
            if (strcmp(expr->data.binary_op.op, "+") == 0) *out = l + r;
            else if (strcmp(expr->data.binary_op.op, "-") == 0) *out = l - r;
            else if (strcmp(expr->data.binary_op.op, "|") == 0) *out = l | r;
            else if (strcmp(expr->data.binary_op.op, "&") == 0) *out = l & r;
            else if (strcmp(expr->data.binary_op.op, "^") == 0) *out = l ^ r;
            else if (strcmp(expr->data.binary_op.op, "<<") == 0) *out = l << (r & 63);
            else if (strcmp(expr->data.binary_op.op, ">>") == 0) *out = l >> (r & 63);
            else return -1;
            return 0;
        default:
            return -1;
    }
}

CodeGenerator* codegen_create(FILE *output) {
    CodeGenerator *gen = malloc(sizeof(CodeGenerator));
    if (!gen) return NULL;
    gen->output = output;
    gen->label_count = 0;
    gen->var_offset = 0;
    gen->machine_bits = 64;
    gen->target_isa = "x86_64";
    gen->target_format = 0;
    gen->use_uefi = 0;
    gen->use_syscall = 1;
    gen->in_hardware_block = 0;
    gen->emit_assembly = 0;
    gen->entry_point_name = NULL;
    strcpy(gen->error, "");
    gen->error_count = 0;
    memset(gen->errors, 0, sizeof(gen->errors));  /* 初始化错误历史 */
    return gen;
}

int codegen_set_target(CodeGenerator *gen, const char *isa, int machine_bits) {
    if (!gen || !isa) return -1;
    if (!(machine_bits == 16 || machine_bits == 32 || machine_bits == 64)) return -1;
    if (strcmp(isa, "x86") != 0 && strcmp(isa, "x86_64") != 0) return -1;
    if (strcmp(isa, "x86_64") == 0 && machine_bits != 64) return -1;
    gen->target_isa = isa;
    gen->machine_bits = machine_bits;
    return 0;
}

void codegen_destroy(CodeGenerator *gen) {
    free(gen);
}

int codegen_generate(CodeGenerator *gen, ASTNode *ast) {
    /* 创建 AETB 生成器 */
    AETBGenerator *aetb = aetb_gen_create(gen->output, 2, 1);
    McCtx mc;
    int i;
    if (!gen) return 1;
    if (!(gen->machine_bits == 16 || gen->machine_bits == 32 || gen->machine_bits == 64)) {
        strcpy(gen->error, "Unsupported machine bits");
        return 1;
    }
    if (!gen->target_isa ||
        (strcmp(gen->target_isa, "x86") != 0 && strcmp(gen->target_isa, "x86_64") != 0)) {
        strcpy(gen->error, "Unsupported target ISA for Stage1 codegen");
        return 1;
    }
    if (strcmp(gen->target_isa, "x86_64") == 0 && gen->machine_bits != 64) {
        strcpy(gen->error, "x86_64 requires 64-bit machine mode");
        return 1;
    }
    if (!aetb) {
        strcpy(gen->error, "Failed to create AETB generator");
        return 1;
    }
    memset(&mc, 0, sizeof(mc));
    mc.aetb = aetb;
    mc.entry_point = 0;
    mc.machine_bits = gen->machine_bits;
    mc.target_isa = gen->target_isa;
    mc.target_format = gen->target_format;
    mc.current_func_is_efi = 0;
    
    /* 如果没有AST（parse error）或AST不是program，生成最小化的AETB二进制 */
    if (!ast || ast->type != AST_PROGRAM) {
        /* 生成最小化的AETB：只有header + 空code/data */
        if (aetb_gen_finalize(aetb) != 0) {
            strcpy(gen->error, "Failed to finalize minimal AETB binary");
            aetb_gen_destroy(aetb);
            return 1;
        }
        aetb_gen_destroy(aetb);
        return 0;  /* 成功生成了最小化的二进制 */
    }
    
    /* 先处理 extern 声明（导入符号） */
    for (i = 0; i < ast->data.program.decl_count; i++) {
        ASTNode *decl = ast->data.program.declarations[i];
        if (!decl) continue;
        if (decl->type == AST_FUNC_DECL && decl->data.func_decl.is_extern && decl->data.func_decl.name) {
            aetb_gen_add_symbol(aetb, decl->data.func_decl.name, 1, 2, 0, 0, 0);
        }
    }

    /* 收集可折叠的顶层常量，供硬件路径/端口常量等表达式读取 */
    for (i = 0; i < ast->data.program.decl_count; i++) {
        ASTNode *decl = ast->data.program.declarations[i];
        if (!decl || decl->type != AST_STRUCT_DECL) continue;
        if (mc_add_struct_layout(&mc, decl) != 0) {
            strcpy(gen->error, "Failed to build struct layout table");
            mc_ctx_free(&mc);
            aetb_gen_destroy(aetb);
            return 1;
        }
    }

    /* 收集可折叠的顶层常量，供硬件路径/端口常量等表达式读取 */
    for (i = 0; i < ast->data.program.decl_count; i++) {
        ASTNode *decl = ast->data.program.declarations[i];
        uint64_t const_value = 0;
        ASTNode *const_init = NULL;
        if (!decl || decl->type != AST_VAR_DECL || !decl->data.var_decl.name) continue;
        const_init = decl->data.var_decl.init ? decl->data.var_decl.init : decl->data.var_decl.init_value;
        if (!const_init) continue;
        if (mc_eval_const_expr(&mc, const_init, &const_value) == 0) {
            if (mc_find_symbol(mc.const_symbols, mc.const_symbol_count, decl->data.var_decl.name) == NULL) {
                if (mc_add_const_symbol(&mc, decl->data.var_decl.name, const_value) != 0) {
                    strcpy(gen->error, "Failed to register constant symbol");
                    mc_ctx_free(&mc);
                    aetb_gen_destroy(aetb);
                    return 1;
                }
            }
        }
    }

    /* [TODO-06] 第一步：识别@entry装饰的函数并记录 */
    int found_entry = 0;
    for (i = 0; i < ast->data.program.decl_count; i++) {
        ASTNode *decl = ast->data.program.declarations[i];
        if (!decl || decl->type != AST_FUNC_DECL) continue;
        
        if (has_entry_decorator(decl)) {
            found_entry = 1;
            if (gen->entry_point_name == NULL || strlen(gen->entry_point_name) == 0) {
                /* 如果命令行没有指定 --entry，使用@entry装饰的函数 */
                gen->entry_point_name = decl->data.func_decl.name;
            }
            break;
        }
    }

    /* 生成真实 x86-64 函数机器码 */
    for (i = 0; i < ast->data.program.decl_count; i++) {
        ASTNode *decl = ast->data.program.declarations[i];
        if (!decl) continue;
        if (decl->type == AST_FUNC_DECL) {
            if (mc_emit_function(&mc, decl) != 0) {
                snprintf(gen->error, sizeof(gen->error),
                         "Failed to emit function machine code: %s (line %d)",
                         decl->data.func_decl.name ? decl->data.func_decl.name : "<anonymous>",
                         decl->line);
                mc_ctx_free(&mc);
                aetb_gen_destroy(aetb);
                return 1;
            }
            
            /* [TODO-06] 检查是否为入口点函数 */
            if (decl->data.func_decl.name) {
                /* 优先级：@entry装饰器 > --entry命令行参数 > EFI特殊名 */
                if (has_entry_decorator(decl)) {
                    /* @entry装饰的函数自动成为入口点 */
                    size_t j;
                    for (j = 0; j < mc.func_count; j++) {
                        if (strcmp(mc.funcs[j].name, decl->data.func_decl.name) == 0) {
                            mc.entry_point = mc.funcs[j].offset;
                            break;
                        }
                    }
                } else if (gen->entry_point_name && strcmp(gen->entry_point_name, decl->data.func_decl.name) == 0) {
                    /* 命令行指定的入口点 */
                    size_t j;
                    for (j = 0; j < mc.func_count; j++) {
                        if (strcmp(mc.funcs[j].name, decl->data.func_decl.name) == 0) {
                            mc.entry_point = mc.funcs[j].offset;
                            break;
                        }
                    }
                }
            }
        } else if (decl->type == AST_VAR_DECL) {
            const char *var_name = decl->data.var_decl.name;
            if (var_name) {
                uint8_t zero8[8] = {0};
                uint64_t var_off = aetb->data_size;
                aetb_gen_emit_data(aetb, zero8, sizeof(zero8));
                aetb_gen_add_symbol(aetb, var_name, 0, 1, 2, var_off, sizeof(zero8));
            }
        }
    }

    if (mc_resolve_calls(&mc) != 0) {
        strcpy(gen->error, "Failed to resolve/relocate call sites");
        mc_ctx_free(&mc);
        aetb_gen_destroy(aetb);
        return 1;
    }

    if (mc.code.size > UINT32_MAX) {
        strcpy(gen->error, "Generated code section too large");
        mc_ctx_free(&mc);
        aetb_gen_destroy(aetb);
        return 1;
    }

    aetb_gen_emit_code(aetb, mc.code.data, (uint32_t)mc.code.size);

    for (i = 0; i < (int)mc.func_count; i++) {
        aetb_gen_add_symbol(aetb,
                            mc.funcs[i].name,
                            1,
                            1,
                            1,
                            mc.funcs[i].offset,
                            mc.funcs[i].size);
    }

    aetb_gen_set_entry_point(aetb, mc.entry_point);
    mc_ctx_free(&mc);
    
    /* 最终化 AETB 二进制输出 */
    if (aetb_gen_finalize(aetb) != 0) {
        strcpy(gen->error, "Failed to finalize AETB binary");
        aetb_gen_destroy(aetb);
        return 1;
    }
    
    aetb_gen_destroy(aetb);
    return 0;
}

const char* codegen_get_error(CodeGenerator *gen) {
    return gen->error;
}
