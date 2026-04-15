/*
 * =========================================================================
 * Aethelium EXE X64 Code Generation - Industrial Grade Implementation
 * =========================================================================
 * 
 * File: exe_codegen_win64.h
 * Purpose: X64 machine code generation with Aethelium register preservation
 *          Register allocation strategy for R12-R15 global state
 *          Stack shadow space management for Win64 ABI compliance
 *          Exception handling (SEH/PDATA generation)
 *          Relocation record generation for ASLR support
 * 
 * Windows x64 ABI Requirements:
 *   - Shadow space: 32 bytes (0x20) on stack for first 4 parameters
 *   - Stack alignment: 16-byte alignment (including return address)
 *   - Callee-saved: RBX, RBP, RDI, RSI, R12-R15, XMM6-XMM15
 *   - Caller-saved: RAX, RCX, RDX, R8-R11, XMM0-XMM5
 * 
 * Aethelium Register Strategy:
 *   - R12: Global ActFlow Context (Portal Pool)
 *   - R13: Mirror State Base (constants, metadata)
 *   - R14: TEB (Thread Environment Block)
 *   - R15: SIMD state masks and temporary storage
 * 
 * =========================================================================
 */

#ifndef AETHELIUM_EXE_CODEGEN_WIN64_H
#define AETHELIUM_EXE_CODEGEN_WIN64_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
   X64 Instruction Encoding Constants
   ========================================================================= */

/* REX prefix byte */
#define X64_REX_W                    0x48  /* 64-bit operand */
#define X64_REX_R                    0x44  /* ModRM.reg extension */
#define X64_REX_X                    0x42  /* SIB.index extension */
#define X64_REX_B                    0x41  /* ModRM.r/m or SIB.base extension */

/* Opcode categories */
#define X64_OP_MOV_IMM64             0xB8  /* mov r64, imm64 */
#define X64_OP_MOV_RM                0x8B  /* mov r, r/m */
#define X64_OP_MOV_MR                0x89  /* mov r/m, r */
#define X64_OP_MOV_RM_IMM32          0xC7  /* mov r/m64, sign-ext imm32 */
#define X64_OP_LEA                   0x8D  /* lea r, [rip+disp] */
#define X64_OP_PUSH                  0x50  /* push r */
#define X64_OP_POP                   0x58  /* pop r */
#define X64_OP_ADD_RM_IMM32          0x81  /* add r/m64, imm32 */
#define X64_OP_SUB_RM_IMM32          0x81  /* sub r/m64, imm32 (with /5) */
#define X64_OP_XOR_RM_IMM32          0x81  /* xor r/m64, imm32 (with /6) */
#define X64_OP_CMP_RM_IMM32          0x81  /* cmp r/m64, imm32 (with /7) */
#define X64_OP_CALL_RIP_REL          0xE8  /* call rel32 */
#define X64_OP_JMP_RIP_REL           0xE9  /* jmp rel32 */
#define X64_OP_JMP_R64               0xFF  /* jmp r64 (with /4) */
#define X64_OP_SYSCALL               0x0F05 /* syscall */
#define X64_OP_RET                   0xC3  /* ret */
#define X64_OP_NOP                   0x90  /* nop */

/* ModRM format: [mod(2) | reg(3) | r/m(3)] */
#define X64_MODRM(mod, reg, rm)      (((mod) << 6) | (((reg) & 7) << 3) | ((rm) & 7))

/* Register encoding (for ModRM and opcodes) */
#define X64_REG_RAX                  0
#define X64_REG_RCX                  1
#define X64_REG_RDX                  2
#define X64_REG_RBX                  3
#define X64_REG_RSP                  4
#define X64_REG_RBP                  5
#define X64_REG_RSI                  6
#define X64_REG_RDI                  7
#define X64_REG_R8                   0  /* With REX.B = 1 */
#define X64_REG_R9                   1  /* With REX.B = 1 */
#define X64_REG_R10                  2  /* With REX.B = 1 */
#define X64_REG_R11                  3  /* With REX.B = 1 */
#define X64_REG_R12                  4  /* With REX.B = 1 */
#define X64_REG_R13                  5  /* With REX.B = 1 */
#define X64_REG_R14                  6  /* With REX.B = 1 */
#define X64_REG_R15                  7  /* With REX.B = 1 */

