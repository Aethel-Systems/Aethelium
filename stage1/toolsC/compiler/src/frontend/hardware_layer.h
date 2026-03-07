/*
 * AethelOS Aethelium Compiler - Hardware Layer Header
 * 硬件层规范：零行汇编内核与驱动开发接口
 * 
 * 版本：1.0 (Industrial Grade)
 * 状态：完整实现
 * 
 * 本头文件定义了Aethelium硬件层（Hardware Layer）的完整语义、数据结构
 * 和处理接口。它与应用层、系统层、原初层构成了Aethelium的四层核心。
 */

#ifndef AETHELIUM_HARDWARE_LAYER_H
#define AETHELIUM_HARDWARE_LAYER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 前向声明 */
typedef struct ASTNode ASTNode;
struct Parser;

/* =====================================================================
 * 硬件层节点类型定义
 * ===================================================================== */

typedef enum {
    /* 核心硬件层AST节点 */
    HW_BLOCK = 200,                   /* hardware { ... } 块 */
    HW_REC_BINDING = 201,             /* reg<"rax", UInt64> 物理寄存器绑定 */
    HW_PORT_IO = 202,                 /* port<T> 端口I/O类型 */
    HW_VOLATILE_VIEW = 203,           /* @volatile view<T> 挥发性视图 */
    HW_GATE_FUNC = 204,               /* @gate(type: \interrupt) 门装饰器 */
    HW_NAKED_FUNC = 205,              /* @naked 裸体函数 */
    HW_MORPH_BLOCK = 206,             /* morph { ... } 形态置换块 */
    HW_ISA_CALL = 207,                /* hardware\isa\syscall() ISA直通调用 */
    HW_VECTOR_TYPE = 208,             /* vector<T, N> SIMD向量类型 */
    HW_CONTROL_REG = 209,             /* CPU/Current\Control\CR0 控制寄存器访问 */
    HW_FLAG_ACCESS = 210,             /* CPU/Flags\Carry CPU标志位访问 */
    HW_STATE_SNAPSHOT = 211,          /* hardware\state\snapshot() 状态快照 */
} HardwareNodeType;

/* =====================================================================
 * 物理寄存器绑定类型
 * ===================================================================== */

typedef enum {
    REG_CLASS_GPR,          /* 通用寄存器 (RAX, RBX, RCX, ...) */
    REG_CLASS_CONTROL,      /* 控制寄存器 (CR0, CR3, CR4) */
    REG_CLASS_MSR,          /* 机器特定寄存器 (MSR_EFER, MSR_LSTAR) */
    REG_CLASS_FLAGS,        /* 标志寄存器 (RFLAGS) */
    REG_CLASS_SEGMENT,      /* 段寄存器 (CS, DS, SS) */
    REG_CLASS_VECTOR,       /* 向量寄存器 (XMM, YMM, ZMM) */
} RegisterClass;

typedef struct {
    char *reg_name;         /* 寄存器名称 ("rax", "cr0", "msr_efer") */
    RegisterClass reg_class; /* 寄存器类别 */
    uint64_t reg_id;        /* 硬件ID (0-15 for GPR, MSR address, etc.) */
    ASTNode *bound_type;    /* 变量绑定的类型 (UInt64, UInt32, etc.) */
    int is_volatile;        /* 是否挥发性 */
    int size_bytes;         /* 寄存器大小（字节） */
} RegisterBinding;

typedef struct {
    char *var_name;         /* 变量名称 */
    RegisterBinding binding; /* 绑定信息 */
    int spill_offset;       /* 溢出时的栈偏移（-1表示未溢出） */
} BoundVariable;

/* =====================================================================
 * 端口与MMIO类型
 * ===================================================================== */

typedef enum {
    IO_MODE_IN,             /* in 指令 (读取端口) */
    IO_MODE_OUT,            /* out 指令 (写入端口) */
    IO_MODE_MMIO,           /* MMIO映射（内存操作） */
} IOMode;

