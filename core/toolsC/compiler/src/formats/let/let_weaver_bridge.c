#include "let_weaver_bridge.h"

#include "let_format.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/stat.h>

typedef struct {
    const char *output_filename;
    const uint8_t *act_flow_buffer;
    uint64_t act_flow_size;
    const uint8_t *mirror_state_buffer;
    uint64_t mirror_state_size;
    const uint8_t *constant_truth_buffer;
    uint64_t constant_truth_size;
    uint64_t genesis_point;
    const uint8_t *aethel_id;
    const LetRelocDNAEntry *reloc_table_ptr;
    uint64_t reloc_count;
    const LetIdentityNexusEntry *identity_table_ptr;
    uint64_t identity_count;
    uint64_t trap_hint_count;
    uint64_t ipc_count;
    uint64_t min_sip;
    uint64_t mode_affinity;
    uint64_t sip_vector;
    uint64_t target_isa;
    uint64_t machine_bits;
    uint64_t endianness;
    uint64_t abi_kind;
    uint64_t code_model;
    uint64_t reloc_width;
    uint64_t entry_encoding;
    uint64_t bin_flags;
    uint64_t bin_entry_offset;
} WeaverInput;

_Static_assert(offsetof(WeaverInput, output_filename) == 0, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, act_flow_buffer) == 8, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, act_flow_size) == 16, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, mirror_state_buffer) == 24, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, mirror_state_size) == 32, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, constant_truth_buffer) == 40, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, constant_truth_size) == 48, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, genesis_point) == 56, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, aethel_id) == 64, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, reloc_table_ptr) == 72, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, reloc_count) == 80, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, identity_table_ptr) == 88, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, identity_count) == 96, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, trap_hint_count) == 104, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, ipc_count) == 112, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, min_sip) == 120, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, mode_affinity) == 128, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, sip_vector) == 136, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, target_isa) == 144, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, machine_bits) == 152, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, endianness) == 160, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, abi_kind) == 168, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, code_model) == 176, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, reloc_width) == 184, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, entry_encoding) == 192, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, bin_flags) == 200, "weaver input offset mismatch");
_Static_assert(offsetof(WeaverInput, bin_entry_offset) == 208, "weaver input offset mismatch");

extern int weave_aki_structure(void *input);
extern int weave_srv_structure(void *input);
extern int weave_hda_structure(void *input);
extern int weave_aetb_structure(void *input);
extern int weave_bin_structure(void *input);

typedef struct {
    uint8_t *file_buf;
    size_t file_size;
    const LetHeader *hdr;
    const LetMachineContract *contract;
    const LetGeneTableHeader *gene;
    const LetRelocDNAEntry *relocs;
    const LetIdentityNexusEntry *ids;
    uint64_t reloc_table_size;
    uint64_t id_table_size;
} ParsedLet;

static int read_whole_file(const char *path, uint8_t **data_out, size_t *size_out) {
    FILE *f;
    long sz;
    uint8_t *buf;

    *data_out = NULL;
    *size_out = 0;

    f = fopen(path, "rb");
    if (!f) return -1;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }

    sz = ftell(f);
    if (sz <= 0) {
        fclose(f);
        return -1;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return -1;
    }

    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }

    fclose(f);
    *data_out = buf;
    *size_out = (size_t)sz;
    return 0;
}

static int check_range(uint64_t off, uint64_t sz, size_t file_size) {
    if (off > (uint64_t)file_size) return -1;
    if (sz > (uint64_t)file_size) return -1;
    if (off + sz > (uint64_t)file_size) return -1;
    return 0;
}

static uint64_t calc_sip_vector(uint32_t min_sip, uint32_t flags) {
    uint64_t vec = 0;
    if (min_sip <= 1) vec |= 0x1ull;
    if (min_sip <= 2) vec |= 0x2ull;
    if (min_sip <= 3) vec |= 0x4ull;
    if (flags & 0x1u) vec |= 0x8000000000000000ull;
    if (flags & 0x2u) vec |= 0x4000000000000000ull;
    if (flags & 0x4u) vec |= 0x2000000000000000ull;
    return vec;
}

