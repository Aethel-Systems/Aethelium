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

#include "let_gen.h"
#include "let_format.h"

#include "aefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>

#define LET_MAX_IMPORTS 128u

_Static_assert(sizeof(LetHeader) == LET_HEADER_SIZE, "LetHeader must be 256 bytes");

static uint32_t let_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0;
    size_t i;
    int bit;

    for (i = 0; i < size; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return crc;
}

static uint64_t fnv1a64(const char *text) {
    uint64_t hash = 1469598103934665603ull;
    size_t i;
    if (!text) return hash;
    for (i = 0; text[i] != '\0'; i++) {
        hash ^= (uint8_t)text[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static size_t collect_reloc_dna(const uint8_t *code, size_t code_size, LetRelocDNAEntry **out_entries) {
    size_t i;
    size_t count = 0;
    size_t capacity = 64;
    LetRelocDNAEntry *entries;

    if (!out_entries) return 0;
    *out_entries = NULL;

    if (!code || code_size < 5) {
        return 0;
    }

    entries = (LetRelocDNAEntry *)malloc(capacity * sizeof(LetRelocDNAEntry));
    if (!entries) {
        return 0;
    }

    for (i = 0; i < code_size; ) {
        uint8_t op = code[i];
        uint8_t op2 = (i + 1 < code_size) ? code[i + 1] : 0;
        uint32_t reloc_type = 0u;
        size_t inst_len = 1u;

        if (op == 0xE8 && i + 4 < code_size) {
            reloc_type = LET_RELOC_CALL_REL32;
            inst_len = 5u;
        } else if (op == 0xE9 && i + 4 < code_size) {
            reloc_type = LET_RELOC_JMP_REL32;
            inst_len = 5u;
        } else if (op == 0xEB && i + 1 < code_size) {
            reloc_type = LET_RELOC_JMP_REL8;
            inst_len = 2u;
        } else if (op == 0x0F && i + 5 < code_size && (op2 >= 0x80 && op2 <= 0x8F)) {
            reloc_type = LET_RELOC_JCC_REL32;
            inst_len = 6u;
        } else if (op >= 0x70 && op <= 0x7F && i + 1 < code_size) {
            reloc_type = LET_RELOC_JCC_REL8;
            inst_len = 2u;
        } else {
            /* 最小解码器：覆盖当前 codegen 实际发射指令长度，避免误把立即数中的 E8/E9 当作指令 */
            if (op == 0x55 || op == 0x50 || op == 0x59 || op == 0x5A || op == 0x5E || op == 0x5F ||
                op == 0x58 || op == 0xC3 || op == 0xC9 || op == 0x90 || op == 0x41) {
                inst_len = 1u;
            } else if (op == 0x48 || op == 0x49 || op == 0x4C || op == 0x0F) {
                /* 变长指令前缀，保守推进，防止死循环 */
                inst_len = 1u;
            } else {
                inst_len = 1u;
            }
        }

        if (reloc_type != 0u) {
            if (count >= capacity) {
                LetRelocDNAEntry *grown;
                capacity *= 2;
                grown = (LetRelocDNAEntry *)realloc(entries, capacity * sizeof(LetRelocDNAEntry));
                if (!grown) {
                    free(entries);
                    return 0;
                }
                entries = grown;
            }
            entries[count].code_offset = i;
            entries[count].reloc_type = reloc_type;
            entries[count].reserved = 0u;
            count++;
        }

        i += inst_len;
    }

    if (count == 0) {
        free(entries);
        return 0;
    }

    *out_entries = entries;
    return count;
}

static size_t collect_identity_nexus(const SemanticResult *semantic, LetIdentityNexusEntry *entries, size_t capacity) {
    size_t i;
    size_t count = 0;

    if (!semantic || !entries || capacity == 0) {
        return 0;
    }

    for (i = 0; i < semantic->import_count && count < capacity; i++) {
        const SemanticImportRecord *rec = &semantic->imports[i];
        LetIdentityNexusEntry *dst = &entries[count];
        memset(dst, 0, sizeof(*dst));
        dst->name_hash = fnv1a64(rec->module);
        dst->import_kind = (uint32_t)rec->kind;
        dst->name_len = (uint32_t)strlen(rec->module);
        snprintf(dst->module, sizeof(dst->module), "%s", rec->module);
        count++;
    }

    return count;
}

int let_generate_image(const char *output_file,
                       const uint8_t *logic_stream,
                       size_t logic_size,
                       const uint8_t *state_snapshot,
                       size_t state_size,
                       const uint8_t *constant_truth,
                       size_t constant_size,
                       const SemanticResult *semantic,
                       const LetEmitOptions *options) {
    FILE *out;
    LetHeader header;
    LetGeneTableHeader gene_header;
    LetRelocDNAEntry *relocs = NULL;
    LetIdentityNexusEntry identities[LET_MAX_IMPORTS];
    size_t reloc_count = 0;
    size_t identity_count = 0;
    size_t gene_size;
    uint32_t fingerprint[2] = {0, 0};
    uint64_t cursor;
    LetMachineContract contract;

    if (!output_file || !semantic) {
        return -1;
    }
    if (logic_size > 0 && !logic_stream) {
        return -1;
    }

    out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "Error: cannot create LET output '%s'\n", output_file);
        return -1;
    }

    memset(&header, 0, sizeof(header));
    memset(&gene_header, 0, sizeof(gene_header));
    memset(identities, 0, sizeof(identities));
    memset(&contract, 0, sizeof(contract));

    reloc_count = collect_reloc_dna(logic_stream, logic_size, &relocs);
    identity_count = collect_identity_nexus(semantic, identities, LET_MAX_IMPORTS);

    gene_header.version = 1u;
    gene_header.reloc_count = (uint32_t)reloc_count;
    gene_header.identity_count = (uint32_t)identity_count;
    gene_header.trap_hint_count = (uint32_t)semantic->trap_hint_count;
    gene_header.ipc_count = (uint32_t)semantic->ipc_contract_count;
    gene_header.import_count = (uint32_t)semantic->import_count;
    gene_header.min_sip = (uint32_t)semantic->identity_contract_min_sip;
    gene_header.flags = (uint32_t)((semantic->requires_architect_mode ? 0x1u : 0u) |
                                   (semantic->has_metal_block ? 0x2u : 0u) |
                                   (semantic->has_rimport ? 0x4u : 0u));
    gene_header.reloc_encoding_version = options ? options->reloc_encoding_version : 1u;
    gene_header.instruction_profile = options ? options->instruction_profile : 1u;
    gene_header.syscall_profile = options ? options->syscall_profile : 1u;
    gene_header.sandbox_patch_required = options ? options->sandbox_patch_required : 0u;

    contract.target_isa = options ? options->target_isa : LET_ISA_X86_64;
    contract.machine_bits = options ? options->machine_bits : 64u;
    contract.endianness = options ? options->endianness : LET_ENDIAN_LITTLE;
    contract.abi_kind = options ? options->abi_kind : LET_ABI_SYSV;
    contract.code_model = options ? options->code_model : LET_CODE_MODEL_FLAT;
    contract.reloc_width = options ? options->reloc_width : 32u;
    contract.entry_encoding = options ? options->entry_encoding : LET_ENTRY_ENCODING_OFFSET;
    contract.bin_flags = options ? options->bin_flags : LET_BIN_FLAG_EXPORTABLE;
    contract.reloc_encoding_version = options ? options->reloc_encoding_version : 1u;
    contract.instruction_profile = options ? options->instruction_profile : 1u;
    contract.syscall_profile = options ? options->syscall_profile : 1u;
    contract.sandbox_patch_required = options ? options->sandbox_patch_required : 0u;

    gene_size = sizeof(gene_header) +
                reloc_count * sizeof(LetRelocDNAEntry) +
                identity_count * sizeof(LetIdentityNexusEntry);

    header.magic = LET_MAGIC;
    header.version = LET_VERSION;
    header.flags = gene_header.flags;
    header.genesis_point = 0x100000ull;

    cursor = LET_HEADER_SIZE;
    header.logic_stream_offset = cursor;
    header.logic_stream_size = logic_size;
    cursor += logic_size;

    header.state_snapshot_offset = cursor;
    header.state_snapshot_size = state_size;
    cursor += state_size;

    if (constant_truth && constant_size > 0) {
        cursor += constant_size;
    }

    header.gene_table_offset = cursor;
    header.gene_table_size = gene_size;
    cursor += gene_size;

    header.fingerprint_offset = cursor;
    header.fingerprint_size = sizeof(fingerprint);
    memcpy(header.extended, &contract, sizeof(contract));

    if (logic_size > 0) {
        fingerprint[0] = aefs_checksum_xxh3(logic_stream, logic_size);
        fingerprint[1] = let_crc32(logic_stream, logic_size);
    } else {
        fingerprint[0] = 0u;
        fingerprint[1] = 0u;
    }

    header.build_timestamp = (uint32_t)time(NULL);
    header.build_version = 1u;
    header.compiler_version = 3u;
    header.header_crc = let_crc32((const uint8_t *)&header, offsetof(LetHeader, header_crc));

    if (fwrite(&header, 1, sizeof(header), out) != sizeof(header)) {
        fclose(out);
        free(relocs);
        return -1;
    }

    if (logic_size > 0) {
        if (fwrite(logic_stream, 1, logic_size, out) != logic_size) {
            fclose(out);
            free(relocs);
            return -1;
        }
    }

    if (state_snapshot && state_size > 0) {
        if (fwrite(state_snapshot, 1, state_size, out) != state_size) {
            fclose(out);
            free(relocs);
            return -1;
        }
    }

    if (constant_truth && constant_size > 0) {
        if (fwrite(constant_truth, 1, constant_size, out) != constant_size) {
            fclose(out);
            free(relocs);
            return -1;
        }
    }

    if (fwrite(&gene_header, 1, sizeof(gene_header), out) != sizeof(gene_header)) {
        fclose(out);
        free(relocs);
        return -1;
    }

    if (reloc_count > 0) {
        if (fwrite(relocs, sizeof(LetRelocDNAEntry), reloc_count, out) != reloc_count) {
            fclose(out);
            free(relocs);
            return -1;
        }
    }

    if (identity_count > 0) {
        if (fwrite(identities, sizeof(LetIdentityNexusEntry), identity_count, out) != identity_count) {
            fclose(out);
            free(relocs);
            return -1;
        }
    }

    if (fwrite(fingerprint, 1, sizeof(fingerprint), out) != sizeof(fingerprint)) {
        fclose(out);
        free(relocs);
        return -1;
    }

    fclose(out);
    free(relocs);
    return 0;
}
