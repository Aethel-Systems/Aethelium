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
 * AethelFS (ÆFS) - 完整的集成存储架构实现
 * 基于 AEFS.txt 设计文档的严格规范实现
 *
 * 该头文件定义了ÆFS的核心数据结构，包括：
 * 1. 物理层 (ADL - Aethel Disk Layout)
 * 2. 存储池层 (ÆFS Partition with Log-Structured File System)
 * 3. 逻辑层 (Aethel Volume)
 *
 * 编译后的ÆFS卷可以被EFI引导程序或内核识别，没有挂载。
 */

#ifndef AEFS_H
#define AEFS_H

#include <stdint.h>
#include <string.h>
#include "adl_disk.h"

/* 结构体打包宏 */
#define PACKED __attribute__((packed))

/* ============================================================================
 * 全局常量定义
 * ============================================================================ */

/* 魔数 - 标识ÆFS结构 */
#define AETHEL_DISK_ADL_MAGIC 0x4452445F4C544541ULL  /* "_ELTDRLA" (小端: "AEDL") */
#define AEFS_MAGIC 0x53465341UL                       /* "ASFS" */
#define AEFS_PARTITION_MAGIC 0x46534541UL             /* "AESF" */
#define AEFS_ANCHOR_MAGIC 0x524F434E41455341ULL       /* "ASANCOR" */
#define AEFS_CHECKPOINT_MAGIC 0x4B50434841455341ULL   /* "ASACHPK" */
#define AEFS_NODE_MAGIC 0x444F4E45535341ULL           /* "ASSENOD" */

/* 版本号 */
#define ADL_VERSION 1
#define AEFS_VERSION 1
#define AEFS_ANCHOR_VERSION 1
#define AEFS_CHECKPOINT_VERSION 1

/* 块大小 (标准4KB) */
#define AEFS_BLOCK_SIZE 4096
#define AEFS_BLOCK_SHIFT 12

/* 段大小 (2MB) - 日志结构的基本单位 */
#define AEFS_SEGMENT_SIZE (2 * 1024 * 1024)
#define AEFS_SEGMENT_SHIFT 21
#define AEFS_BLOCKS_PER_SEGMENT (AEFS_SEGMENT_SIZE / AEFS_BLOCK_SIZE)

/* 校验和算法 */
#define AEFS_CHECKSUM_INIT 0
#define AEFS_CHECKSUM_SIZE 4

/* 硬件配置文件 (Media Profile) */
#define AEFS_MEDIA_UNKNOWN 0
#define AEFS_MEDIA_SSD_NVME 1
#define AEFS_MEDIA_SSD_SATA 2
#define AEFS_MEDIA_HDD 3
#define AEFS_MEDIA_RAM 4

/* 分区类型UUID (标准UUID格式，这里用简化的定义) */
#define AEFS_PTYPE_POOL 0x4150554C50534541ULL         /* "AEPSLUPOA" */
#define AEFS_PTYPE_BOOT 0x544F4F42504541ULL           /* "AEPBOOT" */
#define AEFS_PTYPE_SWAP 0x50415753504541ULL           /* "AEPSSWAP" */

/* 分区标志 */
#define ADL_FLAG_READONLY 0x0001
#define ADL_FLAG_HIDDEN 0x0002
#define ADL_FLAG_BOOTABLE 0x0004
#define ADL_FLAG_MIRRORED 0x0008

/* 韧性策略 */
#define AEFS_RESILIENCE_INDEPENDENT 0
#define AEFS_RESILIENCE_MIRRORED 1
#define AEFS_RESILIENCE_STRIPED 2

/* Volume属性 */
#define AEFS_VOLUME_COMPRESSION_OFF 0
#define AEFS_VOLUME_COMPRESSION_ZSTD 1
#define AEFS_VOLUME_COMPRESSION_LZ4 2

/* Æ-Node类型 */
#define AEFS_INODE_TYPE_REGULAR_FILE 1
#define AEFS_INODE_TYPE_DIRECTORY 2
#define AEFS_INODE_TYPE_SYMLINK 3
#define AEFS_INODE_TYPE_DEVICE 4
#define AEFS_INODE_TYPE_FIFO 5
#define AEFS_INODE_TYPE_SOCKET 6

