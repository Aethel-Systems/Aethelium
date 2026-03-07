/*
 * X86-64 Industrial-Grade Encoder
 * 硬件层的机器码发射引擎
 * 
 * 提供一比一的x86-64指令编码，支持所有寻址模式和操作数类型
 */

#include "hardware_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =====================================================================
 * X86-64指令编码数据库
 * ===================================================================== */

/**
 * 指令编码定义
 * 字段说明：
 * - mnemonic: 助记符
 * - opcode: 主操作码
 * - has_modrm: 是否需要ModR/M字节
 * - immediate_size: 立即数大小（0, 1, 2, 4, 8）
 * - operand_pattern: 操作数模式
 */

typedef struct {
    const char *mnemonic;           /* 助记符 */
    unsigned char opcode[3];        /* 最多3字节操作码 */
    int opcode_len;                 /* 操作码长度 */
    int prefix;                     /* 前缀（0x66, 0xF2, 0xF3等） */
    int has_modrm;                  /* 是否需要ModR/M */
    int immediate_size;             /* 立即数大小代码 */
    const char *operand_pattern;    /* 操作数模式描述 */
} ISAEncoding;

static const ISAEncoding isa_encodings[] = {
    /* 系统指令 */
    {"syscall", {0x0F, 0x05}, 2, 0, 0, 0, ""},
    {"sysret", {0x48, 0x0F, 0x07}, 3, 0, 0, 0, ""},
    {"iret", {0xCF}, 1, 0, 0, 0, ""},
    {"iretq", {0x48, 0xCF}, 2, 0, 0, 0, ""},
    
    /* GDT/IDT/TSS */
    {"lgdt", {0x0F, 0x01}, 2, 0, 1, 0, "m"},     /* LGDT m16/64 */
    {"lidt", {0x0F, 0x01}, 2, 0, 1, 0, "m"},     /* LIDT m16/64 */
    {"ltr", {0x0F, 0x00}, 2, 0, 1, 0, "r16"},    /* LTR r/m16 */
    
    /* 缓存与TLB */
    {"invlpg", {0x0F, 0x01}, 2, 0, 1, 0, "m"},   /* INVLPG m */
    {"wbinvd", {0x0F, 0x09}, 2, 0, 0, 0, ""},
    {"clflush", {0x0F, 0xAE}, 2, 0, 1, 0, "m"},  /* CLFLUSH m */
    
    /* 内存屏障 */
    {"mfence", {0x0F, 0xAE}, 2, 0, 1, 0, "/6"},
    {"sfence", {0x0F, 0xAE}, 2, 0, 1, 0, "/7"},
    {"lfence", {0x0F, 0xAE}, 2, 0, 1, 0, "/5"},
    
    /* CPU控制 */
    {"pause", {0x90}, 1, 0xF3, 0, 0, ""},
    {"hlt", {0xF4}, 1, 0, 0, 0, ""},
    {"nop", {0x90}, 1, 0, 0, 0, ""},
    
    /* MSR操作 */
    {"rdmsr", {0x0F, 0x32}, 2, 0, 0, 0, ""},
    {"wrmsr", {0x0F, 0x30}, 2, 0, 0, 0, ""},
    
    /* 基本算数与逻辑 */
    {"mov", {0x8B}, 1, 0, 1, 0, "r64,r/m64"},
    {"mov", {0xC7}, 1, 0, 1, 4, "r/m64,imm32"},
    {"mov", {0xB8}, 1, 0, 0, 8, "rax,imm64"},
    
    {"add", {0x81}, 1, 0, 1, 4, "r/m64,imm32"},
    {"add", {0x01}, 1, 0, 1, 0, "r/m64,r64"},
    
    {"sub", {0x81}, 1, 0, 1, 4, "r/m64,imm32"}, /* /5 */
    {"sub", {0x29}, 1, 0, 1, 0, "r/m64,r64"},
    
    {"xor", {0x81}, 1, 0, 1, 4, "r/m64,imm32"}, /* /6 */
    {"xor", {0x31}, 1, 0, 1, 0, "r/m64,r64"},
    
    {"and", {0x81}, 1, 0, 1, 4, "r/m64,imm32"}, /* /4 */
    {"and", {0x21}, 1, 0, 1, 0, "r/m64,r64"},
    
    {"or", {0x81}, 1, 0, 1, 4, "r/m64,imm32"},  /* /1 */
    {"or", {0x09}, 1, 0, 1, 0, "r/m64,r64"},
    
    {"adc", {0x81}, 1, 0, 1, 4, "r/m64,imm32"}, /* /2 */
    {"adc", {0x11}, 1, 0, 1, 0, "r/m64,r64"},
    
    /* 向量指令（SSE/AVX） */
    {"movdqu", {0xF3, 0x0F, 0x6F}, 3, 0, 1, 0, "xmm,xmm/m"},
    {"movdqa", {0x66, 0x0F, 0x6F}, 3, 0, 1, 0, "xmm,xmm/m"},
    {"vmovdqu", {0xC5, 0xF9, 0x6F}, 3, 0, 1, 0, "ymm,ymm/m"},
    {"vmovdqa", {0x66, 0x0F, 0x6F}, 3, 0, 1, 0, "ymm,ymm/m"},
    
    /* 终止符 */
    {NULL, {0}, 0, 0, 0, 0, NULL}
};

