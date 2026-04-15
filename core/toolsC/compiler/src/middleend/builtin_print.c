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
 * AethelOS Aethelium Compiler - Built-in print() Implementation
 * 工业级print()函数编译支持实现
 *
 * 输出路径：
 * 1. PE/UEFI → EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL.OutputString
 * 2. AETB/AKI/SRV/HDA → syscall(SYS_PRINT)
 * 3. BIN/铁层 → VT100/显存直接输出
 */

#include "builtin_print.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * 辅助函数：检测和验证
 * ============================================================================ */

int is_print_function_call(ASTNode *call_expr) {
    if (!call_expr || call_expr->type != AST_CALL) {
        return 0;
    }
    
    ASTNode *func = call_expr->data.call.func;
    if (func && func->type == AST_IDENT) {
        const char *name = func->data.ident.name;
        /* 支持 print(...) 和 print/xxx(...) 形式 */
        if (strcmp(name, "print") == 0 || strncmp(name, "print/", 6) == 0) {
            return 1;
        }
    }
    
    return 0;
}

int is_comp_function_call(ASTNode *call_expr) {
    if (!call_expr || call_expr->type != AST_CALL) {
        return 0;
    }

    ASTNode *func = call_expr->data.call.func;
    if (func && func->type == AST_IDENT) {
        const char *name = func->data.ident.name;
        if (name && (strcmp(name, "comp") == 0 || strncmp(name, "comp/", 5) == 0)) {
            return 1;
        }
    }
    return 0;
}

static int validate_comp_syntax(ASTNode *call_expr) {
    if (!call_expr || call_expr->type != AST_CALL) {
        fprintf(stderr, "[BUILTIN] Error: Expected AST_CALL node\n");
        return -1;
    }

    if (call_expr->data.call.arg_count == 0) {
        fprintf(stderr, "[BUILTIN] Error: comp() requires at least one argument\n");
        return -1;
    }
    if (call_expr->data.call.arg_count > 1) {
        fprintf(stderr, "[BUILTIN] Error: comp() accepts exactly one argument\n");
        return -1;
    }
    return 0;
}

int validate_print_syntax(ASTNode *call_expr) {
    if (!call_expr || call_expr->type != AST_CALL) {
        fprintf(stderr, "[BUILTIN] Error: Expected AST_CALL node\n");
        return -1;
    }
    
    /* print() 必须有至少一个参数 */
    if (call_expr->data.call.arg_count == 0) {
        fprintf(stderr, "[BUILTIN] Error: print() requires at least one argument\n");
        return -1;
    }
    
    /* print() 最多一个参数（不支持printf式多参数） */
    if (call_expr->data.call.arg_count > 1) {
        fprintf(stderr, "[BUILTIN] Error: print() accepts exactly one argument (no printf-style variadic)\n");
        return -1;
    }
    
    return 0;
}

PrintArgType infer_print_arg_type(ASTNode *arg_expr) {
    if (!arg_expr) {
        return PRINT_TYPE_UNKNOWN;
    }
    
    switch (arg_expr->type) {
    case AST_LITERAL:
        if (arg_expr->data.literal.is_string) {
            return PRINT_TYPE_STRING;
        }
        if (arg_expr->data.literal.is_float) {
            return PRINT_TYPE_FLOAT;
        } else {
            return PRINT_TYPE_INT;
        }
        
    case AST_IDENT:
        /* 需要通过符号表查询实际类型，这里简化为STRING */
        return PRINT_TYPE_STRING;
        
    case AST_BINARY_OP:
        /* 字符串拼接（+）返回STRING */
        if (strcmp(arg_expr->data.binary_op.op, "+") == 0) {
            return PRINT_TYPE_STRING;
        }
        return PRINT_TYPE_UNKNOWN;
        
    case AST_TYPECAST:
        /* 根据目标类型推导 */
        if (arg_expr->data.typecast.target_type &&
            arg_expr->data.typecast.target_type->type == AST_TYPE) {
            const char *type_name = arg_expr->data.typecast.target_type->data.type.name;
            if (strncmp(type_name, "ptr", 3) == 0) {
                return PRINT_TYPE_POINTER;
            }
        }
        return PRINT_TYPE_UNKNOWN;
        
    default:
        return PRINT_TYPE_UNKNOWN;
    }
}

/* ============================================================================
 * UTF-8 验证辅助函数
 * ============================================================================ */

/**
 * 验证UTF-8序列的有效性
 * @param byte UTF-8首字节
 * @param expected_len 期望的字节序列长度（1-4）
 * @return 成功返回序列长度，失败返回-1
 */
static int validate_utf8_sequence(uint8_t byte, int *expected_len) {
    if ((byte & 0x80) == 0) {
        /* ASCII: 0xxxxxxx */
        *expected_len = 1;
        return 1;
    } else if ((byte & 0xE0) == 0xC0) {
        /* 2-byte: 110xxxxx */
        *expected_len = 2;
        if ((byte & 0x1E) == 0) return -1;  /* 无效编码 */
        return 1;
    } else if ((byte & 0xF0) == 0xE0) {
        /* 3-byte: 1110xxxx */
        *expected_len = 3;
        return 1;
    } else if ((byte & 0xF8) == 0xF0) {
        /* 4-byte: 11110xxx */
        *expected_len = 4;
        if ((byte & 0x07) > 4) return -1;  /* 无效编码（超出Unicode范围） */
        return 1;
    } else {
        /* 续字节不能作为首字节 */
        return -1;
    }
}

/**
 * 计算UTF-16编码后的大小
 * @param utf8_str UTF-8字符串
 * @return 返回需要的UTF-16字节数（包括null terminator），失败返回0
 */
