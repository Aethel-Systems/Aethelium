/*
 * AethelOS 自定义语法映射系统实现
 * 完整的工业级别 syntax 映射引擎
 */

#include "syntax_mapper.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>

/* ===================================================================
 * 内部工具函数
 * =================================================================== */

/* 安全内存分配 */
static void* safe_malloc_syntax(size_t size) {
    if (size == 0) return NULL;
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "FATAL: 内存分配失败，请求大小 %zu 字节\n", size);
        abort();
    }
    return ptr;
}

/* 安全字符串复制 */
static char* safe_strdup_syntax(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *dup = (char*)safe_malloc_syntax(len + 1);
    memcpy(dup, str, len);
    dup[len] = '\0';
    return dup;
}

/* 比较函数（用于优先级排序） */
static int compare_priority(const void *a, const void *b, void *context) {
    SyntaxMappingTable *table = (SyntaxMappingTable*)context;
    int idx_a = *(const int*)a;
    int idx_b = *(const int*)b;
    
    int priority_a = table->entries[idx_a].priority;
    int priority_b = table->entries[idx_b].priority;
    
    /* 高优先级先 */
    return priority_b - priority_a;
}

/* 快速排序实现（带上下文） */
static void qsort_with_context(void *base, size_t nmemb, size_t size,
                               int (*compar)(const void*, const void*, void*),
                               void *context) {
    /* 简化版 QSort，用于排序索引数组 */
    if (nmemb <= 1) return;
    
    int *indices = (int*)base;
    for (size_t i = 0; i < nmemb - 1; i++) {
        for (size_t j = i + 1; j < nmemb; j++) {
            if (compar(&indices[i], &indices[j], context) < 0) {
                int tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
            }
        }
    }
}

/* ===================================================================
 * 核心 API 实现
 * =================================================================== */

/**
 * 创建新的 Syntax 映射管理器
 */
SyntaxMapperManager* syntax_mapper_create(void) {
    SyntaxMapperManager *manager = (SyntaxMapperManager*)safe_malloc_syntax(sizeof(SyntaxMapperManager));
    
    manager->tables = (SyntaxMappingTable**)safe_malloc_syntax(sizeof(SyntaxMappingTable*) * 16);
    manager->table_count = 0;
    manager->table_capacity = 16;
    
    manager->lookup_cache = (struct { char *syntax_name; SyntaxMappingTable *table; }*)
        safe_malloc_syntax(sizeof(struct { char *syntax_name; SyntaxMappingTable *table; }) * 8);
    manager->cache_size = 0;
    
    manager->total_pattern_matches = 0;
    manager->failed_patterns = 0;
    manager->deprecated_usage_count = 0;
    manager->compiler_context = NULL;
    
    return manager;
}

/**
 * 从 AST syntax_def 节点注册一个 syntax 块
 */
