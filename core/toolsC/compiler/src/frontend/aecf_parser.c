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

#include "aecf_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#endif

typedef enum {
    SEC_NONE,
    SEC_BASIC,
    SEC_PROJECT,
    SEC_EXPAND,
    SEC_LIBRARY
} SectionType;

typedef enum {
    STATE_EXPECT_ANY,
    STATE_EXPECT_VALUE
} ParserState;

static char *trim_whitespace(char *str) {
    char *end;
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static char *extract_bracketed_value(char *str) {
    str = trim_whitespace(str);
    if (!str) return NULL;
    size_t len = strlen(str);
    if (len >= 2 && str[0] == '[' && str[len - 1] == ']') {
        str[len - 1] = '\0';
        return trim_whitespace(str + 1);
    }
    return str;
}

static void strip_comments(char *line) {
    if (!line) return;
    char *comment = strstr(line, "//");
    if (comment) {
        *comment = '\0';
    }
}

char *aecf_translate_path(const char *raw_path) {
    if (!raw_path) return NULL;

    char *result = malloc(4096);
    if (!result) {
        fprintf(stderr, "[FATAL] Out of memory during path translation\n");
        exit(1);
    }
    result[0] = '\0';

    const char *rest = NULL;
    if (strncmp(raw_path, ">:home-", 7) == 0) {
        const char *home = getenv("HOME");
        if (!home) home = getenv("USERPROFILE");
        if (!home) home = ".";
        strcpy(result, home);
        strcat(result, "/");
        rest = raw_path + 7;
    } else if (strncmp(raw_path, ">|-", 3) == 0) {
#ifdef _WIN32
        strcpy(result, "C:/");
#else
        strcpy(result, "/");
#endif
        rest = raw_path + 3;
    } else if (strncmp(raw_path, ">:", 2) == 0) {
        rest = raw_path + 2;
    } else {
        strcpy(result, raw_path);
        return result;
    }

    size_t len = strlen(result);
    for (size_t i = 0; rest[i] != '\0' && len < 4095; i++) {
        if (rest[i] == '-') {
            result[len++] = '/';
        } else {
            result[len++] = rest[i];
        }
    }
    result[len] = '\0';
    return result;
}

void aecf_config_init(AecfConfig *config) {
    memset(config, 0, sizeof(AecfConfig));
    config->input_capacity = 32;
    config->input_files = malloc(sizeof(char*) * config->input_capacity);
    if (!config->input_files) {
        fprintf(stderr, "[FATAL] Out of memory initializing AECF config\n");
        exit(1);
    }
    config->opt_level = -1;
}

void aecf_config_destroy(AecfConfig *config) {
    if (!config) return;
    free(config->target_format);
    free(config->version);
    free(config->output_file);
    for (int i = 0; i < config->input_count; i++) {
        free(config->input_files[i]);
    }
    free(config->input_files);
    free(config->icon_path);
    free(config->isa);
    free(config->mode);
    free(config->lib_model);
}

static void process_key_value(SectionType current_section, const char *k, const char *v, AecfConfig *config) {
    if (!k || !v) return;

    if (current_section == SEC_BASIC) {
        if (strcasecmp(k, "Version") == 0) {
            config->version = strdup(v);
        } else if (strcasecmp(k, "Output") == 0) {
            char *trans = aecf_translate_path(v);
            config->output_file = trans ? trans : strdup(v);
        }
    } else if (current_section == SEC_PROJECT) {
        if (strcasecmp(k, "ico") == 0) {
            char *trans = aecf_translate_path(v);
            config->icon_path = trans ? trans : strdup(v);
        }
    } else if (current_section == SEC_LIBRARY) {
        if (strcasecmp(k, "lib") == 0) {
            if (strcasecmp(v, "yes") == 0 || strcasecmp(v, "true") == 0) {
                config->use_lib = 1;
            }
        } else if (strcasecmp(k, "model") == 0) {
            config->lib_model = strdup(v);
        }
    } else if (current_section == SEC_EXPAND) {
        /* Expand parameters outside of a block but within Expand section */
        if (strcasecmp(k, "isa") == 0) config->isa = strdup(v);
        else if (strcasecmp(k, "machine_bits") == 0) config->machine_bits = atoi(v);
        else if (strcasecmp(k, "mode") == 0) config->mode = strdup(v);
        else if (strcasecmp(k, "optimize") == 0) config->opt_level = atoi(v);
        else if (strcasecmp(k, "bin_flat") == 0) config->bin_flat = (strcasecmp(v, "true") == 0 || strcmp(v, "1") == 0);
        else if (strcasecmp(k, "freestanding") == 0) config->freestanding = (strcasecmp(v, "true") == 0 || strcmp(v, "1") == 0);
        else if (strcasecmp(k, "rom") == 0) config->rom_mode = (strcasecmp(v, "true") == 0 || strcmp(v, "1") == 0);
    }
}

int aecf_parse_file(const char *filename, AecfConfig *config) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "[AECF] Error: Cannot open config file '%s'\n", filename);
        return -1;
    }

    char line[2048];
    SectionType current_section = SEC_NONE;
    int in_block = 0;
    char block_name[128] = {0};
    int line_num = 0;

    ParserState state = STATE_EXPECT_ANY;
    char pending_key[256] = {0};

    fprintf(stderr, "[AECF] Parsing configuration manifest: %s\n", filename);

    while (fgets(line, sizeof(line), f)) {
        line_num++;
        strip_comments(line);
        
        char *tline = trim_whitespace(line);
        if (tline[0] == '\0') continue;

        /* 处理等待多行 Value 的状态 */
        if (state == STATE_EXPECT_VALUE) {
            char *v = extract_bracketed_value(tline);
            if (v && v[0] != '\0') {
                process_key_value(current_section, pending_key, v, config);
                state = STATE_EXPECT_ANY;
                pending_key[0] = '\0';
                continue;
            } else if (tline[0] == '>' || tline[0] == '}') {
                /* 如果在此处遇到新的 section 或 block 闭合，说明上一个 key 是无值的悬空 key */
                state = STATE_EXPECT_ANY;
                pending_key[0] = '\0';
                /* 回退并继续执行正常的区段和块解析逻辑 */
            } else {
                /* 如果仍然是空行或无效字符则继续读取（已由开头的 tline[0]=='\0' 拦截过） */
                continue;
            }
        }

        /* 识别 Section: > [SectionName] : \target */
        if (tline[0] == '>') {
            char *colon = strchr(tline, ':');
            char sec_name[128] = {0};

            if (colon) {
                *colon = '\0';
                char *s = extract_bracketed_value(trim_whitespace(tline + 1));
                if (s) strncpy(sec_name, s, sizeof(sec_name) - 1);
                
                char *target = trim_whitespace(colon + 1);
                if (target && target[0] == '\\') target++; /* 剔除可能的反斜杠 */
                if (target && target[0] != '\0') {
                    config->target_format = strdup(target);
                }
            } else {
                char *s = extract_bracketed_value(trim_whitespace(tline + 1));
                if (s) strncpy(sec_name, s, sizeof(sec_name) - 1);
            }
            
            if (strcasecmp(sec_name, "Basic") == 0) current_section = SEC_BASIC;
            else if (strcasecmp(sec_name, "Project") == 0) current_section = SEC_PROJECT;
            else if (strcasecmp(sec_name, "Expand") == 0) current_section = SEC_EXPAND;
            else if (strcasecmp(sec_name, "Library") == 0) current_section = SEC_LIBRARY;
            else {
                fprintf(stderr, "[AECF] Warning: Unknown section '%s' at line %d\n", sec_name, line_num);
                current_section = SEC_NONE;
            }
            
            in_block = 0;
            continue;
        }

        /* 块的退出 */
        if (tline[0] == '}') {
            in_block = 0;
            block_name[0] = '\0';
            continue;
        }

        /* 块的进入: blockName { */
        char *brace = strchr(tline, '{');
        if (brace && !strchr(tline, ':')) {
            *brace = '\0';
            strncpy(block_name, trim_whitespace(tline), sizeof(block_name) - 1);
            in_block = 1;
            continue;
        }

        /* Block 内部逻辑解析 */
        if (in_block) {
            if (current_section == SEC_PROJECT && strcasecmp(block_name, "file") == 0) {
                /* Project/file 块下每一行都是一个文件路径 */
                char *trans = aecf_translate_path(tline);
                char *final_path = trans ? trans : strdup(tline);
                if (final_path) {
                    if (config->input_count >= config->input_capacity) {
                        config->input_capacity *= 2;
                        config->input_files = realloc(config->input_files, sizeof(char*) * config->input_capacity);
                        if (!config->input_files) {
                            fprintf(stderr, "[FATAL] Out of memory expanding input files\n");
                            exit(1);
                        }
                    }
                    config->input_files[config->input_count++] = final_path;
                }
            } else if (current_section == SEC_EXPAND && strcasecmp(block_name, "Parameter") == 0) {
                /* Expand/Parameter 块内的键值对 */
                char *colon = strchr(tline, ':');
                if (colon) {
                    *colon = '\0';
                    char *k = trim_whitespace(tline);
                    char *v = extract_bracketed_value(trim_whitespace(colon + 1));
                    if (v && v[0] != '\0') {
                        process_key_value(SEC_EXPAND, k, v, config);
                    } else {
                        /* 如果值跨行，在此处拦截并转移状态 */
                        strncpy(pending_key, k, sizeof(pending_key) - 1);
                        state = STATE_EXPECT_VALUE;
                    }
                }
            }
            continue;
        }

        /* 根级 Key-Value 解析 */
        char *colon = strchr(tline, ':');
        if (colon) {
            *colon = '\0';
            char *k = trim_whitespace(tline);
            char *v_raw = trim_whitespace(colon + 1);

            if (v_raw[0] == '\0') {
                /* 跨行值：Key: \n [Value] */
                strncpy(pending_key, k, sizeof(pending_key) - 1);
                state = STATE_EXPECT_VALUE;
            } else {
                /* 单行值：Key: [Value] */
                char *v = extract_bracketed_value(v_raw);
                process_key_value(current_section, k, v, config);
            }
        }
    }

    if (state == STATE_EXPECT_VALUE) {
        fprintf(stderr, "[AECF] Warning: Expected value for key '%s' before EOF\n", pending_key);
    }

    fclose(f);
    return 0;
}
