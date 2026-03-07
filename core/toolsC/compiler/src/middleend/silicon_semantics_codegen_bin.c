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
 * AethelOS Aethelium Compiler - Silicon Semantics Binary Code Generator
 * 硅基语义二进制代码生成实现
 * 
 * 版本：2.0 工业级
 * 状态：直接生成x86-64机器码，零汇编中介
 * 
 * 输出纯二进制机器码字节到缓冲区（不是汇编文本）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* =====================================================================
 * 二进制代码缓冲区管理
 * ===================================================================== */

typedef struct {
    uint8_t *bytes;      /* 机器码字节缓冲区 */
    size_t size;         /* 当前大小 */
    size_t capacity;     /* 最大容量 */
} BinBuffer;

static BinBuffer* bin_buffer_create(size_t initial_size) {
    BinBuffer *buf = (BinBuffer*)malloc(sizeof(BinBuffer));
    if (!buf) return NULL;
    
    buf->capacity = initial_size ? initial_size : 4096;
    buf->bytes = (uint8_t*)malloc(buf->capacity);
    buf->size = 0;
    
    if (!buf->bytes) {
        free(buf);
        return NULL;
    }
    
    return buf;
}

static void bin_buffer_destroy(BinBuffer *buf) {
    if (buf) {
        free(buf->bytes);
        free(buf);
    }
}

static void bin_buffer_append(BinBuffer *buf, const uint8_t *data, size_t len) {
    if (!buf || !data || len == 0) return;
    
    /* 扩展缓冲区如果必要 */
    while (buf->size + len > buf->capacity) {
        buf->capacity *= 2;
        uint8_t *new_bytes = (uint8_t*)realloc(buf->bytes, buf->capacity);
        if (!new_bytes) return;
        buf->bytes = new_bytes;
    }
    
    memcpy(&buf->bytes[buf->size], data, len);
    buf->size += len;
}

static void bin_buffer_append_byte(BinBuffer *buf, uint8_t byte) {
    bin_buffer_append(buf, &byte, 1);
}

/* =====================================================================
 * x86-64机器码生成函数
 * ===================================================================== */

/**
 * 生成: mov $imm32, %ecx
 * 机器码: b9 imm32(小端)
 */
static void gen_mov_imm32_ecx(BinBuffer *buf, uint32_t imm) {
    uint8_t code[5];
    code[0] = 0xb9;  /* MOV imm32→ECX */
    code[1] = (imm & 0xFF);
    code[2] = ((imm >> 8) & 0xFF);
    code[3] = ((imm >> 16) & 0xFF);
    code[4] = ((imm >> 24) & 0xFF);
    bin_buffer_append(buf, code, 5);
}

/**
 * 生成: rdmsr
 * 机器码: 0f 32
 */
static void gen_rdmsr(BinBuffer *buf) {
    uint8_t code[] = {0x0f, 0x32};
    bin_buffer_append(buf, code, 2);
}

/**
 * 生成: bts $bit, %rax
 * 机器码: 48 0f ab c0 + bit位置编码
 * bts Ib, RAX: REX.W 0F AB /0 ib
 */
static void gen_bts_bit_rax(BinBuffer *buf, uint32_t bit_pos) {
    uint8_t code[] = {
        0x48, 0x0f, 0xba, 0xe8,  /* REX.W BTS r/m64, imm8配置 */
        (uint8_t)(bit_pos & 0xFF)  /* 位位置 */
    };
    bin_buffer_append(buf, code, 5);
}

/**
 * 生成: wrmsr
 * 机器码: 0f 30
 */
static void gen_wrmsr(BinBuffer *buf) {
    uint8_t code[] = {0x0f, 0x30};
    bin_buffer_append(buf, code, 2);
}

/**
 * 生成: mov %cr0, %rax
 * 机器码: 0f 20 c0
 */
static void gen_mov_cr0_to_rax(BinBuffer *buf) {
    uint8_t code[] = {0x0f, 0x20, 0xc0};
    bin_buffer_append(buf, code, 3);
}

/**
 * 生成: mov %rax, %cr0
 * 机器码: 0f 22 c0
 */
static void gen_mov_rax_to_cr0(BinBuffer *buf) {
    uint8_t code[] = {0x0f, 0x22, 0xc0};
    bin_buffer_append(buf, code, 3);
}

/**
 * 生成: mov %rax, %cr3
 * 机器码: 0f 22 d8
 */
static void gen_mov_rax_to_cr3(BinBuffer *buf) {
    uint8_t code[] = {0x0f, 0x22, 0xd8};
    bin_buffer_append(buf, code, 3);
}

/**
 * 生成: mfence (full memory barrier)
 * 机器码: 0f ae f0
 */
static void gen_mfence(BinBuffer *buf) {
    uint8_t code[] = {0x0f, 0xae, 0xf0};
    bin_buffer_append(buf, code, 3);
}

/**
 * 生成: lfence (load barrier)
 * 机器码: 0f ae e8
 */
static void gen_lfence(BinBuffer *buf) {
    uint8_t code[] = {0x0f, 0xae, 0xe8};
    bin_buffer_append(buf, code, 3);
}

/**
 * 生成: sfence (store barrier)
 * 机器码: 0f ae f8
 */
static void gen_sfence(BinBuffer *buf) {
    uint8_t code[] = {0x0f, 0xae, 0xf8};
    bin_buffer_append(buf, code, 3);
}

