/*
 * AethelOS Aethelium Compiler - AST String Table Implementation
 * 工业级字符串表实现
 */

#include "ast_string_table.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <errno.h>

/* =====================================================================
 * 内部常量和底层数据结构
 * ===================================================================== */

/* 哈希表链表节点 */
typedef struct StringHashNode {
    StringId                id;          /* 该条目的ID */
    struct StringHashNode   *next;       /* 链表下一个节点 */
} StringHashNode;

/* 私有字符串表结构 */
struct StringTable {
    /* 条目数组 */
    StringEntry         *entries;       /* 字符串条目数组 */
    uint32_t            entry_count;    /* 当前条目数 */
    uint32_t            entry_capacity; /* 条目数组容量 */
    
    /* 哈希表 */
    StringHashNode      **hash_table;   /* 哈希表（链表数组） */
    uint32_t            hash_capacity;  /* 哈希表容量 */
    uint32_t            hash_collisions;/* 碰撞统计 */
    
    /* 内存池 */
    char                *pool;          /* 内存池 */
    uint32_t            pool_used;      /* 已使用大小 */
    uint32_t            pool_size;      /* 内存池总大小 */
    
    /* 配置 */
    StringTableConfig   config;         /* 配置参数 */
    
    /* 统计 */
    uint32_t            dedup_hits;     /* 去重命中次数 */
    uint32_t            dedup_misses;   /* 去重未命中次数 */
};

/* =====================================================================
 * 哈希函数：MurmurHash3 的简化版本
 * ===================================================================== */

static uint32_t hash_murmur3_32(const char *data, uint32_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t hash = 0;
    
    /* 处理完整的4字节块 */
    uint32_t nblocks = len / 4;
    for (uint32_t i = 0; i < nblocks; i++) {
        uint32_t k = *(uint32_t *)&bytes[i * 4];
        
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        
        hash ^= k;
        hash = (hash << 13) | (hash >> 19);
        hash = hash * 5 + 0xe6546b64;
    }
    
    /* 处理剩余字节 */
    const uint8_t *tail = &bytes[nblocks * 4];
    uint32_t k1 = 0;
    
    switch (len & 3) {
        case 3: k1 ^= tail[2] << 16; /* Fall through */
        case 2: k1 ^= tail[1] << 8;  /* Fall through */
        case 1:
            k1 ^= tail[0];
            k1 *= 0xcc9e2d51;
            k1 = (k1 << 15) | (k1 >> 17);
            k1 *= 0x1b873593;
            hash ^= k1;
    }
    
    /* 混合最终的哈希值 */
    hash ^= len;
    
    /* fmix32 */
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;
    
    return hash;
}

/* =====================================================================
 * 配置管理
 * ===================================================================== */

StringTableConfig* stringtable_config_create(void) {
    StringTableConfig *config = (StringTableConfig *)malloc(sizeof(StringTableConfig));
    if (!config) return NULL;
    stringtable_config_set_defaults(config);
    return config;
}

void stringtable_config_destroy(StringTableConfig *config) {
    if (config) free(config);
}

void stringtable_config_set_defaults(StringTableConfig *config) {
    if (!config) return;
    
    /* 内存池默认配置：初始8MB，最大512MB */
    config->initial_pool_size = 8 * 1024 * 1024;      /* 8MB */
    config->pool_grow_factor = 150;                    /* 增长因子150% */
    config->max_pool_size = 512 * 1024 * 1024;        /* 512MB */
    
    /* 哈希表默认配置 */
    config->initial_capacity = 8191;                   /* 质数，容量 8K */
    config->load_factor_threshold = 0.75f;             /* 负载因子 75% */
    config->shrink_threshold = 0.1f;                   /* 缩小阈值 10% */
    
    /* 去重配置 */
    config->enable_dedup = 1;                          /* 启用去重 */
    config->min_length_for_dedup = 4;                  /* 4字符以上去重 */
    
    /* 统计和调试 */
    config->enable_stats = 1;                          /* 启用统计 */
    config->enable_trace = 0;                          /* 不启用追踪 */
}