typedef struct {
    uint16_t port_number;   /* 端口号 (0-65535) */
    IOMode mode;            /* I/O模式 */
    ASTNode *element_type;  /* 元素类型 (UInt8, UInt16, UInt32, UInt64) */
    int element_size;       /* 元素大小（字节） */
} PortBinding;

typedef struct {
    uint64_t phys_addr;     /* 物理地址 */
    ASTNode *mapped_type;   /* 映射的结构体类型 */
    int is_volatile;        /* 是否挥发性（禁用缓存） */
    int access_size;        /* 访问大小（1,2,4,8字节） */
} MMIOView;

/* =====================================================================
 * ABI门与函数修饰
 * =====================================================================*/

typedef enum {
    GATE_TYPE_INTERRUPT,    /* @gate(type: \interrupt) 硬件中断处理 */
    GATE_TYPE_SYSCALL,      /* @gate(type: \syscall) 系统调用入口 */
    GATE_TYPE_EFI,          /* @gate(type: \efi) UEFI应用入口 */
    GATE_TYPE_NAKED,        /* @gate(type: \naked) 裸体函数（无栈帧） */
    GATE_TYPE_EXCEPTION,    /* @gate(type: \exception) 异常处理 */
} GateType;

typedef struct {
    GateType gate_type;                /* 门类型 */
    int save_all_registers;            /* 是否保存所有寄存器 */
    int generate_prologue;             /* 是否生成函数序言 */
    int generate_epilogue;             /* 是否生成函数尾声 */
    char *return_instruction;          /* 返回指令 (ret, iretq, sysretq) */
    int context_frame_size;            /* 上下文帧大小 */
} GateDescriptor;

typedef struct {
    char *frame_type_name;             /* 栈帧结构体类型名 */
    ASTNode *frame_type;               /* 栈帧结构体类型AST */
    int is_packed;                     /* 是否@packed */
    uint64_t cpu_push_mask;            /* CPU硬件压栈的寄存器掩码 */
} TrapFrame;

/* =====================================================================
 * 形态置换语义
 * ===================================================================== */

typedef struct {
    char *target_state_name;           /* 目标执行状态名称 */
    ASTNode *stack_pointer;            /* 目标栈指针值 */
    ASTNode *instruction_pointer;      /* 目标指令指针值 */
    ASTNode *control_registers[4];     /* CR0, CR3, CR4等 */
    int is_far_jump;                   /* 是否跨段跳转（远跳转） */
    uint16_t target_segment;           /* 目标段选择子 */
    int morph_type;                    /* 形态置换类型 (JMP, IRET, SYSRET) */
} MorphTarget;

/* =====================================================================
 * ISA直通指令集操作
 * ===================================================================== */

typedef enum {
    ISA_SYSCALL,            /* syscall 系统调用 */
    ISA_SYSRET,             /* sysret 系统调用返回 */
    ISA_CPUID,              /* cpuid CPU信息 */
    ISA_LGDT,               /* lgdt 加载GDT */
    ISA_LIDT,               /* lidt 加载IDT */
    ISA_LTR,                /* ltr 加载TSS */
    ISA_INVLPG,             /* invlpg TLB刷新 */
    ISA_WBINVD,             /* wbinvd 缓存回写和无效化 */
    ISA_CLFLUSH,            /* clflush 缓存行冲刷 */
    ISA_MFENCE,             /* mfence 完全内存屏障 */
    ISA_SFENCE,             /* sfence 存储屏障 */
    ISA_LFENCE,             /* lfence 加载屏障 */
    ISA_PAUSE,              /* pause CPU延迟 */
    ISA_HLT,                /* hlt CPU暂停 */
    ISA_IRET,               /* iret/iretq 中断返回 */
    ISA_RDMSR,              /* rdmsr 读MSR */
    ISA_WRMSR,              /* wrmsr 写MSR */
    ISA_ADC,                /* adc 带进位加法 */
    ISA_MOV,                /* mov 移动 */
    ISA_ADD,                /* add 加法 */
    ISA_SUB,                /* sub 减法 */
    ISA_XOR,                /* xor 异或 */
    ISA_AND,                /* and 与 */
    ISA_OR,                 /* or 或 */
} ISAOpcode;