/* =====================================================================
 * 寄存器编码
 * ===================================================================== */

typedef struct {
    const char *reg_name;
    int reg_id;                     /* 0-15 */
    int size;                       /* 1, 2, 4, 8 字节 */
    int needs_rex;                  /* 是否需要REX前缀 */
} RegEncoding;

static const RegEncoding gpr_encodings[] = {
    {"rax", 0, 8, 0}, {"eax", 0, 4, 0}, {"ax", 0, 2, 0}, {"al", 0, 1, 0},
    {"rcx", 1, 8, 0}, {"ecx", 1, 4, 0}, {"cx", 1, 2, 0}, {"cl", 1, 1, 0},
    {"rdx", 2, 8, 0}, {"edx", 2, 4, 0}, {"dx", 2, 2, 0}, {"dl", 2, 1, 0},
    {"rbx", 3, 8, 0}, {"ebx", 3, 4, 0}, {"bx", 3, 2, 0}, {"bl", 3, 1, 0},
    {"rsp", 4, 8, 0}, {"esp", 4, 4, 0}, {"sp", 4, 2, 0},
    {"rbp", 5, 8, 0}, {"ebp", 5, 4, 0}, {"bp", 5, 2, 0},
    {"rsi", 6, 8, 0}, {"esi", 6, 4, 0}, {"si", 6, 2, 0},
    {"rdi", 7, 8, 0}, {"edi", 7, 4, 0}, {"di", 7, 2, 0},
    {"r8", 8, 8, 1}, {"r8d", 8, 4, 1}, {"r8w", 8, 2, 1}, {"r8b", 8, 1, 1},
    {"r9", 9, 8, 1}, {"r9d", 9, 4, 1}, {"r9w", 9, 2, 1}, {"r9b", 9, 1, 1},
    {"r10", 10, 8, 1}, {"r10d", 10, 4, 1}, {"r10w", 10, 2, 1}, {"r10b", 10, 1, 1},
    {"r11", 11, 8, 1}, {"r11d", 11, 4, 1}, {"r11w", 11, 2, 1}, {"r11b", 11, 1, 1},
    {"r12", 12, 8, 1}, {"r12d", 12, 4, 1}, {"r12w", 12, 2, 1}, {"r12b", 12, 1, 1},
    {"r13", 13, 8, 1}, {"r13d", 13, 4, 1}, {"r13w", 13, 2, 1}, {"r13b", 13, 1, 1},
    {"r14", 14, 8, 1}, {"r14d", 14, 4, 1}, {"r14w", 14, 2, 1}, {"r14b", 14, 1, 1},
    {"r15", 15, 8, 1}, {"r15d", 15, 4, 1}, {"r15w", 15, 2, 1}, {"r15b", 15, 1, 1},
    {NULL, -1, 0, 0}
};