/* GC (垃圾回收) 策略 */
#define AEFS_GC_PRIORITY_LOWEST 0
#define AEFS_GC_PRIORITY_LOW 1
#define AEFS_GC_PRIORITY_NORMAL 2
#define AEFS_GC_PRIORITY_HIGH 3

/* ============================================================================
 * 2. 物理层：Aethel Disk Layout (ADL)
 * ============================================================================ */

/* 2.1 Aethel Disk Header */
typedef struct {
    uint64_t magic_number;              /* 固定为 AETHEL_DISK_ADL_MAGIC */
    uint8_t disk_uuid[16];              /* 磁盘的128位唯一标识符 */
    uint32_t version;                   /* ADL格式版本 */
    uint32_t block_size;                /* 逻辑块大小 (通常 4096) */
    uint64_t total_blocks;              /* 磁盘总块数 */
    uint64_t partition_table_lba;       /* 主分区表起始LBA */
    uint64_t partition_table_backup_lba;/* 备份分区表起始LBA */
    uint8_t media_profile;              /* 媒介配置文件 (AEFS_MEDIA_*) */
    uint8_t reserved1[7];               /* 保留字节 */
    uint32_t header_checksum;           /* 整个Header的XXH3校验和 */
    uint8_t reserved2[60];              /* 保留 - 调整以达到128字节 */
} PACKED Aethel_Disk_Header;

_Static_assert(sizeof(Aethel_Disk_Header) == 128, "Aethel_Disk_Header must be exactly 128 bytes");

/* 2.2 Partition Entry */
typedef struct {
    uint8_t partition_uuid[16];         /* 分区唯一标识符 */
    uint64_t partition_type_uuid;       /* 分区类型UUID */
    uint64_t start_lba;                 /* 分区起始LBA */
    uint64_t end_lba;                   /* 分区结束LBA */
    uint64_t flags;                     /* 分区属性标志 */
    uint8_t resilience_policy;          /* 韧性策略 */
    uint8_t member_count;               /* 组成此分区的磁盘成员数 */
    uint16_t reserved1;                 /* 保留 */
    uint8_t name[128];                  /* UTF-8分区标签 */
    
    /* 成员描述符数组 (最多4个) - 共128字节 */
    struct {
        uint8_t disk_uuid[16];
        uint64_t lba_start;
        uint64_t lba_end;
    } members[4];                       /* 减少到4个 */
    
    uint32_t entry_checksum;            /* 该条目的XXH3校验和 */
    uint8_t reserved2[200];             /* 保留以达到512字节 */
} PACKED Partition_Entry;

_Static_assert(sizeof(Partition_Entry) == 512, "Partition_Entry must be exactly 512 bytes");

/* ============================================================================
 * 3. 存储池层：ÆFS 分区内部布局
 * ============================================================================ */

/* 3.1 Partition Anchor Block */
typedef struct {
    uint64_t magic;                     /* AEFS_ANCHOR_MAGIC */
    uint32_t version;                   /* AEFS_ANCHOR_VERSION */
    uint64_t checkpoint_lba;            /* 指向当前有效的Checkpoint区域LBA */
    uint64_t checkpoint_backup_lba;     /* 备份Checkpoint区域LBA */
    uint64_t log_head_lba;              /* 日志区域头部LBA */
    uint64_t log_tail_lba;              /* 日志区域尾部LBA */
    uint64_t log_sequence_number;       /* 全局日志序列号 */
    uint32_t segment_size_log2;         /* 段大小的对数 (21 = 2MB) */
    uint32_t block_size_log2;           /* 块大小的对数 (12 = 4KB) */
    uint8_t partition_uuid[16];         /* 分区唯一标识符 */
    uint32_t checksum;                  /* 校验和 */
    uint8_t reserved[48];               /* 保留以达到128字节 */
} PACKED AEFS_Anchor_Block;

_Static_assert(sizeof(AEFS_Anchor_Block) == 128, "AEFS_Anchor_Block must be exactly 128 bytes");