static int parse_let_file(const char *let_file, ParsedLet *parsed) {
    if (!let_file || !parsed) return -1;
    memset(parsed, 0, sizeof(*parsed));

    if (read_whole_file(let_file, &parsed->file_buf, &parsed->file_size) != 0) {
        return -1;
    }
    if (parsed->file_size < sizeof(LetHeader)) {
        return -1;
    }
    parsed->hdr = (const LetHeader *)parsed->file_buf;
    if (parsed->hdr->magic != LET_MAGIC) {
        return -1;
    }
    if (check_range(parsed->hdr->logic_stream_offset, parsed->hdr->logic_stream_size, parsed->file_size) != 0 ||
        check_range(parsed->hdr->state_snapshot_offset, parsed->hdr->state_snapshot_size, parsed->file_size) != 0 ||
        check_range(parsed->hdr->gene_table_offset, parsed->hdr->gene_table_size, parsed->file_size) != 0) {
        return -1;
    }
    if (parsed->hdr->gene_table_size < sizeof(LetGeneTableHeader)) {
        return -1;
    }

    parsed->contract = (const LetMachineContract *)parsed->hdr->extended;
    parsed->gene = (const LetGeneTableHeader *)(parsed->file_buf + parsed->hdr->gene_table_offset);
    parsed->relocs = (const LetRelocDNAEntry *)((const uint8_t *)parsed->gene + sizeof(LetGeneTableHeader));
    parsed->reloc_table_size = (uint64_t)parsed->gene->reloc_count * sizeof(LetRelocDNAEntry);
    parsed->id_table_size = (uint64_t)parsed->gene->identity_count * sizeof(LetIdentityNexusEntry);
    if (sizeof(LetGeneTableHeader) + parsed->reloc_table_size + parsed->id_table_size > parsed->hdr->gene_table_size) {
        return -1;
    }
    parsed->ids = (const LetIdentityNexusEntry *)((const uint8_t *)parsed->relocs + parsed->reloc_table_size);
    return 0;
}

static int validate_contract_for_target(const ParsedLet *parsed, int target_format, int verbose) {
    const LetMachineContract *c;
    if (!parsed || !parsed->contract) return -1;
    c = parsed->contract;

    if (c->endianness != LET_ENDIAN_LITTLE) {
        fprintf(stderr, "Error: unsupported endianness in LET contract: %u\n", (unsigned)c->endianness);
        return -1;
    }
    if (!(c->machine_bits == 16 || c->machine_bits == 32 || c->machine_bits == 64)) {
        fprintf(stderr, "Error: invalid machine bits in LET contract: %u\n", (unsigned)c->machine_bits);
        return -1;
    }
    if (!(c->reloc_width == 8 || c->reloc_width == 16 || c->reloc_width == 32 || c->reloc_width == 64)) {
        fprintf(stderr, "Error: invalid relocation width in LET contract: %u\n", (unsigned)c->reloc_width);
        return -1;
    }
    if (c->reloc_width > c->machine_bits) {
        fprintf(stderr,
                "Error: reloc width (%u) exceeds machine bits (%u)\n",
                (unsigned)c->reloc_width,
                (unsigned)c->machine_bits);
        return -1;
    }
    if (target_format == LET_WEAVE_TARGET_BIN &&
        (c->bin_flags & LET_BIN_FLAG_EXPORTABLE) == 0u) {
        fprintf(stderr, "Error: LET contract forbids BIN export\n");
        return -1;
    }

    if (verbose) {
        fprintf(stderr,
                "[LET-CONTRACT] isa=%u bits=%u abi=%u endian=%u code_model=%u reloc_width=%u entry=%u bin_flags=0x%02x\n",
                (unsigned)c->target_isa,
                (unsigned)c->machine_bits,
                (unsigned)c->abi_kind,
                (unsigned)c->endianness,
                (unsigned)c->code_model,
                (unsigned)c->reloc_width,
                (unsigned)c->entry_encoding,
                (unsigned)c->bin_flags);
    }
    return 0;
}

static void write_bin_map(const char *bin_path, const ParsedLet *parsed, unsigned long long entry_off) {
    char map_path[1024];
    FILE *f;

    if (!bin_path || !parsed) return;
    snprintf(map_path, sizeof(map_path), "%s.map", bin_path);
    f = fopen(map_path, "w");
    if (!f) return;

    fprintf(f, "# let/bin/map\n");
    fprintf(f, "bin/path=%s\n", bin_path);
    fprintf(f, "entry/offset=0x%llx\n", entry_off);
    fprintf(f, "logic/stream/size=%llu\n", (unsigned long long)parsed->hdr->logic_stream_size);
    fprintf(f, "reloc/count=%u\n", parsed->gene->reloc_count);
    fprintf(f, "identity/count=%u\n", parsed->gene->identity_count);
    fclose(f);
}

