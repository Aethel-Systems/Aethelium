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

/**
 * ============================================================================
 * AethelOS ADL ISO Builder - Core Implementation
 * ============================================================================
 * 完整的、符合AEFS规范的ISO镜像生成实现
 */

#include "adl_iso_builder.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

/* ============================================================================
 * 前置声明
 * ============================================================================ */
static int adl_write_file_to_iso(FILE *iso, const char *filename, uint64_t start_lba, 
                                  uint64_t *out_size);

/* ============================================================================
 * 结构大小验证（编译时）
 * ============================================================================
 * 
 * 这些编译时检查确保GPT结构的大小符合UEFI规范
 */

/* GPT Header必须恰好512字节（1个扇区） */
_Static_assert(sizeof(GPT_Header) == 512, 
    "GPT_Header must be exactly 512 bytes");

/* GPT分区项必须恰好128字节 */
_Static_assert(sizeof(GPT_Partition_Entry) == 128, 
    "GPT_Partition_Entry must be exactly 128 bytes");

/* ============================================================================
 * 工具函数
 * ============================================================================ */

/**
 * XXH3 哈希函数简化实现（用于校验和，不需要加密强度）
 */
static uint32_t adl_xxh3_hash(const uint8_t *data, size_t size) {
    uint32_t hash = 0x9E3779B1;
    
    for (size_t i = 0; i < size; i++) {
        hash = ((hash << 5) | (hash >> 27)) ^ data[i];
        hash *= 0xF4243;
    }
    
    return hash;
}

/**
 * 生成随机UUID
 */
static void adl_generate_uuid(uint8_t *uuid) {
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) {
        for (int i = 0; i < 16; i++) {
            uuid[i] = (uint8_t)rand();
        }
    } else {
        fread(uuid, 1, 16, f);
        fclose(f);
    }
    
    /* RFC 4122版本4标记 */
    uuid[6] = (uuid[6] & 0x0F) | 0x40;
    uuid[8] = (uuid[8] & 0x3F) | 0x80;
}

/**
 * 小端序写入16位整数
 */
static inline void le16_write(uint8_t *buf, uint16_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
}

/**
 * 小端序写入32位整数
 */
static inline void le32_write(uint8_t *buf, uint32_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;
}

/**
 * 大端序写入16位整数
 */
static inline void be16_write(uint8_t *buf, uint16_t val) {
    buf[0] = (val >> 8) & 0xFF;
    buf[1] = val & 0xFF;
}

/**
 * 大端序写入32位整数
 */
static inline void be32_write(uint8_t *buf, uint32_t val) {
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

/**
 * 小端序写入64位整数
 */
static inline void le64_write(uint8_t *buf, uint64_t val) {
    le32_write(buf, val & 0xFFFFFFFF);
    le32_write(buf + 4, (val >> 32) & 0xFFFFFFFF);
}

/**
 * 获取文件大小
 */
static uint64_t adl_get_file_size(const char *filename) {
    struct stat st;
    if (stat(filename, &st) != 0) {
        return 0;
    }
    return (uint64_t)st.st_size;
}

/**
 * 递归计算目录大小
 */
static uint64_t adl_get_directory_size(const char *dirpath) {
    DIR *dir = opendir(dirpath);
    if (!dir) {
        return 0;
    }
    
    uint64_t total_size = 0;
    struct dirent *entry;
    char full_path[1024];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(full_path, sizeof(full_path), "%s/%s", dirpath, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }
        
        if (S_ISDIR(st.st_mode)) {
            total_size += adl_get_directory_size(full_path);
        } else {
            total_size += (uint64_t)st.st_size;
        }
    }
    
    closedir(dir);
    return total_size;
}

/**
 * 检测输入是否为目录，如果是返回目录大小，否则返回文件大小
 */
static uint64_t adl_get_item_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    
    if (S_ISDIR(st.st_mode)) {
        return adl_get_directory_size(path);
    } else {
        return (uint64_t)st.st_size;
    }
}

/**
 * 检测输入是否为目录
 */
static int adl_is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

/**
 * 递归写入目录到ISO
 */
static int adl_write_directory_tree(FILE *iso, const char *dirpath, uint64_t *current_lba) {
    DIR *dir = opendir(dirpath);
    if (!dir) {
        fprintf(stderr, "ERROR: Cannot open directory %s: %s\n", dirpath, strerror(errno));
        return -1;
    }
    
    struct dirent *entry;
    char full_path[1024];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(full_path, sizeof(full_path), "%s/%s", dirpath, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) != 0) {
            fprintf(stderr, "WARNING: Cannot stat %s\n", full_path);
            continue;
        }
        
        if (S_ISDIR(st.st_mode)) {
            /* 递归处理子目录 */
            if (adl_write_directory_tree(iso, full_path, current_lba) != 0) {
                closedir(dir);
                return -1;
            }
        } else {
            /* 写入文件 */
            fprintf(stderr, "[ADL] Writing directory item: %s\n", entry->d_name);
            if (adl_write_file_to_iso(iso, full_path, *current_lba, NULL) != 0) {
                fprintf(stderr, "WARNING: Failed to write %s from directory\n", entry->d_name);
                continue;
            }
            
            uint64_t file_size = (uint64_t)st.st_size;
            uint64_t sectors_used = (file_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
            *current_lba += sectors_used;
        }
    }
    
    closedir(dir);
    return 0;
}

/**
 * 寻址到ISO中的特定LBA
 */
static int adl_seek_lba(FILE *iso, uint64_t lba) {
    uint64_t offset = lba * SECTOR_SIZE;
    if (fseek(iso, offset, SEEK_SET) != 0) {
        fprintf(stderr, "ERROR: Failed to seek to LBA %llu\n", (unsigned long long)lba);
        return -1;
    }
    return 0;
}

/**
 * 写入指定大小的数据并填充到扇区大小（最多512字节）
 */
static int adl_write_sector_padded(FILE *iso, const void *data, size_t size) {
    if (size > SECTOR_SIZE) {
        fprintf(stderr, "ERROR: Data size %zu exceeds sector size %d\n", size, SECTOR_SIZE);
        return -1;
    }
    
    if (fwrite(data, 1, size, iso) != size) {
        fprintf(stderr, "ERROR: Failed to write sector data\n");
        return -1;
    }
    
    if (size < SECTOR_SIZE) {
        uint8_t padding[SECTOR_SIZE];
        memset(padding, 0, SECTOR_SIZE - size);
        if (fwrite(padding, 1, SECTOR_SIZE - size, iso) != SECTOR_SIZE - size) {
            fprintf(stderr, "ERROR: Failed to write sector padding\n");
            return -1;
        }
    }
    
    return 0;
}

/**
 * 写入指定大小的数据并填充到块大小（最多4096字节）
 */
static int adl_write_block_padded(FILE *iso, const void *data, size_t size) {
    if (size > BLOCK_SIZE) {
        fprintf(stderr, "ERROR: Data size %zu exceeds block size %d\n", size, BLOCK_SIZE);
        return -1;
    }
    
    if (fwrite(data, 1, size, iso) != size) {
        fprintf(stderr, "ERROR: Failed to write block data\n");
        return -1;
    }
    
    if (size < BLOCK_SIZE) {
        uint8_t padding[BLOCK_SIZE];
        memset(padding, 0, BLOCK_SIZE - size);
        if (fwrite(padding, 1, BLOCK_SIZE - size, iso) != BLOCK_SIZE - size) {
            fprintf(stderr, "ERROR: Failed to write block padding\n");
            return -1;
        }
    }
    
    return 0;
}

/**
 * 写入完整扇区（需要恰好512字节）
 */
static int adl_write_sector(FILE *iso, const uint8_t *sector) {
    if (fwrite(sector, 1, SECTOR_SIZE, iso) != SECTOR_SIZE) {
        fprintf(stderr, "ERROR: Failed to write full sector\n");
        return -1;
    }
    return 0;
}

/**
 * 将整个文件写入ISO从指定位置开始
 */
static int adl_write_file_to_iso(FILE *iso, const char *filename, uint64_t start_lba, 
                                  uint64_t *out_size) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: Cannot open file %s: %s\n", filename, strerror(errno));
        return -1;
    }
    
    if (adl_seek_lba(iso, start_lba) != 0) {
        fclose(f);
        return -1;
    }
    
    uint8_t buffer[8192];
    uint64_t total_written = 0;
    size_t bytes_read;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        if (fwrite(buffer, 1, bytes_read, iso) != bytes_read) {
            fprintf(stderr, "ERROR: Failed to write data to ISO\n");
            fclose(f);
            return -1;
        }
        total_written += bytes_read;
    }
    
    if (ferror(f)) {
        fprintf(stderr, "ERROR: Error reading file %s\n", filename);
        fclose(f);
        return -1;
    }
    
    fclose(f);
    
    if (out_size) {
        *out_size = total_written;
    }
    
    return 0;
}

/* ============================================================================
 * ADL Header 生成
 * ============================================================================ */

/**
 * 生成ADL Header（前440字节）
 * 现在位于LBA 2（不是LBA 0），以避免破坏MBR识别
 */
static int adl_generate_header(uint8_t *header, uint64_t total_lba, 
                                const uint8_t *disk_uuid) {
    memset(header, 0, 440);
    
    /* 启动跳转指令（兼容某些工具） */
    header[0] = 0xEB;
    header[1] = 0x3C;
    header[2] = 0x90;
    
    /* OEM ID */
    memcpy(&header[3], "AETHELOS", 8);
    
    /* 字节/扇区 */
    le16_write(&header[11], SECTOR_SIZE);
    
    /* 扇区/簇 */
    header[13] = 8;
    
    /* 保留扇区 */
    le16_write(&header[14], 1);
    
    /* FAT表数 */
    header[16] = 2;
    
    /* 根目录项数 */
    le16_write(&header[17], 512);
    
    /* 介质描述符 */
    header[21] = 0xF8;
    
    /* 磁道扇区数 */
    le16_write(&header[24], 63);
    
    /* 磁头数 */
    le16_write(&header[26], 255);
    
    /* === ADL特定字段 === */
    
    /* ADL魔数 */
    le32_write(&header[36], (uint32_t)(ADL_MAGIC_NUMBER & 0xFFFFFFFF));
    le32_write(&header[40], (uint32_t)((ADL_MAGIC_NUMBER >> 32) & 0xFFFFFFFF));
    
    /* 分区表LBA */
    le64_write(&header[44], ADL_PARTITION_TABLE_LBA);
    
    /* 备份分区表LBA - 真实实现（在靠近磁盘末尾处） */
    le64_write(&header[52], total_lba - 34);  /* 备份在倒数第34个块 */
    
    /* 总LBA数 */
    le64_write(&header[60], total_lba);
    
    /* 磁盘UUID */
    memcpy(&header[68], disk_uuid, 16);
    
    /* ADL版本 */
    header[84] = 2;  /* 主版本 */
    header[85] = 1;  /* 副版本 */
    
    /* 磁盘类型 */
    header[86] = 1;  /* SSD */
    
    return 0;
}

/**
 * 在ADL_METADATA_LBA处写入ADL Header
 * 新的设计：ADL Header可在任意位置（不必在LBA 0）
 * 现在放在ESP之后、AEFS之前的预留区域
 */
