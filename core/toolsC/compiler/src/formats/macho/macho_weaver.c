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
 * Aethelium Bare-metal Mach-O Firmware Weaver Implementation
 * 工业级实现，完整的三区块到段映射、加载命令生成、线程状态初始化
 */

#include "macho_weaver.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
   内部辅助函数 - 完整的防御编程实现
   ============================================================================ */

/**
 * 检查整数加法是否溢出
 * a + b > SIZE_MAX 返回 1，否则返回 0
 */
static int check_add_overflow_size(size_t a, size_t b, size_t *result) {
    if (b > SIZE_MAX - a) {
        return 1;
    }
    *result = a + b;
    return 0;
}

/**
 * 检查地址是否 4KB 对齐
 */
static int is_aligned_4k(uint64_t addr) {
    return (addr & MACHO_PAGE_MASK) == 0;
}

/**
 * 写入 64 位小端序值到缓冲区
 * 返回下一个写入位置的偏移
 */
static size_t write_u64_le(uint8_t *buffer, size_t offset, size_t capacity,
                           uint64_t value) {
    if (offset + 8 > capacity) {
        return (size_t)-1;
    }
    buffer[offset + 0] = (uint8_t)((value) & 0xFF);
    buffer[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    buffer[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    buffer[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
    buffer[offset + 4] = (uint8_t)((value >> 32) & 0xFF);
    buffer[offset + 5] = (uint8_t)((value >> 40) & 0xFF);
    buffer[offset + 6] = (uint8_t)((value >> 48) & 0xFF);
    buffer[offset + 7] = (uint8_t)((value >> 56) & 0xFF);
    return offset + 8;
}

/**
 * 写入 32 位小端序值到缓冲区
 */
static size_t write_u32_le(uint8_t *buffer, size_t offset, size_t capacity,
                           uint32_t value) {
    if (offset + 4 > capacity) {
        return (size_t)-1;
    }
    buffer[offset + 0] = (uint8_t)((value) & 0xFF);
    buffer[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    buffer[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    buffer[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
    return offset + 4;
}

/**
 * 向缓冲区追加字节数据
 * 返回下一个写入位置，若溢出返回 (size_t)-1
 */
static size_t write_bytes(uint8_t *buffer, size_t offset, size_t capacity,
                          const uint8_t *data, size_t data_len) {
    if (offset + data_len > capacity || offset + data_len < offset) {
        return (size_t)-1;
    }
    if (data_len > 0 && data != NULL) {
        memcpy(&buffer[offset], data, data_len);
    }
    return offset + data_len;
}

/**
 * 对齐缓冲区位置到 4KB 页面边界（向上）
 */
static size_t align_to_page(size_t offset) {
    return (offset + MACHO_PAGE_MASK) & ~MACHO_PAGE_MASK;
}

/**
 * 计算 4KB 对齐后的虚拟地址
 */
static uint64_t align_addr_4k(uint64_t addr) {
    return (addr + MACHO_PAGE_MASK) & ~MACHO_PAGE_MASK;
}

/* ============================================================================
   织机上下文生命周期管理
   ============================================================================ */

macho_weaver_context_t* macho_weaver_create(uint64_t phys_base, int verbose) {
    macho_weaver_context_t *ctx;
    
    /* 前置校验：物理基址必须 4KB 对齐 */
    if (!is_aligned_4k(phys_base)) {
        fprintf(stderr, "[ERROR] Mach-O: Physical base 0x%llx not 4KB aligned\n",
                (unsigned long long)phys_base);
        return NULL;
    }
    
    ctx = (macho_weaver_context_t*)calloc(1, sizeof(macho_weaver_context_t));
    if (!ctx) {
        fprintf(stderr, "[ERROR] Mach-O: Out of memory allocating context\n");
        return NULL;
    }
    
    ctx->phys_base = phys_base;
    ctx->entry_point_offset = 0;
    ctx->verbose = verbose;
    
    /* 初始化输出缓冲区（初始容量 1MB，足以容纳典型的固件） */
    ctx->output_capacity = 1024 * 1024;
    ctx->output_buffer = (uint8_t*)malloc(ctx->output_capacity);
    if (!ctx->output_buffer) {
        fprintf(stderr, "[ERROR] Mach-O: Out of memory allocating output buffer\n");
        free(ctx);
        return NULL;
    }
    
    ctx->output_size = 0;
    memset(ctx->error_message, 0, sizeof(ctx->error_message));
    
    if (verbose) {
        fprintf(stderr, "[INFO] Mach-O weaver context created (phys_base=0x%llx)\n",
                (unsigned long long)phys_base);
    }
    
    return ctx;
}

void macho_weaver_destroy(macho_weaver_context_t *ctx) {
    if (!ctx) return;
    if (ctx->output_buffer) {
        free(ctx->output_buffer);
        ctx->output_buffer = NULL;
    }
    free(ctx);
}

/* ============================================================================
   区块添加与验证
   ============================================================================ */

int macho_weaver_add_zones(macho_weaver_context_t *ctx,
                          const uint8_t *actflow,
                          size_t actflow_size,
                          const uint8_t *mirror,
                          size_t mirror_size,
                          const uint8_t *constant,
                          size_t constant_size) {
    size_t dummy_sum;
    
    /* 前置条件校验 */
    if (!ctx) {
        fprintf(stderr, "[ERROR] Mach-O: NULL context\n");
        return -1;
    }
    
    if (!actflow || actflow_size == 0) {
        snprintf(ctx->error_message, sizeof(ctx->error_message),
                "ActFlow zone is NULL or empty");
        fprintf(stderr, "[ERROR] Mach-O: %s\n", ctx->error_message);
        return -1;
    }
    
    /* 验证大小 - ActFlow 最多 512MB（合理的固件上限） */
    if (actflow_size > 512 * 1024 * 1024) {
        snprintf(ctx->error_message, sizeof(ctx->error_message),
                "ActFlow size 0x%zx exceeds 512MB limit", actflow_size);
        fprintf(stderr, "[ERROR] Mach-O: %s\n", ctx->error_message);
        return -1;
    }
    
    /* 验证大小溢出：ActFlow + MirrorState + ConstantTruth + 头部 */
    if (check_add_overflow_size(actflow_size, mirror_size, &dummy_sum)) {
        snprintf(ctx->error_message, sizeof(ctx->error_message),
                "ActFlow + MirrorState size overflow");
        fprintf(stderr, "[ERROR] Mach-O: %s\n", ctx->error_message);
        return -1;
    }
    
    /* MirrorState 可选但若存在则必须有数据 */
    if (mirror_size > 0 && !mirror) {
        snprintf(ctx->error_message, sizeof(ctx->error_message),
                "MirrorState size nonzero but data pointer is NULL");
        fprintf(stderr, "[ERROR] Mach-O: %s\n", ctx->error_message);
        return -1;
    }
    
    /* ConstantTruth 可选但若存在则必须有数据 */
    if (constant_size > 0 && !constant) {
        snprintf(ctx->error_message, sizeof(ctx->error_message),
                "ConstantTruth size nonzero but data pointer is NULL");
        fprintf(stderr, "[ERROR] Mach-O: %s\n", ctx->error_message);
        return -1;
    }
    
    /* 保存区块指针和大小 */
    ctx->actflow_data = actflow;
    ctx->actflow_size = actflow_size;
    ctx->mirror_data = mirror;
    ctx->mirror_size = mirror_size;
    ctx->constant_data = constant;
    ctx->constant_size = constant_size;
    
    if (ctx->verbose) {
        fprintf(stderr, "[INFO] Mach-O zones added: ActFlow=%zu, MirrorState=%zu, ConstantTruth=%zu\n",
                actflow_size, mirror_size, constant_size);
    }
    
    return 0;
}

/* ============================================================================
   Mach-O 二进制构造和发射
   ============================================================================ */

int macho_weaver_emit(macho_weaver_context_t *ctx) {
    uint8_t *buf;
    size_t capacity;
    size_t offset;
    size_t cmd_start_offset;
    
    /* Mach-O 头部 */
    macho_header_64_t header;
    
    /* 加载命令 1: TEXT 段 (ActFlow) */
    macho_segment_command_64_t text_seg;
    
    /* 加载命令 2: DATA 段 (MirrorState) */
    macho_segment_command_64_t data_seg;
    
    /* 加载命令 3: RODATA 段 (ConstantTruth) */
    macho_segment_command_64_t rodata_seg;
    
    /* 加载命令 4: UNIXTHREAD (入口点) */
    macho_thread_command_t thread_cmd;
    arm64_thread_state_t thread_state;
    
    uint32_t ncmds;
    uint32_t total_cmd_size;
    size_t text_offset, data_offset, rodata_offset;
    uint64_t text_vm_addr, data_vm_addr, rodata_vm_addr;
    
    /* 前置条件 */
    if (!ctx) {
        fprintf(stderr, "[ERROR] Mach-O emit: NULL context\n");
        return -1;
    }
    
    if (!ctx->output_buffer) {
        fprintf(stderr, "[ERROR] Mach-O emit: output buffer not allocated\n");
        return -1;
    }
    
    if (!ctx->actflow_data || ctx->actflow_size == 0) {
        fprintf(stderr, "[ERROR] Mach-O emit: ActFlow zone not set\n");
        return -1;
    }
    
    buf = ctx->output_buffer;
    capacity = ctx->output_capacity;
    offset = 0;
    
    /* ========== 第 1 步：构造 Mach-O 头部 ========== */
    
    memset(&header, 0, sizeof(header));
    header.magic = MACHO_MAGIC_64;
    header.cpu_type = MACHO_CPU_TYPE_ARM64;
    header.cpu_subtype = MACHO_CPU_SUBTYPE_ARM64_ALL;
    header.file_type = MACHO_FILE_TYPE_PRELOAD;
    
    /* 加载命令计数：
     * 1. TEXT segment (ActFlow)
     * 2. DATA segment (MirrorState)
     * 3. RODATA segment (ConstantTruth)
     * 4. UNIXTHREAD (入口点)
     */
    ncmds = 4;
    header.ncmds = ncmds;
    header.flags = 0x0;  /* 固件无特殊标志 */
    header.reserved = 0;
    
    /* 计算加载命令总大小 */
    total_cmd_size = 0;
    total_cmd_size += sizeof(macho_segment_command_64_t);  /* TEXT */
    total_cmd_size += sizeof(macho_segment_command_64_t);  /* DATA */
    total_cmd_size += sizeof(macho_segment_command_64_t);  /* RODATA */
    total_cmd_size += sizeof(macho_thread_command_t) + sizeof(arm64_thread_state_t);  /* UNIXTHREAD */
    
    header.size_cmds = total_cmd_size;
    
    /* 写入 Mach-O 头部（32 字节） */
    offset = write_u32_le(buf, offset, capacity, header.magic);
    if (offset == (size_t)-1) goto overflow;
    
    offset = write_u32_le(buf, offset, capacity, header.cpu_type);
    if (offset == (size_t)-1) goto overflow;
    
    offset = write_u32_le(buf, offset, capacity, header.cpu_subtype);
    if (offset == (size_t)-1) goto overflow;
    
    offset = write_u32_le(buf, offset, capacity, header.file_type);
    if (offset == (size_t)-1) goto overflow;
    
    offset = write_u32_le(buf, offset, capacity, header.ncmds);
    if (offset == (size_t)-1) goto overflow;
    
    offset = write_u32_le(buf, offset, capacity, header.size_cmds);
    if (offset == (size_t)-1) goto overflow;
    
    offset = write_u32_le(buf, offset, capacity, header.flags);
    if (offset == (size_t)-1) goto overflow;
    
    offset = write_u32_le(buf, offset, capacity, header.reserved);
    if (offset == (size_t)-1) goto overflow;
    
    if (ctx->verbose) {
        fprintf(stderr, "[INFO] Mach-O header written (32 bytes), offset now 0x%zx\n", offset);
    }
    
    /* ========== 第 2 步：数据段布置与地址计算 ========== */
    
    /* 文件偏移：Mach-O 头部（32字节）+ 所有加载命令 + 填充到页面边界 */
    text_offset = 32 + total_cmd_size;
    text_offset = align_to_page(text_offset);
    
    data_offset = text_offset + ctx->actflow_size;
    data_offset = align_to_page(data_offset);
    
    rodata_offset = data_offset + ctx->mirror_size;
    rodata_offset = align_to_page(rodata_offset);
    
    /* 虚拟地址（这里直接使用物理基址，在固件环境中通常相同） */
    text_vm_addr = ctx->phys_base;
    data_vm_addr = text_vm_addr + align_addr_4k(ctx->actflow_size);
    rodata_vm_addr = data_vm_addr + align_addr_4k(ctx->mirror_size);
    
    if (ctx->verbose) {
        fprintf(stderr, "[INFO] Segment layout: TEXT @ 0x%llx (file 0x%zx), "
                "DATA @ 0x%llx (file 0x%zx), RODATA @ 0x%llx (file 0x%zx)\n",
                (unsigned long long)text_vm_addr, text_offset,
                (unsigned long long)data_vm_addr, data_offset,
                (unsigned long long)rodata_vm_addr, rodata_offset);
    }
    
    /* ========== 第 3 步：构造 TEXT Segment 加载命令 ========== */
    
    cmd_start_offset = offset;
    
    memset(&text_seg, 0, sizeof(text_seg));
    strncpy(text_seg.segname, "__TEXT", 16);
    text_seg.cmd = MACH_LOAD_CMD_SEGMENT_64;
    text_seg.cmd_size = sizeof(macho_segment_command_64_t);
    text_seg.vm_addr = text_vm_addr;
    text_seg.vm_size = ctx->actflow_size;
    text_seg.file_off = text_offset;
    text_seg.file_size = ctx->actflow_size;
    text_seg.init_prot = MACHO_PROT_READ | MACHO_PROT_EXEC;  /* rx */
    text_seg.max_prot = MACHO_PROT_READ | MACHO_PROT_WRITE | MACHO_PROT_EXEC;  /* rwx */
    text_seg.nsects = 0;
    text_seg.flags = 0;
    
    /* 写入 TEXT 段命令 */
    offset = write_u32_le(buf, offset, capacity, text_seg.cmd);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, text_seg.cmd_size);
    if (offset == (size_t)-1) goto overflow;
    offset = write_bytes(buf, offset, capacity, (uint8_t*)text_seg.segname, 16);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, text_seg.vm_addr);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, text_seg.vm_size);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, text_seg.file_off);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, text_seg.file_size);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, text_seg.init_prot);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, text_seg.max_prot);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, text_seg.nsects);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, text_seg.flags);
    if (offset == (size_t)-1) goto overflow;
    
    if (ctx->verbose) {
        fprintf(stderr, "[INFO] TEXT segment command written (56 bytes)\n");
    }
    
    /* ========== 第 4 步：构造 DATA Segment 加载命令 ========== */
    
    memset(&data_seg, 0, sizeof(data_seg));
    strncpy(data_seg.segname, "__DATA", 16);
    data_seg.cmd = MACH_LOAD_CMD_SEGMENT_64;
    data_seg.cmd_size = sizeof(macho_segment_command_64_t);
    data_seg.vm_addr = data_vm_addr;
    data_seg.vm_size = ctx->mirror_size;
    data_seg.file_off = data_offset;
    data_seg.file_size = ctx->mirror_size;
    data_seg.init_prot = MACHO_PROT_READ | MACHO_PROT_WRITE;  /* rw */
    data_seg.max_prot = MACHO_PROT_READ | MACHO_PROT_WRITE | MACHO_PROT_EXEC;  /* rwx */
    data_seg.nsects = 0;
    data_seg.flags = 0;
    
    /* 写入 DATA 段命令 */
    offset = write_u32_le(buf, offset, capacity, data_seg.cmd);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, data_seg.cmd_size);
    if (offset == (size_t)-1) goto overflow;
    offset = write_bytes(buf, offset, capacity, (uint8_t*)data_seg.segname, 16);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, data_seg.vm_addr);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, data_seg.vm_size);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, data_seg.file_off);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, data_seg.file_size);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, data_seg.init_prot);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, data_seg.max_prot);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, data_seg.nsects);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, data_seg.flags);
    if (offset == (size_t)-1) goto overflow;
    
    if (ctx->verbose) {
        fprintf(stderr, "[INFO] DATA segment command written (56 bytes)\n");
    }
    
    /* ========== 第 5 步：构造 RODATA Segment 加载命令 ========== */
    
    memset(&rodata_seg, 0, sizeof(rodata_seg));
    strncpy(rodata_seg.segname, "__RODATA", 16);
    rodata_seg.cmd = MACH_LOAD_CMD_SEGMENT_64;
    rodata_seg.cmd_size = sizeof(macho_segment_command_64_t);
    rodata_seg.vm_addr = rodata_vm_addr;
    rodata_seg.vm_size = ctx->constant_size;
    rodata_seg.file_off = rodata_offset;
    rodata_seg.file_size = ctx->constant_size;
    rodata_seg.init_prot = MACHO_PROT_READ;  /* r */
    rodata_seg.max_prot = MACHO_PROT_READ | MACHO_PROT_WRITE;  /* rw */
    rodata_seg.nsects = 0;
    rodata_seg.flags = 0;
    
    /* 写入 RODATA 段命令 */
    offset = write_u32_le(buf, offset, capacity, rodata_seg.cmd);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, rodata_seg.cmd_size);
    if (offset == (size_t)-1) goto overflow;
    offset = write_bytes(buf, offset, capacity, (uint8_t*)rodata_seg.segname, 16);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, rodata_seg.vm_addr);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, rodata_seg.vm_size);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, rodata_seg.file_off);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, rodata_seg.file_size);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, rodata_seg.init_prot);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, rodata_seg.max_prot);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, rodata_seg.nsects);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, rodata_seg.flags);
    if (offset == (size_t)-1) goto overflow;
    
    if (ctx->verbose) {
        fprintf(stderr, "[INFO] RODATA segment command written (56 bytes)\n");
    }
    
    /* ========== 第 6 步：构造 UNIXTHREAD 加载命令（入口点） ========== */
    
    memset(&thread_cmd, 0, sizeof(thread_cmd));
    memset(&thread_state, 0, sizeof(thread_state));
    
    /* 线程命令头 */
    thread_cmd.cmd = MACH_LOAD_CMD_UNIXTHREAD;
    thread_cmd.cmd_size = sizeof(macho_thread_command_t) + sizeof(arm64_thread_state_t);
    thread_cmd.flavor = ARM64_THREAD_STATE;
    thread_cmd.count = ARM64_THREAD_STATE_COUNT;
    
    /* ARM64 线程状态：全部清零，仅设置 PC (程序计数器) */
    thread_state.pc = text_vm_addr + ctx->entry_point_offset;
    /* 其余寄存器保持清零 */
    
    /* 写入线程命令 */
    offset = write_u32_le(buf, offset, capacity, thread_cmd.cmd);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, thread_cmd.cmd_size);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, thread_cmd.flavor);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, thread_cmd.count);
    if (offset == (size_t)-1) goto overflow;
    
    /* 写入 ARM64 寄存器状态 */
    for (int i = 0; i < 32; i++) {
        offset = write_u64_le(buf, offset, capacity, thread_state.regs[i]);
        if (offset == (size_t)-1) goto overflow;
    }
    offset = write_u64_le(buf, offset, capacity, thread_state.sp);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, thread_state.lr);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u64_le(buf, offset, capacity, thread_state.pc);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, thread_state.cpsr);
    if (offset == (size_t)-1) goto overflow;
    offset = write_u32_le(buf, offset, capacity, thread_state.reserved);
    if (offset == (size_t)-1) goto overflow;
    
    if (ctx->verbose) {
        fprintf(stderr, "[INFO] UNIXTHREAD command written, PC set to 0x%llx\n",
                (unsigned long long)thread_state.pc);
    }
    
    /* ========== 第 7 步：写入区块数据 ========== */
    
    /* 填充到 TEXT 段偏移 */
    while (offset < text_offset) {
        if (offset + 1 > capacity) goto overflow;
        buf[offset++] = 0;
    }
    
    /* 写入 ActFlow (TEXT 段) */
    offset = write_bytes(buf, offset, capacity, ctx->actflow_data, ctx->actflow_size);
    if (offset == (size_t)-1) goto overflow;
    
    /* 填充到 DATA 段偏移 */
    while (offset < data_offset) {
        if (offset + 1 > capacity) goto overflow;
        buf[offset++] = 0;
    }
    
    /* 写入 MirrorState (DATA 段) */
    if (ctx->mirror_size > 0 && ctx->mirror_data) {
        offset = write_bytes(buf, offset, capacity, ctx->mirror_data, ctx->mirror_size);
        if (offset == (size_t)-1) goto overflow;
    }
    
    /* 填充到 RODATA 段偏移 */
    while (offset < rodata_offset) {
        if (offset + 1 > capacity) goto overflow;
        buf[offset++] = 0;
    }
    
    /* 写入 ConstantTruth (RODATA 段) */
    if (ctx->constant_size > 0 && ctx->constant_data) {
        offset = write_bytes(buf, offset, capacity, ctx->constant_data, ctx->constant_size);
        if (offset == (size_t)-1) goto overflow;
    }
    
    ctx->output_size = offset;
    
    if (ctx->verbose) {
        fprintf(stderr, "[INFO] Mach-O binary emit complete: total size 0x%zx bytes\n",
                ctx->output_size);
    }
    
    return 0;

