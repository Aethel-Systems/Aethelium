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

/*
 * AethelOS AECF Configuration Parser
 * 工业级项目配置与依赖装配解析引擎
 */

#ifndef AECF_PARSER_H
#define AECF_PARSER_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    char *target_format;    /* e.g., "exe", "aki", "rom" */
    char *version;
    char *output_file;
    
    char **input_files;
    int input_count;
    int input_capacity;
    
    char *icon_path;
    int require_admin;
    
    char *isa;              /* "x86_64", "aarch64" */
    int machine_bits;
    char *mode;             /* "sandbox", "architect" */
    int opt_level;
    
    int use_lib;
    char *lib_model;        /* "GUI", "Core", etc. */
    
    int bin_flat;
    int bin_with_map;
    int freestanding;
    int rom_mode;
} AecfConfig;

void aecf_config_init(AecfConfig *config);
int aecf_parse_file(const char *filename, AecfConfig *config);
void aecf_config_destroy(AecfConfig *config);
char *aecf_translate_path(const char *raw_path);

#endif /* AECF_PARSER_H */
