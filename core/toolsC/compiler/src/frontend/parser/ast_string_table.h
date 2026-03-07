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
 * AethelOS Aethelium Compiler - AST String Table
 * 工业级字符串表实现：支持高效的字符串存储、去重和查询
 *
 * 设计目标：
 * 1. 字符串去重：相同内容的字符串共享存储
 * 2. 高性能查询：O(1) 平均查询时间
 * 3. 内存高效：采用内存池和哈希表结合
 * 4. 工业可靠：完整的错误处理和边界检查
 * 5. 易于集成：与AST无缝协作
 */

#ifndef AST_STRING_TABLE_H
#define AST_STRING_TABLE_H

#include <stdint.h>
#include <stddef.h>

/* =====================================================================
 * 字符串ID定义
 * 
 * 采用32位ID，格式：
 *   - 高1位：保留（用于特殊标记）
 *   - 低31位：字符串在表中的索引
 * 
 * 特殊ID：
 *   - 0x00000000: 空字符串（NULL 字符串）
 *   - 0x00000001: 无效ID，表示查询失败
 * ===================================================================== */

typedef uint32_t StringId;

#define STRING_ID_NULL      0x00000000  /* 空字符串ID */
#define STRING_ID_INVALID   0x00000001  /* 无效ID */
#define STRING_ID_MAX       0x7FFFFFFF  /* 最大有效ID */

/* 检查StringId是否有效 */
#define STRING_ID_IS_VALID(id) ((id) > STRING_ID_INVALID && (id) <= STRING_ID_MAX)
#define STRING_ID_IS_NULL(id)  ((id) == STRING_ID_NULL)

/* =====================================================================
 * 字符串条目结构
 * ===================================================================== */

typedef struct {
    char        *data;           /* 字符串数据指针 */
    uint32_t    length;          /* 字符串长度（不含\0） */
    uint32_t    hash;            /* 预计算的哈希值 */
    uint32_t    ref_count;       /* 引用计数 */
    uint16_t    flags;           /* 标志位 */
    uint16_t    reserved;        /* 保留字段 */
} StringEntry;

#define STRING_FLAG_INTERNED    0x0001  /* 字符串被内化（去重） */
#define STRING_FLAG_READONLY    0x0002  /* 只读字符串 */
#define STRING_FLAG_EXTERNAL    0x0004  /* 外部字符串（不由表管理内存） */

/* =====================================================================
 * 字符串表配置参数
 * ===================================================================== */

typedef struct {
    /* 内存池配置 */
    uint32_t    initial_pool_size;      /* 初始内存池大小 */
    uint32_t    pool_grow_factor;       /* 内存池增长因子（百分比） */
    uint32_t    max_pool_size;          /* 内存池最大大小 */
    
    /* 哈希表配置 */
    uint32_t    initial_capacity;       /* 初始容量（条目数） */
    float       load_factor_threshold;  /* 负载因子阈值（0.0-1.0） */
    float       shrink_threshold;       /* 缩小阈值（0.0-1.0） */
    
    /* 去重配置 */
    int         enable_dedup;           /* 启用字符串去重 */
    uint32_t    min_length_for_dedup;   /* 去重最小长度 */
    
    /* 统计和调试 */
    int         enable_stats;           /* 启用统计信息 */
    int         enable_trace;           /* 启用追踪输出 */
} StringTableConfig;

/* =====================================================================
 * 字符串表结构
 * ===================================================================== */

typedef struct StringTable StringTable;

/* =====================================================================
 * 接口函数声明
 * ===================================================================== */

/* 配置管理 */
StringTableConfig* stringtable_config_create();
void stringtable_config_destroy(StringTableConfig *config);
void stringtable_config_set_defaults(StringTableConfig *config);
void stringtable_config_print(const StringTableConfig *config);

/* 创建和销毁 */
StringTable* stringtable_create(void);
StringTable* stringtable_create_with_config(const StringTableConfig *config);
void stringtable_destroy(StringTable *table);

/* 核心操作 */

/**
 * 向字符串表中添加或查询字符串
 * 
 * 如果字符串已存在，返回其ID（支持去重）
 * 如果字符串为新增，分配ID并存储
 * 
 * @param table     字符串表指针
 * @param str       字符串指针（以\0结尾）
 * @return          字符串ID，失败返回STRING_ID_INVALID
 */