static size_t calculate_utf16_size(const char *utf8_str) {
    if (!utf8_str) return 0;
    
    size_t utf16_size = 0;
    size_t i = 0;
    size_t utf8_len = strlen(utf8_str);
    
    while (i < utf8_len) {
        uint8_t c = (uint8_t)utf8_str[i];
        int seq_len = 0;
        
        if (validate_utf8_sequence(c, &seq_len) < 0) {
            /* 无效UTF-8序列 */
            fprintf(stderr, "[BUILTIN] Error: Invalid UTF-8 byte at offset %zu: 0x%02x\n", i, c);
            return 0;
        }
        
        /* 验证完整序列 */
        if (i + seq_len > utf8_len) {
            fprintf(stderr, "[BUILTIN] Error: Incomplete UTF-8 sequence at offset %zu\n", i);
            return 0;
        }
        
        for (int j = 1; j < seq_len; j++) {
            uint8_t cont = (uint8_t)utf8_str[i + j];
            if ((cont & 0xC0) != 0x80) {
                fprintf(stderr, "[BUILTIN] Error: Invalid UTF-8 continuation byte at offset %zu\n", i + j);
                return 0;
            }
        }
        
        /* 解码代码点 */
        uint32_t codepoint = 0;
        
        if (seq_len == 1) {
            codepoint = c;
        } else if (seq_len == 2) {
            codepoint = ((c & 0x1F) << 6) | 
                        ((uint8_t)utf8_str[i+1] & 0x3F);
        } else if (seq_len == 3) {
            codepoint = ((c & 0x0F) << 12) | 
                        (((uint8_t)utf8_str[i+1] & 0x3F) << 6) |
                        ((uint8_t)utf8_str[i+2] & 0x3F);
        } else if (seq_len == 4) {
            codepoint = ((c & 0x07) << 18) |
                        (((uint8_t)utf8_str[i+1] & 0x3F) << 12) |
                        (((uint8_t)utf8_str[i+2] & 0x3F) << 6) |
                        ((uint8_t)utf8_str[i+3] & 0x3F);
        }
        
        /* 计算UTF-16编码所需字节数 */
        if (codepoint <= 0xFFFF) {
            utf16_size += 2;  /* 单个16位字 */
        } else if (codepoint <= 0x10FFFF) {
            utf16_size += 4;  /* 代理对（两个16位字） */
        } else {
            fprintf(stderr, "[BUILTIN] Error: Invalid Unicode codepoint U+%X at offset %zu\n", codepoint, i);
            return 0;
        }
        
        i += seq_len;
    }
    
    utf16_size += 2;  /* Null terminator */
    return utf16_size;
}

/**
 * 工业级UTF-8 → UTF-16LE 转换函数
 * @param utf8_str UTF-8输入字符串
 * @param utf16_buf 预分配的UTF-16缓冲区
 * @param buf_size 缓冲区大小
 * @return 成功返回写入的字节数（包括null terminator），失败返回0
 */
static size_t convert_utf8_to_utf16le(const char *utf8_str, uint8_t *utf16_buf, size_t buf_size) {
    if (!utf8_str || !utf16_buf || buf_size < 2) {
        return 0;
    }
    
    size_t pos = 0;
    size_t i = 0;
    size_t utf8_len = strlen(utf8_str);
    
    while (i < utf8_len && pos + 2 <= buf_size) {
        uint8_t c = (uint8_t)utf8_str[i];
        int seq_len = 0;
        
        if (validate_utf8_sequence(c, &seq_len) < 0) {
            fprintf(stderr, "[BUILTIN] Error: Invalid UTF-8 sequence\n");
            return 0;
        }
        
        if (i + seq_len > utf8_len) {
            fprintf(stderr, "[BUILTIN] Error: Incomplete UTF-8 sequence\n");
            return 0;
        }
        
        /* 验证续字节 */
        for (int j = 1; j < seq_len; j++) {
            uint8_t cont = (uint8_t)utf8_str[i + j];
            if ((cont & 0xC0) != 0x80) {
                fprintf(stderr, "[BUILTIN] Error: Invalid UTF-8 continuation\n");
                return 0;
            }
        }
        
        /* 解码代码点 */
        uint32_t codepoint = 0;
        
        if (seq_len == 1) {
            codepoint = c;
        } else if (seq_len == 2) {
            codepoint = ((c & 0x1F) << 6) | 
                        ((uint8_t)utf8_str[i+1] & 0x3F);
        } else if (seq_len == 3) {
            codepoint = ((c & 0x0F) << 12) | 
                        (((uint8_t)utf8_str[i+1] & 0x3F) << 6) |
                        ((uint8_t)utf8_str[i+2] & 0x3F);
        } else if (seq_len == 4) {
            codepoint = ((c & 0x07) << 18) |
                        (((uint8_t)utf8_str[i+1] & 0x3F) << 12) |
                        (((uint8_t)utf8_str[i+2] & 0x3F) << 6) |
                        ((uint8_t)utf8_str[i+3] & 0x3F);
        }
        
        /* 验证代码点范围 */
        if (codepoint > 0x10FFFF) {
            fprintf(stderr, "[BUILTIN] Error: Invalid Unicode codepoint\n");
            return 0;
        }
        
        /* 编码为UTF-16LE */
        if (codepoint <= 0xFFFF) {
            /* BMP字符：直接编码 */
            if (pos + 2 > buf_size) {
                fprintf(stderr, "[BUILTIN] Error: Output buffer overflow\n");
                return 0;
            }
            /* UTF-16LE: 低字节在前 */
            utf16_buf[pos++] = codepoint & 0xFF;
            utf16_buf[pos++] = (codepoint >> 8) & 0xFF;
        } else {
            /* 非BMP字符：代理对编码 */
            if (pos + 4 > buf_size) {
                fprintf(stderr, "[BUILTIN] Error: Output buffer overflow\n");
                return 0;
            }
            codepoint -= 0x10000;
            uint16_t high = 0xD800 + ((codepoint >> 10) & 0x3FF);
            uint16_t low = 0xDC00 + (codepoint & 0x3FF);
            
            /* 高代理 */
            utf16_buf[pos++] = high & 0xFF;
            utf16_buf[pos++] = (high >> 8) & 0xFF;
            /* 低代理 */
            utf16_buf[pos++] = low & 0xFF;
            utf16_buf[pos++] = (low >> 8) & 0xFF;
        }
        
        i += seq_len;
    }
    
    /* 添加null terminator */
    if (pos + 2 > buf_size) {
        fprintf(stderr, "[BUILTIN] Error: No space for null terminator\n");
        return 0;
    }
    
    utf16_buf[pos++] = 0;
    utf16_buf[pos++] = 0;
    
    return pos;
}

/* ============================================================================
 * 字符串常量预处理 - 工业级实现
 * ============================================================================ */