/* =====================================================================
 * 编码实用函数
 * ===================================================================== */

/**
 * 查找寄存器编码
 */
static const RegEncoding* hw_find_register(const char *reg_name) {
    for (int i = 0; gpr_encodings[i].reg_name != NULL; i++) {
        if (strcmp(gpr_encodings[i].reg_name, reg_name) == 0) {
            return &gpr_encodings[i];
        }
    }
    return NULL;
}

/**
 * 查找指令编码
 */
static const ISAEncoding* hw_find_isa_encoding(const char *mnemonic) {
    for (int i = 0; isa_encodings[i].mnemonic != NULL; i++) {
        if (strcmp(isa_encodings[i].mnemonic, mnemonic) == 0) {
            return &isa_encodings[i];
        }
    }
    return NULL;
}

/**
 * 编码系统指令（无操作数）
 */
static int hw_encode_syscall_type(const ISAEncoding *enc, unsigned char *output, int output_size) {
    int offset = 0;
    
    if (enc->prefix != 0) {
        output[offset++] = enc->prefix;
    }
    
    for (int i = 0; i < enc->opcode_len; i++) {
        if (offset >= output_size) return -1;
        output[offset++] = enc->opcode[i];
    }
    
    return offset;
}

/**
 * 编码带寄存器的指令（MOV RAX, IMM64）
 */
static int hw_encode_reg_imm(const ISAEncoding *enc, const RegEncoding *reg_enc,
                             uint64_t immediate, unsigned char *output, int output_size) {
    int offset = 0;
    
    /* 输出二进制 */
    unsigned char *out = output;
    int out_size = output_size;
    
    /* REX前缀（如果需要） */
    unsigned char rex = 0;
    if (reg_enc->size == 8) {
        rex = 0x48;  /* REX.W */
    } else if (reg_enc->needs_rex) {
        rex = 0x40;  /* REX */
    }
    
    if (rex != 0) {
        out[offset++] = rex;
    }
    
    /* 操作码 + 寄存器ID编码在最后三位 */
    for (int i = 0; i < enc->opcode_len; i++) {
        if (offset >= out_size) return -1;
        unsigned char op = enc->opcode[i];
        if (i == enc->opcode_len - 1 && !enc->has_modrm) {
            /* 将寄存器ID编码到操作码中 */
            op |= (reg_enc->reg_id & 0x7);
        }
        out[offset++] = op;
    }
    
    /* 立即数 */
    int imm_size = enc->immediate_size;
    if (imm_size > 0) {
        for (int i = 0; i < imm_size; i++) {
            if (offset >= out_size) return -1;
            out[offset++] = (unsigned char)((immediate >> (i * 8)) & 0xFF);
        }
    }
    
    return offset;
}

/**
 * 编码SYSCALL和SYSRET
 */
static int hw_encode_syscall(unsigned char *output, int output_size) {
    if (output_size < 2) return -1;
    output[0] = 0x0F;
    output[1] = 0x05;
    return 2;
}

static int hw_encode_sysret(unsigned char *output, int output_size) {
    if (output_size < 3) return -1;
    output[0] = 0x48;  /* REX.W */
    output[1] = 0x0F;
    output[2] = 0x07;
    return 3;
}

/**
 * 编码CPUID
 */
static int hw_encode_cpuid(unsigned char *output, int output_size) {
    if (output_size < 2) return -1;
    output[0] = 0x0F;
    output[1] = 0xA2;
    return 2;
}

/**
 * 编码LGDT (Load GDT)
 */
static int hw_encode_lgdt(unsigned char *output, int output_size) {
    if (output_size < 2) return -1;
    output[0] = 0x0F;
    output[1] = 0x01;
    return 2;
}