static int adl_write_adl_header(FILE *iso, uint64_t total_lba, 
                                 const uint8_t *disk_uuid) {
    uint8_t adl_header[440];
    adl_generate_header(adl_header, total_lba, disk_uuid);
    
    /* 计算Header校验和（包括checksum字段作为0） */
    uint32_t checksum = adl_xxh3_hash(adl_header, 440);
    le32_write(&adl_header[88], checksum);  /* 校验和位置在offset 88 */
    
    /* 在ADL_METADATA_LBA处写入ADL Header（元数据位置） */
    if (adl_seek_lba(iso, ADL_METADATA_LBA) != 0) {
        return -1;
    }
    
    /* 虽然ADL Header只有440字节，但必须填充到扇区大小（512字节） */
    uint8_t sector[SECTOR_SIZE];
    memset(sector, 0, SECTOR_SIZE);
    memcpy(sector, adl_header, 440);
    
    if (adl_write_sector(iso, sector) != 0) {
        return -1;
    }
    
    fprintf(stderr, "[ADL] Wrote ADL Header at LBA %d (metadata after ESP)\n", ADL_METADATA_LBA);
    return 0;
}

/**
 * 生成标准的 Protective MBR（64字节）
 * 这是标准的MBR分区表格式，LBA 0必须以这个开头才能被UEFI正确识别
 * 包含一个0xEE类型的分区项用于保护GPT分区表
 */
static int adl_generate_protective_mbr(uint8_t *table) {
    memset(table, 0, 64);
    
    /* 
     * Protective MBR遵循标准MBR分区表格式：
     * 4个分区项，每个16字节，共64字节
     * UEFI固件会检查这个来决定是否读取GPT
     */
    
    /* 分区项1（偏移0）：GPT保护分区（类型0xEE）*/
    table[0] = 0x00;      /* 启动标记：不可启动 */
    table[1] = 0x00;      /* 起始磁头 */
    table[2] = 0x01;      /* 起始扇区 (低6位) | 柱面(高2位) */
    table[3] = 0x00;      /* 起始柱面 */
    table[4] = 0xEE;      /* 分区类型: GPT保护分区 (UEFI识别标记) */
    table[5] = 0xFF;      /* 结束磁头: 255 */
    table[6] = 0xFF;      /* 结束扇区: 63 (低6位) | 结束柱面(高2位) = 0xFF */
    table[7] = 0xFF;      /* 结束柱面: 255 */
    le32_write(&table[8], 1);                   /* 起始LBA: 1 (GPT Header位置) */
    le32_write(&table[12], 0xFFFFFFFF);         /* 大小：填满整个磁盘（作为保护） */
    
    /* 分区项2-4（偏移16、32、48）：保持为零（空分区） */
    /* 这是标准做法，Protective MBR通常只有一个分区项 */
    
    /* MBR签名 */
    table[62] = 0x55;
    table[63] = 0xAA;
    
    return 0;
}

/* ============================================================================
 * ADL分区表生成
 * ============================================================================ */

/**
 * 生成ADL分区表项（esp和aefs两个分区）
 */
static int adl_generate_partition_table(FILE *iso, 
                                        const uint8_t *esp_uuid,
                                        const uint8_t *aefs_uuid,
                                        uint64_t total_lba) {
    uint8_t sector[SECTOR_SIZE];
    memset(sector, 0, SECTOR_SIZE);
    
    ADL_Partition_Entry *entries = (ADL_Partition_Entry *)sector;
    
    /* 第一个分区: ESP */
    {
        uint8_t esp_type_uuid[16] = {
            0xC1, 0x2A, 0x73, 0x28, 0xF8, 0x1F, 0x11, 0xD2,
            0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
        };
        
        ADL_Partition_Entry *esp_entry = &entries[0];
        memcpy(esp_entry->partition_uuid, esp_uuid, 16);
        memcpy(esp_entry->partition_type_uuid, esp_type_uuid, 16);
        le64_write((uint8_t*)&esp_entry->start_lba, ESP_START_LBA);
        le64_write((uint8_t*)&esp_entry->end_lba, ESP_START_LBA + ESP_SIZE_SECTORS - 1);
        le64_write((uint8_t*)&esp_entry->flags, 0x01);  /* 活跃 */
        esp_entry->resilience_policy = 0;  /* 独立 */
        esp_entry->member_count = 1;
        
        /* 计算校验和 */
        uint32_t checksum = adl_xxh3_hash((uint8_t*)esp_entry, 
                                          sizeof(ADL_Partition_Entry) - 4);
        le32_write((uint8_t*)&esp_entry->entry_checksum, checksum);
    }
    
    /* 第二个分区: AEFS_POOL */
    {
        uint8_t aefs_type_uuid[16] = {
            0x41, 0x45, 0x46, 0x53, 0x50, 0x4F, 0x4F, 0x4C,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        
        ADL_Partition_Entry *aefs_entry = &entries[1];
        memcpy(aefs_entry->partition_uuid, aefs_uuid, 16);
        memcpy(aefs_entry->partition_type_uuid, aefs_type_uuid, 16);
        le64_write((uint8_t*)&aefs_entry->start_lba, AEFS_POOL_START_LBA);
        le64_write((uint8_t*)&aefs_entry->end_lba, total_lba - 34);  /* 预留33个块用于备份 */
        le64_write((uint8_t*)&aefs_entry->flags, 0x01);  /* 活跃 */
        aefs_entry->resilience_policy = 0;  /* 独立 */
        aefs_entry->member_count = 1;
        
        /* 计算校验和 */
        uint32_t checksum = adl_xxh3_hash((uint8_t*)aefs_entry, 
                                          sizeof(ADL_Partition_Entry) - 4);
        le32_write((uint8_t*)&aefs_entry->entry_checksum, checksum);
    }
    
    if (adl_seek_lba(iso, ADL_PARTITION_TABLE_LBA) != 0) {
        return -1;
    }
    
    return adl_write_sector(iso, sector);
}

/* ============================================================================
 * AEFS核心结构生成
 * ============================================================================ */

/**
 * 生成AEFS Anchor Block（第一个元数据块）
 */
static int adl_generate_aefs_anchor(FILE *iso, uint64_t aefs_lba, 
                                     const uint8_t *pool_uuid) {
    AEFS_Anchor_Block anchor;
    memset(&anchor, 0, sizeof(anchor));
    
    anchor.magic = 0x5346454100000000ULL;  /* "AEFS\0\0" */
    anchor.version = 1;
    anchor.block_size = BLOCK_SIZE;
    anchor.total_blocks = 0;  /* 会根据ISO大小计算 */
    anchor.checkpoint_lba = aefs_lba + 1;  /* Checkpoint紧跟Anchor */
    anchor.checkpoint_backup_lba = 0;  /* 暂时未实现 */
    anchor.segment_log_start_lba = aefs_lba + 10;  /* 日志从LBA10开始 */
    memcpy(anchor.pool_uuid, pool_uuid, 16);
    anchor.generation = 1;
    
    /* 计算校验和 */
    anchor.anchor_checksum = 0;  /* 先清零 */
    uint32_t checksum = adl_xxh3_hash((uint8_t*)&anchor, sizeof(AEFS_Anchor_Block) - 4);
    anchor.anchor_checksum = checksum;
    
    if (adl_seek_lba(iso, aefs_lba) != 0) {
        return -1;
    }
    
    return adl_write_sector_padded(iso, &anchor, sizeof(AEFS_Anchor_Block));
}

/**
 * 生成AEFS Checkpoint - 真实实现
 */
static int adl_generate_aefs_checkpoint(FILE *iso, uint64_t checkpoint_lba,
                                         uint64_t aefs_start_lba) {
    AEFS_Checkpoint checkpoint;
    memset(&checkpoint, 0, sizeof(checkpoint));
    
    checkpoint.magic = 0x5245534b50434841ULL;  /* "CHECKPOINT" */
    checkpoint.version = 1;
    checkpoint.timestamp = time(NULL);
    checkpoint.generation = 1;
    checkpoint.log_head_lba = aefs_start_lba + 10;
    checkpoint.log_tail_lba = aefs_start_lba + 10;
    checkpoint.volume_count = 2;  /* >:BOOT 和 >:PAYLOAD */
    checkpoint.volume_tree_lba = aefs_start_lba + 2;  /* Volume树在Checkpoint紧后 */
    
    /* 计算校验和 */
    checkpoint.checkpoint_checksum = 0;
    uint32_t checksum = adl_xxh3_hash((uint8_t*)&checkpoint, 
                                      sizeof(AEFS_Checkpoint) - 4);
    checkpoint.checkpoint_checksum = checksum;
    
    if (adl_seek_lba(iso, checkpoint_lba) != 0) {
        return -1;
    }
    
    // 主checkpoint写入
    int ret = adl_write_block_padded(iso, &checkpoint, sizeof(AEFS_Checkpoint));
    if (ret != 0) return ret;
    
    // 写入备份checkpoint（在主checkpoint之后）
    uint64_t backup_checkpoint_lba = checkpoint_lba + 1;
    if (adl_seek_lba(iso, backup_checkpoint_lba) != 0) {
        return -1;
    }
    
    return adl_write_block_padded(iso, &checkpoint, sizeof(AEFS_Checkpoint));
}

/**
 * 生成Volume B+树根（包含>:BOOT和>:PAYLOAD卷描述符）
 */
static int adl_generate_volume_tree(FILE *iso, uint64_t tree_lba,
                                     const uint8_t *boot_uuid,
                                     const uint8_t *payload_uuid) {
    uint8_t sector[BLOCK_SIZE];
    memset(sector, 0, BLOCK_SIZE);
    
    /* 第一个卷: >:BOOT */
    AEFS_Volume_Descriptor *boot_desc = (AEFS_Volume_Descriptor *)(sector + 0);
    memcpy(boot_desc->volume_uuid, boot_uuid, 16);
    boot_desc->root_node_lba = tree_lba + (BLOCK_SIZE / 512) + 10;  /* 真实初始化root节点LBA */
    boot_desc->volume_name_len = strlen(">:BOOT");
    memcpy(boot_desc->volume_name, (uint8_t*)">:BOOT", boot_desc->volume_name_len);
    boot_desc->flags = 0;  /* 读写 */
    boot_desc->used_blocks = 0;
    boot_desc->quota_blocks = 0;  /* 无配额限制 */
    boot_desc->compression = 0;  /* 无压缩 */
    boot_desc->redundancy = 1;  /* 单副本 */
    boot_desc->created_time = time(NULL);
    boot_desc->modified_time = time(NULL);
    
    uint32_t checksum = adl_xxh3_hash((uint8_t*)boot_desc, 
                                      sizeof(AEFS_Volume_Descriptor) - 4);
    boot_desc->descriptor_checksum = checksum;
    
    /* 第二个卷: >:PAYLOAD */
    AEFS_Volume_Descriptor *payload_desc = (AEFS_Volume_Descriptor *)(sector + 
                                            sizeof(AEFS_Volume_Descriptor));
    memcpy(payload_desc->volume_uuid, payload_uuid, 16);
    payload_desc->root_node_lba = tree_lba + (BLOCK_SIZE / 512) + 50;  /* >:PAYLOAD的root节点LBA */
    payload_desc->volume_name_len = strlen(">:PAYLOAD");
    memcpy(payload_desc->volume_name, (uint8_t*)">:PAYLOAD", 
           payload_desc->volume_name_len);
    payload_desc->flags = 0;  /* 读写 */
    payload_desc->used_blocks = 0;
    payload_desc->quota_blocks = 0;
    payload_desc->compression = 0;
    payload_desc->redundancy = 1;
    payload_desc->created_time = time(NULL);
    payload_desc->modified_time = time(NULL);
    
    checksum = adl_xxh3_hash((uint8_t*)payload_desc, 
                             sizeof(AEFS_Volume_Descriptor) - 4);
    payload_desc->descriptor_checksum = checksum;
    
    if (adl_seek_lba(iso, tree_lba) != 0) {
        return -1;
    }
    
    return adl_write_block_padded(iso, sector, BLOCK_SIZE);
}
/* ============================================================================
 * FAT32 ESP 初始化
 * ============================================================================ */

/**
 * 计算CRC32（标准UEFI/GPT CRC32-CCITT算法）
 * 这是UEFI标准中的CRC32实现，用于GPT分区表验证
 * 多项式：0xEDB88320（CRC-32反向）
 * 初始值：0xFFFFFFFF
 * 最终XOR：0xFFFFFFFF
 */
static uint32_t gpt_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    static const uint32_t poly = 0xEDB88320;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? poly : 0);
        }
    }
    
    return crc ^ 0xFFFFFFFF;
}

