/*
 * AethelOS Aethelium Compiler - Hardware Gate Descriptors
 * ABI穿透与门描述符：实现中断门、系统调用、异常处理
 */

#include "hardware_layer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =====================================================================
 * 门类型特性定义
 * ===================================================================== */

typedef struct {
    GateType gate_type;
    const char *type_name;
    int save_all_regs;
    int generate_prologue;
    int generate_epilogue;
    int has_error_code;
    int uses_frame_pointer;
    const char *return_instruction;
} GateTypeProperties;

static const GateTypeProperties gate_properties[] = {
    {
        GATE_TYPE_INTERRUPT,
        "interrupt",
        1,  /* 保存所有寄存器 */
        0,  /* CPU硬件自动处理 */
        1,  /* 生成IRETQ */
        0,  /* 有些情况有错误码，有些没有 */
        0,
        "iretq"
    },
    {
        GATE_TYPE_SYSCALL,
        "syscall",
        0,  /* 仅保存调用者保存的寄存器 */
        1,  /* 生成序言 */
        1,  /* 生成SYSRETQ */
        0,
        0,
        "sysretq"
    },
    {
        GATE_TYPE_EFI,
        "efi",
        0,  /* Microsoft x64 ABI */
        1,
        1,  /* 标准RET */
        0,
        1,  /* 使用帧指针 */
        "ret"
    },
    {
        GATE_TYPE_NAKED,
        "naked",
        0,  /* 不生成任何序言*/
        0,
        0,
        0,
        0,
        ""  /* 由用户提供 */
    },
    {
        GATE_TYPE_EXCEPTION,
        "exception",
        1,
        0,  /* CPU硬件处理 */
        1,  /* IRETQ */
        1,  /* 包含错误码 */
        0,
        "iretq"
    }
};

/**
 * 获取门类型的属性
 */
static const GateTypeProperties* hw_get_gate_properties(GateType gate_type) {
    for (int i = 0; i < 5; i++) {
        if (gate_properties[i].gate_type == gate_type) {
            return &gate_properties[i];
        }
    }
    return NULL;
}

/* =====================================================================
 * 栈帧（Trap Frame）管理
 * ===================================================================== */

/**
 * 创建栈帧
 */
TrapFrame* hw_create_trap_frame(const char *frame_type_name, int is_packed) {
    TrapFrame *frame = (TrapFrame *)malloc(sizeof(TrapFrame));
    if (!frame) return NULL;
    
    memset(frame, 0, sizeof(TrapFrame));
    
    frame->frame_type_name = (char *)malloc(strlen(frame_type_name) + 1);
    strcpy(frame->frame_type_name, frame_type_name);
    
    frame->is_packed = is_packed;
    
    /* 初始化CPU压栈掩码
     * 中断时，CPU压栈：RIP, CS, RFLAGS, RSP (long mode中)
     * 异常或错误码的情况下，还会压栈错误码
     */
    frame->cpu_push_mask = (1ULL << 63) | (1ULL << 47) | (1ULL << 15);
    
    return frame;
}

/**
 * 销毁栈帧
 */
void hw_destroy_trap_frame(TrapFrame *frame) {
    if (!frame) return;
    if (frame->frame_type_name) free(frame->frame_type_name);
    free(frame);
}

/* =====================================================================
 * 门描述符生成
 * ===================================================================== */

/**
 * 创建门描述符
 */
GateDescriptor* hw_create_gate_descriptor(GateType gate_type) {
    GateDescriptor *desc = (GateDescriptor *)malloc(sizeof(GateDescriptor));
    if (!desc) return NULL;
    
    memset(desc, 0, sizeof(GateDescriptor));
    
    const GateTypeProperties *props = hw_get_gate_properties(gate_type);
    if (!props) {
        free(desc);
        return NULL;
    }
    
    desc->gate_type = gate_type;
    desc->save_all_registers = props->save_all_regs;
    desc->generate_prologue = props->generate_prologue;
    desc->generate_epilogue = props->generate_epilogue;
    desc->return_instruction = props->return_instruction;
    
    /* 计算栈帧大小
     * 基础: 所有通用寄存器 + 返回地址等
     * x86-64: 16个通用寄存器 × 8字节 = 128字节
     * 加上中断/异常信息 = 约160字节
     */
    desc->context_frame_size = 160;
    
    return desc;
}