void stringtable_config_print(const StringTableConfig *config) {
    if (!config) return;
    
    printf("=== String Table Configuration ===\n");
    printf("  Pool: initial=%u KB, grow=%u%%, max=%u KB\n",
           config->initial_pool_size / 1024,
           config->pool_grow_factor,
           config->max_pool_size / 1024);
    printf("  Hash: capacity=%u, load_factor=%.2f, shrink=%.2f\n",
           config->initial_capacity,
           config->load_factor_threshold,
           config->shrink_threshold);
    printf("  Dedup: %s, min_length=%u\n",
           config->enable_dedup ? "enabled" : "disabled",
           config->min_length_for_dedup);
    printf("  Debug: stats=%s, trace=%s\n",
           config->enable_stats ? "yes" : "no",
           config->enable_trace ? "yes" : "no");
}

/* =====================================================================
 * 创建和销毁
 * ===================================================================== */

StringTable* stringtable_create(void) {
    StringTableConfig config;
    stringtable_config_set_defaults(&config);
    return stringtable_create_with_config(&config);
}

StringTable* stringtable_create_with_config(const StringTableConfig *config) {
    StringTable *table = (StringTable *)malloc(sizeof(StringTable));
    if (!table) {
        fprintf(stderr, "[ERROR] Failed to allocate StringTable\n");
        return NULL;
    }
    
    memset(table, 0, sizeof(StringTable));
    
    if (config) {
        memcpy(&table->config, config, sizeof(StringTableConfig));
    } else {
        stringtable_config_set_defaults(&table->config);
    }
    
    /* 初始化条目数组 */
    table->entry_capacity = table->config.initial_capacity;
    table->entries = (StringEntry *)malloc(sizeof(StringEntry) * table->entry_capacity);
    if (!table->entries) {
        fprintf(stderr, "[ERROR] Failed to allocate entries array\n");
        free(table);
        return NULL;
    }
    memset(table->entries, 0, sizeof(StringEntry) * table->entry_capacity);
    
    /* 条目0保留作为NULL字符串 */
    table->entries[0].data = "";
    table->entries[0].length = 0;
    table->entries[0].hash = 0;
    table->entries[0].ref_count = 1;
    table->entry_count = 1;
    
    /* 初始化哈希表 */
    table->hash_capacity = table->config.initial_capacity;
    table->hash_table = (StringHashNode **)malloc(sizeof(StringHashNode *) * table->hash_capacity);
    if (!table->hash_table) {
        fprintf(stderr, "[ERROR] Failed to allocate hash table\n");
        free(table->entries);
        free(table);
        return NULL;
    }
    memset(table->hash_table, 0, sizeof(StringHashNode *) * table->hash_capacity);
    
    /* 初始化哈希表条目0 */
    StringHashNode *node = (StringHashNode *)malloc(sizeof(StringHashNode));
    if (!node) {
        fprintf(stderr, "[ERROR] Failed to allocate hash node\n");
        free(table->hash_table);
        free(table->entries);
        free(table);
        return NULL;
    }
    node->id = 0;
    node->next = NULL;
    table->hash_table[0] = node;
    
    /* 初始化内存池 */
    table->pool_size = table->config.initial_pool_size;
    table->pool = (char *)malloc(table->pool_size);
    if (!table->pool) {
        fprintf(stderr, "[ERROR] Failed to allocate string pool (size=%u)\n", 
                table->pool_size);
        free(table->hash_table);
        free(table->entries);
        free(table);
        return NULL;
    }
    table->pool_used = 0;
    
    if (table->config.enable_stats) {
        fprintf(stderr, "[INFO] String Table initialized: capacity=%u, pool=%u MB\n",
                table->entry_capacity, table->pool_size / (1024*1024));
    }
    
    return table;
}

void stringtable_destroy(StringTable *table) {
    if (!table) return;
    
    /* 释放所有哈希表链表节点 */
    if (table->hash_table) {
        for (uint32_t i = 0; i < table->hash_capacity; i++) {
            StringHashNode *node = table->hash_table[i];
            while (node) {
                StringHashNode *next = node->next;
                free(node);
                node = next;
            }
        }
        free(table->hash_table);
    }
    
    /* 释放条目数组 */
    if (table->entries) {
        free(table->entries);
    }
    
    /* 释放内存池 */
    if (table->pool) {
        free(table->pool);
    }
    
    free(table);
}

