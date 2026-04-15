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
 * AethelOS Aethelium Compiler - Zero-Copy View Code Generation Implementation
 * 零拷贝视图代码生成实现
 * 
 * 版本：1.0 工业级
 * 
 * 核心机制：
 * view<T>将任意内存区域直接解释为T类型，不发生数据拷贝。
 * 编译器生成的机器码简单地将指针存储到目标寄存器，后续访问直接从该地址读取。
 */

#include "zero_copy_view.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* =====================================================================
 * 内部辅助函数
 * ===================================================================== */

/**
 * 从类型字符串提取基础类型名
 * 例如 "view<PacketHeader>" -> "PacketHeader"
 * 或   "view<[UInt8]>" -> "[UInt8]"
 */
static char* extract_view_base_type(const char *view_type) {
    if (!view_type) return NULL;
    
    /* 查找 < 和 > */
    const char *start = strchr(view_type, '<');
    const char *end = strchr(view_type, '>');
    
    if (!start || !end || start >= end) {
        return NULL;
    }
    
    size_t len = end - start - 1;
    char *result = malloc(len + 1);
    if (result) {
        strncpy(result, start + 1, len);
        result[len] = '\0';
    }
    return result;
}

/**
 * 检查类型是否为数组类型
 * 例如 "[UInt8]" 返回1
 */
static int is_array_type(const char *type) {
    if (!type || type[0] != '[') return 0;
    const char *end = strchr(type, ']');
    return (end != NULL);
}

/**
 * 从数组类型提取元素类型
 * 例如 "[UInt8]" -> "UInt8"
 */
static char* extract_array_element_type(const char *array_type) {
    if (!array_type || array_type[0] != '[') return NULL;
    
    const char *end = strchr(array_type, ']');
    if (!end) return NULL;
    
    size_t len = end - array_type - 1;
    char *result = malloc(len + 1);
    if (result) {
        strncpy(result, array_type + 1, len);
        result[len] = '\0';
    }
    return result;
}

/**
 * 获取类型的大小（字节）
 * 支持基础类型、UInt/Int类型、PhysAddr/VirtAddr等
 */
static uint32_t get_type_size(const char *type) {
    if (!type) return 0;
    
    if (strcmp(type, "UInt8") == 0 || strcmp(type, "Int8") == 0) return 1;
    if (strcmp(type, "UInt16") == 0 || strcmp(type, "Int16") == 0) return 2;
    if (strcmp(type, "UInt32") == 0 || strcmp(type, "Int32") == 0 || strcmp(type, "Float") == 0) return 4;
    if (strcmp(type, "UInt64") == 0 || strcmp(type, "Int64") == 0 || strcmp(type, "Double") == 0) return 8;
    if (strcmp(type, "UInt") == 0 || strcmp(type, "Int") == 0) return 8;  /* 默认64位 */
    if (strcmp(type, "PhysAddr") == 0 || strcmp(type, "VirtAddr") == 0) return 8;
    if (strcmp(type, "Bool") == 0) return 1;
    
    /* 结构体类型：无法在这里静态确定，需要符号表 */
    return 0;
}

/**
 * 获取类型的对齐要求（字节）
 */
static uint32_t get_type_alignment(const char *type) {
    if (!type) return 1;
    
    uint32_t size = get_type_size(type);
    if (size == 0) return 1;  /* 保守估计 */
    if (size > 8) return 8;   /* x86-64中最大对齐 */
    return size;
}

static ZeroCopyViewInfo* zcv_parse_view_type_name(const char *type_name) {
    ASTNode stub;
    memset(&stub, 0, sizeof(stub));
    stub.type = AST_TYPE;
    stub.data.type.name = (char *)type_name;
    return zcv_parse_view_type(&stub);
}

/* =====================================================================
 * 公开函数实现
 * ===================================================================== */

