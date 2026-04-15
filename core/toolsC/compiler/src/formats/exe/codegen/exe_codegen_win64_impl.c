/*
 * =========================================================================
 * Aethelium EXE X64 Code Generation Implementation
 * =========================================================================
 */

#include "exe_codegen_win64.h"

#include <stdio.h>
#include <string.h>

#define CODEGEN_WIN64_SHADOW_SPACE 32U
#define CODEGEN_WIN64_TEB_GS_OFFSET 0x30U

typedef struct {
    const char *name;
    uint32_t slot_offset;
} Portal_Slot_Map;

static const Portal_Slot_Map g_portal_slot_map[] = {
    { "NtAllocateVirtualMemory", 0x00U },
    { "NtAllocateVirtualMemoryEx", 0x08U },
    { "NtFreeVirtualMemory", 0x10U },
    { "NtCreateThreadEx", 0x58U },
    { "NtCreateFile", 0x90U },
    { "NtReadFile", 0xB0U },
    { "NtWriteFile", 0xB8U },
    { "NtCreateEvent", 0xE0U },
    { "NtWaitForSingleObject", 0x130U },
    { "NtDelayExecution", 0x158U },
    { "NtRemoveIoCompletion", 0x1C8U },
    { "NtTerminateProcess", 0x28U }
};

static int is_extended_reg(int reg) {
    return reg >= 8;
}

static uint8_t rex_prefix(int w, int r, int x, int b) {
    return (uint8_t)(0x40U |
                     (w ? 0x08U : 0U) |
                     (r ? 0x04U : 0U) |
                     (x ? 0x02U : 0U) |
                     (b ? 0x01U : 0U));
}

static void emit_modrm(Codegen_Context *ctx, uint8_t mod, uint8_t reg, uint8_t rm) {
    Codegen_EmitByte(ctx, (uint8_t)((mod << 6U) | ((reg & 7U) << 3U) | (rm & 7U)));
}

static uint32_t portal_slot_offset(const char *function_name) {
    size_t i;
    if (!function_name) {
        return 0U;
    }
    for (i = 0; i < sizeof(g_portal_slot_map) / sizeof(g_portal_slot_map[0]); ++i) {
        if (strcmp(g_portal_slot_map[i].name, function_name) == 0) {
            return g_portal_slot_map[i].slot_offset;
        }
    }
    return 0U;
}

int Codegen_Initialize(Codegen_Context *ctx,
                       uint8_t *code_buffer,
                       uint32_t buffer_size) {
    if (!ctx || !code_buffer || buffer_size == 0U) {
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->code_buffer = code_buffer;
    ctx->code_capacity = buffer_size;
    ctx->preserved_regs = (1U << 12) | (1U << 13) | (1U << 14) | (1U << 15);
    ctx->emit_unwind_info = 1;
    return 0;
}

void Codegen_EmitByte(Codegen_Context *ctx, uint8_t byte) {
    if (ctx && ctx->code_offset < ctx->code_capacity) {
        ctx->code_buffer[ctx->code_offset++] = byte;
    }
}

void Codegen_EmitWord(Codegen_Context *ctx, uint16_t word) {
    Codegen_EmitByte(ctx, (uint8_t)(word & 0xFFU));
    Codegen_EmitByte(ctx, (uint8_t)((word >> 8U) & 0xFFU));
}

void Codegen_EmitDword(Codegen_Context *ctx, uint32_t dword) {
    Codegen_EmitByte(ctx, (uint8_t)(dword & 0xFFU));
    Codegen_EmitByte(ctx, (uint8_t)((dword >> 8U) & 0xFFU));
    Codegen_EmitByte(ctx, (uint8_t)((dword >> 16U) & 0xFFU));
    Codegen_EmitByte(ctx, (uint8_t)((dword >> 24U) & 0xFFU));
}

void Codegen_EmitQword(Codegen_Context *ctx, uint64_t qword) {
    Codegen_EmitDword(ctx, (uint32_t)(qword & 0xFFFFFFFFULL));
    Codegen_EmitDword(ctx, (uint32_t)((qword >> 32U) & 0xFFFFFFFFULL));
}

void Codegen_EmitBytes(Codegen_Context *ctx,
                       const uint8_t *data,
                       uint32_t size) {
    uint32_t i;
    if (!ctx || !data) {
        return;
    }
    for (i = 0; i < size; ++i) {
        Codegen_EmitByte(ctx, data[i]);
    }
}

int Codegen_AllocateRegister(Codegen_Context *ctx) {
    int reg;
    if (!ctx) {
        return -1;
    }
    for (reg = 0; reg < 12; ++reg) {
        if ((ctx->allocated_regs & (1U << reg)) == 0U) {
            ctx->allocated_regs |= (1U << reg);
            return reg;
        }
    }
    return -1;
}

void Codegen_FreeRegister(Codegen_Context *ctx, int reg) {
    if (ctx && reg >= 0 && reg < 12) {
        ctx->allocated_regs &= ~(1U << reg);
    }
}

void Codegen_EmitPush(Codegen_Context *ctx, int reg) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    if (is_extended_reg(reg)) {
        Codegen_EmitByte(ctx, 0x41U);
    }
    Codegen_EmitByte(ctx, (uint8_t)(0x50U + (reg & 7)));
}