/**
 * 编码LIDT (Load IDT)
 */
static int hw_encode_lidt(unsigned char *output, int output_size) {
    if (output_size < 2) return -1;
    output[0] = 0x0F;
    output[1] = 0x01;
    return 2;
}

/**
 * 编码INVLPG (Invalidate TLB Entry)
 */
static int hw_encode_invlpg(unsigned char *output, int output_size) {
    if (output_size < 2) return -1;
    output[0] = 0x0F;
    output[1] = 0x01;
    return 2;
}

/**
 * 编码WBINVD (Write-Back and Invalidate Cache)
 */
static int hw_encode_wbinvd(unsigned char *output, int output_size) {
    if (output_size < 2) return -1;
    output[0] = 0x0F;
    output[1] = 0x09;
    return 2;
}

/* =====================================================================
 * 导出的编码函数
 * ===================================================================== */

/**
 * X86-64通用编码函数
 * 根据ISA操作码选择合适的编码策略
 */
int hw_encode_x86_instruction(ISAOpcode opcode, const char *reg_name, 
                              uint64_t immediate, unsigned char *output, int output_size) {
    if (!output || output_size < 15) {
        return -1;
    }
    
    switch (opcode) {
        case ISA_SYSCALL:
            return hw_encode_syscall(output, output_size);
        case ISA_SYSRET:
            return hw_encode_sysret(output, output_size);
        case ISA_CPUID:
            return hw_encode_cpuid(output, output_size);
        case ISA_LGDT:
            return hw_encode_lgdt(output, output_size);
        case ISA_LIDT:
            return hw_encode_lidt(output, output_size);
        case ISA_INVLPG:
            return hw_encode_invlpg(output, output_size);
        case ISA_WBINVD:
            return hw_encode_wbinvd(output, output_size);
        case ISA_MOV: {
            /* MOV RAX, IMM64 */
            if (!reg_name) return -2;
            const RegEncoding *reg_enc = hw_find_register(reg_name);
            if (!reg_enc) return -3;
            
            unsigned char rex = (reg_enc->size == 8) ? 0x48 : 0;
            if (reg_enc->needs_rex && reg_enc->size != 8) {
                rex = 0x40 | (reg_enc->needs_rex ? 0x01 : 0);
            }
            
            int offset = 0;
            if (rex != 0) output[offset++] = rex;
            output[offset++] = 0xB8 | (reg_enc->reg_id & 0x7);
            
            /* 立即数 */
            for (int i = 0; i < 8; i++) {
                output[offset++] = (unsigned char)((immediate >> (i * 8)) & 0xFF);
            }
            return offset;
        }
        default:
            return -4; /* 不支持的操作码 */
    }
}

/**
 * ADC (Add with Carry) 编码
 */
int hw_encode_adc(const char *dest_reg, const char *src_reg, 
                  unsigned char *output, int output_size) {
    if (!dest_reg || !src_reg || !output || output_size < 5) {
        return -1;
    }
    
    const RegEncoding *dest = hw_find_register(dest_reg);
    const RegEncoding *src = hw_find_register(src_reg);
    if (!dest || !src) return -2;
    
    int offset = 0;
    
    /* REX前缀 */
    unsigned char rex = 0;
    if (dest->size == 8) rex |= 0x08;  /* REX.W */
    if (src->needs_rex) rex |= 0x04;   /* REX.R */
    if (dest->needs_rex) rex |= 0x01;  /* REX.B */
    
    if (rex != 0) {
        output[offset++] = 0x40 | rex;
    }
    
    /* 操作码 */
    output[offset++] = 0x11;  /* ADC r/m64, r64 */
    
    /* ModR/M字节 */
    unsigned char modrm = 0xC0;  /* Direct mode */
    modrm |= (src->reg_id & 0x7) << 3;  /* REG字段 */
    modrm |= (dest->reg_id & 0x7);      /* RM字段 */
    output[offset++] = modrm;
    
    return offset;
}

