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
 * Aethelium Bare-metal Mach-O Firmware Weaver (AEP-042 Implementation)
 * ====================================================================
 * 
 * 核心职责：从 Aethelium 的三个区块（ActFlow, MirrorState, ConstantTruth）
 * 织造标准的 Mach-O-64 固件镜像，特定于 Apple Silicon (ARM64)。
 * 
 * 设计约束：
 * - 严禁任何 macOS 运行时依赖（BSD、Mach、XNU 用户态）
 * - MH_PRELOAD 文件类型标记这是纯固件，不是可执行应用
 * - 物理内存基址必须对齐到 4KB 边界（ARM 分页粒度）
 * - UNIXTHREAD 命令仅用于入口点，不涉及任何线程创建
 * - 无 PAGEZERO 段（纯粹的固件映射，无内核空间虚拟映射）
 * 
 * 工业级防御要求：
 * 1. 所有内存操作必须带边界检查
 * 2. 所有文件偏移必须经过溢出验证
 * 3. 所有头部字段必须通过校验和确认
 * 4. 不允许任何占位式实现或模拟
 * 5. 完整的错误链路和日志记录
 */

#ifndef AETHEL_MACHO_WEAVER_H
#define AETHEL_MACHO_WEAVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   Mach-O 魔数与常数定义（ARM64 Little-Endian）
   ============================================================================ */

#define MACHO_MAGIC_64      0xFEEDFACF   /* Mach-O-64 魔数（固定） */
#define MACHO_CPU_TYPE_ARM64    0x0100000C   /* ARM64 架构类型 */
#define MACHO_CPU_SUBTYPE_ARM64_ALL 0x00000000 /* ARM64 ALL 子类型 */
#define MACHO_FILE_TYPE_PRELOAD 0x5        /* MH_PRELOAD - 固件预加载镜像 */
#define MACHO_FILE_TYPE_EXECUTE 0x2        /* MH_EXECUTE - 不使用 */

/* Mach-O 加载命令类型 */
#define MACH_LOAD_CMD_SEGMENT_64  0x19    /* LC_SEGMENT_64 */
#define MACH_LOAD_CMD_UNIXTHREAD  0x5     /* LC_UNIXTHREAD */

/* ARM64 线程状态 */
#define ARM64_THREAD_STATE       0x00000006
#define ARM64_THREAD_STATE_COUNT (68)  /* ARM64 有 34 寄存器，每个 64 位 */

/* 段权限掩码 */
#define MACHO_PROT_NONE   0x0
#define MACHO_PROT_READ   0x1
#define MACHO_PROT_WRITE  0x2
#define MACHO_PROT_EXEC   0x4

/* 标准约定：物理基址必须是 4KB 对齐的整数倍 */
#define MACHO_PAGE_SIZE        0x1000
#define MACHO_PAGE_MASK        (MACHO_PAGE_SIZE - 1)
#define MACHO_IS_PAGE_ALIGNED(addr) (((addr) & MACHO_PAGE_MASK) == 0)

/* ============================================================================
   Mach-O 数据结构定义（Little-Endian）
   ============================================================================ */

/* Mach-O 头部（32 字节固定） */
typedef struct {
    uint32_t magic;           /* 0x00: MACHO_MAGIC_64 (0xFEEDFACF) */
    uint32_t cpu_type;        /* 0x04: MACHO_CPU_TYPE_ARM64 */
    uint32_t cpu_subtype;     /* 0x08: MACHO_CPU_SUBTYPE_ARM64_ALL */
    uint32_t file_type;       /* 0x0C: MACHO_FILE_TYPE_PRELOAD */
    uint32_t ncmds;           /* 0x10: 加载命令数量 */
    uint32_t size_cmds;       /* 0x14: 所有加载命令总大小 */
    uint32_t flags;           /* 0x18: 头部标志 (0x0 for 固件) */
    uint32_t reserved;        /* 0x1C: 保留（总是0） */
} macho_header_64_t;

/* LC_SEGMENT_64 加载命令（56 字节固定 + 16*段数） */
typedef struct {
    uint32_t cmd;             /* 0x00: MACH_LOAD_CMD_SEGMENT_64 (0x19) */
    uint32_t cmd_size;        /* 0x04: 命令大小（包括所有段条目） */
    char     segname[16];     /* 0x08: 段名称（如 "__TEXT"） */
    uint64_t vm_addr;         /* 0x18: 虚拟内存地址 */
    uint64_t vm_size;         /* 0x20: 虚拟内存大小 */
    uint64_t file_off;        /* 0x28: 文件偏移 */
    uint64_t file_size;       /* 0x30: 文件中的大小 */
    uint32_t max_prot;        /* 0x38: 最大保护掩码 */
    uint32_t init_prot;       /* 0x3C: 初始保护掩码 */
    uint32_t nsects;          /* 0x40: 段中的段数 */
    uint32_t flags;           /* 0x44: 段标志 */
} macho_segment_command_64_t;