/**
 * 销毁门描述符
 */
void hw_destroy_gate_descriptor(GateDescriptor *desc) {
    if (!desc) return;
    free(desc);
}

/* =====================================================================
 * 中断门特性处理
 * ===================================================================== */

/**
 * 验证并设置中断门
 */
int hw_setup_interrupt_gate(HardwareContext *ctx, GateDescriptor *desc,
                           const char *handler_name, uint8_t interrupt_number) {
    if (!ctx || !desc || !handler_name) {
        return -1;
    }
    
    if (desc->gate_type != GATE_TYPE_INTERRUPT) {
        return -2;  /* 不是中断门类型 */
    }
    
    /* 验证中断号有效性 */
    if (interrupt_number < 0 || interrupt_number > 255) {
        return -3;
    }
    
    /* 在中断处理时，CPU会自动压栈：
     * - 异常码（可选）
     * - Return RIP
     * - Return CS
     * - Return RFLAGS
     * - Return RSP (long mode)
     * - Return SS (long mode)
     */
    
    return 0;
}

/**
 * 验证并设置系统调用门
 */
int hw_setup_syscall_gate(HardwareContext *ctx, GateDescriptor *desc,
                         const char *handler_name) {
    if (!ctx || !desc || !handler_name) {
        return -1;
    }
    
    if (desc->gate_type != GATE_TYPE_SYSCALL) {
        return -2;
    }
    
    /* 系统调用约定（x86-64）：
     * - RAX: 系统调用号
     * - RCX: 返回地址（SYSCALL自动设置）
     * - R11: RFLAGS保存（SYSCALL自动设置）
     * - RDI, RSI, RDX, R10, R8, R9: 参数
     */
    
    return 0;
}

/**
 * 验证并设置EFI门
 */
int hw_setup_efi_gate(HardwareContext *ctx, GateDescriptor *desc,
                     const char *handler_name) {
    if (!ctx || !desc || !handler_name) {
        return -1;
    }
    
    if (desc->gate_type != GATE_TYPE_EFI) {
        return -2;
    }
    
    /* UEFI/MS-ABI x86-64约定：
     * - RCX: 第一个参数（传递handle）
     * - RDX: 第二个参数（传递table指针）
     * - R8, R9: 第三、四个参数
     * - 调用者保存：RAX, RCX, RDX, R8, R9, R10, R11
     * - 被调用者保存：RBX, RBP, RDI, RSI, R12-R15
     * - Red Zone: 栈指针下方128字节
     * - 栈对齐：16字节
     */
    
    return 0;
}

/**
 * 验证并设置异常门
 */
int hw_setup_exception_gate(HardwareContext *ctx, GateDescriptor *desc,
                           const char *handler_name, uint8_t exception_number) {
    if (!ctx || !desc || !handler_name) {
        return -1;
    }
    
    if (desc->gate_type != GATE_TYPE_EXCEPTION) {
        return -2;
    }
    
    /* 某些异常会产生错误码：
     * - #DF (2)
     * - #TS (10)
     * - #NP (11)
     * - #SS (12)
     * - #GP (13)
     * - #PF (14)
     * - #AC (17)
     * - #CE (30)
     */
    
    int has_error_code = 0;
    if (exception_number == 8 || (exception_number >= 10 && exception_number <= 14) ||
        exception_number == 17 || exception_number == 30) {
        has_error_code = 1;
    }
    
    return has_error_code;
}

/* =====================================================================
 * 函数序言与尾声生成助手
 * ===================================================================== */

/**
 * 获取中断处理函数的栈帧布局
 * 返回栈帧的结构描述
 */