static int write_bin_fallback(const char *output_file,
                              const uint8_t *code,
                              uint64_t code_size,
                              uint64_t entry_off,
                              uint64_t bin_flags,
                              const LetMachineContract *contract) {
    FILE *out;
    uint8_t header[64];
    if (!output_file || entry_off > code_size) return -1;
    if (code_size > 0 && !code) return -1;
    if ((bin_flags & LET_BIN_FLAG_FLAT_DEFAULT) && code_size > 0 && entry_off >= code_size) {
        return -1;
    }
    out = fopen(output_file, "wb");
    if (!out) return -1;

    if (bin_flags & LET_BIN_FLAG_FLAT_DEFAULT) {
        if (code_size > entry_off) {
            if (fwrite(code + entry_off, 1, (size_t)(code_size - entry_off), out) != (size_t)(code_size - entry_off)) {
                fclose(out);
                return -1;
            }
        }
    } else {
        memset(header, 0, sizeof(header));
        header[0] = 'B';
        header[1] = 'I';
        header[2] = 'N';
        header[3] = '!';
        header[4] = 1;
        header[8] = (uint8_t)(contract ? contract->machine_bits : 64);
        header[16] = (uint8_t)(contract ? contract->target_isa : LET_ISA_X86_64);
        memcpy(&header[24], &code_size, sizeof(code_size));
        memcpy(&header[32], &entry_off, sizeof(entry_off));
        if (fwrite(header, 1, sizeof(header), out) != sizeof(header)) {
            fclose(out);
            return -1;
        }
        if (code_size > 0 &&
            fwrite(code, 1, (size_t)code_size, out) != (size_t)code_size) {
            fclose(out);
            return -1;
        }
    }

    fclose(out);
    return 0;
}