typedef struct {
    ISAOpcode opcode;                  /* 操作码 */
    char *opcode_name;                 /* 助记符 */
    ASTNode **operands;                /* 操作数 */
    int operand_count;                 /* 操作数个数 */
    int prefix_byte;                   /* 前缀字节 (REX, 0x66, LOCK) */
    int primary_opcode;                /* 主操作码字节 */
    int secondary_opcode;              /* 副操作码字节（如果需要） */
    int modRM_byte;                    /* ModR/M字节 */
    uint64_t immediate;                /* 立即数 */
    int immediate_size;                /* 立即数大小（1,2,4,8） */
} ISAInstruction;

/* =====================================================================
 * SIMD向量类型与操作
 * ===================================================================== */

typedef struct {
    ASTNode *element_type;             /* 元素类型 (UInt8, UInt32, etc.) */
    int element_count;                 /* 元素个数 (N) */
    int vector_size_bits;              /* 向量大小 (128, 256, 512) */
    int register_class;                /* 寄存器类: XMM(128), YMM(256), ZMM(512) */
} VectorType;

typedef struct {
    char *reg_name;                    /* 向量寄存器名 (ymm0, zmm5, etc.) */
    VectorType vec_type;               /* 向量类型信息 */
    int is_bound;                      /* 是否已绑定 */
} BoundVectorRegister;

/* =====================================================================
 * 硬件层编译环境
 * ===================================================================== */

typedef struct {
    BoundVariable *bound_vars;         /* 已绑定的变量表 */
    int bound_var_count;
    int bound_var_capacity;
    
    PortBinding *port_bindings;        /* 端口绑定表 */
    int port_count;
    int port_capacity;
    
    MMIOView *mmio_views;              /* MMIO视图表 */
    int mmio_count;
    int mmio_capacity;
    
    ISAInstruction *isa_cache;         /* ISA指令缓存 */
    int isa_cache_count;
    int isa_cache_capacity;
    
    BoundVectorRegister *vector_regs;  /* 绑定的向量寄存器 */
    int vector_reg_count;
    int vector_reg_capacity;
    
    int current_function_gate_type;    /* 当前函数的门类型 */
    TrapFrame *current_trap_frame;     /* 当前函数的异常帧 */
    
    uint64_t spill_offset;             /* 栈溢出偏移量 */
    int in_morph_block;                /* 是否在morph块中 */
    int morph_count;                   /* morph块计数 */
} HardwareContext;

/* =====================================================================
 * X86-64编码器数据结构
 * ===================================================================== */

typedef enum {
    /* ModR/M编码 */
    MOD_DIRECT = 3,                    /* 直接寄存器寻址 */
    MOD_INDIRECT = 0,                  /* 间接寮址 */
    MOD_INDIRECT_DISP8 = 1,            /* 间接+8位偏移 */
    MOD_INDIRECT_DISP32 = 2,           /* 间接+32位偏移 */
} ModRMMode;

typedef struct {
    unsigned char prefix;              /* REX前缀 */
    unsigned char primary_opcode;      /* 主操作码 */
    unsigned char secondary_opcode;    /* 副操作码（可选） */
    int modRM_byte;                    /* ModR/M字节 */
    uint64_t immediate;                /* 立即数 */
    int immediate_size;                /* 立即数大小 */
    int displacement;                  /* 内存寻址偏移 */
    int displacement_size;             /* 偏移大小 */
} X86Instruction;

/* =====================================================================
 * 硬件层处理函数声明
 * ===================================================================== */

/**
 * 创建硬件编译环境
 * @return 初始化的硬件上下文
 */
HardwareContext* hw_context_create(void);

/**
 * 销毁硬件编译环境
 * @param ctx 硬件上下文
 */
void hw_context_destroy(HardwareContext *ctx);