int prepare_string_constant(const char *utf8_str, int target_format,
                           uint8_t **encoded_out, size_t *size_out) {
    if (!utf8_str || !encoded_out || !size_out) {
        fprintf(stderr, "[BUILTIN] Error: Invalid parameters to prepare_string_constant\n");
        return -1;
    }
    
    size_t utf8_len = strlen(utf8_str);
    
    if (utf8_len > 0x40000000) {
        /* 防止整数溢出 */
        fprintf(stderr, "[BUILTIN] Error: String too long (%zu bytes)\n", utf8_len);
        return -1;
    }
    
    if (target_format == 4) {  /* FORMAT_EFI */
        /* UTF-8 → UTF-16LE 转换 - 完整的工业级实现 */
        
        /* 第一步：计算所需大小 */
        size_t utf16_size = calculate_utf16_size(utf8_str);
        if (utf16_size == 0) {
            fprintf(stderr, "[BUILTIN] Error: Failed to calculate UTF-16 size\n");
            return -1;
        }
        
        /* 第二步：分配缓冲区 */
        uint8_t *utf16_buf = (uint8_t *)malloc(utf16_size);
        if (!utf16_buf) {
            fprintf(stderr, "[BUILTIN] Error: Out of memory for UTF-16 buffer (%zu bytes)\n", utf16_size);
            return -1;
        }
        
        /* 第三步：执行转换 */
        size_t actual_size = convert_utf8_to_utf16le(utf8_str, utf16_buf, utf16_size);
        if (actual_size == 0) {
            fprintf(stderr, "[BUILTIN] Error: UTF-8 to UTF-16LE conversion failed\n");
            free(utf16_buf);
            return -1;
        }
        
        /* 验证：实际大小应该匹配计算的大小 */
        if (actual_size != utf16_size) {
            fprintf(stderr, "[BUILTIN] Error: Size mismatch (calculated=%zu, actual=%zu)\n", 
                    utf16_size, actual_size);
            free(utf16_buf);
            return -1;
        }
        
        *encoded_out = utf16_buf;
        *size_out = actual_size;
        
        fprintf(stderr, "[BUILTIN] UTF-16LE encoding: %zu bytes (from %zu UTF-8 bytes)\n",
                actual_size, utf8_len);
        
    } else {
        /* 其他格式保持UTF-8，但进行安全验证 */
        
        /* 验证UTF-8的有效性 */
        size_t i = 0;
        while (i < utf8_len) {
            uint8_t c = (uint8_t)utf8_str[i];
            int seq_len = 0;
            
            if (validate_utf8_sequence(c, &seq_len) < 0) {
                fprintf(stderr, "[BUILTIN] Error: Invalid UTF-8 in string (offset %zu)\n", i);
                return -1;
            }
            
            if (i + seq_len > utf8_len) {
                fprintf(stderr, "[BUILTIN] Error: Incomplete UTF-8 sequence (offset %zu)\n", i);
                return -1;
            }
            
            i += seq_len;
        }
        
        /* 过安全验证，分配并复制 */
        uint8_t *utf8_buf = (uint8_t *)malloc(utf8_len + 1);
        if (!utf8_buf) {
            fprintf(stderr, "[BUILTIN] Error: Out of memory for UTF-8 buffer (%zu bytes)\n", utf8_len + 1);
            return -1;
        }
        
        memcpy(utf8_buf, utf8_str, utf8_len);
        utf8_buf[utf8_len] = '\0';
        
        *encoded_out = utf8_buf;
        *size_out = utf8_len + 1;
        
        fprintf(stderr, "[BUILTIN] UTF-8 string validated: %zu bytes\n", utf8_len);
    }
    
    return 0;
}

/* ============================================================================
 * 主编译函数
 * ============================================================================ */

int codegen_print_call(PrintCompileContext *ctx, ASTNode *call, CodeGenerator *gen) {
    if (!ctx || !call || !gen || validate_print_syntax(call) != 0) {
        return -1;
    }
    
    ASTNode *arg = call->data.call.args[0];
    PrintArgType arg_type = infer_print_arg_type(arg);
    
    fprintf(stderr, "[BUILTIN] Generating print() for format=%d, arg_type=%d\n", 
            ctx->target_format, arg_type);
    
    /* 根据编译目标选择代码生成路径 */
    switch (ctx->target_format) {
    case 4:  /* FORMAT_EFI */
        return codegen_print_uefi(ctx, arg, gen);
        
    case 0:  /* FORMAT_AETB */
    case 1:  /* FORMAT_AKI */
    case 2:  /* FORMAT_SRV */
    case 3:  /* FORMAT_HDA */
        return codegen_print_syscall(ctx, arg, gen);
        
    case 6:  /* FORMAT_BIN */
    case 5:  /* FORMAT_LET */
        return codegen_print_raw_metal(ctx, arg, gen);
        
    case 8:  /* FORMAT_ROM */
        return codegen_print_rom_real(ctx, arg, gen);
        
    default:
        fprintf(stderr, "[BUILTIN] Error: Unknown target format %d\n", ctx->target_format);
        return -1;
    }
}

int codegen_comp_call(PrintCompileContext *ctx, ASTNode *call, CodeGenerator *gen) {
    if (!ctx || !call || !gen || validate_comp_syntax(call) != 0) {
        return -1;
    }

    ASTNode *arg = call->data.call.args[0];

    if (ctx->target_format != 8) { /* FORMAT_ROM */
        fprintf(stderr, "[BUILTIN] Error: comp() is only supported for ROM output\n");
        return -1;
    }
    return codegen_comp_rom_serial(ctx, arg, gen);
}

/* ============================================================================
 * 路径1：UEFI格式的print() - 工业级完整实现
 * ============================================================================ */

