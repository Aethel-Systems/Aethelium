/**
 * ============================================================================
 * AethelOS ADL ISO Builder - Industrial Grade Implementation
 * ============================================================================
 * 完整实现符合《AethelOS启动引导及镜像规范.txt》和《AEFS.txt》的ISO镜像生成
 * 
 * 物理布局符合规范：
 *   LBA 0        - ADL-MBR混合引导扇区 (440字节ADL Header + 72字节MBR分区表)
 *   LBA 1        - Shadow GPT头
 *   LBA 2-33     - Shadow GPT分区项
 *   LBA 34-2047  - BIOS Stage2 引导区（保留）
 *   LBA 64       - ADL分区表（主）
 *   LBA 2048     - ESP (EFI System Partition) FAT32
 *   LBA 263168   - ADL扩展元数据区（ESP后）
 *   LBA 264192   - AEFS主存储池开始
 * ============================================================================
 */

#ifndef AETHELOS_ADL_ISO_BUILDER_H
#define AETHELOS_ADL_ISO_BUILDER_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 常量定义
 * ============================================================================ */

#define SECTOR_SIZE                             512
#define BLOCK_SIZE                              4096
#define AEFS_SEGMENT_SIZE                       (2 * 1024 * 1024)  /* 2MB */
#define AEFS_MAX_VOLUMES                        256

/* LBA定义（严格遵循规范） */
#define ADL_MBR_LBA                             0
#define SHADOW_GPT_HEAD_LBA                     1
#define SHADOW_GPT_ENTRY_LBA                    2
#define ADL_METADATA_LBA                        263168
#define ADL_PARTITION_TABLE_LBA                 64
#define ADL_RESERVED_END_LBA                    1023
#define ESP_START_LBA                           2048
#define ESP_SIZE_SECTORS                        261120  /* 127.5 MiB */
#define AEFS_POOL_START_LBA                     264192

/* ADL Magic Number - 符合规范 */
#define ADL_MAGIC_NUMBER                        0x4145544845004C02ULL

/* 分区类型定义 */
#define AEFS_PARTITION_TYPE_POOL                0x41455350  /* "AESP" */
#define AEFS_PARTITION_TYPE_BOOT                0x42544520  /* "BT " */

/* XXH3 hash参见adl_xxh3.h */
typedef uint32_t aefs_hash_t;

/* ============================================================================
 * 数据结构定义 - 严格按规范
 * ============================================================================ */

/**
 * ADL Header - 512字节，位于MBR扇区的前部分
 * 前440字节是ADL Header，后72字节是MBR分区表
 */
typedef struct {
    /* === 标准MBR前缀 === */
    uint8_t boot_jump[3];                       /* +0: EB 3C 90 */
    uint8_t oem_id[8];                          /* +3: "AETHELOS" */
    uint16_t bytes_per_sector;                  /* +11: 512 */
    uint8_t sectors_per_cluster;                /* +13: 8 */
    uint16_t reserved_sectors;                  /* +14: 1 */
    uint8_t fats;                               /* +16: 2 */
    uint16_t root_entries;                      /* +17: 512 */
    uint16_t total_sectors_16;                  /* +19: 0 */
    uint8_t media_descriptor;                   /* +21: 0xF8 */
    uint16_t sectors_per_fat_16;                /* +22: 0 */
    uint16_t sectors_per_track;                 /* +24: 63 */
    uint16_t heads;                             /* +26: 255 */
    uint32_t hidden_sectors;                    /* +28: 0 */
    uint32_t total_sectors_32;                  /* +32: 0 */
    
    /* === ADL特定字段 === */
    uint32_t magic_number_low;                  /* +36: ADL magic低32位 */
    uint32_t magic_number_high;                 /* +40: ADL magic高32位 */
    uint64_t partition_table_lba;               /* +44: 分区表LBA (64) */
    uint64_t partition_table_backup_lba;        /* +52: 备份分区表LBA */
    uint64_t total_lba;                         /* +60: 总LBA数 */
    uint8_t disk_uuid[16];                      /* +68: 磁盘UUID */
    uint8_t adl_version_major;                  /* +84: 主版本 */
    uint8_t adl_version_minor;                  /* +85: 副版本 */
    uint8_t disk_type;                          /* +86: 磁盘类型 (SSD/HDD/RamDisk) */
    uint8_t reserved1;                          /* +87: 保留 */
    uint32_t header_checksum;                   /* +88: XXH3校验和 */
    uint8_t reserved2[352];                     /* +92: 保留至440字节 */
} ADL_Header;