SyntaxMappingTable* syntax_mapper_register(
    SyntaxMapperManager *manager,
    const char *syntax_name,
    const char **patterns,
    const char **actions,
    int pattern_count)
{
    if (!manager || !syntax_name || !patterns || !actions || pattern_count <= 0) {
        fprintf(stderr, "警告：syntax 注册参数无效\n");
        return NULL;
    }
    
    /* 检查是否已经存在同名的 syntax */
    for (int i = 0; i < manager->table_count; i++) {
        if (strcmp(manager->tables[i]->syntax_name, syntax_name) == 0) {
            fprintf(stderr, "警告：syntax '%s' 已存在，跳过重复注册\n", syntax_name);
            return manager->tables[i];
        }
    }
    
    /* 创建新的映射表 */
    SyntaxMappingTable *table = (SyntaxMappingTable*)safe_malloc_syntax(sizeof(SyntaxMappingTable));
    
    table->syntax_name = safe_strdup_syntax(syntax_name);
    table->syntax_id = manager->table_count;  /* 使用简单的递增 ID */
    
    /* 初始化条目数组 */
    table->entries = (SyntaxMapEntry*)safe_malloc_syntax(sizeof(SyntaxMapEntry) * pattern_count);
    table->entry_count = 0;
    table->entry_capacity = pattern_count;
    
    /* 初始化优先级索引 */
    table->sorted_priority_indices = (int*)safe_malloc_syntax(sizeof(int) * pattern_count);
    table->is_optimized = false;
    
    table->scope_level = 0;
    table->definition_addr = 0;
    table->total_matches = 0;
    table->cache_hits = 0;
    
    /* 填充映射条目 */
    for (int i = 0; i < pattern_count; i++) {
        SyntaxMapEntry *entry = &table->entries[table->entry_count];
        
        entry->pattern = safe_strdup_syntax(patterns[i]);
        entry->pattern_type = syntax_mapper_infer_pattern_type(patterns[i]);
        
        entry->action = safe_strdup_syntax(actions[i]);
        entry->action_type = syntax_mapper_infer_action_type(actions[i]);
        
        /* 检测弃用模式 */
        entry->is_deprecated = (strstr(actions[i], "@deprecated") != NULL);
        entry->deprecation_msg = entry->is_deprecated ? 
            safe_strdup_syntax(actions[i]) : NULL;
        
        /* 从操作字符串推断优先级 */
        /* 操作:优先级 的格式 */
        entry->priority = 50;  /* 默认优先级 */
        
        entry->description = NULL;  /* 暂时不设置 */
        entry->match_count = 0;
        entry->last_match_line = 0;
        
        table->entry_count++;
    }
    
    /* 添加到管理器的表列表 */
    if (manager->table_count >= manager->table_capacity) {
        manager->table_capacity *= 2;
        manager->tables = (SyntaxMappingTable**)realloc(manager->tables, 
                                                        sizeof(SyntaxMappingTable*) * manager->table_capacity);
    }
    manager->tables[manager->table_count++] = table;
    
    /* 优化映射表（按优先级排序） */
    syntax_mapper_optimize(table);
    
    return table;
}

/**
 * 查找匹配的映射条目
 */
int syntax_mapper_lookup(
    SyntaxMapperManager *manager,
    const char *syntax_name,
    const char *input_text,
    SyntaxMapEntry **matched_entry)
{
    if (!manager || !syntax_name || !input_text) {
        return -1;
    }
    
    /* 从缓存查找对应的映射表 */
    SyntaxMappingTable *table = NULL;
    for (int i = 0; i < manager->table_count; i++) {
        if (strcmp(manager->tables[i]->syntax_name, syntax_name) == 0) {
            table = manager->tables[i];
            break;
        }
    }
    
    if (!table) {
        fprintf(stderr, "警告：找不到 syntax '%s'，无法匹配\n", syntax_name);
        return -1;
    }
    
    /* 使用优先级排序后的索引数组进行快速匹配 */
    for (int i = 0; i < table->entry_count; i++) {
        int idx = table->is_optimized ? table->sorted_priority_indices[i] : i;
        SyntaxMapEntry *entry = &table->entries[idx];
        
        /* 调试信息 */
        /* fprintf(stderr, "[SYNTAX] 尝试匹配模式: %s (类型: %d)\n", entry->pattern, entry->pattern_type); */
        
        /* 根据模式类型进行匹配 */
        bool matched = false;
        switch (entry->pattern_type) {
            case SYNTAX_PATTERN_LITERAL:
                matched = (strcmp(entry->pattern, input_text) == 0);
                break;
            case SYNTAX_PATTERN_PREFIX:
                matched = (strncmp(entry->pattern, input_text, strlen(entry->pattern)) == 0);
                break;
            case SYNTAX_PATTERN_SUFFIX: {
                size_t input_len = strlen(input_text);
                size_t pattern_len = strlen(entry->pattern);
                if (input_len >= pattern_len) {
                    matched = (strcmp(&input_text[input_len - pattern_len], entry->pattern) == 0);
                }
                break;
            }
            case SYNTAX_PATTERN_SUBSTRING:
                matched = (strstr(input_text, entry->pattern) != NULL);
                break;
            case SYNTAX_PATTERN_REGEX:
                /* 简化版：暂时视为子串匹配 */
                matched = (strstr(input_text, entry->pattern) != NULL);
                break;
            default:
                matched = false;
        }
        
        if (matched) {
            entry->match_count++;
            table->total_matches++;
            manager->total_pattern_matches++;
            
            if (matched_entry) {
                *matched_entry = entry;
            }
            
            /* 检测弃用使用 */
            if (entry->is_deprecated) {
                manager->deprecated_usage_count++;
                fprintf(stderr, "⚠ 弃用警告：使用了弃用的 syntax 模式 '%s'\n", entry->pattern);
            }
            
            return idx;
        }
    }
    
    /* 没有找到匹配 */
    manager->failed_patterns++;
    return -1;
}