void Codegen_EmitPop(Codegen_Context *ctx, int reg) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    if (is_extended_reg(reg)) {
        Codegen_EmitByte(ctx, 0x41U);
    }
    Codegen_EmitByte(ctx, (uint8_t)(0x58U + (reg & 7)));
}

void Codegen_PreserveRegister(Codegen_Context *ctx, int reg) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    ctx->preserved_regs |= (1U << reg);
    Codegen_EmitPush(ctx, reg);
    ctx->stack_frame_size += 8;
}

void Codegen_RestoreRegister(Codegen_Context *ctx, int reg) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    Codegen_EmitPop(ctx, reg);
    if (ctx->stack_frame_size >= 8) {
        ctx->stack_frame_size -= 8;
    }
}

int32_t Codegen_AllocateStack(Codegen_Context *ctx, uint32_t size) {
    uint32_t aligned = (size + 15U) & ~15U;
    if (!ctx) {
        return 0;
    }
    ctx->local_variables_size += (int32_t)aligned;
    if (ctx->local_variables_size > ctx->max_stack_frame) {
        ctx->max_stack_frame = ctx->local_variables_size;
    }
    return -(int32_t)ctx->local_variables_size;
}

void Codegen_FreeStack(Codegen_Context *ctx, uint32_t size) {
    uint32_t aligned = (size + 15U) & ~15U;
    if (!ctx) {
        return;
    }
    if ((uint32_t)ctx->local_variables_size >= aligned) {
        ctx->local_variables_size -= (int32_t)aligned;
    } else {
        ctx->local_variables_size = 0;
    }
}

void Codegen_EmitSubRegImm32(Codegen_Context *ctx, int reg, int32_t imm32) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    Codegen_EmitByte(ctx, rex_prefix(1, 0, 0, is_extended_reg(reg)));
    Codegen_EmitByte(ctx, 0x81U);
    emit_modrm(ctx, 3U, 5U, (uint8_t)reg);
    Codegen_EmitDword(ctx, (uint32_t)imm32);
}

void Codegen_EmitAddRegImm32(Codegen_Context *ctx, int reg, int32_t imm32) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    Codegen_EmitByte(ctx, rex_prefix(1, 0, 0, is_extended_reg(reg)));
    Codegen_EmitByte(ctx, 0x81U);
    emit_modrm(ctx, 3U, 0U, (uint8_t)reg);
    Codegen_EmitDword(ctx, (uint32_t)imm32);
}

void Codegen_EmitPrologue(Codegen_Context *ctx,
                          uint32_t local_var_size,
                          uint32_t preserved_regs_mask) {
    uint32_t i;
    uint32_t stack_bytes = CODEGEN_WIN64_SHADOW_SPACE + ((local_var_size + 15U) & ~15U);
    if (!ctx) {
        return;
    }
    Codegen_EmitPush(ctx, X64_REG_RBP);
    Codegen_EmitByte(ctx, 0x48U);
    Codegen_EmitByte(ctx, 0x89U);
    Codegen_EmitByte(ctx, 0xE5U);
    for (i = 0; i < 16U; ++i) {
        if ((preserved_regs_mask & (1U << i)) != 0U) {
            Codegen_EmitPush(ctx, (int)i);
            stack_bytes += 8U;
        }
    }
    if ((stack_bytes & 0x0FU) != 0U) {
        stack_bytes += 8U;
    }
    if (stack_bytes > 0U) {
        Codegen_EmitSubRegImm32(ctx, X64_REG_RSP, (int32_t)stack_bytes);
    }
    ctx->stack_frame_size = (int32_t)stack_bytes;
    ctx->local_variables_size = (int32_t)((local_var_size + 15U) & ~15U);
    if (ctx->stack_frame_size > ctx->max_stack_frame) {
        ctx->max_stack_frame = ctx->stack_frame_size;
    }
}