/* Condition codes for Jcc */
#define X64_JCC_O                    0x0  /* Overflow */
#define X64_JCC_NO                   0x1  /* Not overflow */
#define X64_JCC_B                    0x2  /* Below (unsigned <) */
#define X64_JCC_AE                   0x3  /* Above or equal */
#define X64_JCC_E                    0x4  /* Equal (zero flag set) */
#define X64_JCC_NE                   0x5  /* Not equal */
#define X64_JCC_BE                   0x6  /* Below or equal */
#define X64_JCC_A                    0x7  /* Above */
#define X64_JCC_S                    0x8  /* Sign flag set */
#define X64_JCC_NS                   0x9  /* Sign flag not set */
#define X64_JCC_P                    0xA  /* Parity flag set */
#define X64_JCC_NP                   0xB  /* Parity flag not set */
#define X64_JCC_L                    0xC  /* Less (signed <) */
#define X64_JCC_GE                   0xD  /* Greater or equal (signed >=) */
#define X64_JCC_LE                   0xE  /* Less or equal (signed <=) */
#define X64_JCC_G                    0xF  /* Greater (signed >) */

/* =========================================================================
   Code Generation Context
   ========================================================================= */

typedef struct {
    /* Output buffer */
    uint8_t *code_buffer;               /* Generated machine code */
    uint32_t code_offset;               /* Current offset in buffer */
    uint32_t code_capacity;             /* Buffer size */
    
    /* Stack frame management */
    int32_t stack_frame_size;           /* Current frame size (negative values) */
    int32_t max_stack_frame;            /* Maximum frame size encountered */
    int32_t local_variables_size;       /* Size of local variable storage */
    
    /* Register allocation */
    uint32_t allocated_regs;            /* Bitmask of allocated registers */
    uint32_t preserved_regs;            /* Bitmask of callee-saved registers */
    
    /* Relocation information */
    uint32_t relocation_count;
    struct {
        uint32_t offset;                /* Code offset where relocation applies */
        uint32_t type;                  /* Relocation type */
        uint32_t symbol_id;             /* Symbol reference */
    } relocations[1024];
    
    /* Exception handling */
    uint32_t unwind_info_count;
    struct {
        uint32_t function_start_rva;    /* Function start RVA */
        uint32_t function_end_rva;      /* Function end RVA */
        uint8_t unwind_info[128];       /* UNWIND_INFO structure */
        uint32_t unwind_info_size;
    } unwind_infos[256];
    
    /* Debug information */
    int emit_debug_info;                /* Generate debug line info */
    int emit_unwind_info;               /* Generate exception handling */
    
} Codegen_Context;

/* =========================================================================
   Code Generation Functions - Instruction Emission
   ========================================================================= */

/* Initialize code generation context */
int Codegen_Initialize(Codegen_Context *ctx,
                       uint8_t *code_buffer,
                       uint32_t buffer_size);

/* Emit single byte to code buffer */
void Codegen_EmitByte(Codegen_Context *ctx, uint8_t byte);

/* Emit 2-byte value (little-endian) */
void Codegen_EmitWord(Codegen_Context *ctx, uint16_t word);

/* Emit 4-byte value (little-endian) */
void Codegen_EmitDword(Codegen_Context *ctx, uint32_t dword);

/* Emit 8-byte value (little-endian) */
void Codegen_EmitQword(Codegen_Context *ctx, uint64_t qword);

/* Emit arbitrary buffer */
void Codegen_EmitBytes(Codegen_Context *ctx,
                       const uint8_t *data,
                       uint32_t size);

/* =========================================================================
   Register Management
   ========================================================================= */

/* Allocate register for use */
int Codegen_AllocateRegister(Codegen_Context *ctx);

/* Free previously allocated register */
void Codegen_FreeRegister(Codegen_Context *ctx, int reg);

/* Preserve callee-saved register at function entry */
void Codegen_PreserveRegister(Codegen_Context *ctx, int reg);

/* Restore callee-saved register at function exit */
void Codegen_RestoreRegister(Codegen_Context *ctx, int reg);

/* =========================================================================
   Stack Frame Management (Windows x64 ABI)
   ========================================================================= */

/* Allocate space on stack for local variables */
int32_t Codegen_AllocateStack(Codegen_Context *ctx, uint32_t size);

/* Free previously allocated stack space */
void Codegen_FreeStack(Codegen_Context *ctx, uint32_t size);

/* Generate function prologue (stack frame setup) */
void Codegen_EmitPrologue(Codegen_Context *ctx,
                          uint32_t local_var_size,
                          uint32_t preserved_regs_mask);

/* Generate function epilogue (stack frame cleanup and return) */
void Codegen_EmitEpilogue(Codegen_Context *ctx,
                          uint32_t preserved_regs_mask);

/* =========================================================================
   Instruction Emission Helpers
   ========================================================================= */

/* MOV instruction variants */
void Codegen_EmitMovRegImm64(Codegen_Context *ctx, int reg, uint64_t imm64);
void Codegen_EmitMovRegReg(Codegen_Context *ctx, int dst_reg, int src_reg);
void Codegen_EmitMovRegMemory(Codegen_Context *ctx, int reg, int32_t addr_offset);
void Codegen_EmitMovMemoryReg(Codegen_Context *ctx, int32_t addr_offset, int reg);
void Codegen_EmitMovRegImm32(Codegen_Context *ctx, int reg, int32_t imm32);

