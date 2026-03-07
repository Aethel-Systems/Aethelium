/*
 * Assembly Orchestration Layer - C Interface Header
 * toolsASM/include/asm_interface.h
 *
 * Declares assembly functions that the C compiler will call
 * to emit binary formats (AKI, HDA, SRV, AETB)
 *
 * These functions form the contract between C logic layer and assembly formatting layer
 * Industrial-grade implementation - no simplified code
 */

#ifndef _ASM_INTERFACE_H_
#define _ASM_INTERFACE_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   CRC32 Integrity Verification
   ============================================================================ */

/**
 * Calculate CRC32 checksum for binary data
 *
 * Uses Ethernet polynomial (0xEDB88320) for integrity verification.
 * This is standard across AKI, HDA, SRV, AETB formats.
 *
 * Parameters:
 *   data      : pointer to data buffer
 *   size      : number of bytes to checksum
 *   initial   : initial CRC value (typically 0xFFFFFFFF)
 *
 * Returns:
 *   uint32_t  : calculated CRC32 (final value XORed with 0xFFFFFFFF)
 *
 * Calling convention: System V AMD64 ABI (macOS)
 * - rdi = data pointer
 * - rsi = size
 * - rdx = initial value
 * - rax = result
 */
extern uint32_t _calculate_crc32(
    const uint8_t *data,
    uint64_t size,
    uint32_t initial
);

/* ============================================================================
   Structure Definitions for Assembly Layer
   ============================================================================ */

/**
 * Input structure for AKI structure weaving from raw buffers
 *
 * The C layer extracts three independent buffers from the compiled code:
 * - ActFlow: Pure x86-64 machine code (microkernel bytecode)
 * - MirrorState: Runtime state data (static initialization data)
 * - ConstantTruth: Read-only configuration and metadata
 *
 * The assembly weaver assembles these into a complete binary per
 * [Full Structure] specification from "AethelOS 二进制及目录结构.txt":
 * - Calculate proper alignments (16-byte ActFlow, 4KB ConstantTruth)
 * - Build complete 256-byte header with zone offsets
 * - Construct IdentityNexus (symbol/AethelID mapping table)
 * - Calculate CRC32 over entire structure
 * - Write to file with proper file permissions
 */
typedef struct {
    const char *output_filename;
    const uint8_t *act_flow_buffer;
    uint64_t act_flow_size;
    const uint8_t *mirror_state_buffer;
    uint64_t mirror_state_size;
    const uint8_t *constant_truth_buffer;
    uint64_t constant_truth_size;
    uint64_t genesis_point;
    const uint8_t *aethel_id;           /* 32 bytes */
} AKI_Weave_Input;

typedef struct {
    const char *output_filename;
    const uint8_t *act_flow_buffer;
    uint64_t act_flow_size;
    const uint8_t *mirror_state_buffer;
    uint64_t mirror_state_size;
    const uint8_t *constant_truth_buffer;
    uint64_t constant_truth_size;
    const uint8_t *aethel_id;           /* 32 bytes */
} SRV_Weave_Input;

typedef struct {
    const char *output_filename;
    const uint8_t *act_flow_buffer;
    uint64_t act_flow_size;
    const uint8_t *mirror_state_buffer;
    uint64_t mirror_state_size;
    const uint8_t *constant_truth_buffer;
    uint64_t constant_truth_size;
    const uint8_t *aethel_id;           /* 32 bytes */
} HDA_Weave_Input;

typedef struct {
    const char *output_filename;
    const uint8_t *act_flow_buffer;
    uint64_t act_flow_size;
    const uint8_t *mirror_state_buffer;
    uint64_t mirror_state_size;
    const uint8_t *constant_truth_buffer;
    uint64_t constant_truth_size;
    const uint8_t *aethel_id;           /* 32 bytes */
} AETB_Weave_Input;

/* ============================================================================
   Structure Weaving Functions (Assembly Layer)
   
   These functions receive raw buffers and construct complete binary
   structure per [Full Structure] specification. They are responsible for:
   - Alignment management
   - Zone offset calculation
   - IdentityNexus building
   - CRC32 calculation
   - File I/O and format compliance
   ============================================================================ */

/**
 * Weave AKI [Full Structure] from raw component buffers
 *
 * Receives three independent buffers (ActFlow, MirrorState, ConstantTruth)
 * and constructs a complete AKI format binary following specification:
 * [Header:256B] [ActFlow:16-aligned] [MirrorState:16-aligned] [ConstantTruth:4KB-aligned]
 *
 * Parameters:
 *   input  : pointer to AKI_Weave_Input structure
 *
 * Returns:
 *   0 on success, -1 on error
 */
extern int weave_aki_structure(const AKI_Weave_Input *input);

