#ifndef SEMANTIC_CHECKER_H
#define SEMANTIC_CHECKER_H

#include <stddef.h>

struct ASTNode;

typedef enum {
    SEM_IMPORT_INTENT = 1,
    SEM_IMPORT_LIBA = 2,
    SEM_IMPORT_PATH = 3,
    SEM_IMPORT_RIMPORT = 4,
} SemanticImportKind;

typedef struct {
    SemanticImportKind kind;
    char module[256];
} SemanticImportRecord;

typedef struct {
    int error_count;
    int warning_count;

    int has_metal_block;
    int requires_architect_mode;
    int has_rimport;

    int trap_hint_count;
    int reloc_dna_count;
    int ipc_contract_count;
    int identity_contract_min_sip;

    size_t import_count;
    SemanticImportRecord imports[256];
} SemanticResult;

void semantic_result_init(SemanticResult *result);

int semantic_analyze_program(const struct ASTNode *ast,
                            const char *source_file,
                            const char *compile_mode,
                            SemanticResult *result,
                            char *error_buf,
                            size_t error_buf_size);

/* 工业级类型转换安全检查 - 原初层地址强类型 */
int ae_type_conversion_allowed(const char *from_type, const char *to_type);

#endif