/**
 * ADL分区表项 - 64字节
 * LBA 64处，最多8个分区
 */
typedef struct {
    uint8_t partition_uuid[16];                 /* 分区UUID */
    uint8_t partition_type_uuid[16];            /* 分区类型UUID */
    uint64_t start_lba;                         /* 起始LBA */
    uint64_t end_lba;                           /* 结束LBA */
    uint64_t flags;                             /* 标志位 */
    uint8_t resilience_policy;                  /* 韧性策略 */
    uint8_t member_count;                       /* 成员数 */
    uint8_t reserved[6];                        /* 保留 */
    uint32_t entry_checksum;                    /* XXH3校验和 */
} ADL_Partition_Entry;

/**
 * AEFS Anchor Block - AEFS分区的固定起始元数据
 * 位于AEFS_POOL_START_LBA处，大小512字节（1个扇区）
 */
typedef struct {
    uint64_t magic;                             /* "AEFS\0\0\0\0" */
    uint32_t version;                           /* AEFS版本 */
    uint32_t block_size;                        /* 块大小 (4096) */
    uint64_t total_blocks;                      /* 总块数 */
    uint64_t checkpoint_lba;                    /* Checkpoint位置 */
    uint64_t checkpoint_backup_lba;             /* 备份Checkpoint */
    uint64_t segment_log_start_lba;             /* 日志段起始位置 */
    uint8_t pool_uuid[16];                      /* 存储池UUID */
    uint32_t generation;                        /* 代数，用于版本控制 */
    uint32_t anchor_checksum;                   /* 校验和 */
    uint8_t reserved[440];                      /* 保留至512字节 */
} AEFS_Anchor_Block;

/**
 * AEFS Volume Descriptor - 描述一个逻辑卷
 * 存储在Checkpoint区域中的Volume清单中
 */
typedef struct {
    uint8_t volume_uuid[16];                    /* 卷UUID */
    uint64_t root_node_lba;                     /* 根Æ-Node位置 */
    uint32_t volume_name_len;                   /* 卷名长度 */
    uint8_t volume_name[256];                   /* 卷名 (UTF-8，如">:BOOT", ">:PAYLOAD") */
    uint32_t flags;                             /* 属性标志 */
    uint64_t used_blocks;                       /* 已用块数 */
    uint64_t quota_blocks;                      /* 空间配额 */
    uint32_t compression;                       /* 压缩算法 */
    uint32_t redundancy;                        /* 冗余度 */
    time_t created_time;                        /* 创建时间 */
    time_t modified_time;                       /* 修改时间 */
    uint32_t descriptor_checksum;               /* 校验和 */
    uint8_t reserved[200];                      /* 保留 */
} AEFS_Volume_Descriptor;

/**
 * AEFS Checkpoint - 文件系统状态快照，4096字节（1个AEFS块）
 * 包含所有卷信息和日志指针
 */
typedef struct {
    uint64_t magic;                             /* "CHECKPOINT" */
    uint32_t version;                           /* Checkpoint版本 */
    time_t timestamp;                           /* 创建时间戳 */
    uint64_t generation;                        /* 代数 */
    uint64_t log_head_lba;                      /* 日志头指针 */
    uint64_t log_tail_lba;                      /* 日志尾指针 */
    uint32_t volume_count;                      /* 卷数 */
    uint32_t reserved1;                         /* 保留 */
    uint64_t total_blocks;                      /* 总块数 */
    uint64_t free_blocks;                       /* 空闲块数 */
    
    /* 卷清单B+树的起始位置 */
    uint64_t volume_tree_lba;                   /* Volume B+树根节点位置 */
    
    uint32_t checkpoint_checksum;               /* 校验和 */
    uint8_t reserved2[3996];                    /* 保留至4096字节 */
} AEFS_Checkpoint;

/**
 * AEFS Æ-Node - 文件/目录元数据对象，4096字节（1个AEFS块）
 */
typedef struct {
    uint64_t magic;                             /* "AENODE" */
    uint8_t node_uuid[16];                      /* 对象UUID */
    uint32_t object_type;                       /* 类型: FILE=1, DIR=2, SYMLINK=3 */
    uint32_t flags;                             /* 属性标志 */
    uint64_t size;                              /* 文件大小 */
    time_t created_time;                        /* 创建时间 */
    time_t modified_time;                       /* 修改时间 */
    uint32_t mode;                              /* 权限 */
    uint32_t owner_uid;                         /* 所有者UID */
    uint32_t owner_gid;                         /* 所有者GID */
    uint32_t refcount;                          /* 引用计数 */
    uint64_t data_lba;                          /* 数据块起始位置 */
    uint32_t block_count;                       /* 数据块数 */
    uint32_t node_checksum;                     /* 校验和 */
    uint8_t reserved[4008];                     /* 保留至4096字节 */
} AEFS_Node;