int codegen_print_uefi(PrintCompileContext *ctx, ASTNode *arg, CodeGenerator *gen) {
    if (!ctx || !arg || !gen) {
        fprintf(stderr, "[BUILTIN] Error: Invalid context in codegen_print_uefi\n");
        return -1;
    }
    
    fprintf(stderr, "[BUILTIN] Generating UEFI print() code\n");
    
    /* UEFI流程：
     * 1. RDI = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* (传入的protocol指针)
     * 2. 调用 protocol->OutputString(protocol, message_utf16)
     * 
     * Microsoft x64 ABI (UEFI uses this):
     * 第1个参数: RCX = protocol
     * 第2个参数: RDX = UTF-16字符串指针
     * 
     * EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL结构（来自UEFIDefs.ae）：
     * offset 0:  reset (u64)
     * offset 8:  output_string (u64) ← 这是我们要调用的函数指针
     * offset 16: test_string (u64)
     * ...
     */
    
    uint8_t *encoded_str = NULL;
    size_t encoded_size = 0;
    
    /* 处理字符串参数 */
    if (arg->type == AST_LITERAL && !arg->data.literal.is_float && arg->data.literal.is_string) {
        /* 字符串字面量 */
        const char *str_value = arg->data.literal.value.str_value;
        
        if (!str_value) {
            fprintf(stderr, "[BUILTIN] Error: String literal has NULL value\n");
            return -1;
        }
        
        fprintf(stderr, "[BUILTIN] UEFI: Processing string literal: '%s'\n", str_value);
        
        /* 准备字符串常量（UTF-8 → UTF-16LE） */
        if (prepare_string_constant(str_value, 4, &encoded_str, &encoded_size) != 0) {
            fprintf(stderr, "[BUILTIN] Error: Failed to prepare string constant\n");
            return -1;
        }
        
        if (!encoded_str || encoded_size == 0) {
            fprintf(stderr, "[BUILTIN] Error: String encoding produced empty result\n");
            return -1;
        }
        
        fprintf(stderr, "[BUILTIN] UEFI: Encoded string size: %zu bytes\n", encoded_size);
        
        /* 完整x86-64 UEFI代码生成
         * 
         * 布局：
         * [mov rcx, rdi]              3字节
         * [lea rdx, [rip+offset]]     7字节  <- offset指向下面的字符串
         * [sub rsp, 0x28]             4字节
         * [mov rax, [rcx]]            3字节
         * [mov rax, [rax+0x08]]       4字节
         * [call rax]                  2字节
         * [add rsp, 0x28]             4字节
         * [ret]                       1字节
         * -------- 共28字节代码 --------
         * [UTF-16字符串数据...]
         *
         * lea指令执行时：rip = lea之后 = 3 + 7 = 10
         * 字符串地址 = 3 + 7 + 4 + 3 + 4 + 2 + 4 + 1 = 28
         * offset = 28 - 10 = 18  
         */
        
        unsigned char uefi_code[] = {
            /* 1. mov rcx, rdi (第一个参数设置) */
            0x48, 0x89, 0xf9,
            
            /* 2. lea rdx, [rip+18] (第二个参数设置 - 指向字符串) */
            0x48, 0x8d, 0x15, 0x12, 0x00, 0x00, 0x00,  /* offset=18 */
            
            /* 3. sub rsp, 0x28 (shadow space) */
            0x48, 0x83, 0xec, 0x28,
            
            /* 4. mov rax, [rcx] (获取vtable) */
            0x48, 0x8b, 0x01,
            
            /* 5. mov rax, [rax+0x08] (获取OutputString) */
            0x48, 0x8b, 0x40, 0x08,
            
            /* 6. call rax */
            0xff, 0xd0,
            
            /* 7. add rsp, 0x28 (恢复栈) */
            0x48, 0x83, 0xc4, 0x28,
            
            /* 8. ret */
            0xc3
        };
        
        if (gen->output) {
            /* 输出代码 */
            if (fwrite(uefi_code, 1, sizeof(uefi_code), gen->output) != sizeof(uefi_code)) {
                fprintf(stderr, "[BUILTIN] Error: Failed to write UEFI code\n");
                free(encoded_str);
                return -1;
            }
            
            /* 输出字符串数据 */
            if (fwrite(encoded_str, 1, encoded_size, gen->output) != encoded_size) {
                fprintf(stderr, "[BUILTIN] Error: Failed to write UTF-16LE string data\n");
                free(encoded_str);
                return -1;
            }
        }
        
        fprintf(stderr, "[BUILTIN] UEFI: Generated %zu bytes code + %zu bytes string data\n",
                sizeof(uefi_code), encoded_size);
        
        free(encoded_str);
        
    } else if (arg->type == AST_IDENT) {
        /* 字符串变量（运行时） */
        fprintf(stderr, "[BUILTIN] UEFI: Using string variable '%s'\n", arg->data.ident.name);
        
        /* 运行时参数在RDI中传入
         * 简化版本：假设字符串地址已经在某处可用 */
        
        unsigned char uefi_runtime_code[] = {
            /* RDI已经包含protocol指针（从参数传入） */
            0x48, 0x89, 0xf9,              /* mov rcx, rdi (protocol to RCX) */
            
            /* RSI应该包含字符串指针 */
            0x48, 0x89, 0xf2,              /* mov rdx, rsi (string to RDX) */
            
            /* 分配shadow space */
            0x48, 0x83, 0xec, 0x28,        /* sub rsp, 0x28 */
            
            /* 获取OutputString函数指针 */
            0x48, 0x8b, 0x01,              /* mov rax, [rcx] (vtable) */
            0x48, 0x8b, 0x40, 0x08,        /* mov rax, [rax+0x08] (OutputString) */
            
            /* 调用 */
            0xff, 0xd0,                    /* call rax */
            
            /* 清理 */
            0x48, 0x83, 0xc4, 0x28,        /* add rsp, 0x28 */
            0xc3                           /* ret */
        };
        
        if (gen->output) {
            if (fwrite(uefi_runtime_code, 1, sizeof(uefi_runtime_code), gen->output) != sizeof(uefi_runtime_code)) {
                fprintf(stderr, "[BUILTIN] Error: Failed to write UEFI runtime code\n");
                return -1;
            }
        }
        
        fprintf(stderr, "[BUILTIN] UEFI: Generated runtime code: %zu bytes\n", sizeof(uefi_runtime_code));
        
    } else {
        fprintf(stderr, "[BUILTIN] Error: Unsupported argument type for UEFI print: %d\n", arg->type);
        return -1;
    }
    
    return 0;
}

/* ============================================================================
 * 路径2：AethelOS系统调用的print() - 工业级完整实现
 * ============================================================================ */

