#ifndef AETHEL_LET_GEN_H
#define AETHEL_LET_GEN_H

#include <stddef.h>
#include <stdint.h>

#include "semantic_checker.h"
#include "let_format.h"

typedef struct {
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
} LetEmitOptions;

int let_generate_image(const char *output_file,
                       const uint8_t *logic_stream,
                       size_t logic_size,
                       const uint8_t *state_snapshot,
                       size_t state_size,
                       const uint8_t *constant_truth,
                       size_t constant_size,
                       const SemanticResult *semantic,
                       const LetEmitOptions *options);

#endif