/**
 * AEFS Directory Entry - 目录项
 */
typedef struct {
    uint8_t entry_uuid[16];                     /* 条目UUID（指向子对象） */
    uint32_t entry_type;                        /* 类型 */
    uint32_t name_len;                          /* 名称长度 */
    uint8_t name[256];                          /* 名称 (UTF-8，用'-'分隔，不用'/') */
    uint64_t entry_lba;                         /* 指向的对象位置 */
    uint32_t entry_checksum;                    /* 校验和 */
    uint8_t reserved[220];                      /* 保留至512字节 */
} AEFS_Directory_Entry;

/* ============================================================================
 * GPT相关结构（Shadow GPT支持，以满足UEFI要求）- 严格字节对齐
 * ============================================================================ */

#pragma pack(push, 1)

/**
 * GPT头 - 512字节 (LBA 1处)
 * 用于UEFI固件识别和启动
 */
typedef struct {
    uint8_t signature[8];                       /* "EFI PART" */
    uint32_t revision;                          /* 0x00010000 */
    uint32_t header_size;                       /* 92字节 */
    uint32_t crc32;                             /* 头CRC32 */
    uint32_t reserved;
    uint64_t current_lba;                       /* 当前LBA (1) */
    uint64_t backup_lba;                        /* 备份LBA */
    uint64_t first_usable_lba;                  /* 最小可用LBA */
    uint64_t last_usable_lba;                   /* 最大可用LBA */
    uint8_t disk_guid[16];                      /* 磁盘GUID */
    uint64_t partition_entry_lba;               /* 分区项起始LBA (2) */
    uint32_t num_partition_entries;             /* 分区数量 */
    uint32_t partition_entry_size;              /* 分区项大小 (128) */
    uint32_t partition_array_crc32;             /* 分区数组CRC32 */
    uint8_t reserved2[420];                     /* 保留至512字节 */
} GPT_Header;

/**
 * GPT分区项 - 128字节
 * 每个分区项定义一个分区
 */
typedef struct {
    uint8_t partition_type_guid[16];            /* 分区类型GUID */
    uint8_t partition_guid[16];                 /* 分区唯一GUID */
    uint64_t starting_lba;                      /* 起始LBA */
    uint64_t ending_lba;                        /* 结束LBA */
    uint64_t attributes;                        /* 分区属性 */
    uint16_t partition_name[36];                /* 分区名(UTF-16LE) */
} GPT_Partition_Entry;

#pragma pack(pop)

/* ============================================================================
 * FAT32相关结构（完整FAT32实现）
 * ============================================================================ */

/* 禁止结构体对齐，确保准确的512字节大小 */
#pragma pack(push, 1)

/**
 * FAT32启动扇区 - 512字节 (LBA 1024处)
 */
typedef struct {
    uint8_t jmp_boot[3];                        /* EB 3C 90 */
    uint8_t oem_name[8];                        /* "AETHEL  " */
    uint16_t bytes_per_sector;                  /* 512 */
    uint8_t sectors_per_cluster;                /* 8 */
    uint16_t reserved_sectors;                  /* FAT启动扇区前的保留扇区数 */
    uint8_t num_fats;                           /* FAT表个数 (2) */
    uint16_t root_entries;                      /* FAT32设为0 */
    uint16_t total_sectors_16;                  /* FAT32设为0 */
    uint8_t media_descriptor;                   /* 0xF8 */
    uint16_t sectors_per_fat_16;                /* FAT32设为0 */
    uint16_t sectors_per_track;                 /* 63 */
    uint16_t num_heads;                         /* 255 */
    uint32_t hidden_sectors;                    /* 0 */
    uint32_t total_sectors_32;                  /* 总扇区数(32位) */
    
    /* FAT32特定字段 */
    uint32_t sectors_per_fat_32;                /* 每个FAT表的扇区数 */
    uint16_t ext_flags;                         /* 扩展标志 */
    uint16_t fs_version;                        /* 文件系统版本 */
    uint32_t root_cluster;                      /* 根目录簇号 (2) */
    uint16_t fsinfo_sector;                     /* FSInfo扇区号 (1) */
    uint16_t backup_boot_sector;                /* 启动扇区备份位置 (6) */
    uint8_t reserved[12];                       /* 保留 */
    uint8_t drive_number;                       /* 驱动器号 */
    uint8_t nt_reserved;
    uint8_t boot_signature;                     /* 0x29 */
    uint32_t volume_serial;                     /* 卷序列号 */
    uint8_t volume_label[11];                   /* 卷标签 */
    uint8_t fs_type[8];                         /* "FAT32   " */
    uint8_t boot_code[420];                     /* 启动代码 (偏移0x5A-0x1FD) */
    uint16_t boot_sector_sig;                   /* 0x55AA */
} FAT32_Boot_Sector;