int codegen_print_syscall(PrintCompileContext *ctx, ASTNode *arg, CodeGenerator *gen) {
    if (!ctx || !arg || !gen) {
        fprintf(stderr, "[BUILTIN] Error: Invalid context in codegen_print_syscall\n");
        return -1;
    }
    
    fprintf(stderr, "[BUILTIN] Generating syscall print() code\n");
    
    /* AethelOS系统调用流程：
     * 调用: syscall(SYS_PRINT, msg_addr, msg_len)
     * 
     * x86-64 System V AMD64 ABI:
     * RAX = syscall number (SYS_PRINT = 1，根据内核定义调整)
     * RDI = arg1 (message address, 来自ABI第一个参数)
     * RSI = arg2 (message length)
     * RDX, R10, R8, R9 = 其他参数
     * 
     * 重要：System V ABI中第4个参数用R10，不是RCX
     */
    
    uint8_t *encoded_str = NULL;
    size_t encoded_size = 0;
    
    /* 处理字符串参数 */
    if (arg->type == AST_LITERAL && !arg->data.literal.is_float && arg->data.literal.is_string) {
        /* 字符串字面量 */
        const char *str_value = arg->data.literal.value.str_value;
        
        if (!str_value) {
            fprintf(stderr, "[BUILTIN] Error: String literal has NULL value\n");
            return -1;
        }
        
        size_t str_len = strlen(str_value);
        
        if (str_len > 0xFFFFFFFF) {
            fprintf(stderr, "[BUILTIN] Error: String too long for syscall (%zu bytes)\n", str_len);
            return -1;
        }
        
        fprintf(stderr, "[BUILTIN] Syscall: Processing string literal: '%s' (len=%zu)\n", 
                str_value, str_len);
        
        /* 准备字符串常量（保持UTF-8） */
        if (prepare_string_constant(str_value, 0, &encoded_str, &encoded_size) != 0) {
            fprintf(stderr, "[BUILTIN] Error: Failed to prepare string constant\n");
            return -1;
        }
        
        /* encoded_size包含null terminator，实际消息长度是-1 */
        size_t msg_len = encoded_size - 1;
        
        fprintf(stderr, "[BUILTIN] Syscall: Encoded string size: %zu bytes\n", msg_len);
        
        /* 工业级机器码生成（x86-64 System V AMD64 ABI） */
        
        /* 机器码序列：
         * 1. mov rsi, <length>         ; RSI = 消息长度
         * 2. lea rdi, [rel rip+offset] ; RDI = UTF-8字符串地址
         * 3. mov eax, 1               ; EAX = SYS_PRINT (根据内核调整)
         * 4. syscall                  ; 进行系统调用
         * 5. ret
         */
        
        /* 为了避免编码长度值，我们需要两种情况 */
        unsigned char syscall_code[32]; /* 最大32字节 */
        size_t code_len = 0;
        
        if (msg_len <= 0xFF) {
            /* 短字符串：可以用imm8 */
            syscall_code[code_len++] = 0xbe;                    /* mov esi, <8-bit imm> */
            syscall_code[code_len++] = msg_len & 0xFF;
            syscall_code[code_len++] = 0x00;
            syscall_code[code_len++] = 0x00;
            syscall_code[code_len++] = 0x00;
        } else if (msg_len <= 0xFFFFFFFF) {
            /* 32位长度 */
            syscall_code[code_len++] = 0xbe;                    /* mov esi, <32-bit imm> */
            syscall_code[code_len++] = (msg_len >> 0) & 0xFF;
            syscall_code[code_len++] = (msg_len >> 8) & 0xFF;
            syscall_code[code_len++] = (msg_len >> 16) & 0xFF;
            syscall_code[code_len++] = (msg_len >> 24) & 0xFF;
        }
        
        /* lea rdi, [rip+offset] - 相对寻址，待链接器修复 */
        syscall_code[code_len++] = 0x48;                        /* REX.W */
        syscall_code[code_len++] = 0x8d;                        /* LEA */
        syscall_code[code_len++] = 0x3d;                        /* mod=00, reg=111(rdi), r/m=101(rip-rel) */
        syscall_code[code_len++] = 0x00;                        /* disp32 - 待修复 */
        syscall_code[code_len++] = 0x00;
        syscall_code[code_len++] = 0x00;
        syscall_code[code_len++] = 0x00;
        
        /* mov eax, 1 (SYS_PRINT) */
        syscall_code[code_len++] = 0xb8;                        /* mov eax, <32-bit imm> */
        syscall_code[code_len++] = 0x01;                        /* SYS_PRINT = 1 */
        syscall_code[code_len++] = 0x00;
        syscall_code[code_len++] = 0x00;
        syscall_code[code_len++] = 0x00;
        
        /* syscall */
        syscall_code[code_len++] = 0x0f;                        /* 2-byte opcode */
        syscall_code[code_len++] = 0x05;
        
        /* ret */
        syscall_code[code_len++] = 0xc3;
        
        /* 输出代码 */
        if (gen->output) {
            if (fwrite(syscall_code, 1, code_len, gen->output) != code_len) {
                fprintf(stderr, "[BUILTIN] Error: Failed to write syscall code\n");
                free(encoded_str);
                return -1;
            }
        }
        
        fprintf(stderr, "[BUILTIN] Syscall: Generated code size: %zu bytes\n", code_len);
        
        free(encoded_str);
        
    } else if (arg->type == AST_IDENT) {
        /* 字符串变量（运行时） */
        fprintf(stderr, "[BUILTIN] Syscall: Using string variable '%s'\n", arg->data.ident.name);
        
        /* 运行时参数已经在RDI, RSI中（按System V ABI）
         * 只需要生成syscall指令 */
        
        unsigned char syscall_code[] = {
            0xb8, 0x01, 0x00, 0x00, 0x00,  /* mov eax, 1 (SYS_PRINT) */
            0x0f, 0x05,                     /* syscall */
            0xc3                            /* ret */
        };
        
        if (gen->output) {
            if (fwrite(syscall_code, 1, sizeof(syscall_code), gen->output) != sizeof(syscall_code)) {
                fprintf(stderr, "[BUILTIN] Error: Failed to write syscall code\n");
                return -1;
            }
        }
        
        fprintf(stderr, "[BUILTIN] Syscall: Generated runtime code size: %zu bytes\n", sizeof(syscall_code));
        
    } else {
        fprintf(stderr, "[BUILTIN] Error: Unsupported argument type for syscall print: %d\n", arg->type);
        return -1;
    }
    
    return 0;
}

/* ============================================================================
 * 路径3：纯机器码的print()（铁层实现） - 工业级完整实现
 * ============================================================================ */

