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

#ifndef AETHEL_LET_WEAVER_BRIDGE_H
#define AETHEL_LET_WEAVER_BRIDGE_H

enum {
    LET_WEAVE_TARGET_AKI = 1,
    LET_WEAVE_TARGET_SRV = 2,
    LET_WEAVE_TARGET_HDA = 3,
    LET_WEAVE_TARGET_AETB = 4,
    LET_WEAVE_TARGET_BIN = 5,
    LET_WEAVE_TARGET_ROM = 6
};

typedef struct {
    int bin_flat;
    int bin_with_map;
    int has_bin_entry_offset;
    unsigned long long bin_entry_offset;
    unsigned long long rom_size_bytes;
    unsigned char rom_fill_byte;
} LetWeaveOptions;

int let_weave_to_target(const char *let_file,
                        const char *output_file,
                        int target_format,
                        int verbose,
                        const LetWeaveOptions *options);

int let_verify_contract(const char *let_file, int verbose);
int let_dump_reloc_dna(const char *let_file, const char *output_file);

#endif