overflow:
    fprintf(stderr, "[ERROR] Mach-O emit: buffer overflow detected\n");
    snprintf(ctx->error_message, sizeof(ctx->error_message),
            "Output buffer overflow during emit");
    return -4;
}

int macho_weaver_write(macho_weaver_context_t *ctx, const char *filename) {
    FILE *f;
    
    if (!ctx || !filename) {
        fprintf(stderr, "[ERROR] Mach-O write: invalid arguments\n");
        return -1;
    }
    
    if (!ctx->output_buffer || ctx->output_size == 0) {
        fprintf(stderr, "[ERROR] Mach-O write: no data to write\n");
        return -1;
    }
    
    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "[ERROR] Mach-O write: failed to open '%s' for writing\n", filename);
        return -1;
    }
    
    size_t written = fwrite(ctx->output_buffer, 1, ctx->output_size, f);
    if (written != ctx->output_size) {
        fprintf(stderr, "[ERROR] Mach-O write: partial write (got %zu, expected %zu)\n",
                written, ctx->output_size);
        fclose(f);
        return -2;
    }
    
    fclose(f);
    
    if (ctx->verbose) {
        fprintf(stderr, "[INFO] Mach-O binary written to '%s' (%zu bytes)\n",
                filename, ctx->output_size);
    }
    
    return 0;
}