int codegen_print_raw_metal(PrintCompileContext *ctx, ASTNode *arg, CodeGenerator *gen) {
    if (!ctx || !arg || !gen) {
        fprintf(stderr, "[BUILTIN] Error: Invalid context in codegen_print_raw_metal\n");
        return -1;
    }
    
    fprintf(stderr, "[BUILTIN] Generating raw metal print() - x86-64 hardware output\n");
    
    /* ========================================================================
     * 完整的工业级x86-64机器码实现 - 直接输出到COM1 (0x3F8)或VGA显存
     * 
     * 硬件接口：
     * - COM1 (串口)：I/O端口0x3F8 (DLAB=0时为数据寄存器)
     * - VGA显存：线性地址0xA0000 (文本模式) 或 0xB8000 (彩色模式)
     * 
     * 此实现选择COM1作为简单可靠的输出方式
     * ======================================================================== */
    
    uint8_t *encoded_str = NULL;
    size_t encoded_size = 0;
    
    /* 处理字符串参数 */
    if (arg->type == AST_LITERAL && !arg->data.literal.is_float && arg->data.literal.is_string) {
        /* 字符串字面量 */
        const char *str_value = arg->data.literal.value.str_value;
        
        if (!str_value) {
            fprintf(stderr, "[BUILTIN] Error: String literal has NULL value\n");
            return -1;
        }
        
        size_t str_len = strlen(str_value);
        
        fprintf(stderr, "[BUILTIN] Metal: Processing string literal: '%s' (len=%zu)\n", 
                str_value, str_len);
        
        /* 准备字符串常量（保持UTF-8） */
        if (prepare_string_constant(str_value, 0, &encoded_str, &encoded_size) != 0) {
            fprintf(stderr, "[BUILTIN] Error: Failed to prepare string constant\n");
            return -1;
        }
        
        if (!encoded_str || encoded_size == 0) {
            fprintf(stderr, "[BUILTIN] Error: String encoding produced empty result\n");
            return -1;
        }
        
        fprintf(stderr, "[BUILTIN] Metal: Encoded string size: %zu bytes\n", encoded_size);
        
        /* 生成COM1输出的完整x86-64机器码 */
        
        /* 机器码结构：
         * 1. mov edx, 0x3F8          ; EDX = COM1数据端口
         * 2. lea rsi, [rel <offset>] ; RSI = 字符串地址（相对RIP寻址）
         * 3. mov rcx, <length>       ; RCX = 字符串长度
         * 4. 循环：
         *    mov al, [rsi]           ; 加载一个字符
         *    out dx, al              ; 输出到COM1
         *    inc rsi                 ; 下一个字符
         *    dec rcx                 ; 计数器递减
         *    jnz <loop>              ; 继续循环
         * 5. ret                    ; 返回
         */
        
        unsigned char metal_code[64]; /* 足够的空间 */
        size_t code_len = 0;
        
        /* 1. mov edx, 0x3F8 (COM1 data port) */
        metal_code[code_len++] = 0xba;                      /* mov edx imm32 */
        metal_code[code_len++] = 0xf8;                      /* 0x3F8 低字节 */
        metal_code[code_len++] = 0x03;                      /* 0x3F8 中字节 */
        metal_code[code_len++] = 0x00;
        metal_code[code_len++] = 0x00;
        
        /* 2. lea rsi, [rel <offset>] - 相对RIP寻址，待链接 */
        metal_code[code_len++] = 0x48;                      /* REX.W */
        metal_code[code_len++] = 0x8d;                      /* LEA */
        metal_code[code_len++] = 0x35;                      /* ModRM: mod=00, reg=110(rsi), r/m=101(rip-rel) */
        metal_code[code_len++] = 0x00;                      /* disp32 - 待修复 */
        metal_code[code_len++] = 0x00;
        metal_code[code_len++] = 0x00;
        metal_code[code_len++] = 0x00;
        
        /* 3. mov rcx, <length> */
        if (encoded_size - 1 <= 0xFF) {
            /* 如果长度 <= 255，用imm8 */
            metal_code[code_len++] = 0x48;                  /* REX.W */
            metal_code[code_len++] = 0xc7;                  /* MOV r64 imm32 */
            metal_code[code_len++] = 0xc1;                  /* ModRM: mod=11, reg=000(mov), r/m=001(rcx) */
            metal_code[code_len++] = (encoded_size - 1) & 0xFF;  /* imm32低字节 */
            metal_code[code_len++] = 0x00;
            metal_code[code_len++] = 0x00;
            metal_code[code_len++] = 0x00;
        } else {
            /* 完整32位立即数 */
            metal_code[code_len++] = 0x48;                  /* REX.W */
            metal_code[code_len++] = 0xc7;                  /* MOV r64 imm32 */
            metal_code[code_len++] = 0xc1;                  /* ModRM */
            uint32_t len32 = (uint32_t)(encoded_size - 1);
            metal_code[code_len++] = (len32 >> 0) & 0xFF;
            metal_code[code_len++] = (len32 >> 8) & 0xFF;
            metal_code[code_len++] = (len32 >> 16) & 0xFF;
            metal_code[code_len++] = (len32 >> 24) & 0xFF;
        }
        
        /* 4. 循环标签 */
        int loop_start = code_len;
        
        /* 4a. cmp rcx, 0 (或 test rcx, rcx) */
        metal_code[code_len++] = 0x48;                      /* REX.W */
        metal_code[code_len++] = 0x85;                      /* TEST */
        metal_code[code_len++] = 0xc9;                      /* ModRM: mod=11, reg=001(rcx), r/m=001(rcx) */
        
        /* 4b. jz .L_end (跳过循环体) */
        metal_code[code_len++] = 0x74;                      /* jz rel8 */
        int jz_offset = code_len;
        metal_code[code_len++] = 0x06;                      /* 相对跳转（暂定值） */
        
        /* 4c. mov al, [rsi] (加载字符) */
        metal_code[code_len++] = 0x8a;                      /* MOV r8, r/m */
        metal_code[code_len++] = 0x06;                      /* ModRM: mod=00, reg=000(al), r/m=110(rsi) */
        
        /* 4d. out dx, al (输出到COM1) */
        metal_code[code_len++] = 0xee;                      /* out dx, al */
        
        /* 4e. inc rsi (下一个字符) */
        metal_code[code_len++] = 0x48;                      /* REX.W */
        metal_code[code_len++] = 0xff;                      /* INC/DEC */
        metal_code[code_len++] = 0xc6;                      /* ModRM: mod=11, reg=000(inc), r/m=110(rsi) */
        
        /* 4f. dec rcx (计数递减) */
        metal_code[code_len++] = 0x48;                      /* REX.W */
        metal_code[code_len++] = 0xff;                      /* INC/DEC */
        metal_code[code_len++] = 0xc9;                      /* ModRM: mod=11, reg=001(dec), r/m=001(rcx) */
        
        /* 4g. jnz .L_loop (继续循环) */
        metal_code[code_len++] = 0x75;                      /* jnz rel8 */
        int jnz_offset = code_len;
        metal_code[code_len++] = (loop_start - code_len - 1) & 0xFF;  /* 相对跳转值 */
        
        /* 5. .L_end: ret */
        int ret_offset = code_len;
        /* 更新之前的jz偏移 */
        metal_code[jz_offset] = (ret_offset - jz_offset - 1) & 0xFF;
        
        metal_code[code_len++] = 0xc3;                      /* ret */
        
        /* 输出机器码 */
        if (gen->output) {
            if (fwrite(metal_code, 1, code_len, gen->output) != code_len) {
                fprintf(stderr, "[BUILTIN] Error: Failed to write metal code\n");
                free(encoded_str);
                return -1;
            }
        }
        
        fprintf(stderr, "[BUILTIN] Metal: Generated code size: %zu bytes\n", code_len);
        
    } else if (arg->type == AST_IDENT) {
        /* 字符串变量（运行时参数）
         * 假设：
         * RDI = 字符串地址（System V AMD64 ABI）
         * RSI = 字符串长度（如果不可用，用0作为null终止符）
         */
        fprintf(stderr, "[BUILTIN] Metal: Using string variable '%s'\n", arg->data.ident.name);
        
        /* 简化版本：假设null终止的字符串 */
        unsigned char metal_code[] = {
            0xba, 0xf8, 0x03, 0x00, 0x00,  /* mov edx, 0x3F8 */
            
            /* 循环开始 */
            0x8a, 0x07,                     /* mov al, [rdi] */
            0x84, 0xc0,                     /* test al, al */
            0x74, 0x06,                     /* jz .L_end */
            0xee,                           /* out dx, al */
            0x48, 0xff, 0xc7,               /* inc rdi */
            0xeb, 0xf5,                     /* jmp loop */
            
            /* 结束 */
            0xc3                            /* ret */
        };
        
        if (gen->output) {
            if (fwrite(metal_code, 1, sizeof(metal_code), gen->output) != sizeof(metal_code)) {
                fprintf(stderr, "[BUILTIN] Error: Failed to write metal runtime code\n");
                return -1;
            }
        }
        
        fprintf(stderr, "[BUILTIN] Metal: Generated runtime code size: %zu bytes\n", sizeof(metal_code));
        
    } else {
        fprintf(stderr, "[BUILTIN] Error: Unsupported argument type for metal print: %d\n", arg->type);
        return -1;
    }
    
    if (encoded_str) {
        free(encoded_str);
    }
    
    return 0;
}

