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

#ifndef IMPORT_RESOLVER_H
#define IMPORT_RESOLVER_H

#include "parser/aec_parser.h"
#include <stddef.h>

#define IMPORT_RESOLVER_MAX 256

typedef struct {
    char module[256];
    int is_rimport;
    char from_file[512];
} ImportRequest;

typedef struct {
    ImportRequest items[IMPORT_RESOLVER_MAX];
    size_t count;
} ImportList;

/* Collect import/Rimport nodes from an AST into list (deduplicated). */
void import_list_collect(const ASTNode *root, const char *from_file, ImportList *list);

/*
 * Resolve an import module name to a filesystem path.
 * - current_dir: directory of the file containing the import
 * - root_hint: optional project root (may be NULL)
 * Returns 0 on success and writes to out_path; non-zero on failure.
 */
int import_resolve_path(const char *module,
                       const char *current_dir,
                       const char *root_hint,
                       char *out_path,
                       size_t out_size);

#endif /* IMPORT_RESOLVER_H */