/**
 * 生成Shadow GPT头（LBA 1处）
 * 满足UEFI固件对GPT的要求-包含完整的CRC32校验
 * 
 * 关键：
 * 1. GPT Header CRC32必须对Header中除了crc32字段外的所有内容计算
 * 2. 分区项数组CRC32由调用者提供，必须对所有128个分区项计算
 * 3. 两个CRC32值都必须被填入GPT Header后再写入磁盘
 */
static int adl_generate_gpt_header(FILE *iso, uint64_t total_lba, 
                                    const uint8_t *disk_uuid,
                                    uint32_t partition_entries_crc32) {
    GPT_Header gpt;
    memset(&gpt, 0, sizeof(gpt));
    
    /* === 填充GPT Header所有字段 === */
    
    /* 签名："EFI PART" */
    memcpy(gpt.signature, "EFI PART", 8);
    
    /* 版本：1.0 */
    gpt.revision = 0x00010000;
    
    /* Header大小：92字节（标准） */
    gpt.header_size = 92;
    
    /* 当前LBA：1（主GPT Header位置） */
    gpt.current_lba = SHADOW_GPT_HEAD_LBA;
    
    /* 备份LBA：iso最后一个LBA */
    gpt.backup_lba = total_lba - 1;
    
    /* 最小可用LBA：GPT分区表后（LBA 2-33是分区表，所以从LBA 34开始） */
    gpt.first_usable_lba = SHADOW_GPT_ENTRY_LBA + 32;
    
    /* 最大可用LBA：保留最后33个扇区用于备份 */
    gpt.last_usable_lba = total_lba - 34;
    
    /* 磁盘GUID */
    memcpy(gpt.disk_guid, disk_uuid, 16);
    
    /* 分区项起始LBA：2 */
    gpt.partition_entry_lba = SHADOW_GPT_ENTRY_LBA;
    
    /* 分区项数量：128（标准） */
    gpt.num_partition_entries = 128;
    
    /* 分区项大小：128字节 */
    gpt.partition_entry_size = 128;
    
    /* 分区项数组CRC32：由调用者计算并提供 */
    gpt.partition_array_crc32 = partition_entries_crc32;
    
    /* === 计算Header CRC32（关键步骤） === */
    /* 
     * 步骤：
     * 1. Header CRC32字段必须为0（不计入CRC的计算）
     * 2. 计算前92字节的CRC32（Header size字段指定的大小）
     * 3. 将计算结果写入crc32字段
     */
    
    /* 步骤1：确保crc32字段为0 */
    gpt.crc32 = 0;
    
    /* 步骤2：计算Header CRC32（前92字节） */
    uint32_t header_crc = gpt_crc32((uint8_t*)&gpt, 92);
    
    /* 步骤3：将计算结果写入crc32字段 */
    gpt.crc32 = header_crc;
    
    /* === 写入GPT Header到磁盘 === */
    if (adl_seek_lba(iso, SHADOW_GPT_HEAD_LBA) != 0) {
        fprintf(stderr, "ERROR: Failed to seek to GPT Header LBA\n");
        return -1;
    }
    
    /* 写入到512字节扇区（后面用0填充） */
    uint8_t sector[SECTOR_SIZE];
    memset(sector, 0, SECTOR_SIZE);
    memcpy(sector, &gpt, sizeof(gpt));  /* sizeof(GPT_Header)应该==512字节 */
    
    if (adl_write_sector(iso, sector) != 0) {
        fprintf(stderr, "ERROR: Failed to write GPT Header\n");
        return -1;
    }
    
    fprintf(stderr, "[GPT] Written GPT header at LBA 1\n");
    fprintf(stderr, "[GPT]   Signature: 'EFI PART' (OK)\n");
    fprintf(stderr, "[GPT]   Revision: 0x%08X\n", gpt.revision);
    fprintf(stderr, "[GPT]   Header Size: %d bytes\n", gpt.header_size);
    fprintf(stderr, "[GPT]   Header CRC32: 0x%08X (calculated from bytes 0-91)\n", gpt.crc32);
    fprintf(stderr, "[GPT]   Partition Array CRC32: 0x%08X (calculated from 128 entries)\n", 
            gpt.partition_array_crc32);
    fprintf(stderr, "[GPT]   Current LBA: %llu\n", (unsigned long long)gpt.current_lba);
    fprintf(stderr, "[GPT]   Partition Entry LBA: %llu\n", 
            (unsigned long long)gpt.partition_entry_lba);
    fprintf(stderr, "[GPT]   Num Partition Entries: %d\n", gpt.num_partition_entries);
    
    return 0;
}

/**
 * 生成Shadow GPT分区项（LBA 2-33处）
 * 完整128个分区项表（第0个为ESP，第1个为ADL Anchor，其余为空）
 * 
 * ADL Anchor分区（第1个）指向LBA 34的ADL Header，使BOOTX64.EFI可通过UEFI
 * EFI_PARTITION_INFO_PROTOCOL发现ADL磁盘标记符
 * 
 * 关键：必须正确计算所有128个分区项的CRC32值
 * - 计算对象：所有128个分区项（128 * 128 = 16384字节）
 * - 方法：调用gpt_crc32()对整个缓冲区计算
 * - 用途：将CRC32值提供给adl_generate_gpt_header用于填入GPT Header
 */
static int adl_generate_gpt_entries(FILE *iso, uint64_t esp_start, uint64_t esp_end,
                                     const uint8_t *esp_uuid,
                                     const uint8_t *adl_anchor_uuid,
                                     uint32_t *out_entries_crc32) {
    /* 完整的128个分区项（32个扇区）= 128 * 128字节 */
    uint8_t entries_buf[128 * 128];
    memset(entries_buf, 0, sizeof(entries_buf));
    
    /* 第一个分区：ESP (FAT32) */
    {
        GPT_Partition_Entry *entry = (GPT_Partition_Entry *)entries_buf;
        
        /* ESP分区类型GUID (C12A7328-F81F-11D2-BA4B-00A0C93EC93B) */
        uint8_t esp_type[16] = {
            0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
            0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
        };
        memcpy(entry->partition_type_guid, esp_type, 16);
        memcpy(entry->partition_guid, esp_uuid, 16);
        
        /* LBA范围（小端序） */
        entry->starting_lba = esp_start;
        entry->ending_lba = esp_end;
        
        /* 分区属性：Bit 0设置为1表示分区必要可用（Platform Required） */
        entry->attributes = 0x0000000000000001;  /* Bit 0 = 1: Platform Required */
        
        /* 分区名(UTF-16LE): "EFI System Partition" */
        memcpy(entry->partition_name, "E\0F\0I\0 \0S\0y\0s\0t\0e\0m\0", 20);
        
        fprintf(stderr, "[GPT] Partition 0: ESP FAT32 (LBA %llu-%llu, attributes=0x%016llx)\n", 
                esp_start, esp_end, (unsigned long long)entry->attributes);
    }
    
    /* 第二个分区：ADL Anchor（指向LBA 34的ADL Header）*/
    {
        GPT_Partition_Entry *entry = (GPT_Partition_Entry *)(entries_buf + 128);
        
        /* ADL Anchor分区类型GUID: A378E145-AD11-4D53-BD4C-ADDE1DA0AD11 */
        /* 按小端序存储 */
        uint8_t adl_anchor_type[16] = {
            0x45, 0xE1, 0x78, 0xA3, 0x11, 0xAD, 0x53, 0x4D,
            0xBD, 0x4C, 0xAD, 0xDE, 0x1D, 0xA0, 0xAD, 0x11
        };
        memcpy(entry->partition_type_guid, adl_anchor_type, 16);
        memcpy(entry->partition_guid, adl_anchor_uuid, 16);
        
        /* LBA范围：ADL Header位于LBA 34，占用1个扇区（LBA 34-34） */
        entry->starting_lba = ADL_METADATA_LBA;
        entry->ending_lba = ADL_METADATA_LBA;
        
        /* 分区属性：无特殊标志 */
        entry->attributes = 0x0000000000000000;
        
        /* 分区名(UTF-16LE): "ADL Anchor" */
        memcpy(entry->partition_name, "A\0D\0L\0 \0A\0n\0c\0h\0o\0r\0", 20);
        
        fprintf(stderr, "[GPT] Partition 1: ADL Anchor (LBA %llu-%llu, attributes=0x%016llx)\n", 
                (unsigned long long)ADL_METADATA_LBA, (unsigned long long)ADL_METADATA_LBA,
                (unsigned long long)entry->attributes);
    }
    
    /* 其余126个分区项保持为零 */
    
    /* 
     * === 计算分区项数组CRC32（关键步骤） ===
     * 
     * UEFI/GPT规范要求：
     * - 对所有128个分区项（16384字节）的完整缓冲区计算CRC32
     * - 使用CRC32-CCITT算法（多项式0xEDB88320）
     * - 此CRC32值会被GPT Header引用
     */
    uint32_t entries_crc = gpt_crc32(entries_buf, sizeof(entries_buf));
    if (out_entries_crc32) {
        *out_entries_crc32 = entries_crc;
    }
    
    /* 
     * === 写入分区项到磁盘 ===
     * 
     * 128个分区项 × 128字节/项 = 16384字节 = 32个扇区
     * 存储位置：LBA 2-33
     */
    if (adl_seek_lba(iso, SHADOW_GPT_ENTRY_LBA) != 0) {
        fprintf(stderr, "ERROR: Failed to seek to GPT entries LBA\n");
        return -1;
    }
    
    for (int i = 0; i < 32; i++) {
        if (fwrite(&entries_buf[i * SECTOR_SIZE], 1, SECTOR_SIZE, iso) != SECTOR_SIZE) {
            fprintf(stderr, "ERROR: Failed to write GPT partition entry sector %d\n", i);
            return -1;
        }
    }
    
    fprintf(stderr, "[GPT] Written partition entries at LBA 2-33\n");
    fprintf(stderr, "[GPT]   Partition 0: EFI System Partition (ESP FAT32)\n");
    fprintf(stderr, "[GPT]     Type GUID: C12A7328-F81F-11D2-BA4B-00A0C93EC93B\n");
    fprintf(stderr, "[GPT]     LBA Range: %llu - %llu\n", 
            (unsigned long long)esp_start, (unsigned long long)esp_end);
    fprintf(stderr, "[GPT]     Attributes: 0x0000000000000001 (Platform Required)\n");
    fprintf(stderr, "[GPT]   Partition 1: ADL Anchor Block\n");
    fprintf(stderr, "[GPT]     Type GUID: A378E145-AD11-4D53-BD4C-ADDE1DA0AD11\n");
    fprintf(stderr, "[GPT]     LBA Range: %d - %d (ADL Header location)\n", 
            ADL_METADATA_LBA, ADL_METADATA_LBA);
    fprintf(stderr, "[GPT]     Attributes: 0x0000000000000000 (No special flags)\n");
    fprintf(stderr, "[GPT]   Partitions 2-127: Empty\n");
    fprintf(stderr, "[GPT]   Total CRC32 (128 entries): 0x%08X\n", entries_crc);
    fprintf(stderr, "[GPT]   Verification: This CRC32 will be stored in GPT Header at offset 88\n");
    
    return 0;
}

/* ============================================================================
 * ISO 9660 和 El Torito 生成函数
 * ============================================================================ */

/**
 * 生成ISO 9660 Primary Volume Descriptor (PVD)
 * 位于LBA 16，32字节对齐
 */