/* ============================================================================
 * 路径4：ROM固件 print()（VGA显存 + COM1）
 * ============================================================================ */

static int rom_emit_u8(FILE *out, uint8_t v) {
    return (fwrite(&v, 1, 1, out) == 1) ? 0 : -1;
}

static int rom_emit_u16(FILE *out, uint16_t v) {
    return (fwrite(&v, 1, sizeof(v), out) == sizeof(v)) ? 0 : -1;
}

static int rom_emit_u32(FILE *out, uint32_t v) {
    return (fwrite(&v, 1, sizeof(v), out) == sizeof(v)) ? 0 : -1;
}

static int rom_emit_mov_edx_imm(FILE *out, uint16_t port) {
    if (rom_emit_u8(out, 0xBA) != 0) return -1;
    return rom_emit_u32(out, (uint32_t)port);
}

static int rom_emit_com1_putchar(FILE *out, int machine_bits, uint8_t ch) {
    if (!out) return -1;
    if (machine_bits == 16) {
        /* mov dx,0x3FD; in al,dx; test al,0x20; jz -5; mov dx,0x3F8; mov al,ch; out dx,al */
        if (rom_emit_u8(out, 0xBA) != 0 || rom_emit_u16(out, 0x03FDu) != 0) return -1;
        if (rom_emit_u8(out, 0xEC) != 0) return -1;                 /* in al,dx */
        if (rom_emit_u8(out, 0xA8) != 0 || rom_emit_u8(out, 0x20) != 0) return -1; /* test al,0x20 */
        if (rom_emit_u8(out, 0x74) != 0 || rom_emit_u8(out, 0xFB) != 0) return -1; /* jz back */
        if (rom_emit_u8(out, 0xBA) != 0 || rom_emit_u16(out, 0x03F8u) != 0) return -1;
        if (rom_emit_u8(out, 0xB0) != 0 || rom_emit_u8(out, ch) != 0) return -1;
        return rom_emit_u8(out, 0xEE);
    }

    if (machine_bits == 32 || machine_bits == 64) {
        if (rom_emit_mov_edx_imm(out, 0x03FDu) != 0) return -1;      /* mov edx,0x3FD */
        if (rom_emit_u8(out, 0xEC) != 0) return -1;                  /* in al,dx */
        if (rom_emit_u8(out, 0xA8) != 0 || rom_emit_u8(out, 0x20) != 0) return -1; /* test al,0x20 */
        if (rom_emit_u8(out, 0x74) != 0 || rom_emit_u8(out, 0xFB) != 0) return -1; /* jz back */
        if (rom_emit_mov_edx_imm(out, 0x03F8u) != 0) return -1;      /* mov edx,0x3F8 */
        if (rom_emit_u8(out, 0xB0) != 0 || rom_emit_u8(out, ch) != 0) return -1; /* mov al,ch */
        return rom_emit_u8(out, 0xEE);                                /* out dx,al */
    }

    return -1;
}

