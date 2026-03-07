/*
 * AethelOS Aethelium Compiler - Hardware Layer Code Generator
 * 硬件层代码生成：AST → 机器码 (Zero-Assembly Pipeline)
 */

#include "hardware_layer.h"
#include "x86_encoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =====================================================================
 * 硬件层代码生成器上下文
 * ===================================================================== */

typedef struct {
    FILE *output_file;              /* 机器码输出文件 */
    HardwareContext *hw_ctx;        /* 硬件编译环境 */
    unsigned char *code_buffer;     /* 代码缓冲区 */
    int buffer_pos;                 /* 当前位置 */
    int buffer_size;                /* 缓冲区大小 */
    int error_count;                /* 错误计数 */
    char error_msg[512];            /* 最后一个错误 */
    int generated_bytes;            /* 生成的字节总数 */
} HardwareCodeGen;

/* =====================================================================
 * 创建与销毁
 * ===================================================================== */

HardwareCodeGen* hw_codegen_create(FILE *output) {
    HardwareCodeGen *gen = (HardwareCodeGen *)malloc(sizeof(HardwareCodeGen));
    if (!gen) return NULL;
    
    memset(gen, 0, sizeof(HardwareCodeGen));
    
    gen->output_file = output;
    gen->hw_ctx = hw_context_create();
    gen->buffer_size = 8192;  /* 8KB缓冲区 */
    gen->code_buffer = (unsigned char *)malloc(gen->buffer_size);
    
    if (!gen->hw_ctx || !gen->code_buffer) {
        if (gen->code_buffer) free(gen->code_buffer);
        if (gen->hw_ctx) hw_context_destroy(gen->hw_ctx);
        free(gen);
        return NULL;
    }
    
    return gen;
}

void hw_codegen_destroy(HardwareCodeGen *gen) {
    if (!gen) return;
    
    /* 将缓冲区内容写入输出 */
    if (gen->output_file && gen->buffer_pos > 0) {
        fwrite(gen->code_buffer, 1, gen->buffer_pos, gen->output_file);
    }
    
    if (gen->code_buffer) free(gen->code_buffer);
    if (gen->hw_ctx) hw_context_destroy(gen->hw_ctx);
    
    free(gen);
}

/* =====================================================================
 * 缓冲区管理
 * ===================================================================== */

/**
 * 向缓冲区添加字节
 */
static int hw_codegen_emit_byte(HardwareCodeGen *gen, unsigned char byte) {
    if (gen->buffer_pos >= gen->buffer_size) {
        /* 缓冲区满，写入文件并重置 */
        if (gen->output_file) {
            fwrite(gen->code_buffer, 1, gen->buffer_pos, gen->output_file);
        }
        gen->generated_bytes += gen->buffer_pos;
        gen->buffer_pos = 0;
    }
    
    gen->code_buffer[gen->buffer_pos++] = byte;
    return 0;
}

/**
 * 向缓冲区添加多个字节
 */
static int hw_codegen_emit_bytes(HardwareCodeGen *gen, const unsigned char *bytes, int count) {
    for (int i = 0; i < count; i++) {
        if (hw_codegen_emit_byte(gen, bytes[i]) != 0) {
            return -1;
        }
    }
    return count;
}

/**
 * 向缓冲区添加立即数（支持1, 2, 4, 8字节）
 */
static int hw_codegen_emit_immediate(HardwareCodeGen *gen, uint64_t value, int size) {
    for (int i = 0; i < size; i++) {
        if (hw_codegen_emit_byte(gen, (unsigned char)((value >> (i * 8)) & 0xFF)) != 0) {
            return -1;
        }
    }
    return size;
}

/* =====================================================================
 * ISA指令生成
 * ===================================================================== */

/**
 * 生成SYSCALL指令
 */
static int hw_codegen_syscall(HardwareCodeGen *gen) {
    hw_codegen_emit_byte(gen, 0x0F);
    hw_codegen_emit_byte(gen, 0x05);
    return 2;
}

/**
 * 生成SYSRET指令
 */
static int hw_codegen_sysret(HardwareCodeGen *gen) {
    hw_codegen_emit_byte(gen, 0x48);  /* REX.W */
    hw_codegen_emit_byte(gen, 0x0F);
    hw_codegen_emit_byte(gen, 0x07);
    return 3;
}