/**
 * Weave SRV [Full Structure] from raw component buffers
 *
 * Parameters:
 *   input  : pointer to SRV_Weave_Input structure
 *
 * Returns:
 *   0 on success, -1 on error
 */
extern int weave_srv_structure(const SRV_Weave_Input *input);

/**
 * Weave HDA [Full Structure] from raw component buffers
 *
 * Parameters:
 *   input  : pointer to HDA_Weave_Input structure
 *
 * Returns:
 *   0 on success, -1 on error
 */
extern int weave_hda_structure(const HDA_Weave_Input *input);

/**
 * Weave AETB [Full Structure] from raw component buffers
 *
 * Parameters:
 *   input  : pointer to AETB_Weave_Input structure
 *
 * Returns:
 *   0 on success, -1 on error
 */
extern int weave_aetb_structure(const AETB_Weave_Input *input);

/* ============================================================================
   Binary Format Emitters - File Output Functions (Legacy for compatibility)
   ============================================================================ */

/**
 * Write AKI (Aethel Kernel Image) binary to file
 *
 * AKI is the system's kernel image format. This function:
 * 1. Opens file for exclusive writing (truncates if exists)
 * 2. Writes the complete AKI image with all sections
 * 3. Closes file descriptor
 *
 * The image buffer should be pre-formatted by C layer with:
 * - 256-byte header (magic, AethelID, section offsets, CRC)
 * - ActFlow zone (microkernel bytecode)
 * - MirrorState zone (kernel metadata)
 * - ConstantTruth zone (read-only configuration)
 *
 * Parameters:
 *   filename  : output file path (null-terminated C string)
 *   image     : pointer to complete AKI image buffer
 *   size      : total image size in bytes
 *   genesis_pt: physical load address (typically 0x100000)
 *
 * Returns:
 *   0 on success, -1 on error
 *
 * Note: File is created with permissions 0644 (rw-r--r--)
 */
extern int _write_aki_binary(
    const char *filename,
    const uint8_t *image,
    uint64_t size,
    uint64_t genesis_pt
);

/**
 * Write SRV (System Service Archive) binary to file
 *
 * SRV is the service container format for system services.
 * Services are IPC-callable logical entities with defined schemas.
 *
 * Parameters:
 *   filename  : output file path
 *   image     : pointer to complete SRV image
 *   size      : total image size in bytes
 *
 * Returns:
 *   0 on success, -1 on error
 */
extern int _write_srv_binary(
    const char *filename,
    const uint8_t *image,
    uint64_t size
);

/**
 * Write HDA (Hardware Driver Archive) binary to file
 *
 * HDA is the hardware contract specification format.
 * Unlike traditional drivers, HDA describes hardware behavior
 * and is interpreted by the AethelA compiler engine.
 *
 * Parameters:
 *   filename  : output file path
 *   image     : pointer to complete HDA image
 *   size      : total image size in bytes
 *
 * Returns:
 *   0 on success, -1 on error
 */
extern int _write_hda_binary(
    const char *filename,
    const uint8_t *image,
    uint64_t size
);

/**
 * Write AETB (Aethelium Binary) to file
 *
 * AETB is the application logic container. It is always embedded
 * within .iya packages and never exists as a standalone file.
 *
 * Parameters:
 *   filename  : output file path
 *   image     : pointer to complete AETB image
 *   size      : total image size in bytes
 *
 * Returns:
 *   0 on success, -1 on error
 */
extern int _write_aetb_binary(
    const char *filename,
    const uint8_t *image,
    uint64_t size
);

/* ============================================================================
   Integration Notes
   ============================================================================

   The C compiler (aethelc) follows this workflow:

   1. LEXING & PARSING: Read .ae source, build AST
   2. SEMANTIC ANALYSIS: Type checking, symbol table building
   3. AST → IR: Convert to intermediate representation (raw opcodes)
   4. CODE GENERATION:
      - Generate ActFlow bytecode (machine instructions)
      - Generate MirrorState data (static initialization data)
      - Generate ConstantTruth/relocations
   5. HEADER CONSTRUCTION (C layer):
      - Create 256-byte header with magic, AethelID, section offsets
      - Calculate header CRC32
   6. ASSEMBLY FORMATTING (via these functions):
      - Package complete binary image
      - Write to file via system calls
      - Assembly layer ensures clean separation from C logic

   This architecture achieves:
   - Logical purity: C layer has no knowledge of binary format details
   - Format correctness: Assembly layer guarantees spec compliance
   - Easy migration: When self-hosting in AethelOS, only syscall layer changes
   - Performance: Direct syscalls, no inefficient marshaling

 ============================================================================ */

#ifdef __cplusplus
}
#endif

#endif /* _ASM_INTERFACE_H_ */