StringId stringtable_add(StringTable *table, const char *str);

/**
 * 添加指定长度的字符串（不要求以\0结尾）
 * 
 * @param table     字符串表指针
 * @param str       字符串指针
 * @param length    字符串长度
 * @return          字符串ID，失败返回STRING_ID_INVALID
 */
StringId stringtable_add_len(StringTable *table, const char *str, uint32_t length);

/**
 * 根据ID查询字符串
 * 
 * @param table     字符串表指针
 * @param id        字符串ID
 * @return          字符串指针，无效ID返回NULL
 */
const char* stringtable_get(StringTable *table, StringId id);

/**
 * 根据ID查询字符串长度
 * 
 * @param table     字符串表指针
 * @param id        字符串ID
 * @return          字符串长度，无效ID返回0
 */
uint32_t stringtable_get_length(StringTable *table, StringId id);

/**
 * 查询字符串是否存在
 * 
 * @param table     字符串表指针
 * @param str       字符串指针
 * @return          存在返回对应ID，不存在返回STRING_ID_INVALID
 */
StringId stringtable_lookup(StringTable *table, const char *str);

/**
 * 查询指定长度的字符串
 * 
 * @param table     字符串表指针
 * @param str       字符串指针
 * @param length    字符串长度
 * @return          存在返回对应ID，不存在返回STRING_ID_INVALID
 */
StringId stringtable_lookup_len(StringTable *table, const char *str, uint32_t length);

/**
 * 比较两个字符串ID
 * 
 * @param table     字符串表指针
 * @param id1       第一个字符串ID
 * @param id2       第二个字符串ID
 * @return          相同返回1，不同或错误返回0
 */
int stringtable_compare_ids(StringTable *table, StringId id1, StringId id2);

/* 内存管理 */

/**
 * 进行垃圾收集（基于引用计数）
 * 
 * @param table     字符串表指针
 * @return          回收字符串数量
 */
uint32_t stringtable_garbage_collect(StringTable *table);

/**
 * 清空所有字符串
 * 
 * @param table     字符串表指针
 */
void stringtable_clear(StringTable *table);

/**
 * 优化字符串表（压缩内存、重建哈希表）
 * 
 * @param table     字符串表指针
 * @return          成功返回0，失败返回-1
 */
int stringtable_compact(StringTable *table);

/* 统计和调试 */

typedef struct {
    uint32_t    total_entries;          /* 总条目数 */
    uint32_t    unique_strings;         /* 不同字符串数 */
    uint64_t    total_bytes;            /* 总字节数 */
    uint32_t    hash_collisions;        /* 哈希碰撞次数 */
    float       avg_chain_length;       /* 平均链长 */
    float       current_load_factor;    /* 当前负载因子 */
    uint32_t    max_chain_length;       /* 最长链长 */
    uint32_t    dedup_hits;             /* 去重命中次数 */
    uint32_t    dedup_misses;           /* 去重未命中次数 */
} StringTableStats;

/**
 * 获取字符串表统计信息
 * 
 * @param table     字符串表指针
 * @param stats     统计信息输出指针
 * @return          成功返回0，失败返回-1
 */
int stringtable_get_stats(StringTable *table, StringTableStats *stats);

/**
 * 打印字符串表统计信息
 * 
 * @param table     字符串表指针
 */
void stringtable_print_stats(StringTable *table);

/**
 * 验证字符串表的一致性
 * 
 * @param table     字符串表指针
 * @return          有效返回0，无效返回-1，并输出错误信息
 */
int stringtable_validate(StringTable *table);

/* 导出和导入 */

/**
 * 获取字符串表中的所有字符串ID（用于遍历）
 * 
 * @param table     字符串表指针
 * @param ids       ID数组输出指针
 * @param capacity  数组容量
 * @return          实际ID数量
 */
uint32_t stringtable_get_all_ids(StringTable *table, StringId *ids, uint32_t capacity);

/**
 * 转储字符串表到文件（调试用）
 * 
 * @param table     字符串表指针
 * @param filename  输出文件路径
 * @return          成功返回0，失败返回-1
 */
int stringtable_dump_to_file(StringTable *table, const char *filename);

#endif /* AST_STRING_TABLE_H */