static int adl_generate_iso9660_pvd(FILE *iso) {
    ISO9660_PVD pvd;
    memset(&pvd, 0, sizeof(pvd));
    
    pvd.type = 0x01;  /* PVD */
    memcpy(pvd.identifier, "CD001", 5);
    pvd.version = 0x01;
    memcpy(pvd.system_identifier, "AETHELOS           ", 32);
    memcpy(pvd.volume_identifier, "AETHELOS ISO       ", 32);
    
    /* 容量设置为足够大的ISO（200MB作为示例） */
    uint32_t total_sectors = 400000;
    le32_write((uint8_t *)&pvd.space_size_le, total_sectors);
    be32_write((uint8_t *)&pvd.space_size_be, total_sectors);
    
    pvd.volume_set_size_le = 0x01;
    pvd.volume_set_size_be = 0x01;
    pvd.volume_seq_num_le = 0x01;
    pvd.volume_seq_num_be = 0x01;
    
    /* 逻辑块大小 = 2048字节（ISO 9660标准） */
    le16_write((uint8_t *)&pvd.logical_block_size_le, 2048);
    be16_write((uint8_t *)&pvd.logical_block_size_be, 2048);
    
    memcpy(pvd.publisher_id, "AETHELOS PROJECT   ", 128);
    memcpy(pvd.application_id, "AETHEL BOOT LOADER ", 128);
    
    /* 根目录记录 */
    uint8_t root_dir[34];
    memset(root_dir, 0, 34);
    
    uint8_t sector_buf[2048];
    memset(sector_buf, 0, 2048);
    memcpy(sector_buf, &pvd, sizeof(pvd));
    
    /* 定位到LBA 16 */
    if (adl_seek_lba(iso, 16) != 0) {
        return -1;
    }
    
    /* ISO 9660使用2048字节块，需要写入4个512字节扇区 */
    for (int i = 0; i < 4; i++) {
        if (fwrite(&sector_buf[i * 512], 1, 512, iso) != 512) {
            return -1;
        }
    }
    
    fprintf(stderr, "[ISO9660] Wrote PVD at LBA 16\n");
    return 0;
}

/**
 * 生成El Torito启动记录和启动目录
 * 启动记录位于LBA 17（紧跟PVD之后）
 * 启动目录位于LBA 18
 */
static int adl_generate_el_torito(FILE *iso) {
    /* === El Torito启动记录 === */
    uint8_t boot_record_sector[512];
    memset(boot_record_sector, 0, 512);
    
    /* 启动记录在sector中从偏移32开始 */
    ElTorito_BootRecord boot_rec;
    memset(&boot_rec, 0, sizeof(boot_rec));
    boot_rec.type = 0x00;
    memcpy(boot_rec.identifier, "EL TO", 5);  /* "EL TORITO SPECIFICATION" 的起始 */
    boot_rec.version = 0x01;
    le32_write((uint8_t *)&boot_rec.boot_catalog_lba, 18);  /* Boot Catalog at LBA 18 */
    
    memcpy(boot_record_sector + 32, &boot_rec, sizeof(boot_rec));
    memset(boot_record_sector + 32 + sizeof(boot_rec), 0, 512 - 32 - sizeof(boot_rec));
    
    /* 增加终止符扇区标记 */
    boot_record_sector[512 - 1] = 0xFF;
    
    /* 定位到LBA 17 */
    if (adl_seek_lba(iso, 17) != 0) {
        return -1;
    }
    if (fwrite(boot_record_sector, 1, 512, iso) != 512) {
        return -1;
    }
    
    fprintf(stderr, "[ElTorito] Wrote Boot Record at LBA 17\n");
    
    /* === El Torito启动目录 === */
    uint8_t boot_catalog_sector[512];
    memset(boot_catalog_sector, 0, 512);
    
    /* 验证区（8字节） */
    boot_catalog_sector[0] = 0x01;  /* 验证区指示符 */
    boot_catalog_sector[1] = 0x00;  /* 平台 80x86 */
    boot_catalog_sector[2] = 0x00;
    boot_catalog_sector[3] = 0x00;
    
    /* 制造商ID */
    memcpy(&boot_catalog_sector[4], "AETH", 4);
    
    /* 校验和（通常为0，暂留） */
    boot_catalog_sector[8] = 0;
    
    /* 启动项1: UEFI启动条目 (偏移32) */
    int entry_offset = 32;
    ElTorito_BootDirEntry uefi_entry;
    memset(&uefi_entry, 0, sizeof(uefi_entry));
    
    uefi_entry.boot_indicator = 0x88;         /* 可启动 */
    uefi_entry.boot_media_type = 0x00;        /* 无仿真 */
    uefi_entry.system_type = 0xEF;            /* UEFI */
    le16_write((uint8_t *)&uefi_entry.sector_count, 1);
    le32_write((uint8_t *)&uefi_entry.load_lba, 1024);  /* 指向ESP FAT32区 */
    
    memcpy(&boot_catalog_sector[entry_offset], &uefi_entry, sizeof(uefi_entry));
    
    fprintf(stderr, "[ElTorito] Created UEFI boot entry at offset %d\n", entry_offset);
    
    /* 定位到LBA 18 */
    if (adl_seek_lba(iso, 18) != 0) {
        return -1;
    }
    if (fwrite(boot_catalog_sector, 1, 512, iso) != 512) {
        return -1;
    }
    
    fprintf(stderr, "[ElTorito] Wrote Boot Catalog at LBA 18\n");
    return 0;
}

/**
 * FAT32的规范目录项编码（8.3短名称）
 */
typedef struct {
    uint8_t name[8];
    uint8_t ext[3];
    uint8_t attr;
    uint8_t reserved;
    uint8_t creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_low;
    uint32_t file_size;
} FAT32_DirEntry;

/**
 * 将字符串填充到FAT32 8.3格式（大小写规范化）
 */
static void fat32_pad_name(uint8_t *name, uint8_t *ext, const char *filename) {
    memset(name, 0x20, 8);
    memset(ext, 0x20, 3);
    
    const char *dot = strchr(filename, '.');
    int name_len = dot ? (int)(dot - filename) : (int)strlen(filename);
    
    /* 填充名称部分（最多8个字符，转大写） */
    for (int i = 0; i < name_len && i < 8; i++) {
        char c = filename[i];
        name[i] = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
    }
    
    /* 填充扩展名部分 */
    if (dot) {
        int ext_len = strlen(dot + 1);
        for (int i = 0; i < ext_len && i < 3; i++) {
            char c = dot[1 + i];
            ext[i] = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
        }
    }
}

/**
 * 写入完整的FAT32目录条目
 */
static void fat32_write_dir_entry(uint8_t *buf, int offset, 
                                   const char *name, const char *ext,
                                   uint16_t first_cluster_low, uint16_t first_cluster_high,
                                   uint32_t file_size, uint8_t attributes) {
    FAT32_DirEntry entry;
    memset(&entry, 0, sizeof(entry));
    
    uint8_t name_pad[8], ext_pad[3];
    fat32_pad_name(name_pad, ext_pad, name);
    memcpy(entry.name, name_pad, 8);
    memcpy(entry.ext, ext_pad, 3);
    
    entry.attr = attributes;
    entry.reserved = 0;
    entry.creation_time_tenths = 0;
    entry.creation_time = 0;
    entry.creation_date = 0;
    entry.access_date = 0;
    entry.cluster_high = first_cluster_high;
    entry.write_time = 0;
    entry.write_date = 0;
    entry.cluster_low = first_cluster_low;
    entry.file_size = file_size;
    
    memcpy(buf + offset, &entry, sizeof(entry));
}

/**
 * 生成完整的FAT32文件表、目录树（包括EFI/BOOT目录）和BOOTX64.EFI文件
 * 符合UEFI标准：\EFI\BOOT\BOOTX64.EFI
 */