ZeroCopyViewInfo* zcv_parse_view_type(ASTNode *type_node) {
    if (!type_node || type_node->type != AST_TYPE) {
        return NULL;
    }
    
    const char *type_name = type_node->data.type.name;
    if (!type_name || strncmp(type_name, "view<", 5) != 0) {
        return NULL;
    }
    
    ZeroCopyViewInfo *info = malloc(sizeof(ZeroCopyViewInfo));
    if (!info) return NULL;
    
    memset(info, 0, sizeof(*info));
    
    /* 提取基础类型 */
    info->base_type_name = extract_view_base_type(type_name);
    if (!info->base_type_name) {
        free(info);
        return NULL;
    }
    
    /* 检查是否为数组视图 */
    info->is_array = is_array_type(info->base_type_name);
    
    if (info->is_array) {
        /* 数组视图：view<[T]> */
        info->view_type = VIEW_TYPE_ARRAY;
        info->element_type = extract_array_element_type(info->base_type_name);
        info->element_size = get_type_size(info->element_type);
        info->base_type_size = 0;  /* 数组大小运行时决定 */
        info->requires_bounds_check = 1;  /* 数组访问需要边界检查 */
    } else {
        /* 结构体或原始类型视图：view<T> */
        info->view_type = (get_type_size(info->base_type_name) > 0) ? 
                         VIEW_TYPE_PRIMITIVE : VIEW_TYPE_STRUCT;
        info->base_type_size = get_type_size(info->base_type_name);
        info->element_type = NULL;
        info->requires_bounds_check = 0;
    }
    
    /* 获取对齐要求 */
    if (info->element_type) {
        info->alignment = get_type_alignment(info->element_type);
    } else {
        info->alignment = get_type_alignment(info->base_type_name);
    }
    
    return info;
}

void zcv_free_view_info(ZeroCopyViewInfo *info) {
    if (!info) return;
    free(info->base_type_name);
    free(info->element_type);
    free(info);
}

int zcv_validate_view_type(const char *view_type_name) {
    if (!view_type_name || strncmp(view_type_name, "view<", 5) != 0) {
        return -1;
    }
    
    /* 检查< 和 >是否配对 */
    const char *start = strchr(view_type_name, '<');
    const char *end = strchr(view_type_name, '>');
    
    if (!start || !end || start >= end) {
        return -1;
    }
    
    /* 提取类型，确保不为空 */
    size_t len = end - start - 1;
    if (len == 0) {
        return -1;  /* 空类型 */
    }
    
    return 0;  /* 合法 */
}

size_t zcv_gen_view_cast(FILE *out,
                        const char *source_ptr_reg,
                        const char *target_type,
                        const char *view_var_name,
                        const char *target_isa) {
    const char *isa;
    ZeroCopyViewInfo *info;

    if (!out || !source_ptr_reg || !target_type || !view_var_name) {
        return 0;
    }

    isa = (target_isa && *target_isa) ? target_isa : "x86_64";
    size_t code_size = 0;

    fprintf(out, "    /* Zero-Copy View Cast [%s]: %s -> %s */\n", isa, source_ptr_reg, target_type);

    /* 验证view类型有效 */
    if (zcv_validate_view_type(target_type) != 0) {
        fprintf(out, "    /* ERROR: Invalid view type %s */\n", target_type);
        return 0;
    }

    if (strcmp(isa, "aarch64") == 0) {
        if (strcmp(source_ptr_reg, "x0") != 0) {
            fprintf(out, "    mov x0, %s\n", source_ptr_reg);
            code_size += 4;
        }
        fprintf(out, "    /* Store view pointer (zero-copy, no data movement) */\n");
        fprintf(out, "    str x0, [x29, #-8]\n");
        code_size += 4;
    } else {
        if (strcmp(source_ptr_reg, "rax") != 0) {
            fprintf(out, "    mov %s, %%rax\n", source_ptr_reg);
            code_size += 3;
        }
        fprintf(out, "    /* Store view pointer (zero-copy, no data movement) */\n");
        fprintf(out, "    mov %%rax, -8(%%rbp)   /* view at -8(rbp) */\n");
        code_size += 3;
    }

    info = zcv_parse_view_type_name(target_type);
    if (info && info->alignment > 1) {
        fprintf(out, "    /* Alignment check: %u-byte alignment */\n", info->alignment);
        if (strcmp(isa, "aarch64") == 0) {
            fprintf(out, "    and x9, x0, #%u\n", info->alignment - 1);
            fprintf(out, "    cbz x9, .view_aligned_%s\n", view_var_name);
            fprintf(out, "    /* Misaligned: would generate trap */\n");
            fprintf(out, ".view_aligned_%s:\n", view_var_name);
        } else {
            fprintf(out, "    mov %%rax, %%rcx\n");
            fprintf(out, "    and $%u, %%rcx\n", info->alignment - 1);
            fprintf(out, "    test %%rcx, %%rcx\n");
            fprintf(out, "    jz .view_aligned_%s\n", view_var_name);
            fprintf(out, "    /* Misaligned: would generate trap */\n");
            fprintf(out, ".view_aligned_%s:\n", view_var_name);
        }
        code_size += 20;  /* 粗略估计 */
        zcv_free_view_info(info);
    }

    fprintf(out, "    /* View cast complete: pointer reinterpreted as %s */\n", target_type);
    return code_size;
}