/* 3.2 Checkpoint Area Header */
typedef struct {
    uint64_t magic;                     /* AEFS_CHECKPOINT_MAGIC */
    uint32_t version;                   /* AEFS_CHECKPOINT_VERSION */
    uint32_t reserved0;                 /* 保留 */
    uint64_t timestamp;                 /* 检查点创建时间戳 */
    uint64_t log_sequence_number;       /* 此检查点时的LSN */
    
    /* 文件系统状态快照 */
    uint64_t root_inode_lba;            /* 根Æ-Node的LBA */
    uint64_t volume_tree_lba;           /* 卷清单B+树的LBA */
    uint64_t gc_metadata_lba;           /* GC元数据的LBA */
    uint64_t free_blocks;               /* 空闲块数量 */
    uint64_t total_blocks;              /* 总块数 */
    
    /* 分段管理 */
    uint64_t segment_info_lba;          /* 段信息表的LBA */
    uint32_t num_segments;              /* 分段总数 */
    uint32_t num_active_segments;       /* 活跃分段数 */
    
    uint8_t reserved[420];              /* 保留以达到512字节 */
    uint32_t checksum;                  /* Checkpoint的XXH3校验和 */
} PACKED AEFS_Checkpoint;

_Static_assert(sizeof(AEFS_Checkpoint) == 512, "AEFS_Checkpoint must be exactly 512 bytes");

/* 3.3 Segment Summary Area (SSA) - 完整的块生命周期管理 */
typedef struct {
    uint64_t segment_lba;               /* 该分段的起始LBA */
    uint32_t segment_index;             /* 分段索引 */
    uint32_t num_blocks_used;           /* 该分段中使用的块数 */
    uint64_t creation_timestamp;        /* 分段创建时间戳 */
    uint64_t last_written_timestamp;    /* 最后一次写入时间戳 */
    
    /* 垃圾回收信息 */
    uint32_t live_bytes;                /* 有效数据字节数 */
    uint32_t dead_bytes;                /* 无效/已删除数据字节数 */
    uint8_t gc_priority;                /* GC优先级 */
    uint8_t reserved1[3];               /* 保留 */
    
    /* 
     * 块状态位图 - 支持多达512个块（512 * 8 = 4096位）
     * 每个块用2位表示: 00=FREE, 01=LIVE, 10=DEAD, 11=RESERVED
     * 这允许每个SSA条目管理一个完整的4MB分段
     */
    uint8_t block_bitmap[512];          /* 512字节位图 = 2048块 */
    
    uint8_t reserved2[464];             /* 保留以达到1024字节 */
    uint32_t checksum;                  /* SSA的XXH3校验和 */
} PACKED AEFS_Segment_Summary;

_Static_assert(sizeof(AEFS_Segment_Summary) == 1024, "AEFS_Segment_Summary must be exactly 1024 bytes");

/* 3.4 Log Entry Header (日志条目) */
typedef struct {
    uint32_t magic;                     /* AEFS_MAGIC */
    uint32_t entry_type;                /* 条目类型 (1=INODE, 2=DATA, 3=DELETE, etc.) */
    uint64_t transaction_id;            /* 事务ID */
    uint32_t entry_size;                /* 条目总大小 (包含header) */
    uint32_t payload_size;              /* 有效载荷大小 */
    uint32_t checksum;                  /* 该条目的XXH3校验和 */
    uint16_t flags;                     /* 标志位 */
    uint16_t reserved;                  /* 保留 */
} PACKED AEFS_Log_Entry_Header;

_Static_assert(sizeof(AEFS_Log_Entry_Header) == 32, "AEFS_Log_Entry_Header must be exactly 32 bytes");

/* ============================================================================
 * 4. 逻辑层：Aethel Volume (Æ-Node Tree)
 * ============================================================================ */