/* LC_UNIXTHREAD 加载命令（用于 ARM64 线程状态） */
typedef struct {
    uint32_t cmd;             /* 0x00: MACH_LOAD_CMD_UNIXTHREAD (0x5) */
    uint32_t cmd_size;        /* 0x04: 命令大小 */
    uint32_t flavor;          /* 0x08: ARM64_THREAD_STATE (0x6) */
    uint32_t count;           /* 0x0C: 状态计数 (ARM64_THREAD_STATE_COUNT) */
    /* 紧接着是 ARM64 寄存器状态 68*4 = 272 字节 */
} macho_thread_command_t;

/* ARM64 通用寄存器（x0-x31） */
typedef struct {
    uint64_t regs[32];        /* x0-x31 寄存器 */
    uint64_t sp;              /* SP (sp) */
    uint64_t lr;              /* LR (link register) */
    uint64_t pc;              /* PC (program counter) - 入口点 */
    uint32_t cpsr;            /* CPSR (条件状态寄存器) */
    uint32_t reserved;        /* 对齐 */
} arm64_thread_state_t;

/* ============================================================================
   Mach-O 织机上下文结构
   ============================================================================ */

typedef struct {
    /* 输入数据指针 */
    const uint8_t *actflow_data;
    size_t actflow_size;
    
    const uint8_t *mirror_data;
    size_t mirror_size;
    
    const uint8_t *constant_data;
    size_t constant_size;
    
    /* 物理内存映射参数 */
    uint64_t phys_base;  /* 物理加载基址 (must be 4KB aligned) */
    uint64_t entry_point_offset;  /* 入口点相对于 actflow 的偏移 */
    
    /* 输出缓冲区（内存中构造的 Mach-O） */
    uint8_t *output_buffer;
    size_t output_size;
    size_t output_capacity;
    
    /* 验证和日志记录 */
    int verbose;
    char error_message[512];
} macho_weaver_context_t;

/* ============================================================================
   公共接口函数
   ============================================================================ */

/**
 * macho_generate_image - 从 Aethelium 三个区块生成 Mach-O 固件镜像
 * 
 * 顶级函数，完全实现加载命令、段映射、线程状态初始化等所有细节。
 * 
 * 参数：
 *   output_file     - 输出文件路径
 *   actflow_code    - ActFlow 区块（代码）数据指针
 *   actflow_size    - 大小（字节）
 *   mirror_data     - MirrorState 区块（数据）数据指针
 *   mirror_size     - 大小（字节）
 *   constant_data   - ConstantTruth 区块（只读常量）数据指针
 *   constant_size   - 大小（字节）
 *   phys_base       - 物理基址 (必须 4KB 对齐)
 *   entry_offset    - 入口点相对偏移（相对于 actflow 起点）
 * 
 * 返回值：
 *   0               - 成功
 *   -1              - 输入参数验证失败
 *   -2              - 内存分配失败
 *   -3              - 文件写入失败
 *   -4              - 加载命令溢出
 */
int macho_generate_image(const char *output_file,
                        const uint8_t *actflow_code,
                        size_t actflow_size,
                        const uint8_t *mirror_data,
                        size_t mirror_size,
                        const uint8_t *constant_data,
                        size_t constant_size,
                        uint64_t phys_base);

/**
 * macho_weaver_create - 创建织机上下文
 * 
 * 参数：
 *   phys_base - 物理基址
 *   verbose   - 是否输出详细日志
 * 
 * 返回：上下文指针，失败返回 NULL
 */
macho_weaver_context_t* macho_weaver_create(uint64_t phys_base, int verbose);

/**
 * macho_weaver_destroy - 销毁织机上下文，释放所有资源
 */
void macho_weaver_destroy(macho_weaver_context_t *ctx);

/**
 * macho_weaver_add_zones - 添加 Aethelium 的三个区块到织机
 * 
 * 返回值：
 *   0   - 成功
 *   -1  - 验证失败（大小溢出等）
 */
int macho_weaver_add_zones(macho_weaver_context_t *ctx,
                          const uint8_t *actflow,
                          size_t actflow_size,
                          const uint8_t *mirror,
                          size_t mirror_size,
                          const uint8_t *constant,
                          size_t constant_size);

/**
 * macho_weaver_emit - 从织机上下文生成完整的 Mach-O 二进制
 * 
 * 返回值：
 *   0   - 成功（ctx->output_buffer 含有有效数据，ctx->output_size 是大小）
 *   -1  - 加载命令计算失败
 *   -2  - 内存不足
 */
int macho_weaver_emit(macho_weaver_context_t *ctx);

/**
 * macho_weaver_write - 将织机结果写入文件
 * 
 * 返回值：
 *   0   - 成功
 *   -1  - 文件打开失败
 *   -2  - 文件写入失败
 */
int macho_weaver_write(macho_weaver_context_t *ctx, const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* AETHEL_MACHO_WEAVER_H */