void Codegen_EmitEpilogue(Codegen_Context *ctx,
                          uint32_t preserved_regs_mask) {
    int i;
    if (!ctx) {
        return;
    }
    if (ctx->stack_frame_size > 0) {
        Codegen_EmitAddRegImm32(ctx, X64_REG_RSP, ctx->stack_frame_size);
    }
    for (i = 15; i >= 0; --i) {
        if ((preserved_regs_mask & (1U << i)) != 0U) {
            Codegen_EmitPop(ctx, i);
        }
    }
    Codegen_EmitPop(ctx, X64_REG_RBP);
    Codegen_EmitRet(ctx);
}

void Codegen_EmitMovRegImm64(Codegen_Context *ctx, int reg, uint64_t imm64) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    Codegen_EmitByte(ctx, rex_prefix(1, 0, 0, is_extended_reg(reg)));
    Codegen_EmitByte(ctx, (uint8_t)(0xB8U + (reg & 7)));
    Codegen_EmitQword(ctx, imm64);
}

void Codegen_EmitMovRegReg(Codegen_Context *ctx, int dst_reg, int src_reg) {
    if (!ctx || dst_reg < 0 || dst_reg > 15 || src_reg < 0 || src_reg > 15) {
        return;
    }
    Codegen_EmitByte(ctx, rex_prefix(1, is_extended_reg(src_reg), 0, is_extended_reg(dst_reg)));
    Codegen_EmitByte(ctx, 0x89U);
    emit_modrm(ctx, 3U, (uint8_t)src_reg, (uint8_t)dst_reg);
}

void Codegen_EmitMovRegMemory(Codegen_Context *ctx, int reg, int32_t offset) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    Codegen_EmitByte(ctx, rex_prefix(1, is_extended_reg(reg), 0, 0));
    Codegen_EmitByte(ctx, 0x8BU);
    emit_modrm(ctx, 2U, (uint8_t)reg, X64_REG_RBP);
    Codegen_EmitDword(ctx, (uint32_t)offset);
}

void Codegen_EmitMovMemoryReg(Codegen_Context *ctx, int32_t offset, int reg) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    Codegen_EmitByte(ctx, rex_prefix(1, is_extended_reg(reg), 0, 0));
    Codegen_EmitByte(ctx, 0x89U);
    emit_modrm(ctx, 2U, (uint8_t)reg, X64_REG_RBP);
    Codegen_EmitDword(ctx, (uint32_t)offset);
}

void Codegen_EmitMovRegImm32(Codegen_Context *ctx, int reg, int32_t imm32) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    Codegen_EmitByte(ctx, rex_prefix(1, 0, 0, is_extended_reg(reg)));
    Codegen_EmitByte(ctx, 0xC7U);
    emit_modrm(ctx, 3U, 0U, (uint8_t)reg);
    Codegen_EmitDword(ctx, (uint32_t)imm32);
}

void Codegen_EmitLea(Codegen_Context *ctx, int reg, int32_t rip_relative_offset) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    Codegen_EmitByte(ctx, rex_prefix(1, is_extended_reg(reg), 0, 0));
    Codegen_EmitByte(ctx, 0x8DU);
    emit_modrm(ctx, 0U, (uint8_t)reg, 5U);
    Codegen_EmitDword(ctx, (uint32_t)rip_relative_offset);
}

void Codegen_EmitXorRegReg(Codegen_Context *ctx, int dst, int src) {
    if (!ctx || dst < 0 || dst > 15 || src < 0 || src > 15) {
        return;
    }
    Codegen_EmitByte(ctx, rex_prefix(1, is_extended_reg(dst), 0, is_extended_reg(src)));
    Codegen_EmitByte(ctx, 0x33U);
    emit_modrm(ctx, 3U, (uint8_t)dst, (uint8_t)src);
}

void Codegen_EmitCmpRegImm32(Codegen_Context *ctx, int reg, int32_t imm32) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    Codegen_EmitByte(ctx, rex_prefix(1, 0, 0, is_extended_reg(reg)));
    Codegen_EmitByte(ctx, 0x81U);
    emit_modrm(ctx, 3U, 7U, (uint8_t)reg);
    Codegen_EmitDword(ctx, (uint32_t)imm32);
}

void Codegen_EmitCallRel32(Codegen_Context *ctx, int32_t target_rva) {
    Codegen_EmitByte(ctx, 0xE8U);
    Codegen_EmitDword(ctx, (uint32_t)target_rva);
}