static int adl_generate_fat32_structures(FILE *iso, uint64_t esp_start, 
                                         const char *efi_bootloader) {
    uint32_t esp_total_sectors = ESP_SIZE_SECTORS;
    uint32_t sectors_per_cluster = 8;  /* 8个扇区 = 4KB */
    uint32_t fat_size_sectors = 256;
    
    /* ====== 簇分配 ====== */
    /* 簇 0-1: 保留 */
    /* 簇 2: Root Directory */
    /* 簇 3: EFI目录 */
    /* 簇 4: BOOT目录 */
    /* 簇 5+: BOOTX64.EFI数据 */
    
    /* === LBA esp_start: FAT32启动扇区 === */
    {
        FAT32_Boot_Sector boot;
        memset(&boot, 0, sizeof(boot));
        
        boot.jmp_boot[0] = 0xEB;
        boot.jmp_boot[1] = 0x3C;
        boot.jmp_boot[2] = 0x90;
        memcpy(boot.oem_name, "AETHEL  ", 8);
        boot.bytes_per_sector = SECTOR_SIZE;
        boot.sectors_per_cluster = sectors_per_cluster;
        boot.reserved_sectors = 33;  /* 保留区：Boot Sector (1) + FSInfo (1) + 备用区 (31) = 33 */
        boot.num_fats = 2;
        boot.root_entries = 0;  /* FAT32为0 */
        boot.total_sectors_16 = 0;
        boot.media_descriptor = 0xF8;
        boot.sectors_per_fat_16 = 0;
        boot.sectors_per_track = 63;
        boot.num_heads = 255;
        boot.hidden_sectors = (uint32_t)esp_start;  /* 关键：分区在驱动器中的起始LBA */
        boot.total_sectors_32 = esp_total_sectors;  /* 正确的总扇区数 */
        
        boot.sectors_per_fat_32 = fat_size_sectors;
        boot.ext_flags = 0;
        boot.fs_version = 0;
        boot.root_cluster = 2;  /* 根目录在簇2 */
        boot.fsinfo_sector = 1;
        boot.backup_boot_sector = 6;
        boot.drive_number = 0x80;
        boot.boot_signature = 0x29;
        boot.volume_serial = 0xDEADBEEF;
        memcpy(boot.volume_label, "AETHELOS   ", 11);
        memcpy(boot.fs_type, "FAT32   ", 8);
        boot.boot_sector_sig = 0xAA55;
        
        uint8_t sector[SECTOR_SIZE];
        memcpy(sector, &boot, sizeof(boot));
        memset(&sector[sizeof(boot)], 0, SECTOR_SIZE - sizeof(boot));
        
        if (adl_seek_lba(iso, esp_start) != 0) {
            fprintf(stderr, "ERROR: Failed to seek to ESP FAT32 boot sector\n");
            return -1;
        }
        if (adl_write_sector(iso, sector) != 0) {
            return -1;
        }
        fprintf(stderr, "[FAT32] Boot sector at LBA %llu\n", (unsigned long long)esp_start);
    }
    
    /* === LBA esp_start+1: FSInfo扇区 === */
    {
        FAT32_FSInfo_Sector fsinfo;
        memset(&fsinfo, 0, sizeof(fsinfo));
        
        fsinfo.signature1 = 0x41615252;
        fsinfo.signature2 = 0x61417272;
        fsinfo.free_clusters = 0xFFFFFFFF;  /* 未知 */
        fsinfo.next_free_cluster = 5;  /* 下一个空闲簇从5开始 */
        fsinfo.signature3 = 0xAA550000;
        
        uint8_t sector[SECTOR_SIZE];
        memcpy(sector, &fsinfo, sizeof(fsinfo));
        memset(&sector[sizeof(fsinfo)], 0, SECTOR_SIZE - sizeof(fsinfo));
        
        if (adl_write_sector(iso, sector) != 0) {
            return -1;
        }
        fprintf(stderr, "[FAT32] FSInfo sector at LBA %llu\n", (unsigned long long)esp_start + 1);
    }
    
    /* === LBA esp_start+2-33: 保留区和FAT启动代码备份 === */
    /* 共31个扇区（reserved_sectors=1+1+31=33）*/
    {
        uint8_t sector[SECTOR_SIZE];
        memset(sector, 0, SECTOR_SIZE);
        for (int i = 2; i <= 32; i++) {  /* 从2到32，共31个扇区 */
            if (adl_write_sector(iso, sector) != 0) {
                return -1;
            }
        }
        fprintf(stderr, "[FAT32] Reserved area filled (LBA %llu-%llu)\n", 
                (unsigned long long)esp_start + 2, (unsigned long long)esp_start + 32);
    }
    
    /* === FAT表(两个副本，起始LBA: esp_start+34) === */
    /* reserved_sectors=33意味着FAT从LBA esp_start+33开始(第34个扇区) */
    {
        uint8_t fat_sector[SECTOR_SIZE];
        uint64_t efi_size = adl_get_file_size(efi_bootloader);
        uint32_t efi_clusters = (efi_size + (sectors_per_cluster * SECTOR_SIZE) - 1) / 
                                (sectors_per_cluster * SECTOR_SIZE);
        
        for (uint32_t fat_idx = 0; fat_idx < 2; fat_idx++) {
            uint64_t fat_lba = esp_start + 33 + (fat_idx * fat_size_sectors);
            
            for (uint32_t fat_sec = 0; fat_sec < fat_size_sectors; fat_sec++) {
                memset(fat_sector, 0, SECTOR_SIZE);
                
                if (fat_sec == 0) {
                    /* 簇0-1: 保留和介质描述符 */
                    le32_write(&fat_sector[0], 0xFFFFFFF8);  /* FAT[0]: 介质描述符 */
                    le32_write(&fat_sector[4], 0xFFFFFFFF);  /* FAT[1]: 保留 */
                    
                    /* 簇2: 根目录（单个簇） */
                    le32_write(&fat_sector[8], 0xFFFFFFFF);  /* 根目录簇链结束 */
                    
                    /* 簇3: EFI目录（单个簇） */
                    le32_write(&fat_sector[12], 0xFFFFFFFF); /* EFI簇链结束 */
                    
                    /* 簇4: BOOT目录（单个簇） */
                    le32_write(&fat_sector[16], 0xFFFFFFFF); /* BOOT簇链结束 */
                    
                    /* 簇5+: BOOTX64.EFI数据（链接） */
                    for (uint32_t efi_clus = 0; efi_clus < efi_clusters - 1; efi_clus++) {
                        le32_write(&fat_sector[20 + (efi_clus * 4)], 6 + efi_clus);
                    }
                    if (efi_clusters > 0) {
                        /* 最后一个簇标记为EOF */
                        le32_write(&fat_sector[20 + ((efi_clusters - 1) * 4)], 0xFFFFFFFF);
                    }
                }
                
                if (adl_seek_lba(iso, fat_lba + fat_sec) != 0) {
                    return -1;
                }
                if (adl_write_sector(iso, fat_sector) != 0) {
                    return -1;
                }
            }
            fprintf(stderr, "[FAT32] FAT%d at LBA %llu-%llu\n", 
                    fat_idx + 1, (unsigned long long)fat_lba, 
                    (unsigned long long)(fat_lba + fat_size_sectors - 1));
        }
    }
    
    /* === 根目录（簇2，从LBA esp_start+33+512开始=esp_start+545） === */
    /* FAT1: esp_start+33到esp_start+288(256扇区) */
    /* FAT2: esp_start+289到esp_start+544(256扇区) */
    /* 根目录从esp_start+545开始 */
    {
        uint64_t root_lba = esp_start + 545;
        uint8_t dir_sector[SECTOR_SIZE];
        memset(dir_sector, 0, SECTOR_SIZE);
        
        /* 根目录项1: EFI目录项 */
        fat32_write_dir_entry(dir_sector, 0,
                             "EFI", "",
                             3,      /* 簇3 (EFI) */
                             0,      /* cluster_high */
                             0,      /* 文件大小（目录不计） */
                             FAT32_ATTR_DIRECTORY);  /* 目录属性 */
        
        /* 写入根目录（8个扇区） */
        for (int i = 0; i < 8; i++) {
            if (adl_seek_lba(iso, root_lba + i) != 0) {
                return -1;
            }
            if (i == 0) {
                if (adl_write_sector(iso, dir_sector) != 0) {
                    return -1;
                }
            } else {
                uint8_t zero[SECTOR_SIZE];
                memset(zero, 0, SECTOR_SIZE);
                if (adl_write_sector(iso, zero) != 0) {
                    return -1;
                }
            }
        }
        fprintf(stderr, "[FAT32] Root Directory at LBA %llu (/EFI)\n", (unsigned long long)root_lba);
    }
    
    /* === EFI目录（簇3，从LBA esp_start+553开始=esp_start+545+8） === */
    /* 根目录占8个扇区 */
    {
        uint64_t efi_lba = esp_start + 553;
        uint8_t dir_sector[SECTOR_SIZE];
        memset(dir_sector, 0, SECTOR_SIZE);
        
        /* EFI目录项1: BOOT子目录 */
        fat32_write_dir_entry(dir_sector, 0,
                             "BOOT", "",
                             4,      /* 簇4 (BOOT) */
                             0,
                             0,
                             FAT32_ATTR_DIRECTORY);
        
        /* 写入EFI目录 */
        for (int i = 0; i < 8; i++) {
            if (adl_seek_lba(iso, efi_lba + i) != 0) {
                return -1;
            }
            if (i == 0) {
                if (adl_write_sector(iso, dir_sector) != 0) {
                    return -1;
                }
            } else {
                uint8_t zero[SECTOR_SIZE];
                memset(zero, 0, SECTOR_SIZE);
                if (adl_write_sector(iso, zero) != 0) {
                    return -1;
                }
            }
        }
        fprintf(stderr, "[FAT32] EFI Directory at LBA %llu (/EFI/BOOT)\n", (unsigned long long)efi_lba);
    }
    
    /* === BOOT目录（簇4，从LBA esp_start+561开始=esp_start+545+16） === */
    /* 根目录+EFI目录各占8个扇区 */
    {
        uint64_t boot_dir_lba = esp_start + 561;
        uint64_t efi_size = adl_get_file_size(efi_bootloader);
        uint8_t dir_sector[SECTOR_SIZE];
        memset(dir_sector, 0, SECTOR_SIZE);
        
        /* BOOT目录项1: BOOTX64.EFI 文件 */
        fat32_write_dir_entry(dir_sector, 0,
                             "BOOTX64.EFI", "",  /* 会自动转换为8.3格式 */
                             5,      /* 簇5 (BOOTX64.EFI数据) */
                             0,
                             (uint32_t)efi_size,
                             FAT32_ATTR_ARCHIVE);
        
        /* 写入BOOT目录 */
        for (int i = 0; i < 8; i++) {
            if (adl_seek_lba(iso, boot_dir_lba + i) != 0) {
                return -1;
            }
            if (i == 0) {
                if (adl_write_sector(iso, dir_sector) != 0) {
                    return -1;
                }
            } else {
                uint8_t zero[SECTOR_SIZE];
                memset(zero, 0, SECTOR_SIZE);
                if (adl_write_sector(iso, zero) != 0) {
                    return -1;
                }
            }
        }
        fprintf(stderr, "[FAT32] BOOT Directory at LBA %llu\n", (unsigned long long)boot_dir_lba);
        fprintf(stderr, "[FAT32] BOOTX64.EFI entry: size=%llu bytes\n", (unsigned long long)efi_size);
    }
    
    /* === 写入BOOTX64.EFI文件数据（从簇5开始） === */
    {
        uint64_t efi_data_lba = esp_start + 569;  /* 簇5的起始LBA = esp_start + 545 + 8 + 8 + 8 */
        /* 根目录(8) + EFI目录(8) + BOOT目录(8) = 24扇区，所以从569开始 */
        /* 或者 esp_start + 545 + 24 = esp_start + 569 */
        if (adl_write_file_to_iso(iso, efi_bootloader, efi_data_lba, NULL) != 0) {
            fprintf(stderr, "ERROR: Failed to write BOOTX64.EFI\n");
            return -1;
        }
        fprintf(stderr, "[FAT32] BOOTX64.EFI written at LBA %llu\n", (unsigned long long)efi_data_lba);
    }
    
    fprintf(stderr, "[FAT32] Complete ESP structure: /EFI/BOOT/BOOTX64.EFI\n");
    return 0;
}

/**
 * 生成AEFS卷的根Æ-Node和目录结构
 * 包括: bootloader.efi, kernel.aki, drivers-, Installer.iya
 */
static int adl_generate_aefs_boot_volume(FILE *iso, uint64_t boot_root_lba,
                                         uint64_t kernel_lba, uint64_t installer_lba,
                                         const uint8_t *boot_uuid) {
    AEFS_Node root_node;
    memset(&root_node, 0, sizeof(root_node));
    
    root_node.magic = 0x45444F4E4541ULL;  /* "AENODE" */
    memcpy(root_node.node_uuid, boot_uuid, 16);
    root_node.object_type = 2;  /* DIR */
    root_node.flags = 0;
    root_node.size = 512 * 4;  /* 4个目录项 */
    root_node.created_time = time(NULL);
    root_node.modified_time = time(NULL);
    root_node.mode = 0755;
    root_node.owner_uid = 0;
    root_node.owner_gid = 0;
    root_node.refcount = 1;
    
    /* 根目录数据从下一个块开始 */
    root_node.data_lba = boot_root_lba + 1;
    root_node.block_count = 1;
    
    uint32_t checksum = adl_xxh3_hash((uint8_t*)&root_node, sizeof(AEFS_Node) - 4);
    root_node.node_checksum = checksum;
    
    /* 写入根Æ-Node */
    if (adl_seek_lba(iso, boot_root_lba) != 0) {
        return -1;
    }
    if (adl_write_block_padded(iso, &root_node, sizeof(AEFS_Node)) != 0) {
        return -1;
    }
    
    fprintf(stderr, "[AEFS] Wrote >:BOOT root Æ-Node at LBA %llu\n", 
            (unsigned long long)boot_root_lba);
    
    /* === 生成根目录项 === */
    {
        uint8_t dir_block[BLOCK_SIZE];
        memset(dir_block, 0, BLOCK_SIZE);
        
        AEFS_Directory_Entry *entries = (AEFS_Directory_Entry *)dir_block;
        uint8_t temp_uuid[16];
        int entry_idx = 0;
        
        /* 项1: kernel.aki (FILE) */
        {
            adl_generate_uuid(temp_uuid);
            AEFS_Directory_Entry *entry = &entries[entry_idx++];
            
            memcpy(entry->entry_uuid, temp_uuid, 16);
            entry->entry_type = 1;  /* FILE */
            entry->name_len = strlen("kernel.aki");
            memcpy(entry->name, (uint8_t*)"kernel.aki", entry->name_len);
            entry->entry_lba = kernel_lba;
            entry->entry_checksum = adl_xxh3_hash((uint8_t*)entry, 
                                                  sizeof(AEFS_Directory_Entry) - 4);
        }
        
        /* 项2: Installer.iya (FILE) */
        {
            adl_generate_uuid(temp_uuid);
            AEFS_Directory_Entry *entry = &entries[entry_idx++];
            
            memcpy(entry->entry_uuid, temp_uuid, 16);
            entry->entry_type = 1;  /* FILE */
            entry->name_len = strlen("Installer.iya");
            memcpy(entry->name, (uint8_t*)"Installer.iya", entry->name_len);
            entry->entry_lba = installer_lba;
            entry->entry_checksum = adl_xxh3_hash((uint8_t*)entry, 
                                                  sizeof(AEFS_Directory_Entry) - 4);
        }
        
        /* 项3: drivers- (DIR) */
        {
            adl_generate_uuid(temp_uuid);
            AEFS_Directory_Entry *entry = &entries[entry_idx++];
            
            memcpy(entry->entry_uuid, temp_uuid, 16);
            entry->entry_type = 2;  /* DIR */
            entry->name_len = strlen("drivers-");
            memcpy(entry->name, (uint8_t*)"drivers-", entry->name_len);
            entry->entry_lba = 0;  /* 子目录在更后的块 */
            entry->entry_checksum = adl_xxh3_hash((uint8_t*)entry, 
                                                  sizeof(AEFS_Directory_Entry) - 4);
        }
        
        /* 写入目录块 */
        if (adl_seek_lba(iso, root_node.data_lba) != 0) {
            return -1;
        }
        if (adl_write_block_padded(iso, dir_block, BLOCK_SIZE) != 0) {
            return -1;
        }
        
        fprintf(stderr, "[AEFS] Wrote >:BOOT directory entries (%d items)\n", entry_idx);
    }
    
    return 0;
}

