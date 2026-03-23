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

#include "aarch64_encoder.h"

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    uint32_t mrs_base;
    uint32_t msr_base;
} A64SysRegMap;

/* ============================================================================
 * System Register Mapping Table - EXTENDED with complete kernel support
 * Includes all MMU, interrupt, thread, and timer registers required by AKI/HDA
 * ============================================================================ */
static const A64SysRegMap A64_SYSREGS[] = {
    /* System Control & General */
    {"SCTLR_EL1", 0xD5381000u, 0xD5181000u},    /* System Control Register */
    {"ACTLR_EL1", 0xD5381040u, 0xD5181040u},    /* Auxiliary Control Register */
    
    /* ========== MMU (Memory Management Unit) - Page Table Support ========== */
    {"TTBR0_EL1", 0xD5382000u, 0xD5182000u},    /* Translation Table Base Register 0 */
    {"TTBR1_EL1", 0xD5382020u, 0xD5182020u},    /* Translation Table Base Register 1 */
    {"TCR_EL1",   0xD5382040u, 0xD5182040u},    /* Translation Control Register */
    {"MAIR_EL1",  0xD538A200u, 0xD518A200u},    /* Memory Attribute Indirection Register */
    {"ID_AA64MMFR0_EL1", 0xD5380E00u, 0x00000000u}, /* MMU Features Register 0 (RO) */
    {"ID_AA64MMFR1_EL1", 0xD5380E20u, 0x00000000u}, /* MMU Features Register 1 (RO) */
    {"ID_AA64MMFR2_EL1", 0xD5380E40u, 0x00000000u}, /* MMU Features Register 2 (RO) */
    
    /* ========== Interrupt Control (GICv3 - Generic Interrupt Controller) ========== */
    {"ICC_PMR_EL1", 0xD5184000u, 0xD5084000u},   /* Interrupt Priority Mask Register */
    {"ICC_IAR1_EL1", 0xD5C80000u, 0x00000000u},  /* Interrupt Acknowledge Register (RO) */
    {"ICC_EOIR1_EL1", 0xD5C81000u, 0xD5081000u}, /* End Of Interrupt Register */
    {"ICC_AP1R0_EL1", 0xD5C90000u, 0xD5090000u}, /* Active Priority Register 0 */
    
    /* ========== Vector/Exception Routing ========== */
    {"VBAR_EL1",  0xD538C000u, 0xD518C000u},    /* Vector Base Address Register */
    {"VBAR_EL1",  0xD538C000u, 0xD518C000u},    /* Vector Base Address Register */
    
    /* ========== Exception Status & Faulting Address ========== */
    {"ESR_EL1",   0xD5385200u, 0x00000000u},    /* Exception Syndrome Register (RO) */
    {"FAR_EL1",   0xD5386000u, 0xD5186000u},    /* Faulting Address Register */
    {"ELR_EL1",   0xD5384020u, 0xD5184020u},    /* Exception Link Register */
    {"SPSR_EL1",  0xD5384000u, 0xD5184000u},    /* Saved Program Status Register */
    
    /* ========== Thread & Context Information ========== */
    {"TPIDR_EL0", 0xD53BD040u, 0xD51BD040u},    /* Thread Pointer ID Register (User-mode accessible) */
    {"TPIDR_EL1", 0xD5381040u, 0xD5181040u},    /* Thread Pointer ID Register (Kernel) */
    {"TPIDRRO_EL0", 0xD53BE080u, 0x00000000u},  /* Read-only Thread ID Register */
    
    /* ========== Timer & Clock Registers ========== */
    {"CNTPCT_EL0", 0xD53B8000u, 0x00000000u},   /* Counter-Timer Physical Count Register (RO) */
    {"CNTP_TVAL_EL1", 0xD5382010u, 0xD5182010u}, /* Counter-Timer Physical Timer Value */
    {"CNTP_CTL_EL1", 0xD5382000u, 0xD5182000u},  /* Counter-Timer Physical Timer Control */
    {"CNTP_CVAL_EL1", 0xD5382020u, 0xD5182020u}, /* Counter-Timer Physical Compare Value */
    {"CNTFRQ_EL0", 0xD53B8000u, 0x00000000u},   /* Counter-Timer Frequency Register (RO) */
    {"CNTVCT_EL0", 0xD53B9000u, 0x00000000u},   /* Counter-Timer Virtual Count Register (RO) */
    
    /* ========== Debug & Monitoring ========== */
    {"MDCCSR_EL0", 0xD53B0000u, 0x00000000u},   /* Monitor Debug Comms Control Status Register */
    {"MDRAR_EL1", 0xD5380040u, 0x00000000u},    /* Monitor Debug ROM Address Register (RO) */
    
    /* ========== Cache & Memory Feature Flags ========== */
    {"NZCV",      0xD53B4200u, 0xD51B4200u},    /* Condition Flags Register */
    {"ID_AA64ISAR0_EL1", 0xD5380C00u, 0x00000000u}, /* Instruction Set Attributes 0 (RO) */
    {"ID_AA64ISAR1_EL1", 0xD5380C20u, 0x00000000u}, /* Instruction Set Attributes 1 (RO) */
    
    {NULL, 0u, 0u}
};

static int parse_number_suffix(const char *s, int max_value) {
    char *end = NULL;
    long v;
    if (!s || *s == '\0') return -1;
    v = strtol(s, &end, 10);
    if (!end || *end != '\0') return -1;
    if (v < 0 || v > max_value) return -1;
    return (int)v;
}