size_t zcv_gen_view_member_access(FILE *out,
                                 const char *view_var_reg,
                                 const char *member_name,
                                 uint32_t member_offset,
                                 uint8_t member_size,
                                 const char *target_reg,
                                 const char *target_isa) {
    if (!out || !view_var_reg || !member_name || !target_reg) {
        return 0;
    }

    size_t code_size = 0;

    fprintf(out, "    /* View member access [%s]: %s\\%s at offset %u */\n",
           (target_isa && *target_isa) ? target_isa : "x86_64", view_var_reg, member_name, member_offset);

    if (target_isa && strcmp(target_isa, "aarch64") == 0) {
        switch (member_size) {
            case 1:
                fprintf(out, "    ldrb %s, [%s, #%u]\n", target_reg, view_var_reg, member_offset);
                return code_size + 4;
            case 2:
                fprintf(out, "    ldrh %s, [%s, #%u]\n", target_reg, view_var_reg, member_offset);
                return code_size + 4;
            case 4:
                fprintf(out, "    ldr w0, [%s, #%u]\n", view_var_reg, member_offset);
                return code_size + 4;
            case 8:
                fprintf(out, "    ldr %s, [%s, #%u]\n", target_reg, view_var_reg, member_offset);
                return code_size + 4;
            default:
                fprintf(out, "    /* Unsupported member size: %u */\n", member_size);
                return 0;
        }
    }

    switch (member_size) {
        case 1:
            fprintf(out, "    movzbl %u(%s), %s\n", member_offset, view_var_reg, target_reg);
            code_size += 4;
            break;
        case 2:
            fprintf(out, "    movzwl %u(%s), %s\n", member_offset, view_var_reg, target_reg);
            code_size += 4;
            break;
        case 4:
            fprintf(out, "    movl %u(%s), %s\n", member_offset, view_var_reg, target_reg);
            code_size += 4;
            break;
        case 8:
            fprintf(out, "    movq %u(%s), %s\n", member_offset, view_var_reg, target_reg);
            code_size += 4;
            break;
        default:
            fprintf(out, "    /* Unsupported member size: %u */\n", member_size);
            return 0;
    }

    return code_size;
}