/**
 * 生成AEFS有效负载卷的根Æ-Node
 */
static int adl_generate_aefs_payload_volume(FILE *iso, uint64_t payload_root_lba,
                                            uint64_t payload_data_lba,
                                            const uint8_t *payload_uuid) {
    AEFS_Node root_node;
    memset(&root_node, 0, sizeof(root_node));
    
    root_node.magic = 0x45444F4E4541ULL;  /* "AENODE" */
    memcpy(root_node.node_uuid, payload_uuid, 16);
    root_node.object_type = 2;  /* DIR */
    root_node.flags = 0;
    root_node.size = 512;  /* 1个目录项 */
    root_node.created_time = time(NULL);
    root_node.modified_time = time(NULL);
    root_node.mode = 0755;
    root_node.owner_uid = 0;
    root_node.owner_gid = 0;
    root_node.refcount = 1;
    
    root_node.data_lba = payload_root_lba + 1;
    root_node.block_count = 1;
    
    uint32_t checksum = adl_xxh3_hash((uint8_t*)&root_node, sizeof(AEFS_Node) - 4);
    root_node.node_checksum = checksum;
    
    if (adl_seek_lba(iso, payload_root_lba) != 0) {
        return -1;
    }
    if (adl_write_block_padded(iso, &root_node, sizeof(AEFS_Node)) != 0) {
        return -1;
    }
    
    fprintf(stderr, "[AEFS] Wrote >:PAYLOAD root Æ-Node at LBA %llu\n", 
            (unsigned long long)payload_root_lba);
    
    /* === 生成有效负载目录项 === */
    {
        uint8_t dir_block[BLOCK_SIZE];
        memset(dir_block, 0, BLOCK_SIZE);
        
        AEFS_Directory_Entry *entry = (AEFS_Directory_Entry *)dir_block;
        uint8_t temp_uuid[16];
        adl_generate_uuid(temp_uuid);
        
        /* 系统镜像项: AethelOS.lz */
        memcpy(entry->entry_uuid, temp_uuid, 16);
        entry->entry_type = 1;  /* FILE */
        entry->name_len = strlen("AethelOS.lz");
        memcpy(entry->name, (uint8_t*)"AethelOS.lz", entry->name_len);
        entry->entry_lba = payload_data_lba;
        entry->entry_checksum = adl_xxh3_hash((uint8_t*)entry, 
                                              sizeof(AEFS_Directory_Entry) - 4);
        
        if (adl_seek_lba(iso, root_node.data_lba) != 0) {
            return -1;
        }
        if (adl_write_block_padded(iso, dir_block, BLOCK_SIZE) != 0) {
            return -1;
        }
        
        fprintf(stderr, "[AEFS] Wrote >:PAYLOAD directory entry (AethelOS.lz)\n");
    }
    
    return 0;
}

/**
 * 生成FAT32启动扇区
 */
static int adl_generate_fat32_boot_sector(uint8_t *sector, uint32_t total_sectors) {
    memset(sector, 0, SECTOR_SIZE);
    
    /* 跳转指令 */
    sector[0] = 0xEB;
    sector[1] = 0x3C;
    sector[2] = 0x90;
    
    /* OEM标识 */
    memcpy(&sector[3], "AETHEL  ", 8);
    
    /* 字节/扇区 */
    le16_write(&sector[11], SECTOR_SIZE);
    
    /* 扇区/簇 */
    sector[13] = 8;  /* 4KB簇 */
    
    /* 保留扇区 */
    le16_write(&sector[14], 32);
    
    /* FAT表数 */
    sector[16] = 2;
    
    /* 根目录项数 (FAT32为0) */
    le16_write(&sector[17], 0);
    
    /* 总扇区 (16位) */
    le16_write(&sector[19], 0);
    
    /* 介质描述符 */
    sector[21] = 0xF8;
    
    /* 每个track的扇区 */
    le16_write(&sector[24], 63);
    
    /* 磁头数 */
    le16_write(&sector[26], 255);
    
    /* 隐藏扇区 */
    le32_write(&sector[28], ESP_START_LBA);  /* 关键：指向ESP分区在驱动器中的起始LBA */
    
    /* 总扇区 (32位) */
    le32_write(&sector[32], total_sectors);
    
    /* === FAT32特定 === */
    
    /* 每个FAT的扇区数 */
    uint32_t fat_size = (total_sectors + 0xFFF) / 0x1000;
    le32_write(&sector[36], fat_size);
    
    /* 标志 */
    le16_write(&sector[40], 0x0000);
    
    /* 版本 */
    le16_write(&sector[42], 0x0000);
    
    /* 根目录簇号 */
    le32_write(&sector[44], 2);
    
    /* FSInfo扇区号 */
    le16_write(&sector[48], 1);
    
    /* 启动代码备份扇区号 */
    le16_write(&sector[50], 6);
    
    /* 签名 */
    sector[510] = 0x55;
    sector[511] = 0xAA;
    
    return 0;
}

/**
 * 初始化ESP FAT32分区（包括BOOTX64.EFI文件）
 */
static int adl_initialize_esp_fat32(FILE *iso, const char *efi_bootloader) {
    uint32_t esp_total_sectors = ESP_SIZE_SECTORS;
    
    /* 生成FAT32启动扇区 */
    uint8_t boot_sector[SECTOR_SIZE];
    adl_generate_fat32_boot_sector(boot_sector, esp_total_sectors);
    
    if (adl_seek_lba(iso, ESP_START_LBA) != 0) {
        return -1;
    }
    
    if (adl_write_sector(iso, boot_sector) != 0) {
        return -1;
    }
    
    /* 生成FAT表 */
    uint32_t fat_size_sectors = 256;  /* 保守估计 */
    
    for (uint32_t fat_idx = 0; fat_idx < 2; fat_idx++) {
        uint8_t fat_sector[SECTOR_SIZE];
        
        if (adl_seek_lba(iso, ESP_START_LBA + 32 + fat_idx * fat_size_sectors) != 0) {
            return -1;
        }
        
        for (uint32_t i = 0; i < fat_size_sectors; i++) {
            memset(fat_sector, 0, SECTOR_SIZE);
            
            /* 第一个FAT扇区：簇链头 */
            if (i == 0) {
                le32_write(&fat_sector[0], 0xFFFFFFF8);  /* 介质描述符 */
                le32_write(&fat_sector[4], 0xFFFFFFFF);  /* EOF标记 */
                le32_write(&fat_sector[8], 0xFFFFFFFF);  /* 为BOOTX64.EFI链接 */
            }
            
            if (adl_write_sector(iso, fat_sector) != 0) {
                return -1;
            }
        }
    }
    
    /* 写入BOOTX64.EFI文件 */
    uint64_t efi_data_lba = ESP_START_LBA + 32 + 512;  /* FAT和根目录后 */
    uint64_t efi_size = 0;
    
    if (adl_write_file_to_iso(iso, efi_bootloader, efi_data_lba, &efi_size) != 0) {
        fprintf(stderr, "ERROR: Failed to write BOOTX64.EFI to FAT32\n");
        return -1;
    }
    
    fprintf(stderr, "[ADL] ESP FAT32: Written BOOTX64.EFI (%llu bytes)\n", 
            (unsigned long long)efi_size);
    
    return 0;
}

/**
 * ============================================================================
 * 写入备份GPT结构
 * ============================================================================
 *
 * 在ISO文件末尾写入备份GPT头和分区表
 */
static int adl_write_backup_gpt(FILE *iso, uint64_t total_lba) {
    fprintf(stderr, "\n[ADL STAGE 6] Writing Backup GPT...\n");

    // 1. 读取主分区表 (LBA 2-33)
    uint8_t primary_entries[32 * SECTOR_SIZE];
    if (adl_seek_lba(iso, SHADOW_GPT_ENTRY_LBA) != 0) {
        fprintf(stderr, "ERROR: Backup: Failed to seek to primary GPT entries\n");
        return -1;
    }
    if (fread(primary_entries, 1, sizeof(primary_entries), iso) != sizeof(primary_entries)) {
        fprintf(stderr, "ERROR: Backup: Failed to read primary GPT entries\n");
        return -1;
    }

    // 2. 将主分区表写入备份位置 (LBA N-33)
    uint64_t backup_entries_lba = total_lba - 33;
    if (adl_seek_lba(iso, backup_entries_lba) != 0) {
        fprintf(stderr, "ERROR: Backup: Failed to seek to backup GPT entries LBA\n");
        return -1;
    }
    if (fwrite(primary_entries, 1, sizeof(primary_entries), iso) != sizeof(primary_entries)) {
        fprintf(stderr, "ERROR: Backup: Failed to write backup GPT entries\n");
        return -1;
    }
    fprintf(stderr, "[GPT-Backup] Wrote partition entries backup at LBA %llu-%llu\n",
            (unsigned long long)backup_entries_lba, (unsigned long long)(total_lba - 2));

    // 3. 读取主GPT头 (LBA 1)
    GPT_Header primary_header;
    uint8_t primary_header_sector[SECTOR_SIZE];
    if (adl_seek_lba(iso, SHADOW_GPT_HEAD_LBA) != 0) {
        fprintf(stderr, "ERROR: Backup: Failed to seek to primary GPT header\n");
        return -1;
    }
    if (fread(primary_header_sector, 1, SECTOR_SIZE, iso) != SECTOR_SIZE) {
        fprintf(stderr, "ERROR: Backup: Failed to read primary GPT header\n");
        return -1;
    }
    memcpy(&primary_header, primary_header_sector, sizeof(GPT_Header));

    // 4. 修改GPT头用于备份
    GPT_Header backup_header = primary_header;
    backup_header.current_lba = total_lba - 1;       // MyLBA
    backup_header.backup_lba = SHADOW_GPT_HEAD_LBA;  // AlternateLBA = 1
    backup_header.partition_entry_lba = backup_entries_lba; // PartitionEntryLBA

    // 5. 重新计算备份头的CRC32
    backup_header.crc32 = 0;
    uint32_t backup_header_crc = gpt_crc32((uint8_t*)&backup_header, backup_header.header_size);
    backup_header.crc32 = backup_header_crc;

    // 6. 将备份头写入文件末尾 (LBA N-1)
    uint8_t backup_header_sector[SECTOR_SIZE];
    memset(backup_header_sector, 0, SECTOR_SIZE);
    memcpy(backup_header_sector, &backup_header, sizeof(GPT_Header));
    
    uint64_t backup_header_lba = total_lba - 1;
    if (adl_seek_lba(iso, backup_header_lba) != 0) {
        fprintf(stderr, "ERROR: Backup: Failed to seek to backup GPT header LBA\n");
        return -1;
    }
    if (adl_write_sector(iso, backup_header_sector) != 0) {
        fprintf(stderr, "ERROR: Backup: Failed to write backup GPT header\n");
        return -1;
    }
    
    fprintf(stderr, "[GPT-Backup] Wrote backup GPT header at LBA %llu\n", (unsigned long long)backup_header_lba);
    fprintf(stderr, "[GPT-Backup]   Header CRC32: 0x%08X\n", backup_header.crc32);
    fprintf(stderr, "[GPT-Backup]   MyLBA: %llu, AlternateLBA: %llu, PartitionEntryLBA: %llu\n",
            (unsigned long long)backup_header.current_lba,
            (unsigned long long)backup_header.backup_lba,
            (unsigned long long)backup_header.partition_entry_lba);
            
    return 0;
}