void Codegen_EmitCallR64(Codegen_Context *ctx, int reg) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    Codegen_EmitByte(ctx, rex_prefix(0, 0, 0, is_extended_reg(reg)));
    Codegen_EmitByte(ctx, 0xFFU);
    emit_modrm(ctx, 3U, 2U, (uint8_t)reg);
}

void Codegen_EmitJmpRel32(Codegen_Context *ctx, int32_t target_rva) {
    Codegen_EmitByte(ctx, 0xE9U);
    Codegen_EmitDword(ctx, (uint32_t)target_rva);
}

void Codegen_EmitJmpR64(Codegen_Context *ctx, int reg) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    Codegen_EmitByte(ctx, rex_prefix(0, 0, 0, is_extended_reg(reg)));
    Codegen_EmitByte(ctx, 0xFFU);
    emit_modrm(ctx, 3U, 4U, (uint8_t)reg);
}

void Codegen_EmitRet(Codegen_Context *ctx) {
    Codegen_EmitByte(ctx, 0xC3U);
}

void Codegen_EmitConditionalJmp(Codegen_Context *ctx, int condition, int32_t target_rva) {
    Codegen_EmitByte(ctx, 0x0FU);
    Codegen_EmitByte(ctx, (uint8_t)(0x80U + (condition & 0x0F)));
    Codegen_EmitDword(ctx, (uint32_t)target_rva);
}

void Codegen_EmitNop(Codegen_Context *ctx) {
    Codegen_EmitByte(ctx, 0x90U);
}

void Codegen_EmitPadding(Codegen_Context *ctx, uint32_t bytes) {
    uint32_t i;
    for (i = 0; i < bytes; ++i) {
        Codegen_EmitNop(ctx);
    }
}

static void emit_mov_reg_gs_dword(Codegen_Context *ctx, int reg, uint32_t gs_offset) {
    if (!ctx || reg < 0 || reg > 15) {
        return;
    }
    Codegen_EmitByte(ctx, 0x65U);
    Codegen_EmitByte(ctx, rex_prefix(1, is_extended_reg(reg), 0, 0));
    Codegen_EmitByte(ctx, 0x8BU);
    emit_modrm(ctx, 0U, (uint8_t)reg, 4U);
    Codegen_EmitByte(ctx, 0x25U);
    Codegen_EmitDword(ctx, gs_offset);
}

static void emit_mov_reg_from_base_disp32(Codegen_Context *ctx, int dst_reg, int base_reg, uint32_t disp32) {
    if (!ctx || dst_reg < 0 || dst_reg > 15 || base_reg < 0 || base_reg > 15) {
        return;
    }
    Codegen_EmitByte(ctx, rex_prefix(1, is_extended_reg(dst_reg), 0, is_extended_reg(base_reg)));
    Codegen_EmitByte(ctx, 0x8BU);
    emit_modrm(ctx, 2U, (uint8_t)dst_reg, (uint8_t)base_reg);
    if ((base_reg & 7) == X64_REG_RSP || (base_reg & 7) == X64_REG_R12) {
        Codegen_EmitByte(ctx, 0x24U);
    }
    Codegen_EmitDword(ctx, disp32);
}

void Codegen_EmitInitializeAetheliumRegisters(Codegen_Context *ctx) {
    if (!ctx) {
        return;
    }
    Codegen_EmitLea(ctx, X64_REG_R12, 0);
    Codegen_EmitLea(ctx, X64_REG_R13, 0);
    emit_mov_reg_gs_dword(ctx, X64_REG_R14, CODEGEN_WIN64_TEB_GS_OFFSET);
    Codegen_EmitXorRegReg(ctx, X64_REG_R15, X64_REG_R15);
}

void Codegen_EmitLoadPortalPool(Codegen_Context *ctx, int dest_reg) {
    emit_mov_reg_from_base_disp32(ctx, dest_reg, X64_REG_R12, 0U);
}

void Codegen_EmitLoadMirrorState(Codegen_Context *ctx, int dest_reg) {
    emit_mov_reg_from_base_disp32(ctx, dest_reg, X64_REG_R13, 0U);
}

void Codegen_EmitPortalCall(Codegen_Context *ctx,
                            const char *function_name,
                            int param_count) {
    uint32_t offset = portal_slot_offset(function_name);
    (void)param_count;
    if (!ctx) {
        return;
    }
    emit_mov_reg_from_base_disp32(ctx, X64_REG_RAX, X64_REG_R12, offset);
    Codegen_EmitCallR64(ctx, X64_REG_RAX);
}