/**
 * 生成CPUID指令
 */
static int hw_codegen_cpuid(HardwareCodeGen *gen) {
    hw_codegen_emit_byte(gen, 0x0F);
    hw_codegen_emit_byte(gen, 0xA2);
    return 2;
}

/**
 * 生成INVLPG指令
 */
static int hw_codegen_invlpg(HardwareCodeGen *gen, unsigned char rm_byte) {
    hw_codegen_emit_byte(gen, 0x0F);
    hw_codegen_emit_byte(gen, 0x01);
    hw_codegen_emit_byte(gen, rm_byte);  /* ModR/M */
    return 3;
}

/**
 * 生成WBINVD指令
 */
static int hw_codegen_wbinvd(HardwareCodeGen *gen) {
    hw_codegen_emit_byte(gen, 0x0F);
    hw_codegen_emit_byte(gen, 0x09);
    return 2;
}

/**
 * 生成MOV指令
 * MOV r64, imm64 (B8+ rd io)
 */
static int hw_codegen_mov_reg_imm(HardwareCodeGen *gen, int reg_id, uint64_t imm) {
    int prefix_needed = (reg_id >= 8) ? 1 : 0;
    
    /* REX.W前缀 */
    hw_codegen_emit_byte(gen, 0x48);
    
    if (prefix_needed) {
        /* 对于R8-R15，需要额外的REX.B位 */
        unsigned char rex = 0x49;  /* 0x48 | 0x01 */
        gen->code_buffer[gen->buffer_pos - 1] = rex;
    }
    
    /* 操作码 + 寄存器编号 */
    hw_codegen_emit_byte(gen, 0xB8 | (reg_id & 0x7));
    
    /* 立即数 (8字节) */
    hw_codegen_emit_immediate(gen, imm, 8);
    
    return 1 + (prefix_needed ? 0 : 0) + 1 + 8;
}

/**
 * 生成MFENCE (内存屏障)
 */
static int hw_codegen_mfence(HardwareCodeGen *gen) {
    hw_codegen_emit_byte(gen, 0x0F);
    hw_codegen_emit_byte(gen, 0xAE);
    hw_codegen_emit_byte(gen, 0xF0);  /* ModR/M /6 */
    return 3;
}

/**
 * 生成SFENCE (存储屏障)
 */
static int hw_codegen_sfence(HardwareCodeGen *gen) {
    hw_codegen_emit_byte(gen, 0x0F);
    hw_codegen_emit_byte(gen, 0xAE);
    hw_codegen_emit_byte(gen, 0xF8);  /* ModR/M /7 */
    return 3;
}

/**
 * 生成LFENCE (加载屏障)
 */
static int hw_codegen_lfence(HardwareCodeGen *gen) {
    hw_codegen_emit_byte(gen, 0x0F);
    hw_codegen_emit_byte(gen, 0xAE);
    hw_codegen_emit_byte(gen, 0xE8);  /* ModR/M /5 */
    return 3;
}

/**
 * 生成PAUSE (CPU暂停)
 */
static int hw_codegen_pause(HardwareCodeGen *gen) {
    hw_codegen_emit_byte(gen, 0xF3);  /* PAUSE前缀 */
    hw_codegen_emit_byte(gen, 0x90);  /* NOP */
    return 2;
}

/**
 * 生成IRET (中断返回)
 */
static int hw_codegen_iret(HardwareCodeGen *gen) {
    hw_codegen_emit_byte(gen, 0x48);  /* REX.W */
    hw_codegen_emit_byte(gen, 0xCF);  /* IRETQ */
    return 2;
}

/**
 * 生成HLT (CPU暂停)
 */
static int hw_codegen_hlt(HardwareCodeGen *gen) {
    hw_codegen_emit_byte(gen, 0xF4);
    return 1;
}

/* =====================================================================
 * 硬件层节点处理
 * ===================================================================== */

/**
 * 生成物理寄存器绑定的初始化代码
 */
