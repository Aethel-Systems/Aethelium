/**
 * ============================================================================
 * AethelOS ADL (Aethel Disk Layout) - 工业级磁盘镜像编织器
 * ============================================================================
 * 实现完整的ADL引导架构、UEFI启动链、AEFS卷接入、及磁盘物理布局
 * 符合《AethelOS启动引导及镜像规范.txt》的所有要求
 * 
 * 物理扇区布局（512B/扇区）：
 *   LBA 0        - ADL-MBR混合引导扇区 (ADL Header + MBR分区表)
 *   LBA 1        - Shadow GPT头（UEFI固件兼容）
 *   LBA 2-33     - Shadow GPT分区项
 *   LBA 34-63    - ADL扩展元数据区
 *   LBA 64       - ADL分区表（主）
 *   LBA 96-1023  - 对齐预留区
 *   LBA 1024     - ESP (EFI System Partition) - FAT32格式
 *   LBA 263168   - AEFS主存储池
 *   末尾-33      - Shadow GPT备份
 *   末尾-1       - ADL Header备份
 * ============================================================================
 */

#ifndef AETHELOS_ADL_DISK_H
#define AETHELOS_ADL_DISK_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 常量定义
 * ============================================================================ */

#define SECTOR_SIZE                     512
#define SECTOR_SHIFT                    9

/* ADL Magic Number */
#define ADL_MAGIC_NUMBER                0x4145544845004C02ULL  /* "AETHEL\x00\x02" */

/* LBA定义 */
#define ADL_MBR_LBA                     0
#define ADL_GPT_PRIMARY_LBA             1
#define ADL_GPT_ENTRIES_LBA             2
#define ADL_METADATA_LBA                34
#define ADL_PARTITION_TABLE_LBA         64
#define ADL_RESERVED_END_LBA            1023
#define ESP_START_LBA                   1024
#define ESP_SIZE_SECTORS                262144  /* LBA 1024到263167，共262144个扇区 = 128MB */
#define AEFS_POOL_START_LBA             263168

/* ESP (FAT32)常数 */
#define ESP_FAT_SIZE_SECTORS            512  /* FAT表大小 */
#define ESP_ROOT_DIR_SECTORS            32   /* 根目录大小 */
#define FAT32_CLUSTER_SIZE              4096 /* 4KB簇 */
#define FAT32_CLUSTERS_PER_SECTOR       (SECTOR_SIZE / 4)

/* AEFS分区类型 */
#define AEFS_PARTITION_TYPE_UUID        {0x41, 0x45, 0x54, 0x48, 0x45, 0x4C, 0x50, 0x4F, \
                                         0x4F, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}

/* 引导文件常数 */
#define BOOTX64_EFI_FILENAME            "BOOTX64.EFI"
#define BOOTX64_MAX_SIZE                (16 * 1024 * 1024)  /* 16MB max */

/* ADL版本 */
#define ADL_VERSION_MAJOR               2
#define ADL_VERSION_MINOR               1

/* SSD识别 */
#define DISK_TYPE_UNKNOWN               0
#define DISK_TYPE_SSD                   1
#define DISK_TYPE_HDD                   2
#define DISK_TYPE_RAMDISK               3

/* ============================================================================
 * 数据结构定义
 * ============================================================================ */

/**
 * ADL Header - 440字节
 * 位于MBR的前440字节，包含引导信息和分区表位置
 */
typedef struct {
    uint8_t boot_code[3];                   /* +0: 跳转指令 EB xx 90 */
    uint8_t oem_id[8];                      /* +3: OEM标识 "AETHELOS" */
    uint16_t bytes_per_sector;              /* +11: 字节/扇区 (512) */
    uint8_t sectors_per_cluster;            /* +13: 扇区/簇 (8) */
    uint16_t reserved_sectors;              /* +14: 保留扇区数 (1) */
    uint8_t fats;                           /* +16: FAT表数 (2) */
    uint16_t root_entries;                  /* +17: 根目录项数 (512) */
    uint16_t total_sectors_16;              /* +19: 总扇区数（16位）(0) */
    uint8_t media_descriptor;               /* +21: 介质描述符 (0xF8) */
    uint16_t sectors_per_fat_16;            /* +22: FAT扇区数（16位）(0) */
    uint16_t sectors_per_track;             /* +24: 磁道扇区数 (63) */
    uint16_t heads;                         /* +26: 磁头数 (255) */
    uint32_t hidden_sectors;                /* +28: 隐藏扇区 (0) */
    uint32_t total_sectors_32;              /* +32: 总扇区数（32位）(0) */
    
    /* --- ADL特定字段（扩展） --- */
    uint32_t magic_number_low;              /* +36: ADL魔数低32位 */
    uint32_t magic_number_high;             /* +40: ADL魔数高32位 */
    uint64_t partition_table_lba;           /* +44: 分区表LBA (64) */
    uint64_t total_lba;                     /* +52: 总LBA数 */
    uint32_t disk_uuid_0;                   /* +60: 磁盘UUID部分1 */
    uint32_t disk_uuid_1;                   /* +64: 磁盘UUID部分2 */
    uint32_t disk_uuid_2;                   /* +68: 磁盘UUID部分3 */
    uint32_t disk_uuid_3;                   /* +72: 磁盘UUID部分4 */
    uint8_t adl_version_major;              /* +76: ADL主版本号 */
    uint8_t adl_version_minor;              /* +77: ADL副版本号 */
    uint8_t disk_type;                      /* +78: 磁盘类型 (SSD/HDD/RamDisk) */
    uint8_t reserved1;                      /* +79: 保留 */
    uint32_t checksum;                      /* +80: ADL Header校验和 */
    uint32_t reserved2;                     /* +84: 保留 */
    
    uint8_t boot_code_continued[360];       /* +88: 启动代码继续(360字节，共440字节) */
} ADL_Header;

