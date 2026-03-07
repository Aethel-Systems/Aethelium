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

#ifndef AETHEL_LET_FORMAT_H
#define AETHEL_LET_FORMAT_H

#include <stdint.h>

#define LET_MAGIC 0x2154454Cu /* "LET!" */
#define LET_VERSION 1u
#define LET_HEADER_SIZE 256u

#define LET_RELOC_CALL_REL32 1u
#define LET_RELOC_JMP_REL32  2u
#define LET_RELOC_JCC_REL32  3u
#define LET_RELOC_JMP_REL8   4u
#define LET_RELOC_JCC_REL8   5u

enum {
    LET_ISA_UNKNOWN = 0u,
    LET_ISA_X86 = 1u,
    LET_ISA_X86_64 = 2u,
    LET_ISA_AARCH64 = 3u
};

enum {
    LET_ENDIAN_UNKNOWN = 0u,
    LET_ENDIAN_LITTLE = 1u,
    LET_ENDIAN_BIG = 2u
};

enum {
    LET_ABI_UNKNOWN = 0u,
    LET_ABI_SYSV = 1u,
    LET_ABI_MS64 = 2u,
    LET_ABI_FREESTANDING = 3u
};

enum {
    LET_CODE_MODEL_FLAT = 1u,
    LET_CODE_MODEL_SEGMENTED = 2u,
    LET_CODE_MODEL_PIC = 3u,
    LET_CODE_MODEL_STATIC = 4u
};

enum {
    LET_ENTRY_ENCODING_OFFSET = 1u,
    LET_ENTRY_ENCODING_ABSOLUTE = 2u,
    LET_ENTRY_ENCODING_SYMBOL = 3u
};

enum {
    LET_BIN_FLAG_EXPORTABLE = 1u << 0,
    LET_BIN_FLAG_RELOC_COMPLETE_REQUIRED = 1u << 1,
    LET_BIN_FLAG_FLAT_DEFAULT = 1u << 2
};

typedef struct __attribute__((packed)) {
    uint16_t target_isa;
    uint16_t machine_bits;
    uint8_t endianness;
    uint8_t abi_kind;
    uint8_t code_model;
    uint8_t reloc_width;
    uint8_t entry_encoding;
    uint8_t bin_flags;
    uint16_t reloc_encoding_version;
    uint16_t instruction_profile;
    uint16_t syscall_profile;
    uint8_t sandbox_patch_required;
    uint8_t reserved[63];
} LetMachineContract;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t reserved0;
    uint8_t logic_id[32];
    uint64_t genesis_point;
    uint64_t reserved1;
    uint64_t logic_stream_offset;
    uint64_t logic_stream_size;
    uint64_t state_snapshot_offset;
    uint64_t state_snapshot_size;
    uint64_t gene_table_offset;
    uint64_t gene_table_size;
    uint64_t fingerprint_offset;
    uint64_t fingerprint_size;
    uint8_t extended[80];
    uint32_t header_crc;
    uint32_t build_timestamp;
    uint32_t build_version;
    uint32_t compiler_version;
    uint8_t reserved_tail[32];
} LetHeader;

typedef struct {
    uint32_t version;
    uint32_t reloc_count;
    uint32_t identity_count;
    uint32_t trap_hint_count;
    uint32_t ipc_count;
    uint32_t import_count;
    uint32_t min_sip;
    uint32_t flags;
    uint32_t reloc_encoding_version;
    uint32_t instruction_profile;
    uint32_t syscall_profile;
    uint32_t sandbox_patch_required;
} LetGeneTableHeader;

typedef struct {
    uint64_t code_offset;
    uint32_t reloc_type;
    uint32_t reserved;
} LetRelocDNAEntry;

typedef struct {
    uint64_t name_hash;
    uint32_t import_kind;
    uint32_t name_len;
    char module[64];
} LetIdentityNexusEntry;

_Static_assert(sizeof(LetMachineContract) == 80u, "LET machine contract must stay 80 bytes");

#endif