/* ============================================================================
   顶级接口函数
   ============================================================================ */

int macho_generate_image(const char *output_file,
                        const uint8_t *actflow_code,
                        size_t actflow_size,
                        const uint8_t *mirror_data,
                        size_t mirror_size,
                        const uint8_t *constant_data,
                        size_t constant_size,
                        uint64_t phys_base) {
    macho_weaver_context_t *ctx;
    int result;
    
    if (!output_file) {
        fprintf(stderr, "[ERROR] Mach-O: NULL output filename\n");
        return -1;
    }
    
    if (!actflow_code || actflow_size == 0) {
        fprintf(stderr, "[ERROR] Mach-O: NULL or empty ActFlow code\n");
        return -1;
    }
    
    if (!is_aligned_4k(phys_base)) {
        fprintf(stderr, "[ERROR] Mach-O: Physical base 0x%llx not 4KB aligned\n",
                (unsigned long long)phys_base);
        return -1;
    }
    
    /* 创建织机上下文 */
    ctx = macho_weaver_create(phys_base, 1);
    if (!ctx) {
        fprintf(stderr, "[ERROR] Mach-O: Failed to create weaver context\n");
        return -2;
    }
    
    /* 添加区块 */
    result = macho_weaver_add_zones(ctx, actflow_code, actflow_size,
                                   mirror_data, mirror_size,
                                   constant_data, constant_size);
    if (result != 0) {
        fprintf(stderr, "[ERROR] Mach-O: Failed to add zones: %s\n", ctx->error_message);
        macho_weaver_destroy(ctx);
        return result;
    }
    
    /* 生成 Mach-O 二进制 */
    result = macho_weaver_emit(ctx);
    if (result != 0) {
        fprintf(stderr, "[ERROR] Mach-O: Failed to emit binary: %s\n", ctx->error_message);
        macho_weaver_destroy(ctx);
        return result;
    }
    
    /* 写出文件 */
    result = macho_weaver_write(ctx, output_file);
    if (result != 0) {
        fprintf(stderr, "[ERROR] Mach-O: Failed to write output file\n");
        macho_weaver_destroy(ctx);
        return result;
    }
    
    macho_weaver_destroy(ctx);
    return 0;
}
