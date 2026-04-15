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

#include "import_resolver.h"
#include "semantic_checker.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <sys/stat.h>
#include <ctype.h>

static int file_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int starts_with(const char *s, const char *prefix) {
    return s && prefix && strncmp(s, prefix, strlen(prefix)) == 0;
}

static void normalize_copy(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
}

static int path_join(char *out, size_t out_sz, const char *a, const char *b) {
    size_t alen = a ? strlen(a) : 0;
    size_t blen = b ? strlen(b) : 0;
    if (out_sz == 0) return -1;
    if (!a || alen == 0) {
        if (blen + 1 > out_sz) return -1;
        memcpy(out, b, blen + 1);
        return 0;
    }
    if (alen + 1 + blen + 1 > out_sz) return -1;
    memcpy(out, a, alen);
    out[alen] = '/';
    memcpy(out + alen + 1, b, blen + 1);
    return 0;
}

/* Aethel 模块名只能用 '-' 分层；解析时转换为文件路径的子目录 */
static void dash_to_path(const char *module, char *buf, size_t buf_sz) {
    size_t i;
    for (i = 0; module && module[i] != '\0' && i + 1 < buf_sz; i++) {
        char c = module[i];
        if (c == '-') buf[i] = '/';
        else buf[i] = c;
    }
    if (buf_sz > 0) buf[i < buf_sz ? i : buf_sz - 1] = '\0';
}

/* libA 形态仍保留：a>b>c 转换成 a/b/c */
static void gt_to_path(const char *module, char *buf, size_t buf_sz) {
    size_t i;
    int gt_seen = 0;
    for (i = 0; module && module[i] != '\0' && i + 1 < buf_sz; i++) {
        char c = module[i];
        if (c == '>') {
            if (gt_seen < 2) {
                buf[i] = '/';
                gt_seen++;
            } else {
                buf[i] = c;
            }
        } else {
            buf[i] = c;
        }
    }
    if (buf_sz > 0) buf[i < buf_sz ? i : buf_sz - 1] = '\0';
}

static int sanitize_module(const char *module) {
    size_t i;
    if (!module || !module[0]) return 0;
    for (i = 0; module[i] != '\0'; i++) {
        char c = module[i];
        /* 彻底禁止 Unix 正斜杠，防止“路径即导入”污染 */
        if (c == '/' || c == '.') return 0;
        if (!(isalnum((unsigned char)c) || c == '-' || c == '>' || c == ':' || c == '[' || c == ']')) {
            return 0;
        }
    }
    return 1;
}

static void add_candidate(const char *dir, const char *name, char *out, size_t out_sz, int *found) {
    if (*found) return;
    if (path_join(out, out_sz, dir, name) == 0 && file_exists(out)) {
        *found = 1;
    }
}

int import_resolve_path(const char *module,
                       const char *current_dir,
                       const char *root_hint,
                       char *out_path,
                       size_t out_size) {
    char candidate[512];
    char translated[256];
    char dash_form[256];
    char gt_form[256];
    int found = 0;

    if (!module || !out_path || out_size == 0) return -1;
    out_path[0] = '\0';

    if (!sanitize_module(module)) return -1;

    /* Build module baseline (append .ae if missing) */
    normalize_copy(translated, sizeof(translated), module);
    if (!strstr(translated, ".ae")) {
        strncat(translated, ".ae", sizeof(translated) - strlen(translated) - 1);
    }

    /* 拒绝任何包含正斜杠的模块名（Unix 化污染） */
    if (strchr(translated, '/')) {
        return -1;
    }

    dash_to_path(translated, dash_form, sizeof(dash_form));
    gt_to_path(translated, gt_form, sizeof(gt_form));

    /* 1) current directory */
    if (current_dir && current_dir[0]) {
        add_candidate(current_dir, dash_form, out_path, out_size, &found);
        add_candidate(current_dir, gt_form, out_path, out_size, &found);
        add_candidate(current_dir, translated, out_path, out_size, &found);
    }

    /* 2) root hint */
    if (!found && root_hint && root_hint[0]) {
        add_candidate(root_hint, dash_form, out_path, out_size, &found);
        add_candidate(root_hint, gt_form, out_path, out_size, &found);
        add_candidate(root_hint, translated, out_path, out_size, &found);
    }

    /* 禁止 '.' 起始的 Unix 相对路径，故不再尝试 translated[0]=='.' 的兜底 */

    if (!found) {
        /* As a last resort, try raw module string (maybe absolute custom mapping) */
        if (file_exists(module)) {
            normalize_copy(out_path, out_size, module);
            return 0;
        }
        return -1;
    }

    return 0;
}

static int import_exists(const ImportList *list, const char *module) {
    size_t i;
    if (!list || !module) return 1;
    for (i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].module, module) == 0) {
            return 1;
        }
    }
    return 0;
}

static void collect_from_node(const ASTNode *node, const char *from_file, ImportList *list) {
    int i;
    if (!node || !list) return;

    switch (node->type) {
        case AST_PROGRAM:
            for (i = 0; i < node->data.program.decl_count; i++) {
                collect_from_node(node->data.program.declarations[i], from_file, list);
            }
            break;
        case AST_IMPORT_STMT:
            if (list->count < IMPORT_RESOLVER_MAX && node->data.import_stmt.module) {
                if (!import_exists(list, node->data.import_stmt.module)) {
                    normalize_copy(list->items[list->count].module,
                                   sizeof(list->items[list->count].module),
                                   node->data.import_stmt.module);
                    normalize_copy(list->items[list->count].from_file,
                                   sizeof(list->items[list->count].from_file),
                                   from_file ? from_file : "");
                    list->items[list->count].is_rimport = node->data.import_stmt.is_rimport;
                    list->count++;
                }
            }
            break;
        case AST_BLOCK:
            for (i = 0; i < node->data.block.stmt_count; i++) {
                collect_from_node(node->data.block.statements[i], from_file, list);
            }
            break;
        case AST_METAL_BLOCK:
            for (i = 0; i < node->data.metal_block.stmt_count; i++) {
                collect_from_node(node->data.metal_block.statements[i], from_file, list);
            }
            break;
        default:
            /* For brevity, only descend into containers above */
            break;
    }
}

void import_list_collect(const ASTNode *root, const char *from_file, ImportList *list) {
    if (!list || !root) return;
    collect_from_node(root, from_file, list);
}