size_t zcv_gen_view_array_access(FILE *out,
                                const char *view_array_reg,
                                const char *index_reg,
                                uint32_t element_size,
                                const char *target_reg,
                                const char *target_isa) {
    if (!out || !view_array_reg || !index_reg || !target_reg) {
        return 0;
    }

    size_t code_size = 0;

    fprintf(out, "    /* View array access [%s]: %s[%s] with element_size=%u */\n",
           (target_isa && *target_isa) ? target_isa : "x86_64", view_array_reg, index_reg, element_size);

    if (target_isa && strcmp(target_isa, "aarch64") == 0) {
        fprintf(out, "    mov x9, %s\n", view_array_reg);
        fprintf(out, "    mov x10, %s\n", index_reg);
        fprintf(out, "    mov x11, #%u\n", element_size);
        fprintf(out, "    mul x10, x10, x11\n");
        fprintf(out, "    add x9, x9, x10\n");
        switch (element_size) {
            case 1:
                fprintf(out, "    ldrb %s, [x9]\n", target_reg);
                return 20;
            case 2:
                fprintf(out, "    ldrh %s, [x9]\n", target_reg);
                return 20;
            case 4:
                fprintf(out, "    ldr w0, [x9]\n");
                return 20;
            case 8:
                fprintf(out, "    ldr %s, [x9]\n", target_reg);
                return 20;
            default:
                fprintf(out, "    /* Unsupported array element size: %u */\n", element_size);
                return 0;
        }
    }

    fprintf(out, "    mov %s, %%rax\n", view_array_reg);
    fprintf(out, "    mov %s, %%rcx\n", index_reg);
    code_size += 6;

    if (element_size == 1) {
        fprintf(out, "    movzbl (%%rax, %%rcx), %s\n", target_reg);
        code_size += 4;
    } else if (element_size == 2) {
        fprintf(out, "    movzwl (%%rax, %%rcx, 2), %s\n", target_reg);
        code_size += 4;
    } else if (element_size == 4) {
        fprintf(out, "    movl (%%rax, %%rcx, 4), %s\n", target_reg);
        code_size += 4;
    } else if (element_size == 8) {
        fprintf(out, "    movq (%%rax, %%rcx, 8), %s\n", target_reg);
        code_size += 4;
    } else {
        /* 通用情况：计算偏移量 */
        fprintf(out, "    imul $%u, %%rcx\n", element_size);
        fprintf(out, "    mov (%%rax, %%rcx), %s\n", target_reg);
        code_size += 8;
    }
    
    return code_size;
}

size_t zcv_gen_alignment_check(FILE *out,
                              const char *ptr_reg,
                              uint32_t alignment,
                              const char *target_isa) {
    if (!out || !ptr_reg || alignment <= 1) {
        return 0;
    }

    size_t code_size = 0;

    fprintf(out, "    /* Alignment check [%s]: %u-byte */\n",
            (target_isa && *target_isa) ? target_isa : "x86_64", alignment);
    if (target_isa && strcmp(target_isa, "aarch64") == 0) {
        fprintf(out, "    and x9, %s, #%u\n", ptr_reg, alignment - 1);
        fprintf(out, "    cbnz x9, .alignment_fault\n");
        code_size += 8;
    } else {
        fprintf(out, "    mov %s, %%rax\n", ptr_reg);
        fprintf(out, "    and $%u, %%rax\n", alignment - 1);
        fprintf(out, "    test %%rax, %%rax\n");
        fprintf(out, "    jnz .alignment_fault\n");
        code_size += 14;
    }

    return code_size;
}

/* 新增：支持向量流式存储（非时序写入） */
size_t zcv_gen_view_stream(FILE *out, const char *ymm_reg, uint32_t offset, const char *target_reg) {
    /* 编码 vmovntdq ymm, [rax + offset] */
    /* 指令：C5 FD E7 ... */
    fprintf(out, "    # Stream vector to memory (non-temporal)\n");
    fprintf(out, "    .byte 0xC5, 0xFD, 0xE7, 0x80, 0x%02X, 0x00, 0x00, 0x00\n", (unsigned char)offset);
    return 8;
}

uint64_t zcv_get_view_total_size(const char *view_type, uint64_t array_element_count) {
    if (!view_type) return 0;

    ZeroCopyViewInfo *info = zcv_parse_view_type_name(view_type);
    if (!info) return 0;

    uint64_t size = 0;

    if (info->is_array) {
        size = info->element_size * array_element_count;
    } else {
        size = info->base_type_size;
    }
    
    zcv_free_view_info(info);
    return size;
}

int zcv_types_compatible(const char *type1, const char *type2) {
    if (!type1 || !type2) return 0;
    
    /* 完全相同 */
    if (strcmp(type1, type2) == 0) return 1;
    
    /* 都是view<T>类型且基础类型相同 */
    char *base1 = extract_view_base_type(type1);
    char *base2 = extract_view_base_type(type2);
    
    int compatible = 0;
    if (base1 && base2 && strcmp(base1, base2) == 0) {
        compatible = 1;
    }
    
    free(base1);
    free(base2);
    
    return compatible;
}