/* ============================================================================
 * 主ISO生成函数
 * ============================================================================ */

int adl_iso_builder_generate(
    const char *kernel_file,
    const char *efi_boot_file,
    const char *installer_file,
    const char *drivers_dir,
    const char *output_iso,
    int auto_size)
{
    fprintf(stderr, "===============================================================================\n");
    fprintf(stderr, "ADL ISO Builder - Industrial Grade Implementation\n");
    fprintf(stderr, "===============================================================================\n");
    
    /* 验证输入文件 */
    if (!kernel_file || !efi_boot_file || !output_iso) {
        fprintf(stderr, "ERROR: Missing required file parameters\n");
        return -1;
    }
    
    uint64_t kernel_size = adl_get_file_size(kernel_file);
    uint64_t efi_size = adl_get_file_size(efi_boot_file);
    uint64_t installer_size = installer_file ? adl_get_item_size(installer_file) : 0;
    
    if (kernel_size == 0 || efi_size == 0) {
        fprintf(stderr, "ERROR: Kernel or EFI bootloader not found\n");
        return -1;
    }
    
    /* 检测 installer 是否为目录 */
    int installer_is_dir = installer_file ? adl_is_directory(installer_file) : 0;
    if (installer_file && installer_size == 0) {
        fprintf(stderr, "WARNING: Installer path is empty or inaccessible: %s\n", installer_file);
    }
    
    /* 计算所需ISO大小 */
    uint64_t aefs_content_size = kernel_size + efi_size + installer_size + (10 * 1024 * 1024);
    uint64_t iso_size_sectors = AEFS_POOL_START_LBA + (aefs_content_size / SECTOR_SIZE) + 1024;
    
    fprintf(stderr, "[ADL] Kernel size: %llu bytes\n", (unsigned long long)kernel_size);
    fprintf(stderr, "[ADL] EFI bootloader size: %llu bytes\n", (unsigned long long)efi_size);
    fprintf(stderr, "[ADL] Installer size: %llu bytes%s\n", (unsigned long long)installer_size,
            installer_is_dir ? " (directory)" : "");
    fprintf(stderr, "[ADL] Calculated ISO size: %llu sectors (%.2f MB)\n", 
            (unsigned long long)iso_size_sectors,
            (double)iso_size_sectors * SECTOR_SIZE / (1024 * 1024));
    
    /* 打开输出ISO文件 */
    FILE *iso = fopen(output_iso, "w+b");
    if (!iso) {
        fprintf(stderr, "ERROR: Cannot open output ISO: %s\n", strerror(errno));
        return -1;
    }
    
    /* 生成磁盘UUID和分区UUID */
    uint8_t disk_uuid[16], boot_uuid[16], payload_uuid[16], esp_uuid[16], adl_anchor_uuid[16];
    adl_generate_uuid(disk_uuid);
    adl_generate_uuid(boot_uuid);
    adl_generate_uuid(payload_uuid);
    adl_generate_uuid(esp_uuid);
    adl_generate_uuid(adl_anchor_uuid);
    
    fprintf(stderr, "\n[ADL STAGE 1] Generating Protective MBR and ADL Header...\n");
    
    /* === LBA 0: 标准的 Protective MBR（不是ADL Header） === */
    /* 这是UEFI/GPT固件识别磁盘的关键：LBA 0必须是有效的MBR格式 */
    {
        uint8_t mbr_table[64];
        uint8_t sector[SECTOR_SIZE];
        
        adl_generate_protective_mbr(mbr_table);
        
        memset(sector, 0, SECTOR_SIZE);
        /* LBA 0的前440字节留空（通常是启动代码，但这里不需要） */
        memcpy(sector + 440, mbr_table, 64);  /* Protective MBR分区表64字节 */
        
        /* MBR签名（扇区末尾）- 这是MBR的必需标记 */
        sector[510] = 0x55;
        sector[511] = 0xAA;
        
        if (adl_seek_lba(iso, ADL_MBR_LBA) != 0 || adl_write_sector(iso, sector) != 0) {
            fprintf(stderr, "ERROR: Failed to write Protective MBR\n");
            fclose(iso);
            return -1;
        }
        
        fprintf(stderr, "[ADL] Wrote Protective MBR at LBA 0 (UEFI-compliant)\n");
    }
    
    /* === LBA 2: ADL Header（新位置） === */
    /* ADL Header现在可以在任意位置，不必破坏LBA 0的MBR结构 */
    if (adl_write_adl_header(iso, iso_size_sectors, disk_uuid) != 0) {
        fprintf(stderr, "ERROR: Failed to write ADL header\n");
        fclose(iso);
        return -1;
    }
    
    fprintf(stderr, "\n[ADL STAGE 2] Generating ADL Partition Table...\n");
    if (adl_generate_partition_table(iso, esp_uuid, payload_uuid, iso_size_sectors) != 0) {
        fprintf(stderr, "ERROR: Failed to generate partition table\n");
        fclose(iso);
        return -1;
    }
    
    fprintf(stderr, "[ADL] Wrote Partition Table at LBA %d\n", ADL_PARTITION_TABLE_LBA);
    
    fprintf(stderr, "\n[ADL STAGE 2.5] Generating Shadow GPT (UEFI compatibility)...\n");
    
    /* 先生成GPT分区项以计算其CRC32 */
    uint32_t entries_crc32 = 0;
    if (adl_generate_gpt_entries(iso, ESP_START_LBA, ESP_START_LBA + ESP_SIZE_SECTORS - 1,
                                  esp_uuid, adl_anchor_uuid, &entries_crc32) != 0) {
        fprintf(stderr, "ERROR: Failed to generate Shadow GPT entries\n");
        fclose(iso);
        return -1;
    }
    fprintf(stderr, "[ADL] Wrote Shadow GPT entries at LBA 2-33\n");
    
    /* 再生成GPT header并使用分区表的CRC32 */
    if (adl_generate_gpt_header(iso, iso_size_sectors, disk_uuid, entries_crc32) != 0) {
        fprintf(stderr, "ERROR: Failed to generate Shadow GPT header\n");
        fclose(iso);
        return -1;
    }
    fprintf(stderr, "[ADL] Wrote Shadow GPT header at LBA 1\n");
    
    /* === ISO 9660 & El Torito 已移除 - 使用纯硬盘逻辑（硬盘运行模式） === */
    /* 移除原因：ISO 9660结构与硬盘分区逻辑混杂导致UEFI固件无法正确识别GPT分区 */
    /* 现在仅使用：ADL MBR + Shadow GPT + FAT32 ESP + AEFS 存储池 */
    
    fprintf(stderr, "\n[ADL STAGE 3] Initializing ESP FAT32...\n");
    if (adl_generate_fat32_structures(iso, ESP_START_LBA, efi_boot_file) != 0) {
        fprintf(stderr, "ERROR: Failed to initialize ESP FAT32\n");
        fclose(iso);
        return -1;
    }
    fprintf(stderr, "[ADL] ESP FAT32 initialized with BOOTX64.EFI\n");
    
    fprintf(stderr, "\n[ADL STAGE 4] Generating AEFS Structures...\n");
    
    /* === AEFS Anchor Block === */
    if (adl_generate_aefs_anchor(iso, AEFS_POOL_START_LBA, payload_uuid) != 0) {
        fprintf(stderr, "ERROR: Failed to generate AEFS Anchor\n");
        fclose(iso);
        return -1;
    }
    fprintf(stderr, "[ADL] Wrote AEFS Anchor Block at LBA %d\n", AEFS_POOL_START_LBA);
    
    /* === AEFS Checkpoint === */
    if (adl_generate_aefs_checkpoint(iso, AEFS_POOL_START_LBA + 1, AEFS_POOL_START_LBA) != 0) {
        fprintf(stderr, "ERROR: Failed to generate AEFS Checkpoint\n");
        fclose(iso);
        return -1;
    }
    fprintf(stderr, "[ADL] Wrote AEFS Checkpoint at LBA %d\n", AEFS_POOL_START_LBA + 1);
    
    /* === Volume Tree === */
    if (adl_generate_volume_tree(iso, AEFS_POOL_START_LBA + 2, boot_uuid, payload_uuid) != 0) {
        fprintf(stderr, "ERROR: Failed to generate Volume Tree\n");
        fclose(iso);
        return -1;
    }
    fprintf(stderr, "[ADL] Wrote Volume Tree at LBA %d\n", AEFS_POOL_START_LBA + 2);
    
    /* === AEFS Root Æ-Nodes for Volumes === */
    uint64_t boot_root_node_lba = AEFS_POOL_START_LBA + 3;
    uint64_t payload_root_node_lba = AEFS_POOL_START_LBA + 5;
    uint64_t kernel_lba = AEFS_POOL_START_LBA + 10;
    uint64_t installer_lba = AEFS_POOL_START_LBA + 15;
    
    if (adl_generate_aefs_boot_volume(iso, boot_root_node_lba, kernel_lba, installer_lba,
                                       boot_uuid) != 0) {
        fprintf(stderr, "ERROR: Failed to generate >:BOOT volume root\n");
        fclose(iso);
        return -1;
    }
    
    if (adl_generate_aefs_payload_volume(iso, payload_root_node_lba, 
                                         AEFS_POOL_START_LBA + 20,
                                         payload_uuid) != 0) {
        fprintf(stderr, "ERROR: Failed to generate >:PAYLOAD volume root\n");
        fclose(iso);
        return -1;
    }
    
    fprintf(stderr, "\n[ADL STAGE 5] Writing Kernel and Boot Content...\n");
    
    /* === Kernel.aki 写入AEFS === */
    if (adl_write_file_to_iso(iso, kernel_file, kernel_lba, NULL) != 0) {
        fprintf(stderr, "ERROR: Failed to write kernel.aki\n");
        fclose(iso);
        return -1;
    }
    fprintf(stderr, "[ADL] Wrote kernel.aki at LBA %llu\n", (unsigned long long)kernel_lba);
    
    /* === Installer 写入AEFS === */
    if (installer_file) {
        if (installer_is_dir) {
            /* 处理IYA目录 */
            fprintf(stderr, "[ADL] Processing installer directory: %s\n", installer_file);
            uint64_t current_lba = installer_lba;
            if (adl_write_directory_tree(iso, installer_file, &current_lba) != 0) {
                fprintf(stderr, "WARNING: Failed to write installer directory\n");
            } else {
                fprintf(stderr, "[ADL] Wrote installer directory tree at LBA %llu\n", 
                        (unsigned long long)installer_lba);
            }
        } else {
            /* 处理单一文件 */
            if (adl_write_file_to_iso(iso, installer_file, installer_lba, NULL) != 0) {
                fprintf(stderr, "WARNING: Failed to write Installer.iya\n");
            } else {
                fprintf(stderr, "[ADL] Wrote Installer.iya at LBA %llu\n", 
                        (unsigned long long)installer_lba);
            }
        }
    }
    
    fprintf(stderr, "\n[ADL] Padding ISO to size...\n");
    
    /* 填充到最终尺寸 */
    fseek(iso, 0, SEEK_END);
    long current_pos = ftell(iso);
    long target_size = (long)iso_size_sectors * SECTOR_SIZE;
    
    if (current_pos < target_size) {
        uint8_t zeros[8192] = {0};
        long remaining = target_size - current_pos;
        
        while (remaining > 0) {
            long to_write = remaining > (long)sizeof(zeros) ? (long)sizeof(zeros) : remaining;
            if (fwrite(zeros, 1, to_write, iso) != (size_t)to_write) {
                fprintf(stderr, "ERROR: Failed to write padding\n");
                fclose(iso);
                return -1;
            }
            remaining -= to_write;
        }
    }
    
    /* === 获取实际的ISO文件大小（关键修复） ===
     * 不能依赖iso_size_sectors的估计值，必须读取实际的文件大小
     * 这样才能正确设置backup_lba和last_usable_lba
     */
    fseek(iso, 0, SEEK_END);
    long actual_file_size = ftell(iso);
    uint64_t actual_total_lba = actual_file_size / SECTOR_SIZE;
    
    fprintf(stderr, "[ADL] Actual ISO file size: %ld bytes = %llu LBA\n", 
            actual_file_size, (unsigned long long)actual_total_lba);
    
    /* === 重新生成主GPT头（关键修复）===
     * 主GPT头之前是基于iso_size_sectors（估计值）生成的
     * 现在需要基于实际文件大小重新生成，确保主备GPT一致
     */
    fprintf(stderr, "[ADL] Regenerating primary GPT header with actual file size...\n");
    
    uint32_t entries_crc32_recalc = 0;
    if (adl_generate_gpt_entries(iso, ESP_START_LBA, ESP_START_LBA + ESP_SIZE_SECTORS - 1,
                                  esp_uuid, adl_anchor_uuid, &entries_crc32_recalc) != 0) {
        fprintf(stderr, "ERROR: Failed to regenerate GPT entries\n");
        fclose(iso);
        return -1;
    }
    
    if (adl_generate_gpt_header(iso, actual_total_lba, disk_uuid, entries_crc32_recalc) != 0) {
        fprintf(stderr, "ERROR: Failed to regenerate primary GPT header\n");
        fclose(iso);
        return -1;
    }
    fprintf(stderr, "[ADL] Regenerated primary GPT header at LBA 1 with actual total_lba=%llu\n",
            (unsigned long long)actual_total_lba);
    
    if (adl_write_backup_gpt(iso, actual_total_lba) != 0) {
        fprintf(stderr, "ERROR: Failed to write backup GPT structures.\n");
        fclose(iso);
        return -1;
    }
    
    fclose(iso);
    
    fprintf(stderr, "\n===============================================================================\n");
    fprintf(stderr, "✓ ADL ISO Generation Successful\n");
    fprintf(stderr, "  Output: %s\n", output_iso);
    fprintf(stderr, "  Size: %.2f MB (%llu sectors)\n", 
            (double)iso_size_sectors * SECTOR_SIZE / (1024 * 1024),
            (unsigned long long)iso_size_sectors);
    fprintf(stderr, "===============================================================================\n");
    
    return 0;
}