/* LEA instruction (Load Effective Address) - for RIP-relative addressing */
void Codegen_EmitLea(Codegen_Context *ctx, int reg, int32_t rip_relative_offset);

/* PUSH/POP instructions */
void Codegen_EmitPush(Codegen_Context *ctx, int reg);
void Codegen_EmitPop(Codegen_Context *ctx, int reg);

/* Arithmetic operations */
void Codegen_EmitAddRegImm32(Codegen_Context *ctx, int reg, int32_t imm32);
void Codegen_EmitSubRegImm32(Codegen_Context *ctx, int reg, int32_t imm32);
void Codegen_EmitXorRegReg(Codegen_Context *ctx, int dst, int src);
void Codegen_EmitCmpRegImm32(Codegen_Context *ctx, int reg, int32_t imm32);

/* Control flow */
void Codegen_EmitCallRel32(Codegen_Context *ctx, int32_t target_rva);
void Codegen_EmitCallR64(Codegen_Context *ctx, int reg);
void Codegen_EmitJmpRel32(Codegen_Context *ctx, int32_t target_rva);
void Codegen_EmitJmpR64(Codegen_Context *ctx, int reg);
void Codegen_EmitRet(Codegen_Context *ctx);
void Codegen_EmitConditionalJmp(Codegen_Context *ctx, int condition, int32_t target_rva);

/* NOP (no operation) for alignment */
void Codegen_EmitNop(Codegen_Context *ctx);
void Codegen_EmitPadding(Codegen_Context *ctx, uint32_t bytes);

/* =========================================================================
   Special Aethelium Instructions
   ========================================================================= */

/* Initialize R12-R15 with Aethelium global state */
void Codegen_EmitInitializeAetheliumRegisters(Codegen_Context *ctx);

/* Load Portal Pool address from R12 */
void Codegen_EmitLoadPortalPool(Codegen_Context *ctx, int dest_reg);

/* Load Mirror State from R13 */
void Codegen_EmitLoadMirrorState(Codegen_Context *ctx, int dest_reg);

/* Access NT function via Portal (indirect call through R12) */
void Codegen_EmitPortalCall(Codegen_Context *ctx,
                            const char *function_name,
                            int param_count);

/* =========================================================================
   Exception Handling (SEH/PDATA)
   ========================================================================= */

/* Begin exception protected region */
int Codegen_BeginExceptionRegion(Codegen_Context *ctx);

/* End exception protected region */
void Codegen_EndExceptionRegion(Codegen_Context *ctx, int region_id);

/* Generate UNWIND_INFO for x64 exception handling */
int Codegen_GenerateUnwindInfo(Codegen_Context *ctx,
                               uint32_t function_start_rva,
                               uint32_t function_end_rva);

/* =========================================================================
   Relocation Records (for ASLR)
   =========================================================================*/

/* Relocation entry structure */
typedef struct {
    uint32_t offset;
    uint32_t type;
    uint32_t symbol_id;
} Relocation_Entry_Type;

/* Add relocation record */
void Codegen_AddRelocation(Codegen_Context *ctx,
                           uint32_t code_offset,
                           uint32_t reloc_type);

/* Get relocation records */
const Relocation_Entry_Type *Codegen_GetRelocations(Codegen_Context *ctx, uint32_t *out_count);

/* =========================================================================
   Code Buffer Management
   ========================================================================= */

/* Get current code offset */
uint32_t Codegen_GetCurrentOffset(Codegen_Context *ctx);

/* Align code to boundary (e.g., 16-byte for cache line) */
void Codegen_AlignCode(Codegen_Context *ctx, uint32_t alignment);

/* Get generated code buffer */
uint8_t *Codegen_GetCodeBuffer(Codegen_Context *ctx, uint32_t *out_size);

/* =========================================================================
   Utility Functions
   ========================================================================= */

/* Verify generated code for correctness */
int Codegen_VerifyCode(Codegen_Context *ctx);

/* Dump code generation statistics */
void Codegen_DumpStatistics(Codegen_Context *ctx);

/* Disassemble generated code (for debugging) */
void Codegen_Disassemble(Codegen_Context *ctx,
                         uint32_t start_offset,
                         uint32_t end_offset);

/* =========================================================================
   Stack Shadow Space (for Win64 ABI compliance)
   =========================================================================*/

/* Ensure shadow space is available for function calls */
void Codegen_EnsureShadowSpace(Codegen_Context *ctx, uint32_t param_count);

/* Release shadow space after call */
void Codegen_ReleaseShadowSpace(Codegen_Context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* AETHELIUM_EXE_CODEGEN_WIN64_H */