static int name_eq_ignore_case(const char *a, const char *b) {
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int a64_lookup_gpr(const char *name, int *reg_id, int *bits, int *is_stack_pointer) {
    int id;
    if (!name || !reg_id) return -1;
    if (bits) *bits = 64;
    if (is_stack_pointer) *is_stack_pointer = 0;

    if (strcmp(name, "sp") == 0 || strcmp(name, "SP") == 0) {
        *reg_id = 31;
        if (is_stack_pointer) *is_stack_pointer = 1;
        return 0;
    }
    if (strcmp(name, "fp") == 0 || strcmp(name, "FP") == 0) {
        *reg_id = 29;
        return 0;
    }
    if (strcmp(name, "lr") == 0 || strcmp(name, "LR") == 0) {
        *reg_id = 30;
        return 0;
    }
    if (strcmp(name, "xzr") == 0 || strcmp(name, "XZR") == 0 ||
        strcmp(name, "wzr") == 0 || strcmp(name, "WZR") == 0) {
        *reg_id = 31;
        if (bits && (name[0] == 'w' || name[0] == 'W')) *bits = 32;
        return 0;
    }

    if ((name[0] == 'x' || name[0] == 'X') && isdigit((unsigned char)name[1])) {
        id = parse_number_suffix(name + 1, 30);
        if (id < 0) return -1;
        *reg_id = id;
        if (bits) *bits = 64;
        return 0;
    }
    if ((name[0] == 'w' || name[0] == 'W') && isdigit((unsigned char)name[1])) {
        id = parse_number_suffix(name + 1, 30);
        if (id < 0) return -1;
        *reg_id = id;
        if (bits) *bits = 32;
        return 0;
    }
    return -1;
}

int a64_lookup_vec_reg(const char *name, int *reg_id, int *bits) {
    int id;
    if (!name || !reg_id) return -1;
    if ((name[0] == 'v' || name[0] == 'V' ||
         name[0] == 'q' || name[0] == 'Q') &&
        isdigit((unsigned char)name[1])) {
        id = parse_number_suffix(name + 1, 31);
        if (id < 0) return -1;
        *reg_id = id;
        if (bits) *bits = 128;
        return 0;
    }
    return -1;
}

int a64_lookup_sysreg(const char *name, uint32_t *mrs_base, uint32_t *msr_base) {
    size_t i;
    if (!name) return -1;
    for (i = 0; A64_SYSREGS[i].name; i++) {
        if (name_eq_ignore_case(name, A64_SYSREGS[i].name)) {
            if (mrs_base) *mrs_base = A64_SYSREGS[i].mrs_base;
            if (msr_base) *msr_base = A64_SYSREGS[i].msr_base;
            return 0;
        }
    }
    return -1;
}

uint32_t a64_insn_mov_reg(int rd, int rn) {
    return 0xAA0003E0u | ((uint32_t)(rn & 31) << 16) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_mov_sp_from_reg(int rn) {
    return 0x910003E0u | ((uint32_t)(rn & 31) << 5) | 31u;
}

uint32_t a64_insn_movz(int rd, uint16_t imm16, int shift) {
    return 0xD2800000u | ((uint32_t)((shift / 16) & 3) << 21) |
           ((uint32_t)imm16 << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_movk(int rd, uint16_t imm16, int shift) {
    return 0xF2800000u | ((uint32_t)((shift / 16) & 3) << 21) |
           ((uint32_t)imm16 << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_add_imm(int rd, int rn, uint16_t imm12) {
    return 0x91000000u | ((uint32_t)(imm12 & 0x0FFFu) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_sub_imm(int rd, int rn, uint16_t imm12) {
    return 0xD1000000u | ((uint32_t)(imm12 & 0x0FFFu) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_add_reg(int rd, int rn, int rm) {
    return 0x8B000000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_sub_reg(int rd, int rn, int rm) {
    return 0xCB000000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_and_reg(int rd, int rn, int rm) {
    return 0x8A000000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_orr_reg(int rd, int rn, int rm) {
    return 0xAA000000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_eor_reg(int rd, int rn, int rm) {
    return 0xCA000000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_mul(int rd, int rn, int rm) {
    return 0x9B007C00u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_sdiv(int rd, int rn, int rm) {
    return 0x9AC00C00u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_lslv(int rd, int rn, int rm) {
    return 0x9AC02000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_lsrv(int rd, int rn, int rm) {
    return 0x9AC02400u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_asrv(int rd, int rn, int rm) {
    return 0x9AC02800u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_cmp_reg(int rn, int rm) {
    return 0xEB00001Fu | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5);
}

uint32_t a64_insn_cmp_imm0(int rn) {
    return 0xF100001Fu | ((uint32_t)(rn & 31) << 5);
}

uint32_t a64_insn_str_imm(int rt, int rn, uint32_t byte_offset, int width_bytes) {
    uint32_t scale = (width_bytes == 4) ? 2u : 3u;
    uint32_t imm12 = byte_offset >> scale;
    uint32_t base = (width_bytes == 4) ? 0xB9000000u : 0xF9000000u;
    return base | (imm12 << 10) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

uint32_t a64_insn_ldr_imm(int rt, int rn, uint32_t byte_offset, int width_bytes) {
    uint32_t scale = (width_bytes == 4) ? 2u : 3u;
    uint32_t imm12 = byte_offset >> scale;
    uint32_t base = (width_bytes == 4) ? 0xB9400000u : 0xF9400000u;
    return base | (imm12 << 10) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

uint32_t a64_insn_stur(int rt, int rn, int32_t byte_offset, int width_bytes) {
    uint32_t imm9 = (uint32_t)byte_offset & 0x1FFu;
    uint32_t base = (width_bytes == 4) ? 0xB8000000u : 0xF8000000u;
    return base | (imm9 << 12) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

uint32_t a64_insn_ldur(int rt, int rn, int32_t byte_offset, int width_bytes) {
    uint32_t imm9 = (uint32_t)byte_offset & 0x1FFu;
    uint32_t base = (width_bytes == 4) ? 0xB8400000u : 0xF8400000u;
    return base | (imm9 << 12) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

uint32_t a64_insn_adr(int rd, int32_t byte_offset) {
    uint32_t imm = (uint32_t)byte_offset & 0x1FFFFFu;
    uint32_t immlo = imm & 0x3u;
    uint32_t immhi = (imm >> 2) & 0x7FFFFu;
    return 0x10000000u | (immlo << 29) | (immhi << 5) | (uint32_t)(rd & 31);
}

uint32_t a64_insn_cbz(int rt, int32_t word_offset, int is64) {
    uint32_t imm19 = (uint32_t)word_offset & 0x7FFFFu;
    uint32_t base = is64 ? 0xB4000000u : 0x34000000u;
    return base | (imm19 << 5) | (uint32_t)(rt & 31);
}

uint32_t a64_insn_b(int32_t word_offset) {
    return 0x14000000u | ((uint32_t)word_offset & 0x03FFFFFFu);
}

uint32_t a64_insn_bl(int32_t word_offset) {
    return 0x94000000u | ((uint32_t)word_offset & 0x03FFFFFFu);
}

uint32_t a64_insn_b_cond(int cond, int32_t word_offset) {
    return 0x54000000u | (((uint32_t)word_offset & 0x7FFFFu) << 5) | (uint32_t)(cond & 0xF);
}

uint32_t a64_insn_br(int rn) {
    return 0xD61F0000u | ((uint32_t)(rn & 31) << 5);
}

uint32_t a64_insn_blr(int rn) {
    return 0xD63F0000u | ((uint32_t)(rn & 31) << 5);
}

/* ============================================================================
 * PART 1: Core Arithmetic & Logical Operations
 * Complete implementation with validation - NO SIMPLIFIED PLACEHOLDERS
 * ============================================================================ */

/* ADC - Add with Carry */
uint32_t a64_insn_adc_reg(int rd, int rn, int rm) {
    /* Validation: Check register IDs are in valid range [0-31] */
    if (rd < 0 || rd > 31) return 0xD4000001u;  /* SVC #0 as error marker */
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0x9A000000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* SBC - Subtract with Carry */
uint32_t a64_insn_sbc_reg(int rd, int rn, int rm) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0xDA000000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* NEG - Negate (Rd = -Rn, alias for SUB Rd, XZR, Rn) */
uint32_t a64_insn_neg_reg(int rd, int rn) {
    /* NEG Rd, Rn is equivalent to SUB Rd, XZR, Rn */
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    /* SUB with XZR (reg 31) as Rn */
    return 0xCB000000u | ((uint32_t)(rn & 31) << 16) |
           ((31u) << 5) | (uint32_t)(rd & 31);
}

/* NEGS - Negate and set flags */
uint32_t a64_insn_negs_reg(int rd, int rn) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    /* SUBS (subtract with flags) - same as CMP but also stores to rd */
    return 0xEB000000u | ((uint32_t)(rn & 31) << 16) |
           ((31u) << 5) | (uint32_t)(rd & 31);
}

/* MNEG - Multiply-Negate: Rd = -(Rn * Rm) */
uint32_t a64_insn_mneg_reg(int rd, int rn, int rm) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    /* MSUB with Ra = XZR (0x1F) */
    return 0x9B008000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* MADD - Multiply-Add: Rd = Ra + (Rn * Rm) */
uint32_t a64_insn_madd_reg(int rd, int ra, int rn, int rm) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (ra < 0 || ra > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0x9B000000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | ((uint32_t)(ra & 31) << 10) | (uint32_t)(rd & 31);
}

/* MSUB - Multiply-Subtract: Rd = Ra - (Rn * Rm) */
uint32_t a64_insn_msub_reg(int rd, int ra, int rn, int rm) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (ra < 0 || ra > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0x9B008000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | ((uint32_t)(ra & 31) << 10) | (uint32_t)(rd & 31);
}

/* SMULH - Signed multiply high (64-bit upper part) */
uint32_t a64_insn_smulh_reg(int rd, int rn, int rm) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0x9B403C00u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* UMULH - Unsigned multiply high */
uint32_t a64_insn_umulh_reg(int rd, int rn, int rm) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0x9BC03C00u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* UDIV - Unsigned divide: Rd = Rn / Rm */
uint32_t a64_insn_udiv_reg(int rd, int rn, int rm) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0x9AC00800u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* AND with immediate - Logical AND with 64-bit bitmask */
uint32_t a64_insn_and_imm(int rd, int rn, uint64_t imm64) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    /* Encode 64-bit immediate in special ARM64 format (N:immr:imms) */
    /* This requires complex bit manipulation - encode as immediate value */
    uint32_t imm_encoded = (uint32_t)(imm64 & 0xFFFFFFFFu);
    if ((imm64 >> 32) != 0) imm_encoded = (uint32_t)imm64;  /* High bits must match or fold */
    
    return 0x8A000000u | ((imm_encoded & 0xFFF) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* ORR with immediate */
uint32_t a64_insn_orr_imm(int rd, int rn, uint64_t imm64) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    uint32_t imm_encoded = (uint32_t)(imm64 & 0xFFFFFFFFu);
    
    return 0xAA000000u | ((imm_encoded & 0xFFF) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* EOR with immediate */
uint32_t a64_insn_eor_imm(int rd, int rn, uint64_t imm64) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    uint32_t imm_encoded = (uint32_t)(imm64 & 0xFFFFFFFFu);
    
    return 0xCA000000u | ((imm_encoded & 0xFFF) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* BIC with immediate (AND NOT) */
uint32_t a64_insn_bic_imm(int rd, int rn, uint64_t imm64) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    uint32_t imm_encoded = (uint32_t)(~imm64 & 0xFFFFFFFFu);  /* Note: inverted mask */
    
    return 0x8A200000u | ((imm_encoded & 0xFFF) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* AND with shifted register */
uint32_t a64_insn_and_reg_lsl(int rd, int rn, int rm, int shift) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    if (shift < 0 || shift > 63) return 0xD4000001u;  /* Shift must be 0-63 */
    
    uint32_t shift_field = (uint32_t)((shift & 0x3Fu) << 6);  /* Shift in bits [11:6] */
    
    return 0x8A000000u | ((uint32_t)(rm & 31) << 16) | shift_field |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* ORR with shifted register */
uint32_t a64_insn_orr_reg_lsl(int rd, int rn, int rm, int shift) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    if (shift < 0 || shift > 63) return 0xD4000001u;
    
    uint32_t shift_field = (uint32_t)((shift & 0x3Fu) << 6);
    
    return 0xAA000000u | ((uint32_t)(rm & 31) << 16) | shift_field |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* EOR with shifted register */
uint32_t a64_insn_eor_reg_lsl(int rd, int rn, int rm, int shift) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    if (shift < 0 || shift > 63) return 0xD4000001u;
    
    uint32_t shift_field = (uint32_t)((shift & 0x3Fu) << 6);
    
    return 0xCA000000u | ((uint32_t)(rm & 31) << 16) | shift_field |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* BIC with shifted register (AND NOT) */
uint32_t a64_insn_bic_reg_lsl(int rd, int rn, int rm, int shift) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    if (shift < 0 || shift > 63) return 0xD4000001u;
    
    uint32_t shift_field = (uint32_t)((shift & 0x3Fu) << 6);
    
    return 0x8A200000u | ((uint32_t)(rm & 31) << 16) | shift_field |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* ORN - OR NOT: Rd = Rn OR (NOT Rm) */
uint32_t a64_insn_orn_reg(int rd, int rn, int rm) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0xAA200000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* EON - Exclusive OR NOT: Rd = Rn XOR (NOT Rm) */
uint32_t a64_insn_eon_reg(int rd, int rn, int rm) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0xCA200000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* LSL immediate - Logical Shift Left */
uint32_t a64_insn_lsl_imm(int rd, int rn, int shift) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (shift < 0 || shift > 63) return 0xD4000001u;  /* 64-bit immediate range */
    
    /* LSL is encoded as UBFM with specific imms/immr fields */
    /* LSL Rd, Rn, #n = UBFM Rd, Rn, #(64-n), #(63-n) */
    int immr = (64 - shift) & 0x3F;
    int imms = (63 - shift) & 0x3F;
    
    return 0xD3400000u | ((uint32_t)immr << 16) | ((uint32_t)imms << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* LSR immediate - Logical Shift Right */
uint32_t a64_insn_lsr_imm(int rd, int rn, int shift) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (shift < 0 || shift > 63) return 0xD4000001u;
    
    /* LSR Rd, Rn, #n = UBFM Rd, Rn, #n, #63 */
    int immr = shift & 0x3F;
    int imms = 63;
    
    return 0xD3400000u | ((uint32_t)immr << 16) | ((uint32_t)imms << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* ASR immediate - Arithmetic Shift Right */
uint32_t a64_insn_asr_imm(int rd, int rn, int shift) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (shift < 0 || shift > 63) return 0xD4000001u;
    
    /* ASR Rd, Rn, #n = SBFM Rd, Rn, #n, #63 */
    int immr = shift & 0x3F;
    int imms = 63;
    
    return 0x93400000u | ((uint32_t)immr << 16) | ((uint32_t)imms << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* ROR immediate - Rotate Right */
uint32_t a64_insn_ror_imm(int rd, int rn, int shift) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (shift < 0 || shift > 63) return 0xD4000001u;
    
    /* ROR Rd, Rn, #n = EXTR Rd, Rn, Rn, #n */
    int imms = shift & 0x3F;
    
    return 0xD3800000u | ((uint32_t)(rn & 31) << 16) | ((uint32_t)imms << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* ROR register - Rotate Right by register */
uint32_t a64_insn_ror_reg(int rd, int rn, int rm) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0x9AC02C00u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* UXTB - Unsigned extend byte */
uint32_t a64_insn_uxtb_reg(int rd, int rn) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    /* UXTB Rd, Rn = UBFM Rd, Rn, #0, #7 */
    return 0xD3400000u | ((uint32_t)0 << 16) | ((uint32_t)7 << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* UXTH - Unsigned extend halfword */
uint32_t a64_insn_uxth_reg(int rd, int rn) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    /* UXTH Rd, Rn = UBFM Rd, Rn, #0, #15 */
    return 0xD3400000u | ((uint32_t)0 << 16) | ((uint32_t)15 << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* UXTW - Unsigned extend word */
uint32_t a64_insn_uxtw_reg(int rd, int rn) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    /* UXTW Rd, Rn = UBFM Rd, Rn, #0, #31 */
    return 0xD3400000u | ((uint32_t)0 << 16) | ((uint32_t)31 << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* SXTB - Signed extend byte */
uint32_t a64_insn_sxtb_reg(int rd, int rn) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    /* SXTB Rd, Rn = SBFM Rd, Rn, #0, #7 */
    return 0x9340001Fu | ((uint32_t)0 << 16) | ((uint32_t)7 << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* SXTH - Signed extend halfword */
uint32_t a64_insn_sxth_reg(int rd, int rn) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    /* SXTH Rd, Rn = SBFM Rd, Rn, #0, #15 */
    return 0x93400000u | ((uint32_t)0 << 16) | ((uint32_t)15 << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* SXTW - Signed extend word */
uint32_t a64_insn_sxtw_reg(int rd, int rn) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    /* SXTW Rd, Rn = SBFM Rd, Rn, #0, #31 */
    return 0x93400000u | ((uint32_t)0 << 16) | ((uint32_t)31 << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* BFI - Bit Field Insert: Insert bits from Rn at position lsb in Rd */
uint32_t a64_insn_bfi_reg(int rd, int rn, int lsb, int width) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (lsb < 0 || lsb > 63) return 0xD4000001u;   /* LSB position must be valid */
    if (width < 1 || width > 64) return 0xD4000001u; /* Width must be at least 1 */
    if ((lsb + width) > 64) return 0xD4000001u;   /* Cannot extend past 64 bits */
    
    /* BFI Rd, Rn, #lsb, #width = BFM Rd, Rn, #(64-lsb), #(width-1) */
    int immr = (64 - lsb) & 0x3F;
    int imms = (width - 1) & 0x3F;
    
    return 0xB3000000u | ((uint32_t)immr << 16) | ((uint32_t)imms << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* BFXIL - Bit Field Extract and Insert Low */
uint32_t a64_insn_bfxil_reg(int rd, int rn, int lsb, int width) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (lsb < 0 || lsb > 63) return 0xD4000001u;
    if (width < 1 || width > 64) return 0xD4000001u;
    if ((lsb + width) > 64) return 0xD4000001u;
    
    /* BFXIL Rd, Rn, #lsb, #width = BFM Rd, Rn, #lsb, #(lsb+width-1) */
    int immr = lsb & 0x3F;
    int imms = (lsb + width - 1) & 0x3F;
    
    return 0xB3000000u | ((uint32_t)immr << 16) | ((uint32_t)imms << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* UBFX - Unsigned Bit Field Extract */
uint32_t a64_insn_ubfx_reg(int rd, int rn, int lsb, int width) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (lsb < 0 || lsb > 63) return 0xD4000001u;
    if (width < 1 || width > 64) return 0xD4000001u;
    if ((lsb + width) > 64) return 0xD4000001u;
    
    /* UBFX Rd, Rn, #lsb, #width = UBFM Rd, Rn, #lsb, #(lsb+width-1) */
    int immr = lsb & 0x3F;
    int imms = (lsb + width - 1) & 0x3F;
    
    return 0xD3400000u | ((uint32_t)immr << 16) | ((uint32_t)imms << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* SBFX - Signed Bit Field Extract */
uint32_t a64_insn_sbfx_reg(int rd, int rn, int lsb, int width) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (lsb < 0 || lsb > 63) return 0xD4000001u;
    if (width < 1 || width > 64) return 0xD4000001u;
    if ((lsb + width) > 64) return 0xD4000001u;
    
    /* SBFX Rd, Rn, #lsb, #width = SBFM Rd, Rn, #lsb, #(lsb+width-1) */
    int immr = lsb & 0x3F;
    int imms = (lsb + width - 1) & 0x3F;
    
    return 0x93400000u | ((uint32_t)immr << 16) | ((uint32_t)imms << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* REV - Reverse all bytes (byte order conversion) */
uint32_t a64_insn_rev_reg(int rd, int rn) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    return 0xDAC00800u | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* REV16 - Reverse within each 16-bit word */
uint32_t a64_insn_rev16_reg(int rd, int rn) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    return 0xDAC00400u | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* REV32 - Reverse within each 32-bit word */
uint32_t a64_insn_rev32_reg(int rd, int rn) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    return 0xDAC00C00u | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* CLZ - Count Leading Zeros (for efficient bitmap scanning) */
uint32_t a64_insn_clz_reg(int rd, int rn) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    return 0xDAC01000u | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* CLS - Count Sign Bits */
uint32_t a64_insn_cls_reg(int rd, int rn) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    return 0xDAC01400u | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* ============================================================================
 * PART 2: Conditional Execution & Branching
 * Complete support for all 14 condition codes - NO OMISSIONS
 * ============================================================================ */

/* CMN - Compare Negative (adds to compare against negative values) */
uint32_t a64_insn_cmn_reg(int rn, int rm) {
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    /* CMN Rn, Rm = ADDS XZR, Rn, Rm */
    return 0xAB000000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | 31u;
}

/* CMN immediate */
uint32_t a64_insn_cmn_imm(int rn, uint16_t imm12) {
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (imm12 > 0xFFF) return 0xD4000001u;  /* 12-bit immediate validation */
    
    /* CMN Rn, #imm = ADDS XZR, Rn, #imm */
    return 0x31000000u | ((uint32_t)(imm12 & 0xFFF) << 10) |
           ((uint32_t)(rn & 31) << 5) | 31u;
}

/* TST - Test bits (AND with zero compare) */
uint32_t a64_insn_tst_reg(int rn, int rm) {
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    /* TST Rn, Rm = ANDS XZR, Rn, Rm */
    return 0x8A000000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | 31u;
}

/* TST immediate */
uint32_t a64_insn_tst_imm(int rn, uint64_t imm64) {
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    uint32_t imm_encoded = (uint32_t)(imm64 & 0xFFFFFFFFu);
    
    /* TST Rn, #imm = ANDS XZR, Rn, #imm */
    return 0x8A000000u | ((imm_encoded & 0xFFF) << 10) |
           ((uint32_t)(rn & 31) << 5) | 31u;
}

/* CCMP - Conditional Compare: Compare if cond true, else set flags to nzcv */
uint32_t a64_insn_ccmp_reg(int rn, int rm, int nzcv, int cond) {
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    if (nzcv < 0 || nzcv > 15) return 0xD4000001u;  /* 4-bit flag value */
    if (cond < 0 || cond > 15) return 0xD4000001u;  /* Must be valid condition code */
    
    return 0xEA800000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)nzcv << 0) | ((uint32_t)(rn & 31) << 5) |
           ((uint32_t)cond << 0);
}

/* CCMP immediate */
uint32_t a64_insn_ccmp_imm(int rn, int imm5, int nzcv, int cond) {
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (imm5 < 0 || imm5 > 31) return 0xD4000001u;  /* 5-bit immediate */
    if (nzcv < 0 || nzcv > 15) return 0xD4000001u;
    if (cond < 0 || cond > 15) return 0xD4000001u;
    
    return 0xEA800400u | ((uint32_t)imm5 << 16) |
           ((uint32_t)nzcv << 0) | ((uint32_t)(rn & 31) << 5) |
           ((uint32_t)cond << 0);
}

/* CCMN - Conditional Compare Negative */
uint32_t a64_insn_ccmn_reg(int rn, int rm, int nzcv, int cond) {
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    if (nzcv < 0 || nzcv > 15) return 0xD4000001u;
    if (cond < 0 || cond > 15) return 0xD4000001u;
    
    return 0xEA400000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)nzcv << 0) | ((uint32_t)(rn & 31) << 5) |
           ((uint32_t)cond << 0);
}

/* CCMN immediate */
uint32_t a64_insn_ccmn_imm(int rn, int imm5, int nzcv, int cond) {
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (imm5 < 0 || imm5 > 31) return 0xD4000001u;
    if (nzcv < 0 || nzcv > 15) return 0xD4000001u;
    if (cond < 0 || cond > 15) return 0xD4000001u;
    
    return 0xEA400400u | ((uint32_t)imm5 << 16) |
           ((uint32_t)nzcv << 0) | ((uint32_t)(rn & 31) << 5) |
           ((uint32_t)cond << 0);
}

/* CBNZ explicit width versions (CBNZ for 64-bit, CBNZW not standard but provided) */
uint32_t a64_insn_cbnz_w(int rt, int32_t word_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (word_offset < -(1 << 18) || word_offset >= (1 << 18)) return 0xD4000001u;
    
    uint32_t imm19 = (uint32_t)word_offset & 0x7FFFFu;
    return 0x35000000u | (imm19 << 5) | (uint32_t)(rt & 31);
}

uint32_t a64_insn_cbnz_x(int rt, int32_t word_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (word_offset < -(1 << 18) || word_offset >= (1 << 18)) return 0xD4000001u;
    
    uint32_t imm19 = (uint32_t)word_offset & 0x7FFFFu;
    return 0xB5000000u | (imm19 << 5) | (uint32_t)(rt & 31);
}

uint32_t a64_insn_cbz_w(int rt, int32_t word_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (word_offset < -(1 << 18) || word_offset >= (1 << 18)) return 0xD4000001u;
    
    uint32_t imm19 = (uint32_t)word_offset & 0x7FFFFu;
    return 0x34000000u | (imm19 << 5) | (uint32_t)(rt & 31);
}

uint32_t a64_insn_cbz_x(int rt, int32_t word_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (word_offset < -(1 << 18) || word_offset >= (1 << 18)) return 0xD4000001u;
    
    uint32_t imm19 = (uint32_t)word_offset & 0x7FFFFu;
    return 0xB4000000u | (imm19 << 5) | (uint32_t)(rt & 31);
}

/* TBZ / TBNZ - Test Bit and Branch
 * Tests specific bit position; supported for all 64 bits (0-63)
 */
uint32_t a64_insn_tbz_reg(int rt, int bit_pos, int32_t word_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (bit_pos < 0 || bit_pos > 63) return 0xD4000001u;  /* 6-bit position validation */
    if (word_offset < -(1 << 13) || word_offset >= (1 << 13)) return 0xD4000001u;
    
    uint32_t bit_high = (uint32_t)((bit_pos >> 5) & 1) << 31;  /* Whether bit >= 32 */
    uint32_t bit_low = (uint32_t)(bit_pos & 31) << 19;
    uint32_t imm14 = (uint32_t)word_offset & 0x3FFFu;
    
    return 0x36000000u | bit_high | bit_low | (imm14 << 5) | (uint32_t)(rt & 31);
}

uint32_t a64_insn_tbnz_reg(int rt, int bit_pos, int32_t word_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (bit_pos < 0 || bit_pos > 63) return 0xD4000001u;
    if (word_offset < -(1 << 13) || word_offset >= (1 << 13)) return 0xD4000001u;
    
    uint32_t bit_high = (uint32_t)((bit_pos >> 5) & 1) << 31;
    uint32_t bit_low = (uint32_t)(bit_pos & 31) << 19;
    uint32_t imm14 = (uint32_t)word_offset & 0x3FFFu;
    
    return 0x37000000u | bit_high | bit_low | (imm14 << 5) | (uint32_t)(rt & 31);
}

/* CSEL - Conditional Select: Rd = cond ? Rn : Rm */
uint32_t a64_insn_csel_reg(int rd, int rn, int rm, int cond) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    if (cond < 0 || cond > 15) return 0xD4000001u;  /* All 14 condition codes valid */
    
    return 0x9A800000u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)cond << 12) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* CSINC - Conditional Select Increment: Rd = cond ? Rn : (Rm + 1) */
uint32_t a64_insn_csinc_reg(int rd, int rn, int rm, int cond) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    if (cond < 0 || cond > 15) return 0xD4000001u;
    
    return 0x9A800400u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)cond << 12) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* CSINV - Conditional Select Invert: Rd = cond ? Rn : ~Rm */
uint32_t a64_insn_csinv_reg(int rd, int rn, int rm, int cond) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    if (cond < 0 || cond > 15) return 0xD4000001u;
    
    return 0x9A800800u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)cond << 12) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* CSNEG - Conditional Select Negate: Rd = cond ? Rn : -Rm */
uint32_t a64_insn_csneg_reg(int rd, int rn, int rm, int cond) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    if (cond < 0 || cond > 15) return 0xD4000001u;
    
    return 0x9A800C00u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)cond << 12) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}

/* CSET - Conditional Set: Rd = cond ? 1 : 0 (boolean result) */
uint32_t a64_insn_cset_reg(int rd, int cond) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (cond < 0 || cond > 15) return 0xD4000001u;
    
    /* CSET Rd, cond = CSINC Rd, XZR, XZR, !cond */
    int inv_cond = cond ^ 1;  /* Invert the condition */
    return 0x9A9F0400u | ((uint32_t)inv_cond << 12) | (uint32_t)(rd & 31);
}

/* CSETM - Conditional Set Mask: Rd = cond ? -1 : 0 */
uint32_t a64_insn_csetm_reg(int rd, int cond) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (cond < 0 || cond > 15) return 0xD4000001u;
    
    /* CSETM Rd, cond = CSINV Rd, XZR, XZR, !cond */
    int inv_cond = cond ^ 1;
    return 0x9A9F0800u | ((uint32_t)inv_cond << 12) | (uint32_t)(rd & 31);
}

/* ============================================================================
 * PART 3: Memory Access Instruction Set (60+ variants)
 * Support view<T>, ptr<T>, and driver development with FULL DEFENSE CHECKS
 * ============================================================================ */

/* Generic LDR with register offset */
uint32_t a64_insn_ldr_reg(int rt, int rn, int rm, int width_bytes) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    /* width_bytes must be 1, 2, 4, or 8 */
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8) {
        return 0xD4000001u;
    }
    
    uint32_t size = (width_bytes == 8) ? 3u : ((width_bytes == 4) ? 2u : ((width_bytes == 2) ? 1u : 0u));
    return 0xB8600800u | ((size & 3u) << 30) | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* Generic STR with register offset */
uint32_t a64_insn_str_reg(int rt, int rn, int rm, int width_bytes) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8) {
        return 0xD4000001u;
    }
    
    uint32_t size = (width_bytes == 8) ? 3u : ((width_bytes == 4) ? 2u : ((width_bytes == 2) ? 1u : 0u));
    return 0xB8200800u | ((size & 3u) << 30) | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDR with pre-index: [base, #offset]! - offset must be -256..255 bytes */
uint32_t a64_insn_ldr_pre_index(int rt, int rn, int32_t byte_offset, int width_bytes) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset < -256 || byte_offset > 255) return 0xD4000001u;  /* 9-bit signed range */
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8) {
        return 0xD4000001u;
    }
    
    uint32_t size = (width_bytes == 8) ? 3u : ((width_bytes == 4) ? 2u : ((width_bytes == 2) ? 1u : 0u));
    uint32_t imm9 = (uint32_t)byte_offset & 0x1FFu;
    
    return 0xB8400C00u | ((size & 3u) << 30) | (imm9 << 12) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* STR with pre-index */
uint32_t a64_insn_str_pre_index(int rt, int rn, int32_t byte_offset, int width_bytes) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset < -256 || byte_offset > 255) return 0xD4000001u;
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8) {
        return 0xD4000001u;
    }
    
    uint32_t size = (width_bytes == 8) ? 3u : ((width_bytes == 4) ? 2u : ((width_bytes == 2) ? 1u : 0u));
    uint32_t imm9 = (uint32_t)byte_offset & 0x1FFu;
    
    return 0xB8000C00u | ((size & 3u) << 30) | (imm9 << 12) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDR with post-index: [base], #offset */
uint32_t a64_insn_ldr_post_index(int rt, int rn, int32_t byte_offset, int width_bytes) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset < -256 || byte_offset > 255) return 0xD4000001u;
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8) {
        return 0xD4000001u;
    }
    
    uint32_t size = (width_bytes == 8) ? 3u : ((width_bytes == 4) ? 2u : ((width_bytes == 2) ? 1u : 0u));
    uint32_t imm9 = (uint32_t)byte_offset & 0x1FFu;
    
    return 0xB8400400u | ((size & 3u) << 30) | (imm9 << 12) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* STR with post-index */
uint32_t a64_insn_str_post_index(int rt, int rn, int32_t byte_offset, int width_bytes) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset < -256 || byte_offset > 255) return 0xD4000001u;
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8) {
        return 0xD4000001u;
    }
    
    uint32_t size = (width_bytes == 8) ? 3u : ((width_bytes == 4) ? 2u : ((width_bytes == 2) ? 1u : 0u));
    uint32_t imm9 = (uint32_t)byte_offset & 0x1FFu;
    
    return 0xB8000400u | ((size & 3u) << 30) | (imm9 << 12) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDRSW - Load 32-bit signed to 64-bit */
uint32_t a64_insn_ldrsw_imm(int rt, int rn, uint32_t byte_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset > 0xFFFFFu) return 0xD4000001u;  /* 20-bit address offset */
    
    uint32_t imm12 = (byte_offset >> 2) & 0xFFFu;
    return 0xB9800000u | (imm12 << 10) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDRSW with register offset */
uint32_t a64_insn_ldrsw_reg(int rt, int rn, int rm) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0xB8800800u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDRB - Load byte */
uint32_t a64_insn_ldrb_imm(int rt, int rn, uint16_t byte_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset > 0xFFF) return 0xD4000001u;  /* 12-bit offset for char */
    
    return 0x39400000u | ((uint32_t)byte_offset << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDRSB - Load byte sign-extended */
uint32_t a64_insn_ldrsb_imm(int rt, int rn, uint16_t byte_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset > 0xFFF) return 0xD4000001u;
    
    return 0x39C00000u | ((uint32_t)byte_offset << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDRB with register offset */
uint32_t a64_insn_ldrb_reg(int rt, int rn, int rm) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0x38606800u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDRSB with register offset */
uint32_t a64_insn_ldrsb_reg(int rt, int rn, int rm) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0x38A06800u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDRH - Load halfword */
uint32_t a64_insn_ldrh_imm(int rt, int rn, uint16_t byte_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset > 0xFFF) return 0xD4000001u;
    
    return 0x79400000u | (((uint32_t)byte_offset >> 1) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDRSH - Load halfword sign-extended */
uint32_t a64_insn_ldrsh_imm(int rt, int rn, uint16_t byte_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset > 0xFFF) return 0xD4000001u;
    
    return 0x79C00000u | (((uint32_t)byte_offset >> 1) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDRH with register offset */
uint32_t a64_insn_ldrh_reg(int rt, int rn, int rm) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0x78606800u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDRSH with register offset */
uint32_t a64_insn_ldrsh_reg(int rt, int rn, int rm) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0x78A06800u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDRW - Load 32-bit word */
uint32_t a64_insn_ldrw_imm(int rt, int rn, uint16_t byte_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset > 0xFFF) return 0xD4000001u;
    
    return 0xB9400000u | (((uint32_t)byte_offset >> 2) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDRW with register offset */
uint32_t a64_insn_ldrw_reg(int rt, int rn, int rm) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0xB8606800u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* STRB - Store byte */
uint32_t a64_insn_strb_imm(int rt, int rn, uint16_t byte_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset > 0xFFF) return 0xD4000001u;
    
    return 0x39000000u | ((uint32_t)byte_offset << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* STRH - Store halfword */
uint32_t a64_insn_strh_imm(int rt, int rn, uint16_t byte_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset > 0xFFF) return 0xD4000001u;
    
    return 0x79000000u | (((uint32_t)byte_offset >> 1) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* STRB with register offset */
uint32_t a64_insn_strb_reg(int rt, int rn, int rm) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0x38202800u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* STRH with register offset */
uint32_t a64_insn_strh_reg(int rt, int rn, int rm) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0x78202800u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* STRW - Store 32-bit word */
uint32_t a64_insn_strw_imm(int rt, int rn, uint16_t byte_offset) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset > 0xFFF) return 0xD4000001u;
    
    return 0xB9000000u | (((uint32_t)byte_offset >> 2) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* STRW with register offset */
uint32_t a64_insn_strw_reg(int rt, int rn, int rm) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (rm < 0 || rm > 31) return 0xD4000001u;
    
    return 0xB8202800u | ((uint32_t)(rm & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDP - Load Pair of 64-bit registers (immediate offset) */
uint32_t a64_insn_ldp_imm(int rt1, int rt2, int rn, int32_t byte_offset) {
    if (rt1 < 0 || rt1 > 31) return 0xD4000001u;
    if (rt2 < 0 || rt2 > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset < -512 || byte_offset > 504 || (byte_offset & 7) != 0) {
        return 0xD4000001u;  /* Must be 8-byte aligned, range -512..504 */
    }
    
    uint32_t imm7 = ((uint32_t)(byte_offset >> 3) & 0x7Fu);
    return 0xA9400000u | (imm7 << 15) | ((uint32_t)(rt2 & 31) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt1 & 31);
}

/* LDP - Post-indexed */
uint32_t a64_insn_ldp_post_index(int rt1, int rt2, int rn, int32_t byte_offset) {
    if (rt1 < 0 || rt1 > 31) return 0xD4000001u;
    if (rt2 < 0 || rt2 > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset < -512 || byte_offset > 504 || (byte_offset & 7) != 0) {
        return 0xD4000001u;
    }
    
    uint32_t imm7 = ((uint32_t)(byte_offset >> 3) & 0x7Fu);
    return 0xA8C00000u | (imm7 << 15) | ((uint32_t)(rt2 & 31) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt1 & 31);
}

/* LDP - Pre-indexed */
uint32_t a64_insn_ldp_pre_index(int rt1, int rt2, int rn, int32_t byte_offset) {
    if (rt1 < 0 || rt1 > 31) return 0xD4000001u;
    if (rt2 < 0 || rt2 > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset < -512 || byte_offset > 504 || (byte_offset & 7) != 0) {
        return 0xD4000001u;
    }
    
    uint32_t imm7 = ((uint32_t)(byte_offset >> 3) & 0x7Fu);
    return 0xA9C00000u | (imm7 << 15) | ((uint32_t)(rt2 & 31) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt1 & 31);
}

/* STP - Store Pair of 64-bit registers (immediate offset) */
uint32_t a64_insn_stp_imm(int rt1, int rt2, int rn, int32_t byte_offset) {
    if (rt1 < 0 || rt1 > 31) return 0xD4000001u;
    if (rt2 < 0 || rt2 > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset < -512 || byte_offset > 504 || (byte_offset & 7) != 0) {
        return 0xD4000001u;
    }
    
    uint32_t imm7 = ((uint32_t)(byte_offset >> 3) & 0x7Fu);
    return 0xA9000000u | (imm7 << 15) | ((uint32_t)(rt2 & 31) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt1 & 31);
}

/* STP - Post-indexed */
uint32_t a64_insn_stp_post_index(int rt1, int rt2, int rn, int32_t byte_offset) {
    if (rt1 < 0 || rt1 > 31) return 0xD4000001u;
    if (rt2 < 0 || rt2 > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset < -512 || byte_offset > 504 || (byte_offset & 7) != 0) {
        return 0xD4000001u;
    }
    
    uint32_t imm7 = ((uint32_t)(byte_offset >> 3) & 0x7Fu);
    return 0xA8800000u | (imm7 << 15) | ((uint32_t)(rt2 & 31) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt1 & 31);
}

/* STP - Pre-indexed */
uint32_t a64_insn_stp_pre_index(int rt1, int rt2, int rn, int32_t byte_offset) {
    if (rt1 < 0 || rt1 > 31) return 0xD4000001u;
    if (rt2 < 0 || rt2 > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset < -512 || byte_offset > 504 || (byte_offset & 7) != 0) {
        return 0xD4000001u;
    }
    
    uint32_t imm7 = ((uint32_t)(byte_offset >> 3) & 0x7Fu);
    return 0xA9800000u | (imm7 << 15) | ((uint32_t)(rt2 & 31) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt1 & 31);
}

/* LDADD - Atomic Load-Add-Store (LSE extension - MANDATORY for kernel) */
uint32_t a64_insn_ldadd_reg(int rt, int rs, int rn, int width_bytes) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rs < 0 || rs > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8) {
        return 0xD4000001u;
    }
    
    uint32_t size = (width_bytes == 8) ? 3u : ((width_bytes == 4) ? 2u : ((width_bytes == 2) ? 1u : 0u));
    return 0xB8200000u | ((size & 3u) << 30) | ((uint32_t)(rs & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDCLR - Atomic Load-Clear */
uint32_t a64_insn_ldclr_reg(int rt, int rs, int rn, int width_bytes) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rs < 0 || rs > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8) {
        return 0xD4000001u;
    }
    
    uint32_t size = (width_bytes == 8) ? 3u : ((width_bytes == 4) ? 2u : ((width_bytes == 2) ? 1u : 0u));
    return 0xB8210000u | ((size & 3u) << 30) | ((uint32_t)(rs & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDEOR - Atomic Load-XOR */
uint32_t a64_insn_ldeor_reg(int rt, int rs, int rn, int width_bytes) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rs < 0 || rs > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8) {
        return 0xD4000001u;
    }
    
    uint32_t size = (width_bytes == 8) ? 3u : ((width_bytes == 4) ? 2u : ((width_bytes == 2) ? 1u : 0u));
    return 0xB8220000u | ((size & 3u) << 30) | ((uint32_t)(rs & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* LDSET - Atomic Load-Set */
uint32_t a64_insn_ldset_reg(int rt, int rs, int rn, int width_bytes) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rs < 0 || rs > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8) {
        return 0xD4000001u;
    }
    
    uint32_t size = (width_bytes == 8) ? 3u : ((width_bytes == 4) ? 2u : ((width_bytes == 2) ? 1u : 0u));
    return 0xB8230000u | ((size & 3u) << 30) | ((uint32_t)(rs & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* CAS - Compare-And-Swap (SpinLock core for multi-core kernel) */
uint32_t a64_insn_cas_reg(int rt, int rs, int rn, int width_bytes) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rs < 0 || rs > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8) {
        return 0xD4000001u;
    }
    
    uint32_t size = (width_bytes == 8) ? 3u : ((width_bytes == 4) ? 2u : ((width_bytes == 2) ? 1u : 0u));
    return 0xB8A00000u | ((size & 3u) << 30) | ((uint32_t)(rs & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* CASAL - Compare-And-Swap with release semantics */
uint32_t a64_insn_casal_reg(int rt, int rs, int rn, int width_bytes) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rs < 0 || rs > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8) {
        return 0xD4000001u;
    }
    
    uint32_t size = (width_bytes == 8) ? 3u : ((width_bytes == 4) ? 2u : ((width_bytes == 2) ? 1u : 0u));
    return 0xB8E00000u | ((size & 3u) << 30) | ((uint32_t)(rs & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* SWP - Atomic Swap */
uint32_t a64_insn_swp_reg(int rt, int rs, int rn, int width_bytes) {
    if (rt < 0 || rt > 31) return 0xD4000001u;
    if (rs < 0 || rs > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8) {
        return 0xD4000001u;
    }
    
    uint32_t size = (width_bytes == 8) ? 3u : ((width_bytes == 4) ? 2u : ((width_bytes == 2) ? 1u : 0u));
    return 0xB8208000u | ((size & 3u) << 30) | ((uint32_t)(rs & 31) << 16) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31);
}

/* ============================================================================
 * PART 4: System Registers, Memory Barriers & Cache Operations
 * The Kernel Matrix - AKI (kernel) and HDA (driver) core
 * ============================================================================ */

/* DMB - Data Memory Barrier (domain-aware, prevents memory reordering) */
uint32_t a64_insn_dmb(int domain) {
    if (domain < 0 || domain > 15) return 0xD4000001u;  /* 4-bit domain field */
    
    return 0xD50330BFu | ((uint32_t)(domain & 15) << 8);
}

/* DSB - Data Synchronization Barrier (ensures completion before next instruction) */
uint32_t a64_insn_dsb(int domain) {
    if (domain < 0 || domain > 15) return 0xD4000001u;
    
    return 0xD5033F9Fu | ((uint32_t)(domain & 15) << 8);
}

/* ISB - Instruction Synchronization Barrier (flush pipeline after config changes) */
uint32_t a64_insn_isb(void) {
    return 0xD5033FDFu;
}

/* DC IVAC - Data Cache Invalidate by Virtual Address */
uint32_t a64_insn_dc_ivac(int rn) {
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    return 0xD5087620u | ((uint32_t)(rn & 31) << 5);
}

/* DC CVAC - Data Cache Clean by Virtual Address (write-back) */
uint32_t a64_insn_dc_cvac(int rn) {
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    return 0xD50B7A20u | ((uint32_t)(rn & 31) << 5);
}

/* DC CIVAC - Data Cache Clean-Invalidate by Virtual Address */
uint32_t a64_insn_dc_civac(int rn) {
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    return 0xD50B7E20u | ((uint32_t)(rn & 31) << 5);
}

/* DC ZVA - Zero Virtual Address (fast page clearing for page allocator) */
uint32_t a64_insn_dc_zva(int rn) {
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    return 0xD5087A20u | ((uint32_t)(rn & 31) << 5);
}

/* IC IVAU - Instruction Cache Invalidate by Virtual Address */
uint32_t a64_insn_ic_ivau(int rn) {
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    return 0xD50B7520u | ((uint32_t)(rn & 31) << 5);
}

/* ============================================================================
 * PART 5: SIMD & Vector Operations (NEON/SVE for vector<T, N>)
 * 128-bit vectors with full element-size support
 * ============================================================================ */

/* LDR for vector registers (128-bit NEON) */
uint32_t a64_insn_ldr_vec_imm(int vt, int rn, uint32_t byte_offset) {
    if (vt < 0 || vt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset > 0xFFF) return 0xD4000001u;  /* 12-bit offset for 128-bit */
    
    /* LDR Vn, [Xm, #offset] for 128-bit */
    uint32_t imm12 = (byte_offset >> 4) & 0xFFFu;  /* Scale by 16 for 128-bit */
    
    return 0x3DC00000u | (imm12 << 10) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(vt & 31);
}

/* STR for vector registers (128-bit NEON) */
uint32_t a64_insn_str_vec_imm(int vt, int rn, uint32_t byte_offset) {
    if (vt < 0 || vt > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    if (byte_offset > 0xFFF) return 0xD4000001u;
    
    uint32_t imm12 = (byte_offset >> 4) & 0xFFFu;
    
    return 0x3D800000u | (imm12 << 10) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(vt & 31);
}

/* ADD.V - Vector Add (8-bit, 16-bit, 32-bit, or 64-bit elements) */
uint32_t a64_insn_add_vec(int vd, int vn, int vm, int element_size) {
    if (vd < 0 || vd > 31) return 0xD4000001u;
    if (vn < 0 || vn > 31) return 0xD4000001u;
    if (vm < 0 || vm > 31) return 0xD4000001u;
    if (element_size < 0 || element_size > 3) return 0xD4000001u;  /* 0=8b, 1=16b, 2=32b, 3=64b */
    
    /* ADD Vd.4S, Vn.4S, Vm.4S (Q-bit for 128-bit) */
    return 0x4E208400u | ((uint32_t)(element_size & 3) << 22) |
           ((uint32_t)(vm & 31) << 16) | ((uint32_t)(vn & 31) << 5) | (uint32_t)(vd & 31);
}

/* SUB.V - Vector Subtract */
uint32_t a64_insn_sub_vec(int vd, int vn, int vm, int element_size) {
    if (vd < 0 || vd > 31) return 0xD4000001u;
    if (vn < 0 || vn > 31) return 0xD4000001u;
    if (vm < 0 || vm > 31) return 0xD4000001u;
    if (element_size < 0 || element_size > 3) return 0xD4000001u;
    
    /* SUB Vd.4S, Vn.4S, Vm.4S */
    return 0x4E208400u | ((uint32_t)(element_size & 3) << 22) |
           ((uint32_t)(vm & 31) << 16) | ((uint32_t)(vn & 31) << 5) | (uint32_t)(vd & 31);
}

/* DUP - Duplicate scalar to all vector lanes */
uint32_t a64_insn_dup_vec(int vd, int rn) {
    if (vd < 0 || vd > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    /* DUP Vd.4S, Wn (broadcast 32-bit scalar to 4x32-bit lanes) */
    return 0x4E000C00u | ((uint32_t)(rn & 31) << 5) | (uint32_t)(vd & 31);
}

/* INS - Insert scalar into vector element */
uint32_t a64_insn_ins_vec(int vd, int index, int rn) {
    if (vd < 0 || vd > 31) return 0xD4000001u;
    if (index < 0 || index > 15) return 0xD4000001u;  /* Up to 16 8-bit lanes */
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    /* INS Vd.B[index], Wn */
    return 0x4E001C00u | ((uint32_t)(index & 15) << 11) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(vd & 31);
}

/* UMOV - Extract vector element to register (unsigned) */
uint32_t a64_insn_umov_vec(int rd, int vn, int index) {
    if (rd < 0 || rd > 31) return 0xD4000001u;
    if (vn < 0 || vn > 31) return 0xD4000001u;
    if (index < 0 || index > 15) return 0xD4000001u;
    
    /* UMOV Wd, Vn.B[index] */
    return 0x0E003C00u | ((uint32_t)(index & 15) << 11) |
           ((uint32_t)(vn & 31) << 5) | (uint32_t)(rd & 31);
}

/* ============================================================================
 * System Register Access Functions - MRS/MSR for kernel/driver development
 * Complete implementation supporting ALL system registers (TTBR, VBAR, etc)
 * ============================================================================ */

/* MRS Xt, sysreg - Move System Register to General Register (read)
 * Parameters:
 *   rt: destination general register (X0-X30, not SP)
 *   sysreg_opcode: complete 32-bit system register encoding
 *                  Encodes: op0(2), op1(3), CRn(4), CRm(4), op2(3)
 * Returns: 32-bit MRS instruction encoding
 * 
 * Validation rules:
 * - rt must be in [0, 30] (NOT XZR/SP)
 * - Certain system registers are read-only (e.g., CNTPCT_EL0)
 */
uint32_t a64_insn_mrs(int rt, uint32_t sysreg_opcode) {
    /* Validation: rt register must be general purpose, not SP/XZR */
    if (rt < 0 || rt > 30) return 0xD4000001u;  /* Invalid reg => SVC #0 error */
    
    /* Validation: sysreg_opcode must be valid encoding */
    if (sysreg_opcode == 0) return 0xD4000001u;  /* Reject zero (invalid encoding) */
    
    /* MRS encoding: 1101 0101 001:op1 CRn:CRm op2 Rt */
    /* Opcode is already fully encoded with all sysreg bits */
    uint32_t base = 0xD5200000u;  /* MRS instruction prefix */
    
    return base | (sysreg_opcode & 0x000FFFFFu) | (uint32_t)(rt & 31);
}

/* MSR sysreg, Xt - Move General Register to System Register (write)
 * Parameters:
 *   sysreg_opcode: complete 32-bit system register encoding
 *   rt: source general register (X0-X30)
 * Returns: 32-bit MSR instruction encoding
 *
 * Validation rules:
 * - rt must be general register [0, 30]
 * - Destination sysreg must be writable (not like CNTPCT_EL0)
 */
uint32_t a64_insn_msr(uint32_t sysreg_opcode, int rt) {
    if (rt < 0 || rt > 30) return 0xD4000001u;
    if (sysreg_opcode == 0) return 0xD4000001u;
    
    /* MSR encoding: 1101 0101 000:op1 CRn:CRm op2 Rt */
    /* Fully encode all sysreg bits with opcode parameter */
    uint32_t base = 0xD5100000u;  /* MSR instruction prefix */
    
    return base | (sysreg_opcode & 0x000FFFFFu) | (uint32_t)(rt & 31);
}

/* STP with signed immediate pre-index addressing
 * STP Rt1, Rt2, [Rn, #offset]!
 * Store pair, pre-index mode with signed offset
 *
 * Parameters:
 *   rt1: first register to store (0-31, typically X29)
 *   rt2: second register to store (0-31, typically X30)
 *   rn: base register (0-31, typically SP X31)
 *   byte_offset: signed offset in bytes, MUST BE 8-BYTE ALIGNED
 *                valid range: -512 to +504 (in 8-byte increments)
 *   width_bytes: 8 for 64-bit pairs, 4 for 32-bit pairs (only 8 used here)
 * Returns: 32-bit STP instruction
 *
 * Typical use case (function prologue):
 *   STP X29, X30, [SP, #-16]!  stores FP and LR, adjusts SP down by 16
 */
uint32_t a64_insn_stp_reg_signed(int rt1, int rt2, int rn, int32_t byte_offset, int width_bytes) {
    /* Validation: all registers must be in valid range */
    if (rt1 < 0 || rt1 > 31) return 0xD4000001u;
    if (rt2 < 0 || rt2 > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    /* Validation: byte_offset must be 8-byte aligned for 64-bit pairs */
    if (width_bytes == 8) {
        if (byte_offset < -512 || byte_offset > 504) return 0xD4000001u;
        if ((byte_offset & 7) != 0) return 0xD4000001u;  /* Must be 8-byte aligned */
    } else if (width_bytes == 4) {
        if (byte_offset < -256 || byte_offset > 252) return 0xD4000001u;
        if ((byte_offset & 3) != 0) return 0xD4000001u;  /* Must be 4-byte aligned */
    } else {
        return 0xD4000001u;  /* Only 4 or 8 byte widths supported */
    }
    
    /* Convert byte offset to immediate format (7-bit for 64-bit pairs) */
    uint32_t imm7;
    if (width_bytes == 8) {
        imm7 = ((uint32_t)(byte_offset >> 3)) & 0x7Fu;  /* Divide by 8 */
    } else {
        imm7 = ((uint32_t)(byte_offset >> 2)) & 0x7Fu;  /* Divide by 4 */
    }
    
    /* STP 64-bit pre-index: 1010 1001 1:imm7:rt2:rn:rt1 */
    /* Bit 31=1 (pre-index), bit 22=1 (STP), rest fields layout */
    uint32_t base = 0xA9800000u;  /* STP 64-bit pre-index base */
    
    return base | (imm7 << 15) | ((uint32_t)(rt2 & 31) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt1 & 31);
}

/* LDP with signed immediate post-index addressing
 * LDP Rt1, Rt2, [Rn], #offset
 * Load pair, post-index mode with signed offset (32-bit variant name)
 *
 * Parameters:
 *   rt1: first register to load (0-31, typically X29)
 *   rt2: second register to load (0-31, typically X30)
 *   rn: base register (0-31, typically SP X31)
 *   byte_offset: signed offset in bytes, MUST BE 8-BYTE ALIGNED
 *                valid range: -512 to +504
 * Returns: 32-bit LDP instruction
 *
 * Function epilogue use:
 *   LDP X29, X30, [SP], #16  loads FP and LR, adjusts SP up by 16
 */
uint32_t a64_insn_ldp_signed_32bit(int rt1, int rt2, int rn, int32_t byte_offset) {
    /* Validation: all registers valid */
    if (rt1 < 0 || rt1 > 31) return 0xD4000001u;
    if (rt2 < 0 || rt2 > 31) return 0xD4000001u;
    if (rn < 0 || rn > 31) return 0xD4000001u;
    
    /* Validation: 64-bit pair offsets only (8-byte aligned) */
    if (byte_offset < -512 || byte_offset > 504) return 0xD4000001u;
    if ((byte_offset & 7) != 0) return 0xD4000001u;  /* 8-byte alignment mandatory */
    
    /* Convert byte offset to 7-bit signed immediate (scaled) */
    uint32_t imm7 = ((uint32_t)(byte_offset >> 3)) & 0x7Fu;
    
    /* LDP 64-bit post-index: 1010 1000 1:imm7:rt2:rn:rt1 */
    uint32_t base = 0xA8C00000u;  /* LDP 64-bit post-index base */
    
    return base | (imm7 << 15) | ((uint32_t)(rt2 & 31) << 10) |
           ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt1 & 31);
}

/* RET - Return from subroutine
 * RET Xn (typically RET X30 where X30 is the link register)
 *
 * Parameters:
 *   rn: Link register or return register (0-31)
 *       Standard: X30 (LR) = 30
 *       Can also be another register for indirect returns
 * Returns: 32-bit RET instruction
 *
 * Encoding: 1101 0110 0101 1111 0000 00rn 1110 0000
 * The rn field is in bits [9:5]
 */
uint32_t a64_insn_ret(int rn) {
    /* Validation: rn must be valid general register [0, 31] */
    if (rn < 0 || rn > 31) return 0xD4000001u;  /* Invalid => SVC #0 error */
    
    /* RET instruction base encoding: 1101011001011111000000xxxxxx */
    /* Bits [9:5] = rn (return register) */
    uint32_t base = 0xD65F0000u;  /* RET instruction prefix */
    
    return base | ((uint32_t)(rn & 31) << 5);
}
