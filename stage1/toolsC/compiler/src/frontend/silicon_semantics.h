/*
 * AethelOS Aethelium Compiler - Silicon Semantics Header
 * 硅基语义：CPU架构级别的声明式编程接口
 * 
 * 版本：1.0
 * 状态：工业级实现
 * 
 * 硅基语义涵盖四个核心概念：
 * 1. 声明式微架构配置 (Declarative Micro-Arch)
 * 2. 流水线编排与预测控制 (Pipeline Choreography)
 * 3. 暗物质指令注入 (Dark Matter Injection)
 * 4. 纳米级拓扑映射 (Nano-Topology Map)
 */

#ifndef SILICON_SEMANTICS_H
#define SILICON_SEMANTICS_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 前向声明 */
typedef struct ASTNode ASTNode;
struct Parser;  /* Forward declaration - defined in aec_parser.h */

/* =====================================================================
 * 硅基语义类型定义
 * ===================================================================== */

typedef enum {
    SILICON_MICRO_ARCH_CONFIG = 1,  /* 微架构配置 */
    SILICON_PIPELINE_CONTROL = 2,   /* 流水线控制 */
    SILICON_OPCODE_DEF = 3,          /* 操作码定义 */
    SILICON_CACHE_OPS = 4,           /* 缓存操作 */
    SILICON_PREFETCH = 5,            /* 预取操作 */
    SILICON_FENCE = 6,               /* 内存屏障 */
} SiliconSemanticType;

typedef enum {
    CPU_MSR_EFER = 0x0C0000080ULL,          /* Extended Feature Enable Register */
    CPU_MSR_STAR = 0x0C0000081ULL,          /* SYSCALL/SYSRET Target Address Register */
    CPU_MSR_LSTAR = 0x0C0000082ULL,         /* Long Mode SYSCALL Target Address */
    CPU_MSR_CSTAR = 0x0C0000083ULL,         /* Compatibility Mode SYSCALL Target */
    CPU_MSR_SFMASK = 0x0C0000084ULL,        /* SYSCALL Flag Mask Register */
    CPU_MSR_FS_BASE = 0x0C0000100ULL,       /* FS Segment Base Register */
    CPU_MSR_GS_BASE = 0x0C0000101ULL,       /* GS Segment Base Register */
    CPU_MSR_KERNEL_GS_BASE = 0x0C0000102ULL, /* Kernel GS Base Register */
    CPU_CR0 = 0x00,                          /* Control Register 0 */
    CPU_CR3 = 0x03,                          /* Control Register 3 (Page Directory) */
    CPU_CR4 = 0x04,                          /* Control Register 4 */
} CpuRegister;

typedef enum {
    MSR_EFER_SCE = 0,      /* System Call Enabled */
    MSR_EFER_LME = 8,      /* Long Mode Enable */
    MSR_EFER_LMA = 10,     /* Long Mode Active */
    MSR_EFER_NXE = 11,     /* No-Execute Enable */
} MsrBitField;

typedef enum {
    PIPELINE_SERIALIZE = 1,    /* 严格顺序执行 */
    PIPELINE_BLOCK = 2,        /* 阻止推测执行 */
    PIPELINE_NOMEM = 4,        /* 无影响内存状态 */
} PipelineBehavior;

typedef enum {
    PIPELINE_BARRIER_LOAD = 1,
    PIPELINE_BARRIER_STORE = 2,
    PIPELINE_BARRIER_FULL = 3,  /* lfence, sfence, mfence */
} PipelineBarrierType;

typedef enum {
    PIPELINE_HINT_BRANCH_TAKEN = 1,
    PIPELINE_HINT_BRANCH_NOT_TAKEN = 2,
    PIPELINE_HINT_STRONG = 4,
} PipelineHintType;

/* =====================================================================
 * 硅基语义节点数据结构
 * ===================================================================== */

typedef struct {
    char *register_name;        /* 寄存器名称 (如 "MSR/EFER", "CR0") */
    char *property_name;        /* 属性名称 (如 "Syscall/Enable") */
    ASTNode *value;             /* 赋值的表达式 */
    uint64_t msr_addr;          /* MSR地址（仅当是MSR时有效） */
    MsrBitField bit_field;      /* MSR位字段 */
} MicroArchConfig;