/**
 * ADL分区表项 - 64字节
 */
typedef struct {
    uint8_t partition_uuid[16];             /* +0: 分区UUID */
    uint64_t partition_lba;                 /* +16: 分区起始LBA */
    uint64_t partition_size_sectors;        /* +24: 分区大小(扇区) */
    uint32_t partition_type;                /* +32: 分区类型 */
    uint32_t flags;                         /* +36: 分区标志 */
    char partition_name[16];                /* +40: 分区名称 */
    uint32_t reserved1;                     /* +56: 保留 */
    uint32_t reserved2;                     /* +60: 保留 */
} ADL_Partition_Entry;

/**
 * ADL分区表 - 扇区64处
 * 最多支持8个分区
 */
typedef struct {
    ADL_Partition_Entry entries[8];         /* 8 × 64字节 = 512字节 */
} ADL_Partition_Table;

/**
 * Shadow GPT Header - 用于UEFI兼容性
 * 位于LBA 1
 */
typedef struct {
    uint8_t signature[8];                   /* "EFI PART" */
    uint32_t revision;                      /* 0x00010000 */
    uint32_t header_size;                   /* 92 */
    uint32_t header_crc32;                  /* CRC32校验和 */
    uint32_t reserved1;                     /* 保留 */
    uint64_t my_lba;                        /* 此Header的LBA (1) */
    uint64_t alternate_lba;                 /* 备份Header的LBA */
    uint64_t first_usable_lba;              /* 第一个可用LBA */
    uint64_t last_usable_lba;               /* 最后一个可用LBA */
    uint8_t disk_guid[16];                  /* 磁盘GUID */
    uint64_t partition_entry_lba;           /* 分区项数组LBA (2) */
    uint32_t num_partition_entries;         /* 分区项数量 (1) */
    uint32_t sizeof_partition_entry;        /* 128字节 */
    uint32_t partition_entry_array_crc32;   /* 分区项数组CRC32 */
    uint8_t reserved2[420];                 /* 保留到512字节 */
} Shadow_GPT_Header;

/**
 * Shadow GPT分区项 - 128字节
 */
typedef struct {
    uint8_t partition_type_guid[16];        /* 分区类型GUID */
    uint8_t unique_partition_guid[16];      /* 唯一分区GUID */
    uint64_t starting_lba;                  /* 起始LBA */
    uint64_t ending_lba;                    /* 结束LBA */
    uint64_t attributes;                    /* 属性 */
    uint16_t partition_name[36];            /* 分区名称(UTF-16LE) */
} Shadow_GPT_Entry;

/**
 * FAT32启动扇区
 */
typedef struct {
    uint8_t jump[3];                        /* 跳转指令 */
    uint8_t oem_id[8];                      /* OEM标识 */
    uint16_t bytes_per_sector;              /* 字节/扇区 (512) */
    uint8_t sectors_per_cluster;            /* 扇区/簇 */
    uint16_t reserved_sectors;              /* 保留扇区数 */
    uint8_t num_fats;                       /* FAT表数 */
    uint16_t root_entries;                  /* 根目录项数 */
    uint16_t total_sectors_16;              /* 总扇区数(16位) */
    uint8_t media_descriptor;               /* 介质描述符 */
    uint16_t sectors_per_fat_16;            /* FAT扇区数(16位) */
    uint16_t sectors_per_track;             /* 磁道扇区数 */
    uint16_t heads;                         /* 磁头数 */
    uint32_t hidden_sectors;                /* 隐藏扇区 */
    uint32_t total_sectors_32;              /* 总扇区数(32位) */
    
    /* FAT32特定 */
    uint32_t sectors_per_fat_32;            /* FAT扇区数(32位) */
    uint16_t ext_flags;                     /* 扩展标志 */
    uint16_t fs_version;                    /* 文件系统版本 */
    uint32_t root_cluster;                  /* 根目录簇号 */
    uint16_t fs_info_sector;                /* FSInfo扇区号 */
    uint16_t backup_boot_sector;            /* 备份启动扇区 */
    uint8_t reserved1[12];                  /* 保留 */
    uint8_t drive_number;                   /* 驱动器号 */
    uint8_t reserved2;                      /* 保留 */
    uint8_t boot_signature;                 /* 启动签名 (0x29) */
    uint32_t volume_id;                     /* 卷ID */
    uint8_t volume_label[11];               /* 卷标签 */
    uint8_t fs_type[8];                     /* 文件系统类型 "FAT32   " */
    uint8_t boot_code[420];                 /* 启动代码 */
    uint16_t boot_sector_signature;         /* 0xAA55 */
} FAT32_BootSector;