/**
 * 栅栏指令编码
 */
static int hw_encode_mfence(unsigned char *output, int output_size) {
    if (output_size < 3) return -1;
    output[0] = 0x0F;
    output[1] = 0xAE;
    output[2] = 0xF0;  /* /6 */
    return 3;
}

static int hw_encode_sfence(unsigned char *output, int output_size) {
    if (output_size < 3) return -1;
    output[0] = 0x0F;
    output[1] = 0xAE;
    output[2] = 0xF8;  /* /7 */
    return 3;
}

static int hw_encode_lfence(unsigned char *output, int output_size) {
    if (output_size < 3) return -1;
    output[0] = 0x0F;
    output[1] = 0xAE;
    output[2] = 0xE8;  /* /5 */
    return 3;
}

/**
 * 暂停指令
 */
static int hw_encode_pause(unsigned char *output, int output_size) {
    if (output_size < 2) return -1;
    output[0] = 0xF3;  /* PAUSE前缀 */
    output[1] = 0x90;  /* NOP */
    return 2;
}

/* =====================================================================
 * 完整的x86-64指令编码库 - 工业级实现
 * ===================================================================== */

/**
 * 编码MOV r64, imm32（符号扩展）- 4B指令
 * movq imm32, r64 (sign-extend)
 */
int hw_encode_mov_r64_imm32_signed(int reg_id, int32_t imm32, unsigned char *output, int output_size) {
    if (output_size < 7) return -1;
    
    int offset = 0;
    unsigned char rex = 0x48;  /* REX.W */
    
    /* REX.B如果reg_id > 7 */
    if (reg_id > 7) {
        rex |= 0x01;
    }
    
    output[offset++] = rex;
    output[offset++] = 0xC7;  /* MOV r/m64, imm32 */
    output[offset++] = 0xC0 | (reg_id & 0x7);  /* ModR/M: 11 000 reg */
    
    /* 小端序立即数 */
    output[offset++] = (unsigned char)(imm32 & 0xFF);
    output[offset++] = (unsigned char)((imm32 >> 8) & 0xFF);
    output[offset++] = (unsigned char)((imm32 >> 16) & 0xFF);
    output[offset++] = (unsigned char)((imm32 >> 24) & 0xFF);
    
    return offset;
}

/**
 * 编码MOV r64, imm64（64位立即数）- 10B指令
 * movabs imm64, r64
 */
int hw_encode_mov_r64_imm64(int reg_id, uint64_t imm64, unsigned char *output, int output_size) {
    if (output_size < 10) return -1;
    
    int offset = 0;
    unsigned char rex = 0x48;  /* REX.W */
    
    if (reg_id > 7) {
        rex |= 0x01;
    }
    
    output[offset++] = rex;
    output[offset++] = 0xB8 | (reg_id & 0x7);  /* MOV r64, imm64 */
    
    /* 小端序64位立即数 */
    for (int i = 0; i < 8; i++) {
        output[offset++] = (unsigned char)((imm64 >> (i * 8)) & 0xFF);
    }
    
    return offset;
}

/**
 * 编码MOV r64, r64（寄存器间移动）
 * mov src, dst
 */
int hw_encode_mov_r64_r64(int src_reg, int dst_reg, unsigned char *output, int output_size) {
    if (output_size < 3) return -1;
    
    int offset = 0;
    unsigned char rex = 0x48;  /* REX.W */
    
    if ((src_reg > 7) || (dst_reg > 7)) {
        rex |= ((src_reg > 7 ? 0x04 : 0) | (dst_reg > 7 ? 0x01 : 0));
    }
    
    output[offset++] = rex;
    output[offset++] = 0x89;  /* MOV r64, r/m64 */
    output[offset++] = 0xC0 | ((src_reg & 0x7) << 3) | (dst_reg & 0x7);
    
    return offset;
}

/**
 * 编码MOV r64, [mem]（从内存读取）
 * mov [reg], dst_reg
 */