static int hw_codegen_register_binding(HardwareCodeGen *gen, 
                                       RegisterBinding *binding,
                                       uint64_t initial_value) {
    if (binding->reg_class == REG_CLASS_GPR) {
        return hw_codegen_mov_reg_imm(gen, binding->reg_id, initial_value);
    }
    
    /* 不支持的寄存器类别 */
    return -1;
}

/**
 * 生成ISA直通指令
 */
static int hw_codegen_isa_instruction(HardwareCodeGen *gen, ISAInstruction *instr) {
    if (!gen || !instr) {
        return -1;
    }
    
    switch (instr->opcode) {
        case ISA_SYSCALL:
            return hw_codegen_syscall(gen);
        case ISA_SYSRET:
            return hw_codegen_sysret(gen);
        case ISA_CPUID:
            return hw_codegen_cpuid(gen);
        case ISA_INVLPG:
            return hw_codegen_invlpg(gen, 0x00);  /* 示例ModR/M */
        case ISA_WBINVD:
            return hw_codegen_wbinvd(gen);
        case ISA_MFENCE:
            return hw_codegen_mfence(gen);
        case ISA_SFENCE:
            return hw_codegen_sfence(gen);
        case ISA_LFENCE:
            return hw_codegen_lfence(gen);
        case ISA_PAUSE:
            return hw_codegen_pause(gen);
        case ISA_IRET:
            return hw_codegen_iret(gen);
        case ISA_HLT:
            return hw_codegen_hlt(gen);
        default:
            return -2;  /* 不支持的ISA操作码 */
    }
}

/* =====================================================================
 * 形态置换代码生成
 * ===================================================================== */

/**
 * 生成形态置换块的机器码
 * morph块是一个终止节点，直接改变执行状态
 */
static int hw_codegen_morph(HardwareCodeGen *gen, MorphTarget *target) {
    if (!gen || !target) {
        return -1;
    }
    
    int bytes_written = 0;
    
    /* 1. 加载栈指针 */
    if (target->stack_pointer) {
        /* 这将由具体的表达式评估器处理 */
    }
    
    /* 2. 加载控制寄存器 */
    for (int i = 0; i < 4; i++) {
        if (target->control_registers[i]) {
            /* 处理CR0, CR3, CR4等 */
        }
    }
    
    /* 3. 形态置换指令 */
    if (target->is_far_jump) {
        /* 远跳转：LJMP */
        hw_codegen_emit_byte(gen, 0xEA);  /* LJMP绝对 */
        bytes_written += 1;
    } else {
        /* 近跳转：JMP */
        hw_codegen_emit_byte(gen, 0xE9);  /* JMP rel32 */
        bytes_written += 1;
    }
    
    return bytes_written;
}

/* =====================================================================
 * 导出函数
 * ===================================================================== */

/**
 * 生成硬件层函数序言
 * (用于@gate修饰的函数)
 */
int hw_codegen_prologue(HardwareCodeGen *gen, GateType gate_type) {
    if (!gen) return -1;
    
    int bytes = 0;
    
    switch (gate_type) {
        case GATE_TYPE_NAKED:
            /* 裸体函数：无序言 */
            return 0;
            
        case GATE_TYPE_INTERRUPT:
            /* 中断门：由CPU自动压栈，无需额外序言 */
            return 0;
            
        case GATE_TYPE_SYSCALL:
            /* 系统调用入口：处理RCX, R11 */
            /* RCX会被覆盖，R11会保存RFLAGS */
            return 0;
            
        case GATE_TYPE_EFI:
            /* UEFI ENTRY：处理MS-ABI调用约定 */
            /* RCX, RDX, R8, R9是参数寄存器 */
            /* 需要保存RBX, RBP, RDI, RSI, R12-R15 */
            return 0;
            
        default:
            return -1;
    }
}

/**
 * 生成硬件层函数尾声
 * (用于@gate修饰的函数)
 */