int Codegen_BeginExceptionRegion(Codegen_Context *ctx) {
    int region_id;
    if (!ctx || ctx->unwind_info_count >= 256U) {
        return -1;
    }
    region_id = (int)ctx->unwind_info_count;
    ctx->unwind_infos[ctx->unwind_info_count].function_start_rva = ctx->code_offset;
    ctx->unwind_infos[ctx->unwind_info_count].unwind_info[0] = 0x01U;
    ctx->unwind_infos[ctx->unwind_info_count].unwind_info[1] = 0x00U;
    ctx->unwind_infos[ctx->unwind_info_count].unwind_info[2] = 0x00U;
    ctx->unwind_infos[ctx->unwind_info_count].unwind_info[3] = 0x00U;
    ctx->unwind_infos[ctx->unwind_info_count].unwind_info_size = 4U;
    ctx->unwind_info_count++;
    return region_id;
}

void Codegen_EndExceptionRegion(Codegen_Context *ctx, int region_id) {
    if (ctx && region_id >= 0 && (uint32_t)region_id < ctx->unwind_info_count) {
        ctx->unwind_infos[region_id].function_end_rva = ctx->code_offset;
    }
}

int Codegen_GenerateUnwindInfo(Codegen_Context *ctx,
                               uint32_t function_start_rva,
                               uint32_t function_end_rva) {
    int region = Codegen_BeginExceptionRegion(ctx);
    if (region < 0) {
        return -1;
    }
    ctx->unwind_infos[region].function_start_rva = function_start_rva;
    ctx->unwind_infos[region].function_end_rva = function_end_rva;
    return 0;
}

void Codegen_AddRelocation(Codegen_Context *ctx,
                           uint32_t code_offset,
                           uint32_t reloc_type) {
    if (!ctx || ctx->relocation_count >= 1024U) {
        return;
    }
    ctx->relocations[ctx->relocation_count].offset = code_offset;
    ctx->relocations[ctx->relocation_count].type = reloc_type;
    ctx->relocations[ctx->relocation_count].symbol_id = 0U;
    ctx->relocation_count++;
}

const Relocation_Entry_Type *Codegen_GetRelocations(Codegen_Context *ctx, uint32_t *out_count) {
    if (!ctx || !out_count) {
        return NULL;
    }
    *out_count = ctx->relocation_count;
    return (const Relocation_Entry_Type *)ctx->relocations;
}

uint32_t Codegen_GetCurrentOffset(Codegen_Context *ctx) {
    return ctx ? ctx->code_offset : 0U;
}

void Codegen_AlignCode(Codegen_Context *ctx, uint32_t alignment) {
    uint32_t remainder;
    if (!ctx || alignment == 0U) {
        return;
    }
    remainder = ctx->code_offset % alignment;
    if (remainder != 0U) {
        Codegen_EmitPadding(ctx, alignment - remainder);
    }
}

uint8_t *Codegen_GetCodeBuffer(Codegen_Context *ctx, uint32_t *out_size) {
    if (!ctx || !out_size) {
        return NULL;
    }
    *out_size = ctx->code_offset;
    return ctx->code_buffer;
}

int Codegen_VerifyCode(Codegen_Context *ctx) {
    if (!ctx) {
        return -1;
    }
    if (ctx->code_offset > ctx->code_capacity) {
        return -1;
    }
    return 0;
}

void Codegen_DumpStatistics(Codegen_Context *ctx) {
    if (!ctx) {
        return;
    }
    fprintf(stderr, "Codegen: size=%u capacity=%u max_frame=%d reloc=%u unwind=%u\n",
            ctx->code_offset,
            ctx->code_capacity,
            ctx->max_stack_frame,
            ctx->relocation_count,
            ctx->unwind_info_count);
}

void Codegen_Disassemble(Codegen_Context *ctx,
                         uint32_t start_offset,
                         uint32_t end_offset) {
    (void)ctx;
    (void)start_offset;
    (void)end_offset;
}

void Codegen_EnsureShadowSpace(Codegen_Context *ctx, uint32_t param_count) {
    uint32_t required = CODEGEN_WIN64_SHADOW_SPACE + (param_count > 4U ? ((param_count - 4U) * 8U) : 0U);
    if (!ctx) {
        return;
    }
    if ((int32_t)required > ctx->max_stack_frame) {
        ctx->max_stack_frame = (int32_t)required;
    }
}

void Codegen_ReleaseShadowSpace(Codegen_Context *ctx) {
    (void)ctx;
}