/**
 * 验证 syntax 映射的有效性
 */
bool syntax_mapper_validate(
    SyntaxMapperManager *manager,
    const char *syntax_name)
{
    if (!manager || !syntax_name) return false;
    
    SyntaxMappingTable *table = NULL;
    for (int i = 0; i < manager->table_count; i++) {
        if (strcmp(manager->tables[i]->syntax_name, syntax_name) == 0) {
            table = manager->tables[i];
            break;
        }
    }
    
    if (!table) return false;
    
    /* 验证检查项 */
    bool is_valid = true;
    
    /* 1. 检查是否有重复的模式 */
    for (int i = 0; i < table->entry_count; i++) {
        for (int j = i + 1; j < table->entry_count; j++) {
            if (strcmp(table->entries[i].pattern, table->entries[j].pattern) == 0) {
                fprintf(stderr, "警告：syntax '%s' 中有重复的模式: '%s'\n",
                        syntax_name, table->entries[i].pattern);
                is_valid = false;
            }
        }
    }
    
    /* 2. 检查空模式或空动作 */
    for (int i = 0; i < table->entry_count; i++) {
        if (table->entries[i].pattern[0] == '\0' || 
            table->entries[i].action[0] == '\0') {
            fprintf(stderr, "警告：syntax '%s' 中有空的模式或动作\n", syntax_name);
            is_valid = false;
        }
    }
    
    /* 3. 检查是否有过于宽泛的模式可能导致始终匹配 */
    for (int i = 0; i < table->entry_count; i++) {
        if (table->entries[i].pattern_type == SYNTAX_PATTERN_SUBSTRING &&
            strlen(table->entries[i].pattern) <= 1) {
            fprintf(stderr, "警告：syntax '%s' 中的子串模式过短，可能导致意外匹配: '%s'\n",
                    syntax_name, table->entries[i].pattern);
        }
    }
    
    return is_valid;
}

/**
 * 优化 syntax 映射（按优先级排序）
 */
void syntax_mapper_optimize(SyntaxMappingTable *table) {
    if (!table || table->entry_count == 0) return;
    
    /* 初始化索引数组 */
    for (int i = 0; i < table->entry_count; i++) {
        table->sorted_priority_indices[i] = i;
    }
    
    /* 执行排序 */
    qsort_with_context(table->sorted_priority_indices, table->entry_count, 
                       sizeof(int), compare_priority, table);
    
    table->is_optimized = true;
}

/**
 * 获取匹配统计
 */
uint64_t syntax_mapper_get_total_matches(const SyntaxMappingTable *table) {
    if (!table) return 0;
    return table->total_matches;
}

/**
 * 生成性能报告
 */
