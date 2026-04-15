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