/* 4.1 Aethel Volume Descriptor */
typedef struct {
    uint8_t volume_uuid[16];            /* 卷UUID */
    uint8_t volume_name[128];           /* 卷名称 (UTF-8, 例如 "home", "documents") */
    uint64_t creation_timestamp;        /* 创建时间戳 */
    uint64_t last_modified;             /* 最后修改时间戳 */
    
    /* 配置属性 */
    uint8_t redundancy;                 /* 冗余度: 1 (默认) 或 2 */
    uint8_t compression_algorithm;      /* 压缩算法 */
    uint8_t readonly;                   /* 只读标志 */
    uint8_t bootable;                   /* 可引导标志 */
    
    uint64_t quota;                     /* 空间配额 (字节) */
    uint64_t used_bytes;                /* 已用字节数 */
    uint64_t root_inode_lba;            /* 根目录Æ-Node的LBA */
    
    /* 
     * 标签/元数据树 - 完整的可扩展标签系统
     * 存储结构化的元数据标签，以NUL分隔的字符串列表
     * 格式: "key1=value1\0key2=value2\0...\0"
     * 预留256字节用于常见的标签（如compression, encryption, tier等）
     */
    uint8_t tags[256];                  /* UTF-8标签，用\0分隔 */
    
    /* 扩展属性计数与引用 */
    uint32_t num_xattr;                 /* 扩展属性数量 */
    uint32_t xattr_block_count;         /* 扩展属性占用的块数 */
    uint64_t xattr_tree_lba;            /* 扩展属性B+树LBA */
    
    uint8_t reserved[560];              /* 保留以达到1024字节 */
    uint32_t checksum;                  /* Volume Descriptor的XXH3校验和 */
} PACKED AEFS_Volume_Descriptor;

_Static_assert(sizeof(AEFS_Volume_Descriptor) == 1024, "AEFS_Volume_Descriptor must be exactly 1024 bytes");

/* 4.2 Æ-Node (AethelFS Inode) */
/*
 * 企业级应用身份与权限枚举
 * 支持新秩序安全框架的完整应用管理
 */
typedef enum {
    /* 系统应用身份 (系统保留: sys:*) */
    AEFS_APP_IDENTITY_SYSTEM_KERNEL = 0x00,
    AEFS_APP_IDENTITY_SYSTEM_SHELL = 0x01,
    AEFS_APP_IDENTITY_SYSTEM_FS = 0x02,
    AEFS_APP_IDENTITY_SYSTEM_SECURITY = 0x03,
    AEFS_APP_IDENTITY_SYSTEM_AUDIT = 0x04,
    
    /* 用户应用身份 (用户范围: user:*) */
    AEFS_APP_IDENTITY_USER_DEFAULT = 0x80,
    AEFS_APP_IDENTITY_USER_SANDBOX = 0x81,
    
    /* 临时/不可信身份 */
    AEFS_APP_IDENTITY_UNTRUSTED = 0xFF,
} AEFS_AppIdentityType;

/*
 * 细粒度SIP (System Integrity Protection) 标志位集
 * 用于替代Unix权限模型，实现新秩序安全框架
 */
typedef enum {
    /* 系统不可变标志 - 即使在Architect模式也受保护 */
    AEFS_SIP_SYSTEM_IMMUTABLE = (1U << 0),     /* 0x00000001 */
    AEFS_SIP_SYSTEM_LIBRARY = (1U << 1),       /* 0x00000002 */
    AEFS_SIP_SYSTEM_RUNNABLE = (1U << 2),   /* 0x00000004 */
    AEFS_SIP_SYSTEM_CONFIGURATION = (1U << 3),/* 0x00000008 */
    
    /* 应用特定保护 */
    AEFS_SIP_APP_PRIVATE = (1U << 4),          /* 0x00000010 只有所有者应用可访问 */
    AEFS_SIP_APP_SIGNED = (1U << 5),           /* 0x00000020 应用签名已验证 */
    AEFS_SIP_APP_SANDBOXED = (1U << 6),        /* 0x00000040 沙盒限制适用 */
    
    /* 加密和完整性 */
    AEFS_SIP_ENCRYPTED = (1U << 7),            /* 0x00000080 内容加密 */
    AEFS_SIP_SIGNED = (1U << 8),               /* 0x00000100 数据签名 */
    AEFS_SIP_HMAC_ENABLED = (1U << 9),         /* 0x00000200 HMAC完整性检查 */
    
    /* 缓存和临时 */
    AEFS_SIP_CACHE = (1U << 10),               /* 0x00000400 系统缓存，可清空 */
    AEFS_SIP_TEMPORARY = (1U << 11),           /* 0x00000800 临时文件标记 */
    AEFS_SIP_SWAP = (1U << 12),                /* 0x00001000 交换区块 */
    
    /* 审计和监视 */
    AEFS_SIP_AUDIT_READ = (1U << 13),          /* 0x00002000 审计所有读操作 */
    AEFS_SIP_AUDIT_WRITE = (1U << 14),         /* 0x00004000 审计所有写操作 */
    AEFS_SIP_AUDIT_RUN = (1U << 15),       /* 0x00008000 审计所有运行 */
    
    /* 访问控制 */
    AEFS_SIP_ACL_PRESENT = (1U << 16),         /* 0x00010000 存在外部ACL */
    AEFS_SIP_DENY_ALL = (1U << 17),            /* 0x00020000 默认拒绝访问 */
    AEFS_SIP_INHERIT_ACL = (1U << 18),         /* 0x00040000 继承父目录ACL */
    
    /* 版本和迁移 */
    AEFS_SIP_VERSION_PINNED = (1U << 19),      /* 0x00080000 版本锁定 */
    AEFS_SIP_MIGRATION_PENDING = (1U << 20),   /* 0x00100000 等待迁移 */
    
    /* 恢复和修复 */
    AEFS_SIP_CORRUPTED = (1U << 21),           /* 0x00200000 数据损坏标记 */
    AEFS_SIP_REPAIR_LOG = (1U << 22),          /* 0x00400000 修复日志存在 */
} AEFS_SIPFlags;