/* =====================================================================
 * 内存池管理
 * ===================================================================== */

static int stringtable_expand_pool(StringTable *table, uint32_t required_size) {
    if (!table) return -1;
    
    if (table->pool_used + required_size <= table->pool_size) {
        return 0; /* 已有足够空间 */
    }
    
    uint32_t new_size = table->pool_size;
    
    /* 计算所需的新大小 */
    while (new_size < table->pool_used + required_size) {
        uint64_t grow_size = (uint64_t)new_size * table->config.pool_grow_factor / 100;
        new_size = (uint32_t)grow_size;
        if (new_size <= table->pool_size) {
            new_size = table->pool_size * 2; /* 防止溢出 */
        }
    }
    
    /* 检查最大限制 */
    if (new_size > table->config.max_pool_size) {
        fprintf(stderr, "[ERROR] String pool would exceed max size: %u > %u\n",
                new_size, table->config.max_pool_size);
        return -1;
    }
    
    /* 重新分配 */
    char *new_pool = (char *)realloc(table->pool, new_size);
    if (!new_pool) {
        fprintf(stderr, "[ERROR] Failed to expand string pool to %u bytes\n", new_size);
        return -1;
    }
    
    if (table->config.enable_stats) {
        fprintf(stderr, "[INFO] String pool expanded: %u -> %u MB\n",
                table->pool_size / (1024*1024), new_size / (1024*1024));
    }
    
    table->pool = new_pool;
    table->pool_size = new_size;
    return 0;
}

/* =====================================================================
 * 哈希表管理
 * ===================================================================== */

static int stringtable_rebuild_hash_table(StringTable *table) {
    if (!table || !table->entries) return -1;
    
    /* 释放旧哈希表 */
    if (table->hash_table) {
        for (uint32_t i = 0; i < table->hash_capacity; i++) {
            StringHashNode *node = table->hash_table[i];
            while (node) {
                StringHashNode *next = node->next;
                free(node);
                node = next;
            }
        }
        free(table->hash_table);
    }
    
    /* 创建新哈希表（增加容量到下一个质数） */
    uint32_t old_capacity = table->hash_capacity;
    table->hash_capacity = old_capacity * 2 - 1; /* 简单的质数化 */
    
    /* 常用的质数列表 */
    static const uint32_t primes[] = {
        8191, 16381, 32749, 65521, 131071, 262139, 524287, 1048573,
        2097143, 4194301, 8388593, 16777213, 33554393, 67108859
    };
    for (int i = 0; i < (int)(sizeof(primes) / sizeof(primes[0])); i++) {
        if (primes[i] > old_capacity) {
            table->hash_capacity = primes[i];
            break;
        }
    }
    
    table->hash_table = (StringHashNode **)malloc(sizeof(StringHashNode *) * table->hash_capacity);
    if (!table->hash_table) {
        fprintf(stderr, "[ERROR] Failed to allocate new hash table (capacity=%u)\n",
                table->hash_capacity);
        table->hash_capacity = old_capacity;
        return -1;
    }
    memset(table->hash_table, 0, sizeof(StringHashNode *) * table->hash_capacity);
    
    /* 重新插入所有条目 */
    for (uint32_t i = 0; i < table->entry_count; i++) {
        if (table->entries[i].data == NULL) continue;
        
        uint32_t bucket = table->entries[i].hash % table->hash_capacity;
        StringHashNode *node = (StringHashNode *)malloc(sizeof(StringHashNode));
        if (!node) {
            fprintf(stderr, "[ERROR] Failed to allocate hash node during rebuild\n");
            return -1;
        }
        
        node->id = i;
        node->next = table->hash_table[bucket];
        table->hash_table[bucket] = node;
    }
    
    if (table->config.enable_stats) {
        fprintf(stderr, "[INFO] Hash table rebuilt: capacity %u -> %u\n",
                old_capacity, table->hash_capacity);
    }
    
    table->hash_collisions = 0;
    return 0;
}