typedef struct {
    uint32_t behavior_flags;    /* 行为标志 (PIPELINE_SERIALIZE等) */
    uint32_t speculation_flags; /* 推测控制标志 */
    ASTNode **statements;       /* 块内的语句 */
    int stmt_count;
    int capacity;
} PipelineBlock;

typedef struct {
    char *barrier_type;         /* "memory/load/store", "full", 等 */
    PipelineBarrierType type;
} PipelineBarrier;

typedef struct {
    char *hint_type;            /* "branch/taken/strong" 等 */
    PipelineHintType hint;
} PipelineHint;

typedef struct {
    char *opcode_name;          /* 操作码符号名 (如 "Op/Halt/NoCheck") */
    char *prefix;               /* 前缀字节（十六进制字符串） */
    unsigned char prefix_byte;
    char *opcode;               /* 操作码字节（十六进制字符串） */
    unsigned char opcode_byte;
    char **operand_positions;   /* 参数占位符位置（如 "$val") */
    int operand_count;
    char *raw_bytes;            /* 完整的十六进制定义 */
} SyntaxOpcodeDef;

typedef struct {
    char *cache_line_name;      /* 缓存行的引用 */
    ASTNode *address;           /* 缓存行地址 */
} CacheOperation;

typedef struct {
    char *memory_region;        /* 内存区域名称 */
    ASTNode *address;           /* 预取地址 */
    char *hint;                 /* 预取提示 (T0, T1, T2, NTA) */
    int intent;                 /* 预取意图 (READ/WRITE) */
} PrefetchOperation;

/* =====================================================================
 * 硅基语义处理函数声明
 * ===================================================================== */

/**
 * 解析微架构配置块
 * 语法: using CPU/Current { MSR/EFER\Syscall/Enable = true ... }
 */
ASTNode* parse_microarch_config(struct Parser *parser);

/**
 * 解析流水线块
 * 语法: pipeline(behavior: .serialize, speculation: .block) { ... }
 */
ASTNode* parse_pipeline_block(struct Parser *parser);

/**
 * 解析管线屏障操作
 * 语法: pipeline\barrier(.memory/load/store)
 */
ASTNode* parse_pipeline_barrier(struct Parser *parser);

/**
 * 解析管线提示
 * 语法: pipeline\hint(.branch/taken/strong)
 */
ASTNode* parse_pipeline_hint(struct Parser *parser);

/**
 * 解析操作码定义
 * 语法: syntax/opcode Op/Halt/NoCheck = bytes "F1 C0"
 */
SyntaxOpcodeDef* parse_syntax_opcode(struct Parser *parser);

/**
 * 解析缓存操作
 * 语法: silicon\cache\flush(line) 或 silicon\cache\invalidate(line)
 */
ASTNode* parse_cache_operation(struct Parser *parser);

/**
 * 解析预取操作
 * 语法: silicon\prefetch(line, hint: .T0, intent: .write)
 */
ASTNode* parse_prefetch_operation(struct Parser *parser);

/* =====================================================================
 * 硅基语义代码生成函数声明
 * ===================================================================== */

/**
 * 生成微架构配置指令
 */
void silicon_gen_microarch_config(FILE *out, MicroArchConfig *config);

/**
 * 生成流水线控制指令
 */
void silicon_gen_pipeline_control(FILE *out, PipelineBlock *pipeline);

/**
 * 生成缓存操作指令
 */
void silicon_gen_cache_op(FILE *out, CacheOperation *cache_op);

/**
 * 生成预取指令
 */
void silicon_gen_prefetch(FILE *out, PrefetchOperation *prefetch_op);

/**
 * 将MSR地址和比特字段转换为wrmsr指令序列
 * 返回值: 生成的汇编指令字符串
 */
char* silicon_msr_to_asm(uint64_t msr_addr, MsrBitField field, int set_value);

/**
 * 生成管线屏障指令（lfence/sfence/mfence）
 */
void silicon_gen_barrier(FILE *out, PipelineBarrierType barrier_type);

/**
 * 不安全性检验：确保silicon块仅在Rimport后可用
 */
int silicon_check_rimport_context(void);

#endif /* SILICON_SEMANTICS_H */