typedef struct {
    /* === 第一部分: 核心标识符与元数据(88字节) === */
    uint64_t magic;                     /* AEFS_NODE_MAGIC */
    uint8_t node_uuid[16];              /* Æ-Node唯一ID */
    uint32_t type;                      /* 节点类型 */
    uint32_t checksum_algorithm;        /* 校验算法(0=XXH3-32) */
    uint32_t reserved_flags;            /* 保留标志 */
    uint32_t reserved_padding;          /* 对齐填充 */
    
    /* === 第二部分: 应用身份与安全(80字节) === */
    uint8_t owner_app_uuid[16];         /* 所有者应用UUID */
    uint8_t delegated_app_uuid[16];     /* 代理应用UUID */
    uint32_t sip_flags;                 /* SIP保护标志 */
    uint32_t sip_audit_mask;            /* SIP审计掩码 */
    uint8_t content_hmac[16];           /* HMAC-256(前128位) */
    uint8_t signature_hash[16];         /* 签名哈希验证 */
    
    /* === 第三部分: 时间戳与大小(48字节) === */
    uint64_t created_time;              /* 创建时间(UTC秒) */
    uint64_t accessed_time;             /* 访问时间 */
    uint64_t modified_time;             /* 修改时间 */
    uint64_t metadata_changed_time;     /* 元数据变更时间 */
    uint64_t size;                      /* 文件大小或目录项数 */
    uint32_t link_count;                /* 硬链接计数 */
    uint16_t block_count_log2;          /* 块数(对数形式) */
    uint16_t acl_entry_count;           /* ACL条目数 */
    
    /* === 第四部分: 扩展属性与版本(24字节) === */
    uint64_t xattr_block;               /* 扩展属性块LBA */
    uint32_t xattr_size;                /* 扩展属性大小 */
    uint32_t version_number;            /* CoW版本号 */
    uint32_t dir_entry_block_count;     /* 目录条目块数 */
    
    /* === 第五部分: 数据块寻址(120字节) === */
    uint64_t direct_blocks[12];         /* 直接块指针 */
    uint64_t indirect_block;            /* 一级间接块 */
    uint64_t double_indirect_block;     /* 二级间接块 */
    uint64_t triple_indirect_block;     /* 三级间接块 */
    
    /* === 第六部分: 目录与审计(32字节) === */
    uint32_t child_count;               /* 子项数 */
    uint32_t checksum;                  /* XXH3-32校验和 */
    uint32_t repair_log_block;          /* 修复日志块 */
    uint32_t audit_log_block;           /* 审计日志块 */
    uint32_t last_repair_time;          /* 最后修复时间 */
    uint32_t last_audit_time;           /* 最后审计时间 */
    uint32_t replica_block;             /* 冗余副本块地址 */
    uint32_t replica_checksum;          /* 副本校验和 */
    
    /* === 第七部分: 扩展安全(80字节) === */
    uint8_t encryption_key_id[16];      /* 加密密钥ID */
    uint8_t security_context[32];       /* 安全上下文/标签 */
    uint8_t domain_id[16];              /* 应用安全域ID */
    uint8_t capability_set[16];         /* 能力集位图(128位) */
    
    /* === 保留与对齐(100字节) === */
    uint8_t reserved[100];              /* 保留,用于向后兼容和企业扩展 */
} PACKED AEFS_Node;

