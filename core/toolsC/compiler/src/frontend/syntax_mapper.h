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
 * AethelOS 自定义语法映射系统
 * syntax 模式到代码生成的工业级映射引擎
 * 
 * 设计目标：
 * 1. 模式匹配：支持字符串模式和正则表达式匹配
 * 2. 代码生成：动态生成等效的ir代码或机器码
 * 3. 验证系统：确保映射模式的有效性和唯一性
 * 4. 编译优化：第一次编译时计算映射，缓存结果
 * 5. 错误恢复：模式不匹配时的降级处理
 */

#ifndef AEC_SYNTAX_MAPPER_H
#define AEC_SYNTAX_MAPPER_H

#include <stdint.h>
#include <stdbool.h>

/* ===================================================================
 * Syntax 映射数据结构
 * =================================================================== */

/* 模式类型 */
typedef enum {
    SYNTAX_PATTERN_LITERAL,     /* 字面字符串匹配：完整匹配 */
    SYNTAX_PATTERN_PREFIX,      /* 前缀匹配：开头匹配即可 */
    SYNTAX_PATTERN_SUFFIX,      /* 后缀匹配：结尾匹配即可 */
    SYNTAX_PATTERN_SUBSTRING,   /* 子串匹配：任何位置 */
    SYNTAX_PATTERN_REGEX,       /* 正则表达式匹配 */
} SyntaxPatternType;

/* 动作类型 */
typedef enum {
    SYNTAX_ACTION_DIRECT_ASM,       /* 直接汇编代码替换 */
    SYNTAX_ACTION_FUNC_CALL,        /* 函数调用 */
    SYNTAX_ACTION_INLINE_IR,        /* 内联 IR 代码 */
    SYNTAX_ACTION_MACRO_EXPAND,     /* 宏展开 */
    SYNTAX_ACTION_DEPRECATION,      /* 不推荐使用警告 */
} SyntaxActionType;

/* 单个映射条目 */
typedef struct {
    /* 模式部分 */
    char *pattern;              /* 模式字符串 */
    SyntaxPatternType pattern_type; /* 匹配类型 */
    
    /* 动作部分 */
    char *action;               /* 要执行的代码/调用 */
    SyntaxActionType action_type;   /* 动作类型 */
    
    /* 元数据 */
    char *description;          /* 映射的描述 */
    char *deprecation_msg;      /* 弃用消息（如果适用） */
    int priority;               /* 优先级（0-100，高优先级先尝试） */
    bool is_deprecated;         /* 是否已弃用 */
    
    /* 统计信息 */
    uint64_t match_count;       /* 匹配次数统计 */
    uint64_t last_match_line;   /* 最后匹配的行号 */
} SyntaxMapEntry;

/* Syntax 映射表 */
typedef struct {
    char *syntax_name;          /* syntax块的名称 */
    int syntax_id;              /* 全局唯一 ID */
    
    /* 映射条目 */
    SyntaxMapEntry *entries;
    int entry_count;
    int entry_capacity;
    
    /* 优化缓存 */
    int *sorted_priority_indices;   /* 按优先级排序的索引 */
    bool is_optimized;              /* 是否已优化排序 */
    
    /* 作用域追踪 */
    int scope_level;            /* 定义时的作用域层级 */
    uint64_t definition_addr;   /* 定义的源文件地址 */
    
    /* 统计 */
    uint64_t total_matches;     /* 总匹配数 */
    uint64_t cache_hits;        /* 缓存命中数 */
} SyntaxMappingTable;

/* Syntax 映射管理器（全局） */
typedef struct {
    SyntaxMappingTable **tables;     /* 所有 syntax 块的映射表 */
    int table_count;
    int table_capacity;
    
    /* 快速查找 */
    struct {
        char *syntax_name;
        SyntaxMappingTable *table;
    } *lookup_cache;            /* 名称到表的快速缓存 */
    int cache_size;
    
    /* 统计分析 */
    uint64_t total_pattern_matches;
    uint64_t failed_patterns;
    uint64_t deprecated_usage_count;
    
    /* 编译器上下文引用 */
    void *compiler_context;     /* 指向编译器上下文，需要的话 */
} SyntaxMapperManager;

/* ===================================================================
 * 任务 API - 用于处理 AST 节点
 * =================================================================== */

/**
 * 创建新的 Syntax 映射管理器
 */
SyntaxMapperManager* syntax_mapper_create(void);

/**
 * 从 AST syntax_def 节点注册一个 syntax 块
 * 参数：
 *   - manager: 映射管理器
 *   - syntax_name: syntax 块的名称
 *   - patterns: 模式字符串数组
 *   - actions: 对应的动作字符串数组
 *   - pattern_count: 模式/动作对数
 * 返回：
 *   - 新创建的 SyntaxMappingTable，如果注册失败则返回 NULL
 */
SyntaxMappingTable* syntax_mapper_register(
    SyntaxMapperManager *manager,
    const char *syntax_name,
    const char **patterns,
    const char **actions,
    int pattern_count
);

/**
 * 查找匹配的映射条目
 * 参数：
 *   - manager: 映射管理器
 *   - syntax_name: syntax 块的名称
 *   - input_text: 要匹配的输入文本
 *   - matched_entry: 输出参数，返回匹配到的条目
 * 返回：
 *   - 匹配的条目索引，如果没有匹配返回 -1
 */
int syntax_mapper_lookup(
    SyntaxMapperManager *manager,
    const char *syntax_name,
    const char *input_text,
    SyntaxMapEntry **matched_entry
);

/**
 * 验证 syntax 映射的有效性
 * 参数：
 *   - manager: 映射管理器
 *   - syntax_name: 要验证的 syntax 块名称
 * 返回：
 *   - true 表示有效，false 表示存在问题
 */
bool syntax_mapper_validate(
    SyntaxMapperManager *manager,
    const char *syntax_name
);

/**
 * 优化 syntax 映射（按优先级排序）
 * 参数：
 *   - table: 要优化的映射表
 */
void syntax_mapper_optimize(SyntaxMappingTable *table);

/**
 * 获取匹配统计
 * 参数：
 *   - table: 映射表
 * 返回：
 *   - 该表的总匹配数
 */
uint64_t syntax_mapper_get_total_matches(const SyntaxMappingTable *table);

/**
 * 生成性能报告
 * 参数：
 *   - manager: 映射管理器
 * 返回：
 *   - 格式化的性能报告字符串
 */
char* syntax_mapper_generate_report(const SyntaxMapperManager *manager);

/**
 * 销毁映射管理器及其所有资源
 * 参数：
 *   - manager: 要销毁的管理器
 */
void syntax_mapper_destroy(SyntaxMapperManager *manager);

/* ===================================================================
 * 高级工具函数
 * =================================================================== */

/**
 * 推断模式类型（从动作字符串推断应该使用的模式匹配类型）
 * 参数：
 *   - pattern_str: 模式字符串
 * 返回：
 *   - 推断出的模式类型
 */
SyntaxPatternType syntax_mapper_infer_pattern_type(const char *pattern_str);

/**
 * 推断动作类型（从动作字符串推断应该使用的动作处理类型）
 * 参数：
 *   - action_str: 动作字符串
 * 返回：
 *   - 推断出的动作类型
 */
SyntaxActionType syntax_mapper_infer_action_type(const char *action_str);

#endif /* AEC_SYNTAX_MAPPER_H */