/**
 * 生成: clflush [rax]
 * 机器码: 0f ae 38
 */
static void gen_clflush_rax(BinBuffer *buf) {
    uint8_t code[] = {0x0f, 0xae, 0x38};
    bin_buffer_append(buf, code, 3);
}

/**
 * 生成: prefetcht0 [rax]
 * 机器码: 0f 18 08
 */
static void gen_prefetcht0_rax(BinBuffer *buf) {
    uint8_t code[] = {0x0f, 0x18, 0x08};
    bin_buffer_append(buf, code, 3);
}

/**
 * 生成: prefetcht1 [rax]
 * 机器码: 0f 18 10
 */
static void gen_prefetcht1_rax(BinBuffer *buf) {
    uint8_t code[] = {0x0f, 0x18, 0x10};
    bin_buffer_append(buf, code, 3);
}

/**
 * 生成: prefetcht2 [rax]
 * 机器码: 0f 18 18
 */
static void gen_prefetcht2_rax(BinBuffer *buf) {
    uint8_t code[] = {0x0f, 0x18, 0x18};
    bin_buffer_append(buf, code, 3);
}

/**
 * 生成: prefetchnta [rax]
 * 机器码: 0f 18 00
 */
static void gen_prefetchnta_rax(BinBuffer *buf) {
    uint8_t code[] = {0x0f, 0x18, 0x00};
    bin_buffer_append(buf, code, 3);
}

/* =====================================================================
 * 高层硅基语义代码生成
 * ===================================================================== */

/**
 * 生成微架构配置的机器码
 * MSR/EFER\Syscall/Enable = true → rdmsr, bts $0 rax, wrmsr
 */
void silicon_bin_gen_microarch_config(BinBuffer *buf, 
                                      const char *msr_name,
                                      const char *prop_name) {
    if (!buf || !msr_name || !prop_name) return;
    
    if (strcmp(msr_name, "MSR/EFER") == 0) {
        /* MSR_EFER地址: 0xc0000080 */
        gen_mov_imm32_ecx(buf, 0xc0000080);
        gen_rdmsr(buf);
        
        if (strcmp(prop_name, "Syscall/Enable") == 0) {
            /* SCE位 = 位0 */
            gen_bts_bit_rax(buf, 0);
        } else if (strcmp(prop_name, "Long/Mode") == 0) {
            /* LME位 = 位8 */
            gen_bts_bit_rax(buf, 8);
        } else if (strcmp(prop_name, "Nx/Enable") == 0) {
            /* NXE位 = 位11 */
            gen_bts_bit_rax(buf, 11);
        }
        
        gen_wrmsr(buf);
    } else if (strcmp(msr_name, "CR0") == 0 || strcmp(msr_name, "CPU/CR0") == 0) {
        gen_mov_cr0_to_rax(buf);
        
        if (strcmp(prop_name, "Cache/Disable") == 0) {
            /* CD位 = 位29 */
            gen_bts_bit_rax(buf, 29);
        } else if (strcmp(prop_name, "Write/Through") == 0) {
            /* WT位 = 位30 */
            gen_bts_bit_rax(buf, 30);
        }
        
        gen_mov_rax_to_cr0(buf);
    } else if (strcmp(msr_name, "CR3") == 0 || strcmp(msr_name, "CPU/CR3") == 0) {
        gen_mov_rax_to_cr3(buf);
    }
}

/**
 * 生成流水线屏障的机器码
 */
void silicon_bin_gen_barrier(BinBuffer *buf, uint32_t barrier_type) {
    if (!buf) return;
    
    if (barrier_type == 1) {      /* PIPELINE_BARRIER_LOAD */
        gen_lfence(buf);
    } else if (barrier_type == 2) { /* PIPELINE_BARRIER_STORE */
        gen_sfence(buf);
    } else if (barrier_type == 3) { /* PIPELINE_BARRIER_FULL */
        gen_mfence(buf);
    }
}

/**
 * 生成缓存操作的机器码
 */
void silicon_bin_gen_cache_op(BinBuffer *buf, const char *op_type) {
    if (!buf || !op_type) return;
    
    if (strcmp(op_type, "flush") == 0) {
        gen_clflush_rax(buf);
    } else if (strcmp(op_type, "invalidate") == 0) {
        /* clflush也可用于invalidate */
        gen_clflush_rax(buf);
    }
}

/**
 * 生成预取操作的机器码
 */
void silicon_bin_gen_prefetch(BinBuffer *buf, const char *hint) {
    if (!buf || !hint) {
        gen_prefetchnta_rax(buf);  /* 默认NTA */
        return;
    }
    
    if (strcmp(hint, "T0") == 0) {
        gen_prefetcht0_rax(buf);
    } else if (strcmp(hint, "T1") == 0) {
        gen_prefetcht1_rax(buf);
    } else if (strcmp(hint, "T2") == 0) {
        gen_prefetcht2_rax(buf);
    } else if (strcmp(hint, "NTA") == 0) {
        gen_prefetchnta_rax(buf);
    } else {
        gen_prefetchnta_rax(buf);  /* 默认 */
    }
}

/**
 * 导出二进制缓冲区到文件
 */
int silicon_bin_export(const char *output_file, const uint8_t *code, size_t size) {
    FILE *f = fopen(output_file, "wb");
    if (!f) return -1;
    
    size_t written = fwrite(code, 1, size, f);
    fclose(f);
    
    return (written == size) ? 0 : -1;
}