/* =====================================================================
 * 核心操作：添加和查询字符串
 * ===================================================================== */

StringId stringtable_add_len(StringTable *table, const char *str, uint32_t length) {
    if (!table || !str || length > 1024*1024) {
        return STRING_ID_INVALID;
    }
    
    if (length == 0) {
        return STRING_ID_NULL; /* 空字符串使用NULL ID */
    }
    
    /* 计算哈希值 */
    uint32_t hash = hash_murmur3_32(str, length);
    
    /* 尝试查找已存在的字符串（启用去重） */
    if (table->config.enable_dedup && length >= table->config.min_length_for_dedup) {
        uint32_t bucket = hash % table->hash_capacity;
        StringHashNode *node = table->hash_table[bucket];
        
        while (node) {
            StringEntry *entry = &table->entries[node->id];
            if (entry->hash == hash && 
                entry->length == length &&
                memcmp(entry->data, str, length) == 0) {
                /* 找到匹配的字符串 */
                entry->ref_count++;
                table->dedup_hits++;
                
                if (table->config.enable_trace) {
                    fprintf(stderr, "[TRACE] String '%.*s' dedup hit (id=%u)\n",
                            length, str, node->id);
                }
                
                return node->id;
            }
            node = node->next;
        }
        table->dedup_misses++;
    }
    
    /* 检查是否需要扩展条目数组 */
    if (table->entry_count >= table->entry_capacity) {
        uint32_t new_capacity = table->entry_capacity * 2;
        StringEntry *new_entries = (StringEntry *)realloc(table->entries,
                                                         sizeof(StringEntry) * new_capacity);
        if (!new_entries) {
            fprintf(stderr, "[ERROR] Failed to expand entries array to %u\n", new_capacity);
            return STRING_ID_INVALID;
        }
        memset(&new_entries[table->entry_capacity], 0,
               sizeof(StringEntry) * (new_capacity - table->entry_capacity));
        table->entries = new_entries;
        table->entry_capacity = new_capacity;
    }
    
    /* 检查是否需要扩展内存池 */
    if (stringtable_expand_pool(table, length + 1) != 0) {
        return STRING_ID_INVALID;
    }
    
    /* 在内存池中分配字符串 */
    char *pool_str = table->pool + table->pool_used;
    memcpy(pool_str, str, length);
    pool_str[length] = '\0';
    
    uint32_t id = table->entry_count;
    StringEntry *entry = &table->entries[id];
    entry->data = pool_str;
    entry->length = length;
    entry->hash = hash;
    entry->ref_count = 1;
    entry->flags = STRING_FLAG_INTERNED;
    
    table->pool_used += length + 1;
    table->entry_count++;
    
    /* 检查负载因子，如果需要则重建哈希表 */
    float load_factor = (float)table->entry_count / table->hash_capacity;
    if (load_factor > table->config.load_factor_threshold) {
        if (stringtable_rebuild_hash_table(table) != 0) {
            /* 重建失败，但字符串已添加，仍然返回ID */
            fprintf(stderr, "[WARN] Hash table rebuild failed, performance may degrade\n");
        }
    }
    
    /* 将新条目添加到哈希表 */
    uint32_t bucket = hash % table->hash_capacity;
    StringHashNode *node = (StringHashNode *)malloc(sizeof(StringHashNode));
    if (!node) {
        fprintf(stderr, "[ERROR] Failed to allocate hash node\n");
        return STRING_ID_INVALID;
    }
    
    node->id = id;
    node->next = table->hash_table[bucket];
    table->hash_table[bucket] = node;
    
    /* 计数碰撞 */
    if (table->hash_table[bucket]->next != NULL) {
        table->hash_collisions++;
    }
    
    if (table->config.enable_trace) {
        fprintf(stderr, "[TRACE] Added string '%.*s' at id=%u, hash=%u\n",
                length, str, id, hash);
    }
    
    return id;
}