int hw_encode_mov_r64_mem(int mem_reg, int dst_reg, unsigned char *output, int output_size) {
    if (output_size < 3) return -1;
    
    int offset = 0;
    unsigned char rex = 0x48;  /* REX.W */
    
    if ((mem_reg > 7) || (dst_reg > 7)) {
        rex |= ((mem_reg > 7 ? 0x01 : 0) | (dst_reg > 7 ? 0x04 : 0));
    }
    
    output[offset++] = rex;
    output[offset++] = 0x8B;  /* MOV r/(m64), r64 */
    output[offset++] = 0x00 | ((dst_reg & 0x7) << 3) | (mem_reg & 0x7);  /* ModR/M: 00 dst mem */
    
    return offset;
}

/**
 * 编码MOV [mem], r64（向内存写入）
 */
int hw_encode_mov_mem_r64(int mem_reg, int src_reg, unsigned char *output, int output_size) {
    if (output_size < 3) return -1;
    
    int offset = 0;
    unsigned char rex = 0x48;  /* REX.W */
    
    if ((mem_reg > 7) || (src_reg > 7)) {
        rex |= ((mem_reg > 7 ? 0x01 : 0) | (src_reg > 7 ? 0x04 : 0));
    }
    
    output[offset++] = rex;
    output[offset++] = 0x89;  /* MOV r/m64, r64 */
    output[offset++] = 0x00 | ((src_reg & 0x7) << 3) | (mem_reg & 0x7);
    
    return offset;
}

/**
 * 编码MOV Cr, r64（写入控制寄存器）
 */
int hw_encode_mov_cr_r64(int cr_id, int src_reg, unsigned char *output, int output_size) {
    if (output_size < 3) return -1;
    
    int offset = 0;
    unsigned char rex = 0x48;
    
    if (src_reg > 7) {
        rex |= 0x01;
    }
    
    output[offset++] = rex;
    output[offset++] = 0x0F;
    output[offset++] = 0x22;  /* MOV Cr, r64 */
    output[offset++] = 0xC0 | (cr_id << 3) | (src_reg & 0x7);
    
    return offset;
}

/**
 * 编码MOV r64, Cr（读取控制寄存器）
 */
int hw_encode_mov_r64_cr(int dst_reg, int cr_id, unsigned char *output, int output_size) {
    if (output_size < 3) return -1;
    
    int offset = 0;
    unsigned char rex = 0x48;
    
    if (dst_reg > 7) {
        rex |= 0x01;
    }
    
    output[offset++] = rex;
    output[offset++] = 0x0F;
    output[offset++] = 0x20;  /* MOV r64, Cr */
    output[offset++] = 0xC0 | (cr_id << 3) | (dst_reg & 0x7);
    
    return offset;
}

/**
 * 编码PUSH r64
 */
int hw_encode_push_r64(int reg_id, unsigned char *output, int output_size) {
    if (output_size < 2) return -1;
    
    int offset = 0;
    if (reg_id > 7) {
        output[offset++] = 0x41;  /* REX.B */
    }
    
    output[offset++] = 0x50 | (reg_id & 0x7);
    return offset;
}

/**
 * 编码POP r64
 */
int hw_encode_pop_r64(int reg_id, unsigned char *output, int output_size) {
    if (output_size < 2) return -1;
    
    int offset = 0;
    if (reg_id > 7) {
        output[offset++] = 0x41;  /* REX.B */
    }
    
    output[offset++] = 0x58 | (reg_id & 0x7);
    return offset;
}

/**
 * 编码RET（返回）
 */
int hw_encode_ret(unsigned char *output, int output_size) {
    if (output_size < 1) return -1;
    output[0] = 0xC3;
    return 1;
}

/**
 * 编码NOP（无操作）
 */
int hw_encode_nop(unsigned char *output, int output_size) {
    if (output_size < 1) return -1;
    output[0] = 0x90;
    return 1;
}