int hw_codegen_epilogue(HardwareCodeGen *gen, GateType gate_type) {
    if (!gen) return -1;
    
    int bytes = 0;
    
    switch (gate_type) {
        case GATE_TYPE_NAKED:
            /* 裸体函数：无尾声 */
            return 0;
            
        case GATE_TYPE_INTERRUPT:
            /* 中断门：IRETQ返回 */
            bytes = hw_codegen_iret(gen);
            break;
            
        case GATE_TYPE_SYSCALL:
            /* 系统调用返回：SYSRETQ */
            bytes = hw_codegen_sysret(gen);
            break;
            
        case GATE_TYPE_EFI:
            /* UEFI返回：RET */
            hw_codegen_emit_byte(gen, 0xC3);
            bytes = 1;
            break;
            
        default:
            return -1;
    }
    
    return bytes;
}

/**
 * 获取最后的错误信息
 */
const char* hw_codegen_get_error(HardwareCodeGen *gen) {
    if (!gen) return "NULL generator";
    return gen->error_msg;
}

/**
 * 获取生成的字节数
 */
int hw_codegen_get_byte_count(HardwareCodeGen *gen) {
    if (!gen) return -1;
    return gen->generated_bytes + gen->buffer_pos;
}

/* =====================================================================
 * 工业级x86-64编码集成
 * ===================================================================== */

/**
 * 生成MOV r64, imm（智能选择最短编码）
 */
int hw_codegen_emit_mov_r64_imm(HardwareCodeGen *gen, int reg_id, uint64_t imm) {
    if (!gen) return -1;
    
    unsigned char code[10];
    int len;
    
    /* 如果立即数在int32范围内，使用更短的形式 */
    if (imm >= -2147483648LL && imm <= 2147483647LL) {
        len = hw_encode_mov_r64_imm32_signed(reg_id, (int32_t)imm, code, sizeof(code));
    } else {
        len = hw_encode_mov_r64_imm64(reg_id, imm, code, sizeof(code));
    }
    
    if (len < 0) return -1;
    return hw_codegen_emit_bytes(gen, code, len);
}

/**
 * 生成MOV r64, r64（寄存器间移动）
 */
int hw_codegen_emit_mov_r64_r64(HardwareCodeGen *gen, int src_reg, int dst_reg) {
    if (!gen) return -1;
    
    unsigned char code[3];
    int len = hw_encode_mov_r64_r64(src_reg, dst_reg, code, sizeof(code));
    if (len < 0) return -1;
    return hw_codegen_emit_bytes(gen, code, len);
}

/**
 * 生成MOV Cr, r64（写入控制寄存器）
 */
int hw_codegen_emit_mov_cr_r64(HardwareCodeGen *gen, int cr_id, int src_reg) {
    if (!gen) return -1;
    
    unsigned char code[4];
    int len = hw_encode_mov_cr_r64(cr_id, src_reg, code, sizeof(code));
    if (len < 0) return -1;
    return hw_codegen_emit_bytes(gen, code, len);
}

/**
 * 生成MOV r64, Cr（读取控制寄存器）
 */
int hw_codegen_emit_mov_r64_cr(HardwareCodeGen *gen, int dst_reg, int cr_id) {
    if (!gen) return -1;
    
    unsigned char code[4];
    int len = hw_encode_mov_r64_cr(dst_reg, cr_id, code, sizeof(code));
    if (len < 0) return -1;
    return hw_codegen_emit_bytes(gen, code, len);
}

/**
 * 生成PUSH r64
 */
int hw_codegen_emit_push_r64(HardwareCodeGen *gen, int reg_id) {
    if (!gen) return -1;
    
    unsigned char code[2];
    int len = hw_encode_push_r64(reg_id, code, sizeof(code));
    if (len < 0) return -1;
    return hw_codegen_emit_bytes(gen, code, len);
}

/**
 * 生成POP r64
 */
int hw_codegen_emit_pop_r64(HardwareCodeGen *gen, int reg_id) {
    if (!gen) return -1;
    
    unsigned char code[2];
    int len = hw_encode_pop_r64(reg_id, code, sizeof(code));
    if (len < 0) return -1;
    return hw_codegen_emit_bytes(gen, code, len);
}

/**
 * 生成RET
 */
int hw_codegen_emit_ret(HardwareCodeGen *gen) {
    if (!gen) return -1;
    
    unsigned char code[1];
    int len = hw_encode_ret(code, sizeof(code));
    if (len < 0) return -1;
    return hw_codegen_emit_bytes(gen, code, len);
}