char* syntax_mapper_generate_report(const SyntaxMapperManager *manager) {
    if (!manager) return NULL;
    
    char *report = (char*)safe_malloc_syntax(4096);
    int offset = 0;
    
    offset += sprintf(report + offset, 
        "=== Syntax 映射性能报告 ===\n"
        "总 Syntax 块数: %d\n"
        "总模式匹配数: %llu\n"
        "失败模式数: %llu\n"
        "弃用使用数: %llu\n\n",
        manager->table_count,
        (unsigned long long)manager->total_pattern_matches,
        (unsigned long long)manager->failed_patterns,
        (unsigned long long)manager->deprecated_usage_count);
    
    /* 详细的每个 syntax 块信息 */
    for (int i = 0; i < manager->table_count; i++) {
        SyntaxMappingTable *table = manager->tables[i];
        offset += sprintf(report + offset,
            "\nSyntax 块 '%s':\n"
            "  ID: %d\n"
            "  条目数: %d\n"
            "  总匹配: %llu\n"
            "  缓存命中: %llu\n"
            "  优化状态: %s\n",
            table->syntax_name,
            table->syntax_id,
            table->entry_count,
            (unsigned long long)table->total_matches,
            (unsigned long long)table->cache_hits,
            table->is_optimized ? "已优化" : "未优化");
        
        /* 前5个高频匹配的模式 */
        offset += sprintf(report + offset, "  高频模式:\n");
        for (int j = 0; j < (table->entry_count < 5 ? table->entry_count : 5); j++) {
            SyntaxMapEntry *entry = &table->entries[j];
            offset += sprintf(report + offset,
                "    - '%s': %llu 次匹配\n",
                entry->pattern,
                (unsigned long long)entry->match_count);
        }
    }
    
    return report;
}

/**
 * 销毁映射管理器及其所有资源
 */
void syntax_mapper_destroy(SyntaxMapperManager *manager) {
    if (!manager) return;
    
    /* 释放所有映射表 */
    for (int i = 0; i < manager->table_count; i++) {
        SyntaxMappingTable *table = manager->tables[i];
        
        /* 释放条目 */
        for (int j = 0; j < table->entry_count; j++) {
            free(table->entries[j].pattern);
            free(table->entries[j].action);
            if (table->entries[j].deprecation_msg) {
                free(table->entries[j].deprecation_msg);
            }
            if (table->entries[j].description) {
                free(table->entries[j].description);
            }
        }
        
        free(table->entries);
        free(table->sorted_priority_indices);
        free(table->syntax_name);
        free(table);
    }
    
    /* 释放管理器资源 */
    free(manager->tables);
    
    /* 清空缓存 */
    for (int i = 0; i < manager->cache_size; i++) {
        free(manager->lookup_cache[i].syntax_name);
    }
    free(manager->lookup_cache);
    
    free(manager);
}

/* ===================================================================
 * 高级工具函数实现
 * =================================================================== */

/**
 * 推断模式类型
 */
SyntaxPatternType syntax_mapper_infer_pattern_type(const char *pattern_str) {
    if (!pattern_str || pattern_str[0] == '\0') {
        return SYNTAX_PATTERN_LITERAL;
    }
    
    /* 检查特殊字符以推断类型 */
    if (strchr(pattern_str, '*')) {
        return SYNTAX_PATTERN_REGEX;  /* 包含 * 则视为正则 */
    }
    if (pattern_str[strlen(pattern_str) - 1] == '*') {
        return SYNTAX_PATTERN_PREFIX;  /* 以 * 结尾 */
    }
    if (pattern_str[0] == '*') {
        return SYNTAX_PATTERN_SUFFIX;  /* 以 * 开头 */
    }
    
    /* 默认字面匹配 */
    return SYNTAX_PATTERN_LITERAL;
}

/**
 * 推断动作类型
 */
SyntaxActionType syntax_mapper_infer_action_type(const char *action_str) {
    if (!action_str || action_str[0] == '\0') {
        return SYNTAX_ACTION_DIRECT_ASM;
    }
    
    /* 检查动作前缀 */
    if (strncmp(action_str, "call @", 6) == 0) {
        return SYNTAX_ACTION_FUNC_CALL;
    }
    if (strncmp(action_str, "@ir:", 4) == 0) {
        return SYNTAX_ACTION_INLINE_IR;
    }
    if (strncmp(action_str, "@macro:", 7) == 0) {
        return SYNTAX_ACTION_MACRO_EXPAND;
    }
    if (strncmp(action_str, "@deprecated", 11) == 0) {
        return SYNTAX_ACTION_DEPRECATION;
    }
    
    /* 默认汇编代码 */
    return SYNTAX_ACTION_DIRECT_ASM;
}