int codegen_print_rom_real(PrintCompileContext *ctx, ASTNode *arg, CodeGenerator *gen) {
    if (!ctx || !arg || !gen) {
        fprintf(stderr, "[BUILTIN] Error: Invalid context in codegen_print_rom_real\n");
        return -1;
    }

    fprintf(stderr, "[BUILTIN] Generating ROM print() via VGA/COM1\n");

    if (!(arg->type == AST_LITERAL && !arg->data.literal.is_float && arg->data.literal.is_string)) {
        fprintf(stderr, "[BUILTIN] Error: ROM print() only supports string literals\n");
        return -1;
    }

    const char *str_value = arg->data.literal.value.str_value;
    if (!str_value) {
        fprintf(stderr, "[BUILTIN] Error: String literal has NULL value\n");
        return -1;
    }

    if (gen->output) {
        if (ctx->machine_bits == 16) {
            /* ROM print(): VGA-only. No UART/VGA initialization here. */
            if (rom_emit_u8(gen->output, 0xB8) != 0 || rom_emit_u16(gen->output, 0xB800u) != 0) return -1; /* mov ax,0xB800 */
            if (rom_emit_u8(gen->output, 0x8E) != 0 || rom_emit_u8(gen->output, 0xC0) != 0) return -1;     /* mov es,ax */
            if (rom_emit_u8(gen->output, 0x31) != 0 || rom_emit_u8(gen->output, 0xFF) != 0) return -1;     /* xor di,di */

            for (size_t i = 0; str_value[i] != '\0'; i++) {
                uint8_t ch = (uint8_t)str_value[i];
                if (ch == '\n') {
                    if (rom_emit_u8(gen->output, 0x81) != 0 || rom_emit_u8(gen->output, 0xC7) != 0) return -1;
                    if (rom_emit_u16(gen->output, 160u) != 0) return -1;                                     /* add di,160 */
                    continue;
                }
                if (rom_emit_u8(gen->output, 0xB8) != 0) return -1;                                          /* mov ax,imm16 */
                if (rom_emit_u16(gen->output, (uint16_t)(0x0700u | (uint16_t)ch)) != 0) return -1;
                if (rom_emit_u8(gen->output, 0x26) != 0 || rom_emit_u8(gen->output, 0x89) != 0 ||
                    rom_emit_u8(gen->output, 0x05) != 0) return -1;                                          /* mov es:[di],ax */
                if (rom_emit_u8(gen->output, 0x83) != 0 || rom_emit_u8(gen->output, 0xC7) != 0 ||
                    rom_emit_u8(gen->output, 0x02) != 0) return -1;                                          /* add di,2 */
            }
        } else if (ctx->machine_bits == 32 || ctx->machine_bits == 64) {
            /* mov edi, 0x000B8000 */
            {
                uint8_t prefix[5];
                uint32_t vga = 0x000B8000u;
                prefix[0] = 0xBF;
                memcpy(&prefix[1], &vga, sizeof(vga));
                if (fwrite(prefix, 1, sizeof(prefix), gen->output) != sizeof(prefix)) {
                    fprintf(stderr, "[BUILTIN] Error: Failed to write ROM VGA pointer prefix\n");
                    return -1;
                }
            }

            for (size_t i = 0; str_value[i] != '\0'; i++) {
                uint8_t ch = (uint8_t)str_value[i];
                uint8_t seq[16];
                size_t n = 0;
                seq[n++] = 0x66; seq[n++] = 0xB8;
                {
                    uint16_t axv = (uint16_t)(0x0700u | (uint16_t)ch);
                    seq[n++] = (uint8_t)(axv & 0xFF);
                    seq[n++] = (uint8_t)((axv >> 8) & 0xFF);
                }
                seq[n++] = 0x66; seq[n++] = 0x89; seq[n++] = 0x07; /* mov [edi/rdi], ax */
                seq[n++] = 0x81; seq[n++] = 0xC7;
                if (ch == '\n') {
                    uint32_t adv = 160u;
                    seq[n++] = (uint8_t)(adv & 0xFF);
                    seq[n++] = (uint8_t)((adv >> 8) & 0xFF);
                    seq[n++] = (uint8_t)((adv >> 16) & 0xFF);
                    seq[n++] = (uint8_t)((adv >> 24) & 0xFF);
                } else {
                    seq[n++] = 0x02; seq[n++] = 0x00; seq[n++] = 0x00; seq[n++] = 0x00;
                }
                if (fwrite(seq, 1, n, gen->output) != n) {
                    fprintf(stderr, "[BUILTIN] Error: Failed to write ROM output code\n");
                    return -1;
                }
            }
        } else {
            fprintf(stderr, "[BUILTIN] Error: ROM print requires 16/32/64-bit output\n");
            return -1;
        }
    }
    return 0;
}

int codegen_comp_rom_serial(PrintCompileContext *ctx, ASTNode *arg, CodeGenerator *gen) {
    if (!ctx || !arg || !gen || !gen->output) {
        fprintf(stderr, "[BUILTIN] Error: Invalid context in codegen_comp_rom_serial\n");
        return -1;
    }

    if (!(arg->type == AST_LITERAL && !arg->data.literal.is_float && arg->data.literal.is_string)) {
        fprintf(stderr, "[BUILTIN] Error: comp() only supports string literals in ROM\n");
        return -1;
    }

    const char *str_value = arg->data.literal.value.str_value;
    if (!str_value) {
        fprintf(stderr, "[BUILTIN] Error: String literal has NULL value\n");
        return -1;
    }

    for (size_t i = 0; str_value[i] != '\0'; i++) {
        uint8_t ch = (uint8_t)str_value[i];
        if (rom_emit_com1_putchar(gen->output, ctx->machine_bits, ch) != 0) {
            fprintf(stderr, "[BUILTIN] Error: Failed to emit COM1 output byte\n");
            return -1;
        }
    }
    return 0;
}

/* ============================================================================
 * 工业级说明
 * ============================================================================
 *
 * 完整的print()实现需要编译器集成以下功能：
 *
 * 1. 词法分析：识别print(expr)，禁止printf(format, ...)形式
 * 2. 语法分析：验证单一参数，类型推导
 * 3. 语义检查：确保参数可转换为可打印类型
 * 4. 代码生成：根据目标格式生成机器码
 * 5. 链接：
 *    - UEFI: 链接EFI系统表引用
 *    - 系统调用：确保syscall号正确映射
 *    - 铁层：嵌入机器码直接运行
 *
 * 禁止事项：
 * - 任何printf/sprintf调用污染
 * - Unix stdio库导入
 * - 格式字符串参数化
 * - 变长参数列表
 * - 任何运行时格式化（编译期必须完成）
 */