/**
 * FAT32 FSInfo扇区 - 512字节 (LBA 1025处)
 */
typedef struct {
    uint32_t signature1;                        /* 0x41615252 ("RRaA") */
    uint8_t reserved1[480];
    uint32_t signature2;                        /* 0x61417272 ("rrAa") */
    uint32_t free_clusters;                     /* 空闲簇数 */
    uint32_t next_free_cluster;                 /* 下一个空闲簇 */
    uint8_t reserved2[12];
    uint32_t signature3;                        /* 0xAA550000 */
} FAT32_FSInfo_Sector;

/* 恢复结构体对齐 */
#pragma pack(pop)

/**
 * FAT32目录项 - 32字节
 */
typedef struct {
    uint8_t name[8];
    uint8_t extension[3];
    uint8_t attributes;                         /* 属性字节 */
    uint8_t nt_reserved;
    uint8_t creation_time_tenth;                /* 创建时间百分位秒 */
    uint16_t creation_time;                     /* 创建时间 */
    uint16_t creation_date;                     /* 创建日期 */
    uint16_t last_access_date;                  /* 最后访问日期 */
    uint16_t first_cluster_high;                /* 首簇高16位 */
    uint16_t write_time;                        /* 写时间 */
    uint16_t write_date;                        /* 写日期 */
    uint16_t first_cluster_low;                 /* 首簇低16位 */
    uint32_t file_size;                         /* 文件大小 */
} FAT32_Directory_Entry;

/* FAT32目录项属性 */
#define FAT32_ATTR_READ_ONLY    0x01
#define FAT32_ATTR_HIDDEN       0x02
#define FAT32_ATTR_SYSTEM       0x04
#define FAT32_ATTR_VOLUME_ID    0x08
#define FAT32_ATTR_DIRECTORY    0x10
#define FAT32_ATTR_ARCHIVE      0x20

/* ============================================================================
 * ISO 9660相关结构（UEFI引导支持）
 * ============================================================================ */

#pragma pack(push, 1)

/**
 * ISO 9660 Primary Volume Descriptor (PVD) - 2048字节
 * 位于LBA 16
 */
typedef struct {
    uint8_t type;                               /* 0x01 = PVD */
    uint8_t identifier[5];                      /* "CD001" */
    uint8_t version;                            /* 0x01 */
    uint8_t unused1;                            /* 0x00 */
    uint8_t system_identifier[32];              /* 系统ID */
    uint8_t volume_identifier[32];              /* 卷标识 */
    uint8_t unused2[8];                         /* 保留 */
    uint32_t space_size_le;                     /* 容量(小端) */
    uint32_t space_size_be;                     /* 容量(大端) */
    uint8_t unused3[32];                        /* 保留 */
    uint16_t volume_set_size_le;                /* 卷集大小(小端) */
    uint16_t volume_set_size_be;                /* 卷集大小(大端) */
    uint16_t volume_seq_num_le;                 /* 卷序列号(小端) */
    uint16_t volume_seq_num_be;                 /* 卷序列号(大端) */
    uint16_t logical_block_size_le;             /* 逻辑块大小(小端) */
    uint16_t logical_block_size_be;             /* 逻辑块大小(大端) */
    uint32_t path_table_size_le;                /* 路径表大小(小端) */
    uint32_t path_table_size_be;                /* 路径表大小(大端) */
    uint32_t path_table_lba_le;                 /* 路径表LBA(小端) */
    uint32_t path_table_lba_be;                 /* 路径表LBA(大端) */
    uint32_t path_table_opt_lba_le;             /* 可选路径表LBA(小端) */
    uint32_t path_table_opt_lba_be;             /* 可选路径表LBA(大端) */
    uint8_t root_dir_record[34];                /* 根目录记录 */
    uint8_t volume_set_id[128];                 /* 卷集ID */
    uint8_t publisher_id[128];                  /* 发布者ID */
    uint8_t preparer_id[128];                   /* 准备者ID */
    uint8_t application_id[128];                /* 应用ID */
    uint8_t copyright_file_id[37];              /* 版权文件ID */
    uint8_t abstract_file_id[37];               /* 摘要文件ID */
    uint8_t biblio_file_id[37];                 /* 参考文件ID */
    uint8_t creation_datetime[17];              /* 创建日期时间 */
    uint8_t modification_datetime[17];          /* 修改日期时间 */
    uint8_t expiration_datetime[17];            /* 过期日期时间 */
    uint8_t effective_datetime[17];             /* 生效日期时间 */
    uint8_t file_structure_version;             /* 文件结构版本 */
    uint8_t unused4;                            /* 保留 */
    uint8_t app_use[512];                       /* 应用使用区域 */
    uint8_t reserved[653];                      /* 保留至2048字节 */
} ISO9660_PVD;