int hw_get_interrupt_frame_layout(TrapFrame *frame,
                                  const char **field_names,
                                  int *field_offsets,
                                  int *field_sizes,
                                  int max_fields) {
    if (!frame || !field_names || !field_offsets || !field_sizes) {
        return -1;
    }
    
    int field_count = 0;
    int offset = 0;
    
    /* CPU自动压栈的字段 */
    if (field_count < max_fields) {
        field_names[field_count] = "error_code";  /* 部分异常 */
        field_offsets[field_count] = offset;
        field_sizes[field_count] = 8;
        offset += 8;
        field_count++;
    }
    
    if (field_count < max_fields) {
        field_names[field_count] = "rip";
        field_offsets[field_count] = offset;
        field_sizes[field_count] = 8;
        offset += 8;
        field_count++;
    }
    
    if (field_count < max_fields) {
        field_names[field_count] = "cs";
        field_offsets[field_count] = offset;
        field_sizes[field_count] = 8;
        offset += 8;
        field_count++;
    }
    
    if (field_count < max_fields) {
        field_names[field_count] = "rflags";
        field_offsets[field_count] = offset;
        field_sizes[field_count] = 8;
        offset += 8;
        field_count++;
    }
    
    if (field_count < max_fields) {
        field_names[field_count] = "rsp";
        field_offsets[field_count] = offset;
        field_sizes[field_count] = 8;
        offset += 8;
        field_count++;
    }
    
    if (field_count < max_fields) {
        field_names[field_count] = "ss";
        field_offsets[field_count] = offset;
        field_sizes[field_count] = 8;
        offset += 8;
        field_count++;
    }
    
    return field_count;
}

/**
 * 验证栈帧与门类型的兼容性
 */
int hw_validate_frame_gate_compatibility(TrapFrame *frame, GateType gate_type) {
    if (!frame) return -1;
    
    switch (gate_type) {
        case GATE_TYPE_INTERRUPT:
        case GATE_TYPE_EXCEPTION:
            /* 中断/异常门必须有TrapFrame结构 */
            if (!frame->frame_type_name) return -2;
            return 0;
            
        case GATE_TYPE_SYSCALL:
            /* 系统调用门可选择有框架 */
            return 0;
            
        case GATE_TYPE_EFI:
            /* UEFI门不需要特殊框架 */
            return 0;
            
        case GATE_TYPE_NAKED:
            /* 裸体函数不需要框架 */
            return 0;
            
        default:
            return -3;
    }
}

/* =====================================================================
 * 寄存器保存/恢复生成
 * ===================================================================== */

/**
 * 获取门类型需要保存的寄存器掩码
 */
uint64_t hw_get_register_save_mask(GateType gate_type) {
    uint64_t mask = 0;
    
    switch (gate_type) {
        case GATE_TYPE_INTERRUPT:
        case GATE_TYPE_EXCEPTION:
            /* 中断处理：保存所有通用寄存器 */
            mask = 0xFFFF;  /* RAX-R15 */
            break;
            
        case GATE_TYPE_SYSCALL:
            /* 系统调用：只保存被调用者保存的寄存器 */
            /* RBX, RBP, R12-R15 */
            mask = (1 << 3) | (1 << 5) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15);
            break;
            
        case GATE_TYPE_EFI:
            /* UEFI/MS-ABI：被调用者保存 */
            /* RBX, RBP, RDI, RSI, R12-R15 */
            mask = (1 << 3) | (1 << 5) | (1 << 7) | (1 << 6) |
                   (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15);
            break;
            
        case GATE_TYPE_NAKED:
            /* 裸体函数：不自动保存任何寄存器 */
            mask = 0;
            break;
            
        default:
            mask = 0;
    }
    
    return mask;
}

/**
 * 获取被破坏的寄存器掩码（调用者保存）
 */
uint64_t hw_get_register_clobber_mask(GateType gate_type) {
    uint64_t mask = 0;
    
    switch (gate_type) {
        case GATE_TYPE_SYSCALL:
            /* SYSCALL破坏RCX（返回地址）和R11（RFLAGS） */
            mask = (1 << 1) | (1 << 11);
            break;
            
        case GATE_TYPE_EFI:
            /* 调用者保存：RAX, RCX, RDX, R8, R9, R10, R11 */
            mask = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 8) |
                   (1 << 9) | (1 << 10) | (1 << 11);
            break;
            
        default:
            mask = 0;
    }
    
    return mask;
}
