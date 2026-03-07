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