StringId stringtable_add(StringTable *table, const char *str) {
    if (!str) return STRING_ID_NULL;
    return stringtable_add_len(table, str, strlen(str));
}

const char* stringtable_get(StringTable *table, StringId id) {
    if (!table || !STRING_ID_IS_VALID(id) || id >= table->entry_count) {
        return NULL;
    }
    return table->entries[id].data;
}

uint32_t stringtable_get_length(StringTable *table, StringId id) {
    if (!table || !STRING_ID_IS_VALID(id) || id >= table->entry_count) {
        return 0;
    }
    return table->entries[id].length;
}

StringId stringtable_lookup(StringTable *table, const char *str) {
    if (!str) return STRING_ID_NULL;
    return stringtable_lookup_len(table, str, strlen(str));
}

StringId stringtable_lookup_len(StringTable *table, const char *str, uint32_t length) {
    if (!table || !str || length == 0) {
        return STRING_ID_NULL;
    }
    
    uint32_t hash = hash_murmur3_32(str, length);
    uint32_t bucket = hash % table->hash_capacity;
    StringHashNode *node = table->hash_table[bucket];
    
    while (node) {
        StringEntry *entry = &table->entries[node->id];
        if (entry->hash == hash &&
            entry->length == length &&
            memcmp(entry->data, str, length) == 0) {
            return node->id;
        }
        node = node->next;
    }
    
    return STRING_ID_INVALID;
}

int stringtable_compare_ids(StringTable *table, StringId id1, StringId id2) {
    if (!table) return 0;
    
    if (id1 == id2) return 1;
    if (!STRING_ID_IS_VALID(id1) || !STRING_ID_IS_VALID(id2)) return 0;
    if (id1 >= table->entry_count || id2 >= table->entry_count) return 0;
    
    StringEntry *e1 = &table->entries[id1];
    StringEntry *e2 = &table->entries[id2];
    
    if (e1->length != e2->length) return 0;
    return memcmp(e1->data, e2->data, e1->length) == 0;
}

/* =====================================================================
 * 内存管理：垃圾收集和清理
 * ===================================================================== */

uint32_t stringtable_garbage_collect(StringTable *table) {
    if (!table) return 0;
    
    /* 当前实现不支持真正的GC，因为所有字符串都在池中
     * 实际项目可能需要实现带移动的分代GC
     * 这里只是占位符实现 */
    return 0;
}

void stringtable_clear(StringTable *table) {
    if (!table) return;
    
    /* 清理哈希表 */
    if (table->hash_table) {
        for (uint32_t i = 0; i < table->hash_capacity; i++) {
            StringHashNode *node = table->hash_table[i];
            while (node) {
                StringHashNode *next = node->next;
                free(node);
                node = next;
            }
            table->hash_table[i] = NULL;
        }
    }
    
    /* 清理条目数组（保留第0个NULL条目） */
    if (table->entries && table->entry_count > 1) {
        memset(&table->entries[1], 0, sizeof(StringEntry) * (table->entry_count - 1));
    }
    
    table->entry_count = 1;
    table->pool_used = 0;
    table->hash_collisions = 0;
    table->dedup_hits = 0;
    table->dedup_misses = 0;
}

int stringtable_compact(StringTable *table) {
    if (!table) return -1;
    
    /* 重建哈希表以清理碰撞链表中的冗余节点
     * 在实际项目中，可能还需要压缩内存池 */
    return stringtable_rebuild_hash_table(table);
}

/* =====================================================================
 * 统计和调试
 * ===================================================================== */