/**
 * 计算所需的ISO大小
 */
uint64_t adl_iso_builder_calculate_size(
    const char *kernel_file,
    const char *efi_boot_file,
    const char *installer_file,
    const char *drivers_dir)
{
    uint64_t kernel_size = adl_get_file_size(kernel_file);
    uint64_t efi_size = adl_get_file_size(efi_boot_file);
    uint64_t installer_size = installer_file ? adl_get_item_size(installer_file) : 0;
    
    uint64_t content_size = kernel_size + efi_size + installer_size + (20 * 1024 * 1024);
    uint64_t total_sectors = AEFS_POOL_START_LBA + (content_size / SECTOR_SIZE) + 2048;
    
    return total_sectors;
}

/**
 * ============================================================================
 * AEFS-only 模式：追加AEFS内容到现有ISO
 * ============================================================================
 * 
 * 这个函数用于两阶段构建：
 * 1. buildIso.sh 使用 dd/gdisk/mtools 创建基础ISO（MBR+GPT+FAT32 ESP只）
 * 2. 本函数打开该ISO，追加AEFS文件系统和内容
 * 
 * 关键：本函数不生成MBR/GPT/FAT32，只关注AEFS部分
 */
int adl_iso_builder_append_aefs(
    const char *iso_path,
    const char *kernel_file,
    const char *installer_file,
    const char *drivers_dir)
{
    fprintf(stderr, "===============================================================================\n");
    fprintf(stderr, "ADL ISO Builder - AEFS-Only Append Mode\n");
    fprintf(stderr, "===============================================================================\n");
    
    /* 验证输入文件 */
    if (!kernel_file || !iso_path) {
        fprintf(stderr, "ERROR: Missing required parameters\n");
        return -1;
    }
    
    uint64_t kernel_size = adl_get_file_size(kernel_file);
    uint64_t installer_size = installer_file ? adl_get_item_size(installer_file) : 0;
    
    if (kernel_size == 0) {
        fprintf(stderr, "ERROR: Kernel file not found\n");
        return -1;
    }
    
    int installer_is_dir = installer_file ? adl_is_directory(installer_file) : 0;
    
    fprintf(stderr, "[ADL] Kernel size: %llu bytes\n", (unsigned long long)kernel_size);
    fprintf(stderr, "[ADL] Installer size: %llu bytes%s\n", (unsigned long long)installer_size,
            installer_is_dir ? " (directory)" : "");
    
    /* 打开现有ISO（追加模式） */
    FILE *iso = fopen(iso_path, "r+b");
    if (!iso) {
        fprintf(stderr, "ERROR: Cannot open ISO file: %s\n", iso_path);
        return -1;
    }
    
    /* 生成UUIDs */
    uint8_t disk_uuid[16];
    uint8_t esp_uuid[16];
    uint8_t boot_uuid[16], payload_uuid[16];
    adl_generate_uuid(disk_uuid);
    adl_generate_uuid(esp_uuid);
    adl_generate_uuid(boot_uuid);
    adl_generate_uuid(payload_uuid);

    /* 在ESP后预留区写入ADL Header（替代旧的LBA34位置） */
    fseek(iso, 0, SEEK_END);
    {
        long iso_size_bytes = ftell(iso);
        uint64_t iso_total_lba = (uint64_t)(iso_size_bytes / SECTOR_SIZE);
        if (adl_write_adl_header(iso, iso_total_lba, disk_uuid) != 0) {
            fprintf(stderr, "ERROR: Failed to write ADL Header\n");
            fclose(iso);
            return -1;
        }

        if (adl_generate_partition_table(iso, esp_uuid, payload_uuid, iso_total_lba) != 0) {
            fprintf(stderr, "ERROR: Failed to write ADL Partition Table\n");
            fclose(iso);
            return -1;
        }
    }

    fprintf(stderr, "[ADL] Wrote Partition Table at LBA %d\n", ADL_PARTITION_TABLE_LBA);
    
    fprintf(stderr, "\n[ADL STAGE 1] Writing AEFS Structures at LBA %d...\n", AEFS_POOL_START_LBA);
    
    /* === AEFS Anchor Block === */
    if (adl_generate_aefs_anchor(iso, AEFS_POOL_START_LBA, payload_uuid) != 0) {
        fprintf(stderr, "ERROR: Failed to generate AEFS Anchor\n");
        fclose(iso);
        return -1;
    }
    fprintf(stderr, "[ADL] Wrote AEFS Anchor Block at LBA %d\n", AEFS_POOL_START_LBA);
    
    /* === AEFS Checkpoint === */
    if (adl_generate_aefs_checkpoint(iso, AEFS_POOL_START_LBA + 1, AEFS_POOL_START_LBA) != 0) {
        fprintf(stderr, "ERROR: Failed to generate AEFS Checkpoint\n");
        fclose(iso);
        return -1;
    }
    fprintf(stderr, "[ADL] Wrote AEFS Checkpoint at LBA %d\n", AEFS_POOL_START_LBA + 1);
    
    /* === Volume Tree === */
    if (adl_generate_volume_tree(iso, AEFS_POOL_START_LBA + 2, boot_uuid, payload_uuid) != 0) {
        fprintf(stderr, "ERROR: Failed to generate Volume Tree\n");
        fclose(iso);
        return -1;
    }
    fprintf(stderr, "[ADL] Wrote Volume Tree at LBA %d\n", AEFS_POOL_START_LBA + 2);
    
    /* === AEFS Root Æ-Nodes for Volumes === */
    uint64_t boot_root_node_lba = AEFS_POOL_START_LBA + 3;
    uint64_t payload_root_node_lba = AEFS_POOL_START_LBA + 5;
    uint64_t kernel_lba = AEFS_POOL_START_LBA + 10;
    uint64_t installer_lba = AEFS_POOL_START_LBA + 15;
    
    if (adl_generate_aefs_boot_volume(iso, boot_root_node_lba, kernel_lba, installer_lba,
                                       boot_uuid) != 0) {
        fprintf(stderr, "ERROR: Failed to generate >:BOOT volume root\n");
        fclose(iso);
        return -1;
    }
    
    if (adl_generate_aefs_payload_volume(iso, payload_root_node_lba, 
                                         AEFS_POOL_START_LBA + 20,
                                         payload_uuid) != 0) {
        fprintf(stderr, "ERROR: Failed to generate >:PAYLOAD volume root\n");
        fclose(iso);
        return -1;
    }
    
    fprintf(stderr, "\n[ADL STAGE 2] Writing Kernel and Content...\n");
    
    /* === Kernel.aki === */
    if (adl_write_file_to_iso(iso, kernel_file, kernel_lba, NULL) != 0) {
        fprintf(stderr, "ERROR: Failed to write kernel.aki\n");
        fclose(iso);
        return -1;
    }
    fprintf(stderr, "[ADL] Wrote kernel.aki at LBA %llu\n", (unsigned long long)kernel_lba);
    
    /* === Installer === */
    if (installer_file) {
        if (installer_is_dir) {
            fprintf(stderr, "[ADL] Processing installer directory: %s\n", installer_file);
            uint64_t current_lba = installer_lba;
            if (adl_write_directory_tree(iso, installer_file, &current_lba) != 0) {
                fprintf(stderr, "WARNING: Failed to write installer directory\n");
            } else {
                fprintf(stderr, "[ADL] Wrote installer directory tree at LBA %llu\n", 
                        (unsigned long long)installer_lba);
            }
        } else {
            if (adl_write_file_to_iso(iso, installer_file, installer_lba, NULL) != 0) {
                fprintf(stderr, "WARNING: Failed to write Installer\n");
            } else {
                fprintf(stderr, "[ADL] Wrote Installer at LBA %llu\n", 
                        (unsigned long long)installer_lba);
            }
        }
    }
    
    /* 获取文件大小 */
    fseek(iso, 0, SEEK_END);
    long actual_file_size = ftell(iso);
    uint64_t actual_total_lba = actual_file_size / SECTOR_SIZE;
    
    fprintf(stderr, "\n[ADL] Actual ISO file size: %ld bytes = %llu LBA\n", 
            actual_file_size, (unsigned long long)actual_total_lba);
    
    /* === 写入备份GPT === */
    if (adl_write_backup_gpt(iso, actual_total_lba) != 0) {
        fprintf(stderr, "ERROR: Failed to write backup GPT structures.\n");
        fclose(iso);
        return -1;
    }
    
    fclose(iso);
    
    fprintf(stderr, "\n===============================================================================\n");
    fprintf(stderr, "✓ AEFS Content Appended Successfully\n");
    fprintf(stderr, "  Output: %s\n", iso_path);
    fprintf(stderr, "  Size: %.2f MB (%llu sectors)\n", 
            (double)actual_total_lba * SECTOR_SIZE / (1024 * 1024),
            (unsigned long long)actual_total_lba);
    fprintf(stderr, "===============================================================================\n");
    
    return 0;
}