_Static_assert(sizeof(AEFS_Node) == 512, "AEFS_Node must be exactly 512 bytes");

/* 4.3 目录条目 (Directory Entry) */
typedef struct {
    uint8_t target_node_uuid[16];       /* 指向的Æ-Node UUID */
    uint32_t entry_name_len;            /* 名称长度 */
    uint32_t entry_type;                /* 条目类型 */
    uint64_t entry_size;                /* 条目占用大小 */
    uint8_t entry_name[256];            /* 条目名称 (UTF-8) */
    uint32_t checksum;                  /* 条目的XXH3校验和 */
    uint8_t reserved[28];               /* 保留以达到320字节 */
} PACKED AEFS_Directory_Entry;

_Static_assert(sizeof(AEFS_Directory_Entry) == 320, "AEFS_Directory_Entry must be exactly 320 bytes");

/* ============================================================================
 * 辅助函数声明 (在aefs.c中实现)
 * ============================================================================ */

#ifdef __cplusplus
extern "C" {
#endif

/* XXH3 校验和计算 (通用) */
uint32_t aefs_checksum_xxh3(const uint8_t *data, size_t len);

/* UUID生成函数 */
int aefs_generate_uuid(uint8_t *uuid);

/* ADL相关函数 */
int aefs_init_disk_header(Aethel_Disk_Header *header, uint64_t total_blocks,
                          uint8_t media_profile);
int aefs_init_partition_entry(Partition_Entry *entry, const uint8_t *partition_uuid,
                              uint64_t start_lba, uint64_t end_lba, 
                              uint64_t partition_type, const char *name);

/* Anchor Block相关函数 */
int aefs_init_anchor_block(AEFS_Anchor_Block *anchor, 
                           const uint8_t *partition_uuid,
                           uint64_t checkpoint_lba, uint64_t log_head_lba);

/* Checkpoint相关函数 */
int aefs_init_checkpoint(AEFS_Checkpoint *checkpoint,
                         uint64_t root_inode_lba, uint64_t volume_tree_lba,
                         uint64_t free_blocks, uint64_t total_blocks);

/* Segment Summary相关函数 */
int aefs_init_segment_summary(AEFS_Segment_Summary *ssa, uint32_t segment_index,
                              uint64_t segment_lba);

/* 块生命周期管理函数 */
int aefs_block_mark_live(AEFS_Segment_Summary *ssa, uint32_t block_idx, uint32_t block_size);
int aefs_block_mark_dead(AEFS_Segment_Summary *ssa, uint32_t block_idx, uint32_t block_size);
int aefs_update_gc_priority(AEFS_Segment_Summary *ssa);

/* Volume Descriptor相关函数 */
int aefs_init_volume_descriptor(AEFS_Volume_Descriptor *vol_desc,
                                const uint8_t *volume_uuid, const char *volume_name,
                                uint64_t root_inode_lba);

/* 卷标签管理函数 */
int aefs_volume_add_tag(AEFS_Volume_Descriptor *vol_desc, const char *key, const char *value);
const char *aefs_volume_get_tag(const AEFS_Volume_Descriptor *vol_desc, const char *key);

/* Æ-Node相关函数 - 企业级安全框架版本 
 * 使用128位应用UUID而非可变长ID字符串，支持细粒度SIP标志和审计
 */
int aefs_init_inode(AEFS_Node *node, const uint8_t *node_uuid, uint32_t type,
                    const uint8_t *owner_app_uuid, uint8_t app_identity_type, 
                    uint32_t sip_flags);
int aefs_add_directory_entry(AEFS_Node *dir_node, const uint8_t *target_uuid,
                             const char *entry_name, uint32_t entry_type);

/* ISO生成函数 */
int aefs_generate_iso_image(const char *kernel_file, const char *efi_boot_file,
                            const char *output_iso, uint64_t iso_size_mb);

#ifdef __cplusplus
}
#endif

#endif /* AEFS_H */