int stringtable_get_stats(StringTable *table, StringTableStats *stats) {
    if (!table || !stats) return -1;
    
    memset(stats, 0, sizeof(StringTableStats));
    
    stats->total_entries = table->entry_count;
    stats->unique_strings = table->entry_count;
    stats->total_bytes = table->pool_used;
    stats->hash_collisions = table->hash_collisions;
    stats->dedup_hits = table->dedup_hits;
    stats->dedup_misses = table->dedup_misses;
    
    /* 计算负载因子和链长统计 */
    stats->current_load_factor = (float)table->entry_count / table->hash_capacity;
    
    uint32_t non_empty_buckets = 0;
    for (uint32_t i = 0; i < table->hash_capacity; i++) {
        if (table->hash_table[i] != NULL) {
            non_empty_buckets++;
            
            int chain_length = 0;
            StringHashNode *node = table->hash_table[i];
            while (node) {
                chain_length++;
                node = node->next;
            }
            
            if (chain_length > (int)stats->max_chain_length) {
                stats->max_chain_length = chain_length;
            }
        }
    }
    
    if (non_empty_buckets > 0) {
        stats->avg_chain_length = (float)table->entry_count / non_empty_buckets;
    }
    
    return 0;
}

void stringtable_print_stats(StringTable *table) {
    if (!table) return;
    
    StringTableStats stats;
    if (stringtable_get_stats(table, &stats) != 0) {
        fprintf(stderr, "[ERROR] Failed to get statistics\n");
        return;
    }
    
    printf("=== String Table Statistics ===\n");
    printf("  Entries: %u (capacity: %u)\n", stats.total_entries, table->entry_capacity);
    printf("  Memory: %llu bytes (%.2f MB)\n", stats.total_bytes,
           (double)stats.total_bytes / (1024 * 1024));
    printf("  Hash: capacity=%u, load_factor=%.3f, avg_chain=%.2f, max_chain=%u\n",
           table->hash_capacity, stats.current_load_factor,
           stats.avg_chain_length, stats.max_chain_length);
    printf("  Collisions: %u\n", stats.hash_collisions);
    printf("  Dedup: hits=%u, misses=%u (ratio=%.2f%%)\n",
           stats.dedup_hits, stats.dedup_misses,
           stats.dedup_hits + stats.dedup_misses > 0 ?
               (float)stats.dedup_hits * 100 / (stats.dedup_hits + stats.dedup_misses) : 0.0f);
}

int stringtable_validate(StringTable *table) {
    if (!table) {
        fprintf(stderr, "[ERROR] NULL table pointer\n");
        return -1;
    }
    
    /* 验证条目数组 */
    if (!table->entries || table->entry_count == 0) {
        fprintf(stderr, "[ERROR] Invalid entries array\n");
        return -1;
    }
    
    /* 验证ID一致性 */
    for (uint32_t i = 0; i < table->hash_capacity; i++) {
        StringHashNode *node = table->hash_table[i];
        while (node) {
            if (node->id >= table->entry_count) {
                fprintf(stderr, "[ERROR] Invalid ID %u in bucket %u (max=%u)\n",
                        node->id, i, table->entry_count - 1);
                return -1;
            }
            node = node->next;
        }
    }
    
    /* 验证内存池 */
    if (table->pool_used > table->pool_size) {
        fprintf(stderr, "[ERROR] Pool overflow: used=%u > size=%u\n",
                table->pool_used, table->pool_size);
        return -1;
    }
    
    return 0;
}

uint32_t stringtable_get_all_ids(StringTable *table, StringId *ids, uint32_t capacity) {
    if (!table || !ids || capacity == 0) return 0;
    
    uint32_t count = 0;
    for (uint32_t i = 0; i < table->entry_count && count < capacity; i++) {
        if (table->entries[i].data != NULL) {
            ids[count++] = i;
        }
    }
    
    return count;
}

int stringtable_dump_to_file(StringTable *table, const char *filename) {
    if (!table || !filename) return -1;
    
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "[ERROR] Failed to open dump file: %s\n", filename);
        return -1;
    }
    
    fprintf(f, "=== AethelOS String Table Dump ===\n");
    fprintf(f, "Total entries: %u\n\n", table->entry_count);
    
    for (uint32_t i = 0; i < table->entry_count; i++) {
        if (table->entries[i].data != NULL) {
            fprintf(f, "ID %u: \"%s\" (len=%u, hash=%u, refs=%u)\n",
                    i, table->entries[i].data, table->entries[i].length,
                    table->entries[i].hash, table->entries[i].ref_count);
        }
    }
    
    fclose(f);
    return 0;
}