static long file_size_or_neg(const char *path) {
    struct stat st;
    if (!path) return -1;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

int let_weave_to_target(const char *let_file,
                        const char *output_file,
                        int target_format,
                        int verbose,
                        const LetWeaveOptions *options) {
    ParsedLet parsed;
    WeaverInput input;
    int ret = -1;
    uint64_t bin_flags = 0;

    if (!let_file || !output_file) {
        return -1;
    }

    if (parse_let_file(let_file, &parsed) != 0) {
        fprintf(stderr, "Error: cannot read LET file '%s'\n", let_file);
        return -1;
    }
    if (validate_contract_for_target(&parsed, target_format, verbose) != 0) {
        goto cleanup;
    }

    memset(&input, 0, sizeof(input));
    input.output_filename = output_file;
    input.act_flow_buffer = parsed.file_buf + parsed.hdr->logic_stream_offset;
    input.act_flow_size = parsed.hdr->logic_stream_size;
    input.mirror_state_buffer = parsed.file_buf + parsed.hdr->state_snapshot_offset;
    input.mirror_state_size = parsed.hdr->state_snapshot_size;

    if (parsed.hdr->state_snapshot_offset + parsed.hdr->state_snapshot_size <= parsed.hdr->gene_table_offset) {
        input.constant_truth_buffer = parsed.file_buf + parsed.hdr->state_snapshot_offset + parsed.hdr->state_snapshot_size;
        input.constant_truth_size = parsed.hdr->gene_table_offset - (parsed.hdr->state_snapshot_offset + parsed.hdr->state_snapshot_size);
    }

    input.genesis_point = parsed.hdr->genesis_point;
    input.aethel_id = parsed.hdr->logic_id;
    input.reloc_table_ptr = parsed.relocs;
    input.reloc_count = parsed.gene->reloc_count;
    input.identity_table_ptr = parsed.ids;
    input.identity_count = parsed.gene->identity_count;
    input.trap_hint_count = parsed.gene->trap_hint_count;
    input.ipc_count = parsed.gene->ipc_count;
    input.min_sip = parsed.gene->min_sip;
    input.mode_affinity = (parsed.gene->flags & 0x1u) ? 1u : 0u;
    input.sip_vector = calc_sip_vector(parsed.gene->min_sip, parsed.gene->flags);
    input.target_isa = parsed.contract->target_isa;
    input.machine_bits = parsed.contract->machine_bits;
    input.endianness = parsed.contract->endianness;
    input.abi_kind = parsed.contract->abi_kind;
    input.code_model = parsed.contract->code_model;
    input.reloc_width = parsed.contract->reloc_width;
    input.entry_encoding = parsed.contract->entry_encoding;

    bin_flags = parsed.contract->bin_flags;
    if (options) {
        if (options->bin_flat) {
            bin_flags |= LET_BIN_FLAG_FLAT_DEFAULT;
        } else {
            bin_flags &= ~((uint64_t)LET_BIN_FLAG_FLAT_DEFAULT);
        }
        if (options->has_bin_entry_offset) {
            input.bin_entry_offset = options->bin_entry_offset;
        }
    }
    input.bin_flags = bin_flags;

    if (verbose) {
        fprintf(stderr,
                "[LET-WEAVER] Gene: reloc=%u identity=%u trap=%u ipc=%u min_sip=%u mode=%s\n",
                parsed.gene->reloc_count,
                parsed.gene->identity_count,
                parsed.gene->trap_hint_count,
                parsed.gene->ipc_count,
                parsed.gene->min_sip,
                input.mode_affinity ? "architect" : "sandbox");
    }

    if (target_format != LET_WEAVE_TARGET_BIN &&
        input.act_flow_size == 7 &&
        input.act_flow_buffer[0] == 0x55 &&
        input.act_flow_buffer[1] == 0x48 &&
        input.act_flow_buffer[2] == 0x89 &&
        input.act_flow_buffer[3] == 0xE5 &&
        input.act_flow_buffer[4] == 0x90 &&
        input.act_flow_buffer[5] == 0x5D &&
        input.act_flow_buffer[6] == 0xC3) {
        fprintf(stderr,
                "[LET-WEAVER] Warning: LET ActFlow is placeholder machine code (55 48 89 E5 90 5D C3)\n");
    }

    switch (target_format) {
        case LET_WEAVE_TARGET_AKI:
            ret = weave_aki_structure(&input);
            break;
        case LET_WEAVE_TARGET_SRV:
            ret = weave_srv_structure(&input);
            break;
        case LET_WEAVE_TARGET_HDA:
            ret = weave_hda_structure(&input);
            break;
        case LET_WEAVE_TARGET_AETB:
            ret = weave_aetb_structure(&input);
            break;
        case LET_WEAVE_TARGET_BIN:
            ret = weave_bin_structure(&input);
            if (ret == 0) {
                long out_size = file_size_or_neg(output_file);
                if (out_size == 0 && input.act_flow_size > input.bin_entry_offset) {
                    ret = write_bin_fallback(output_file,
                                             input.act_flow_buffer,
                                             input.act_flow_size,
                                             input.bin_entry_offset,
                                             input.bin_flags,
                                             parsed.contract);
                }
            } else {
                ret = write_bin_fallback(output_file,
                                         input.act_flow_buffer,
                                         input.act_flow_size,
                                         input.bin_entry_offset,
                                         input.bin_flags,
                                         parsed.contract);
            }
            if (ret == 0 && options && options->bin_with_map) {
                write_bin_map(output_file, &parsed, (unsigned long long)input.bin_entry_offset);
            }
            break;
        default:
            fprintf(stderr, "Error: unsupported weaver target: %d\n", target_format);
            ret = -1;
            break;
    }

cleanup:
    free(parsed.file_buf);
    return ret;
}

int let_verify_contract(const char *let_file, int verbose) {
    ParsedLet parsed;
    int rc;
    if (parse_let_file(let_file, &parsed) != 0) {
        fprintf(stderr, "Error: failed to parse LET: %s\n", let_file ? let_file : "(null)");
        return -1;
    }
    rc = validate_contract_for_target(&parsed, LET_WEAVE_TARGET_AETB, verbose);
    free(parsed.file_buf);
    return rc;
}

int let_dump_reloc_dna(const char *let_file, const char *output_file) {
    ParsedLet parsed;
    FILE *out = stdout;
    uint32_t i;
    if (parse_let_file(let_file, &parsed) != 0) {
        fprintf(stderr, "Error: failed to parse LET: %s\n", let_file ? let_file : "(null)");
        return -1;
    }
    if (output_file && output_file[0] != '\0') {
        out = fopen(output_file, "w");
        if (!out) {
            free(parsed.file_buf);
            return -1;
        }
    }
    fprintf(out, "# reloc/dna file=%s count=%u\n", let_file, parsed.gene->reloc_count);
    for (i = 0; i < parsed.gene->reloc_count; i++) {
        fprintf(out,
                "%u code/offset=0x%llx reloc/type=%u\n",
                i,
                (unsigned long long)parsed.relocs[i].code_offset,
                parsed.relocs[i].reloc_type);
    }
    if (out != stdout) fclose(out);
    free(parsed.file_buf);
    return 0;
}