/**
 * 验证物理寄存器绑定
 * @param ctx 硬件上下文
 * @param binding 寄存器绑定
 * @return 0表示有效，非0表示错误
 */
int hw_validate_register_binding(HardwareContext *ctx, RegisterBinding *binding);

/**
 * 验证端口I/O操作
 * @param ctx 硬件上下文
 * @param port_binding 端口绑定
 * @return 0表示有效，非0表示错误
 */
int hw_validate_port_io(HardwareContext *ctx, PortBinding *port_binding);

/**
 * 绑定向量寄存器
 * @param ctx 硬件上下文
 * @param reg_name 寄存器名称
 * @param vec_type 向量类型
 * @return 0表示成功
 */
int hw_bind_vector_register(HardwareContext *ctx, const char *reg_name, VectorType *vec_type);

/**
 * 生成X86-64指令编码
 * @param instr X86指令
 * @param output 输出缓冲区
 * @param output_size 缓冲区大小
 * @return 生成字节数
 */
int hw_x86_encode_instruction(X86Instruction *instr, unsigned char *output, int output_size);

/**
 * 解析ISA直通调用
 * @param parser 解析器
 * @param ctx 硬件上下文
 * @return 解析后的ISA指令AST节点
 */
ASTNode* hw_parse_isa_call(struct Parser *parser, HardwareContext *ctx);

/**
 * 验证morph块的合法性
 * @param ctx 硬件上下文
 * @param morph_target 形态置换目标
 * @return 0表示有效
 */
int hw_validate_morph(HardwareContext *ctx, MorphTarget *morph_target);

/**
 * 检查硬件约束冲突
 * @param ctx 硬件上下文
 * @param instr ISA指令
 * @return 0表示无冲突
 */
int hw_check_constraints(HardwareContext *ctx, ISAInstruction *instr);

/**
 * 添加已绑定变量
 * @param ctx 硬件上下文
 * @param var_name 变量名
 * @param binding 寄存器绑定
 * @return 0表示成功
 */
int hw_add_bound_variable(HardwareContext *ctx, const char *var_name, RegisterBinding *binding);

/**
 * 获取已绑定变量信息
 * @param ctx 硬件上下文
 * @param var_name 变量名
 * @return 变量信息指针，未找到返回NULL
 */
BoundVariable* hw_get_bound_variable(HardwareContext *ctx, const char *var_name);

/**
 * 生成 ISA 操作的机器码 - 工业级实现
 * @param operation: ISA操作名称 ("syscall", "sysret", "cli", "sti" 等)
 * @param opcode_bytes: 输出缓冲区（至少16字节）
 * @return: 生成的操作码字节数，或 -1 如果操作未识别
 */
int hw_generate_isa_opcode(const char *operation, uint8_t *opcode_bytes);

/**
 * CPU 控制寄存器访问机器码生成
 */
int hw_generate_control_reg_read(const char *cr_name, uint8_t *opcode_bytes);
int hw_generate_control_reg_write(const char *cr_name, uint8_t *opcode_bytes);

/**
 * CPU RFLAGS 标志位操作
 */
int hw_generate_flag_read(uint8_t *opcode_bytes);
int hw_generate_flag_write(uint8_t *opcode_bytes);
int hw_generate_cli(uint8_t *opcode_bytes);
int hw_generate_sti(uint8_t *opcode_bytes);

/**
 * 参数化 ISA 指令
 */
int hw_generate_lgdt(uint8_t *opcode_bytes);
int hw_generate_lidt(uint8_t *opcode_bytes);
int hw_generate_cpuid(uint8_t *opcode_bytes);
int hw_generate_clflush(uint8_t *opcode_bytes);

/**
 * MSR（模型特定寄存器）访问
 */
int hw_generate_rdmsr(uint8_t *opcode_bytes);
int hw_generate_wrmsr(uint8_t *opcode_bytes);
int hw_generate_rdtsc(uint8_t *opcode_bytes);
int hw_generate_rdpmc(uint8_t *opcode_bytes);

#endif /* AETHELIUM_HARDWARE_LAYER_H */