/**
 * El Torito启动记录 - 32字节
 * 紧跟在PVD之后
 */
typedef struct {
    uint8_t type;                               /* 0x00 = 启动记录 */
    uint8_t identifier[5];                      /* "EL TORITO SPECIFICATION" 的开始 */
    uint8_t version;                            /* 0x01 */
    uint32_t boot_catalog_lba;                  /* 启动目录对应的LBA */
    uint8_t unused[32 - 13];                    /* 填充至32字节 */
} ElTorito_BootRecord;

/**
 * El Torito启动目录项 - 32字节
 */
typedef struct {
    /* 验证区 */
    uint8_t validation_indicator;               /* 0x01 有效 */
    uint8_t platform_id;                        /* 0x00 = 80x86, 0xEF = UEFI */
    uint16_t reserved;                          /* 0x0000 */
    uint8_t manufacturer_id[4];
    
    /* 启动项 */
    uint8_t boot_indicator;                     /* 0x88 = 可启动 */
    uint8_t boot_media_type;                    /* 0x00 = 无仿真 */
    uint16_t load_segment;                      /* 加载段 */
    uint8_t system_type;                        /* 0x00 = 保留, 0xEF = UEFI */
    uint8_t unused;                             /* 0x00 */
    uint16_t sector_count;                      /* 扇区数 */
    uint32_t load_lba;                          /* 加载LBA */
    uint8_t reserved2[20];                      /* 保留 */
} ElTorito_BootDirEntry;

#pragma pack(pop)

/* ============================================================================
 * 函数声明
 * ============================================================================ */

/**
 * 生成完整的ADL ISO镜像
 * 
 * 参数:
 *   kernel_file: kernel.aki路径
 *   efi_boot_file: BOOTX64.EFI路径
 *   installer_file: appInstaller.iya路径
 *   drivers_dir: drivers目录路径
 *   output_iso: 输出ISO路径
 *   auto_size: 自动调整ISO大小
 * 
 * 返回值:
 *   0: 成功
 *   -1: 失败
 */
int adl_iso_builder_generate(
    const char *kernel_file,
    const char *efi_boot_file,
    const char *installer_file,
    const char *drivers_dir,
    const char *output_iso,
    int auto_size
);

/**
 * AEFS-only 模式：追加AEFS内容到现有ISO
 * 
 * 用于两阶段构建：
 * 1. 阶段1：buildIso.sh 使用 dd/gdisk/mtools 创建基础可启动ISO（只有MBR+GPT+FAT32）
 * 2. 阶段2：本函数追加AEFS文件系统和内容到ISO
 * 
 * 参数:
 *   iso_path: 现有ISO文件路径（由阶段1创建）
 *   kernel_file: kernel.aki 路径
 *   installer_file: appInstaller.iya 路径（可选）
 *   drivers_dir: 驱动目录（可选）
 * 
 * 返回值:
 *   0: 成功
 *   -1: 失败
 */
int adl_iso_builder_append_aefs(
    const char *iso_path,
    const char *kernel_file,
    const char *installer_file,
    const char *drivers_dir
);

/**
 * 计算文件总大小
 */
uint64_t adl_iso_builder_calculate_size(
    const char *kernel_file,
    const char *efi_boot_file,
    const char *installer_file,
    const char *drivers_dir
);

#ifdef __cplusplus
}
#endif

#endif /* AETHELOS_ADL_ISO_BUILDER_H */