/**
 * FAT32目录项 - 32字节
 */
typedef struct {
    uint8_t filename[8];                    /* 文件名(8.3格式的名部分) */
    uint8_t extension[3];                   /* 扩展名 */
    uint8_t attributes;                     /* 文件属性 */
    uint8_t reserved;                       /* 保留 */
    uint8_t creation_time_tenth;            /* 创建时间(十分之一秒) */
    uint16_t creation_time;                 /* 创建时间(时:分:秒) */
    uint16_t creation_date;                 /* 创建日期(年-1980:月:日) */
    uint16_t last_access_date;              /* 最后访问日期 */
    uint16_t high_cluster;                  /* 簇号高16位 */
    uint16_t write_time;                    /* 最后写入时间 */
    uint16_t write_date;                    /* 最后写入日期 */
    uint16_t low_cluster;                   /* 簇号低16位 */
    uint32_t file_size;                     /* 文件大小 */
} FAT32_DirEntry;

/* AEFS_Checkpoint定义在aefs.h中，这里不再重复定义 */

/**
 * AEFS卷清单项 (简化版本用于ADL ISO生成)
 */
typedef struct {
    uint8_t volume_uuid[16];                /* 卷UUID */
    uint64_t volume_root_lba;               /* 卷根Æ-Node的LBA */
    char volume_path[64];                   /* 卷路径 (如 ">:BOOT") */
    uint32_t reserved;                      /* 保留 */
} AEFS_ADL_Volume_Entry;

/**
 * ISO生成上下文
 */
typedef struct {
    FILE *iso_file;                         /* ISO文件指针 */
    const char *kernel_file;                /* 内核文件路径 */
    const char *efi_boot_file;              /* EFI启动文件路径 */
    uint64_t iso_size_mb;                   /* ISO大小(MB) */
    uint64_t total_lba;                     /* 总LBA数 */
    
    uint8_t disk_uuid[16];                  /* 磁盘UUID */
    uint8_t aefs_pool_uuid[16];             /* AEFS_POOL分区UUID */
    
    int disk_type;                          /* 磁盘类型标记 */
    
    /* 统计信息 */
    uint64_t kernel_size;
    uint64_t bootloader_size;
    
} ADL_ISO_Context;

/* ============================================================================
 * 函数声明
 * ============================================================================ */

/**
 * 初始化ADL磁盘上下文
 */
int adl_iso_context_create(ADL_ISO_Context *ctx, const char *kernel_file,
                           const char *efi_boot_file, const char *output_iso,
                           uint64_t iso_size_mb);

/**
 * 生成ADL Header（MBR + ADL元数据）
 */
int adl_generate_header(FILE *iso_file, uint64_t total_lba, 
                        const uint8_t *disk_uuid, int disk_type);

/**
 * 生成ADL分区表
 */
int adl_generate_partition_table(FILE *iso_file, const uint8_t *aefs_pool_uuid,
                                 uint64_t aefs_pool_lba, uint64_t aefs_pool_size);

/**
 * 生成Shadow GPT结构（UEFI兼容）
 */
int adl_generate_shadow_gpt(FILE *iso_file, const uint8_t *disk_uuid,
                            uint64_t total_lba);

/**
 * 初始化ESP FAT32分区
 */
int adl_initialize_esp_fat32(FILE *iso_file, const char *bootloader_file);

/**
 * 在FAT32中写入启动文件
 */
int adl_write_to_fat32(FILE *iso_file, const char *filename, 
                       const uint8_t *data, uint32_t size);

/**
 * 生成AEFS卷结构
 */
int adl_generate_aefs_volume(FILE *iso_file, uint64_t aefs_start_lba);

/**
 * 完整的ISO镜像生成函数
 */
int adl_generate_complete_iso(const char *kernel_file, const char *efi_boot_file,
                              const char *output_iso, uint64_t iso_size_mb);

/**
 * 磁盘类型识别
 */
int adl_detect_disk_type(const char *image_file);

#ifdef __cplusplus
}
#endif

#endif /* AETHELOS_ADL_DISK_H */
