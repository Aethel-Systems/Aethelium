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
 * AethelOS ADL (Aethel Disk Layout) - 工业级实现
 * ============================================================================
 * 完整的磁盘镜像生成、UEFI启动链、AEFS卷编织实现
 * ============================================================================
 */

#include "adl_disk.h"
#include "aefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

/* ============================================================================
 * 工具函数
 * ============================================================================ */

/**
 * 计算CRC32校验和
 */
static uint32_t crc32_calculate(const uint8_t *data, size_t size, uint32_t init) {
    static uint32_t crc32_table[256] = {0};
    
    /* 初始化CRC32表 */
    if (crc32_table[1] == 0) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t crc = i;
            for (int j = 0; j < 8; j++) {
                crc = (crc >> 1) ^ (((crc & 1) ? 0xEDB88320 : 0));
            }
            crc32_table[i] = crc;
        }
    }
    
    uint32_t crc = init;
    for (size_t i = 0; i < size; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

/**
 * 生成随机UUID
 */
static void generate_uuid(uint8_t *uuid) {
    for (int i = 0; i < 16; i++) {
        uuid[i] = (uint8_t)(rand() & 0xFF);
    }
    /* RFC 4122版本4 UUID标记 */
    uuid[6] = (uuid[6] & 0x0F) | 0x40;
    uuid[8] = (uuid[8] & 0x3F) | 0x80;
}

/**
 * 小端序写入32位整数
 */
static inline void write_le32(uint8_t *buf, uint32_t val) {
    buf[0] = (val >> 0) & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;
}

/**
 * 小端序写入64位整数
 */
static inline void write_le64(uint8_t *buf, uint64_t val) {
    write_le32(buf, (uint32_t)(val & 0xFFFFFFFF));
    write_le32(buf + 4, (uint32_t)((val >> 32) & 0xFFFFFFFF));
}

/**
 * 读取文件大小
 */
static uint64_t get_file_size(const char *filename) {
    struct stat st;
    if (stat(filename, &st) != 0) {
        return 0;
    }
    return (uint64_t)st.st_size;
}

/**
 * 在ISO中寻址到特定LBA
 */
static int seek_to_lba(FILE *iso_file, uint64_t lba) {
    uint64_t offset = lba * SECTOR_SIZE;
    if (fseek(iso_file, offset, SEEK_SET) != 0) {
        fprintf(stderr, "Error: Failed to seek to LBA %llu\n", (unsigned long long)lba);
        return -1;
    }
    return 0;
}


/**
 * 写入一个扇区（自动填充到512字节）
 */
static int write_sector(FILE *iso_file, const uint8_t *data, size_t size) {
    if (size > SECTOR_SIZE) {
        fprintf(stderr, "Error: Data size (%zu) exceeds sector size (%d)\n", size, SECTOR_SIZE);
        return -1;
    }
    
    /* 写入数据 */
    if (fwrite(data, 1, size, iso_file) != size) {
        fprintf(stderr, "Error: Failed to write sector data\n");
        return -1;
    }
    
    /* 填充到512字节 */
    if (size < SECTOR_SIZE) {
        size_t padding = SECTOR_SIZE - size;
        uint8_t zeros[512] = {0};
        if (fwrite(zeros, 1, padding, iso_file) != padding) {
            fprintf(stderr, "Error: Failed to write sector padding\n");
            return -1;
        }
    }
    
    return 0;
}

/**
 * 写入多个扇区
 */
static int write_sectors(FILE *iso_file, const uint8_t *data, uint64_t num_sectors) {
    for (uint64_t i = 0; i < num_sectors; i++) {
        size_t to_write = (num_sectors - i > 1) ? SECTOR_SIZE : SECTOR_SIZE;
        if (fwrite(data + i * SECTOR_SIZE, 1, to_write, iso_file) != to_write) {
            fprintf(stderr, "Error: Failed to write sector %llu\n", (unsigned long long)i);
            return -1;
        }
    }
    return 0;
}

/* ============================================================================
 * ADL Header 生成
 * ============================================================================ */

/**
 * 生成ADL MBR Header（第一个440字节）
 */
static int generate_adl_mbr_header(uint8_t *header, uint64_t total_lba,
                                    const uint8_t *disk_uuid, int disk_type) {
    memset(header, 0, 440);
    
    /* BootCode: EB 3C 90 (跳转到启动代码) */
    header[0] = 0xEB;
    header[1] = 0x3C;
    header[2] = 0x90;
    
    /* OEM ID: "AETHELOS" */
    memcpy(&header[3], "AETHELOS", 8);
    
    /* 字节/扇区: 512 (小端序) */
    write_le32(&header[11], 512);
    
    /* 扇区/簇: 8 */
    header[15] = 8;
    
    /* 保留扇区: 1 */
    write_le32(&header[16], 1);
    
    /* FAT表数: 2 */
    header[20] = 2;
    
    /* 根目录项数: 512 */
    write_le32(&header[21], 512);
    
    /* 介质描述符: 0xF8 */
    header[25] = 0xF8;
    
    /* 磁道扇区数: 63 */
    write_le32(&header[26], 63);
    
    /* 磁头数: 255 */
    write_le32(&header[28], 255);
    
    /* === ADL特定字段 === */
    
    /* ADL魔数 */
    uint64_t magic = ADL_MAGIC_NUMBER;
    write_le32(&header[36], (uint32_t)(magic & 0xFFFFFFFF));
    write_le32(&header[40], (uint32_t)((magic >> 32) & 0xFFFFFFFF));
    
    /* 分区表LBA: 64 */
    write_le64(&header[44], ADL_PARTITION_TABLE_LBA);
    
    /* 总LBA数 */
    write_le64(&header[52], total_lba);
    
    /* 磁盘UUID (16字节，这里用4个32位字段) */
    memcpy(&header[60], disk_uuid, 16);
    
    /* ADL版本 */
    header[76] = ADL_VERSION_MAJOR;
    header[77] = ADL_VERSION_MINOR;
    
    /* 磁盘类型 */
    header[78] = disk_type;
    
    /* 校验和计算（暂时留0） */
    write_le32(&header[80], 0);
    
    return 0;
}

/**
 * 生成MBR分区表（第二个72字节）
 * 为了UEFI兼容，添加GPT保护分区项
 */
static int generate_mbr_partition_table(uint8_t *partition_table) {
    memset(partition_table, 0, 72);
    
    /* 分区项1: GPT保护分区 */
    uint8_t *entry = partition_table;
    
    entry[0] = 0x00;        /* 启动标记 (不可启动) */
    entry[1] = 0x00;        /* 起始磁头 */
    entry[2] = 0x02;        /* 起始扇区 */
    entry[3] = 0x00;        /* 起始柱面 */
    entry[4] = 0xEE;        /* 分区类型: GPT保护 */
    entry[5] = 0xFF;        /* 结束磁头 */
    entry[6] = 0xFF;        /* 结束扇区 */
    entry[7] = 0xFF;        /* 结束柱面 */
    write_le32(&entry[8], 1);                           /* 分区起始LBA: 1 */
    write_le32(&entry[12], 0xFFFFFFFF);                 /* 分区大小（填满） */
    
    /* 签名 */
    partition_table[64] = 0x55;
    partition_table[65] = 0xAA;
    
    return 0;
}

/**
 * 生成ADL分区表项
 */
static int generate_adl_partition_entries(FILE *iso_file, const uint8_t *aefs_pool_uuid,
                                           uint64_t aefs_pool_lba, uint64_t aefs_pool_size) {
    ADL_Partition_Table part_table;
    memset(&part_table, 0, sizeof(part_table));
    
    /* 第一个分区: AEFS_POOL */
    ADL_Partition_Entry *entry = &part_table.entries[0];
    
    memcpy(entry->partition_uuid, aefs_pool_uuid, 16);
    write_le64((uint8_t*)&entry->partition_lba, aefs_pool_lba);
    write_le64((uint8_t*)&entry->partition_size_sectors, aefs_pool_size);
    entry->partition_type = 0x41455350;  /* "AESP" */
    entry->flags = 0x00000001;           /* 标记为活跃 */
    strncpy(entry->partition_name, "AEFS_POOL", 15);
    
    /* 将分区表写到LBA 64 */
    if (seek_to_lba(iso_file, ADL_PARTITION_TABLE_LBA) != 0) {
        return -1;
    }
    
    if (write_sector(iso_file, (const uint8_t *)&part_table, sizeof(part_table)) != 0) {
        return -1;
    }
    
    return 0;
}

/* ============================================================================
 * Shadow GPT 生成
 * ============================================================================ */

/**
 * 生成Shadow GPT Header
 */
static int generate_shadow_gpt_header(FILE *iso_file, const uint8_t *disk_uuid,
                                      uint64_t total_lba) {
    Shadow_GPT_Header gpt;
    memset(&gpt, 0, sizeof(gpt));
    
    /* 签名 */
    memcpy(gpt.signature, "EFI PART", 8);
    gpt.revision = 0x00010000;      /* 1.0 */
    gpt.header_size = 92;
    gpt.my_lba = ADL_GPT_PRIMARY_LBA;
    gpt.alternate_lba = total_lba - 1;
    gpt.first_usable_lba = 1024;
    gpt.last_usable_lba = total_lba - 34;
    
    memcpy(gpt.disk_guid, disk_uuid, 16);
    gpt.partition_entry_lba = ADL_GPT_ENTRIES_LBA;
    gpt.num_partition_entries = 1;
    gpt.sizeof_partition_entry = 128;
    
    /* 计算CRC32 */
    uint32_t header_crc = crc32_calculate((const uint8_t*)&gpt, 92, 0xFFFFFFFF) ^ 0xFFFFFFFF;
    gpt.header_crc32 = header_crc;
    
    if (seek_to_lba(iso_file, ADL_GPT_PRIMARY_LBA) != 0) {
        return -1;
    }
    
    if (write_sector(iso_file, (const uint8_t *)&gpt, sizeof(gpt)) != 0) {
        return -1;
    }
    
    return 0;
}

/**
 * 生成Shadow GPT分区项
 */
static int generate_shadow_gpt_entries(FILE *iso_file, const uint8_t *esp_uuid) {
    Shadow_GPT_Entry entry;
    memset(&entry, 0, sizeof(entry));
    
        /* 
     * EFI System Partition GUID: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
     * 注意：GPT 中 GUID 的前三个字段 (Data1, Data2, Data3) 是小端序存储的！
     */
    uint8_t esp_guid[16] = {
        0x28, 0x73, 0x2A, 0xC1, // Data1: C12A7328 -> 反转为 28 73 2A C1
        0x1F, 0xF8,             // Data2: F81F     -> 反转为 1F F8
        0xD2, 0x11,             // Data3: 11D2     -> 反转为 D2 11
        0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B // Data4: 保持不变
    };
    
    memcpy(entry.partition_type_guid, esp_guid, 16);
    memcpy(entry.unique_partition_guid, esp_uuid, 16);
    /* Shadow GPT分区项指向ESP FAT32分区 */
    entry.starting_lba = ESP_START_LBA;
    entry.ending_lba = ESP_START_LBA + ESP_SIZE_SECTORS - 1;
    entry.attributes = 0x0000000000000001;  /* 必需分区 */
    
    /* 分区名称(UTF-16LE): "ESP" */
    const char *name = "ESP";
    for (int i = 0; i < 3 && name[i]; i++) {
        entry.partition_name[i] = name[i];
    }
    
    if (seek_to_lba(iso_file, ADL_GPT_ENTRIES_LBA) != 0) {
        return -1;
    }
    
    if (write_sector(iso_file, (const uint8_t *)&entry, sizeof(entry)) != 0) {
        return -1;
    }
    
    return 0;
}

/* ============================================================================
 * FAT32 ESP 初始化
 * ============================================================================ */

/**
 * 生成FAT32启动扇区
 */
static int generate_fat32_boot_sector(uint8_t *boot_sector, uint32_t total_sectors) {
    memset(boot_sector, 0, SECTOR_SIZE);
    
    FAT32_BootSector *bs = (FAT32_BootSector *)boot_sector;
    
    /* 跳转指令 */
    bs->jump[0] = 0xEB;
    bs->jump[1] = 0x3C;
    bs->jump[2] = 0x90;
    
    /* OEM ID */
    memcpy(bs->oem_id, "AETHEL  ", 8);
    
    /* 字节/扇区 */
    bs->bytes_per_sector = SECTOR_SIZE;
    
    /* 扇区/簇 */
    bs->sectors_per_cluster = 8;  /* 4KB簇 */
    
    /* 保留扇区 */
    bs->reserved_sectors = 32;
    
    /* FAT表数 */
    bs->num_fats = 2;
    
    /* 根目录项数 (FAT32中为0) */
    bs->root_entries = 0;
    
    /* 介质描述符 */
    bs->media_descriptor = 0xF8;
    
    /* 磁道扇区数 */
    bs->sectors_per_track = 63;
    
    /* 磁头数 */
    bs->heads = 255;
    
    /* 隐藏扇区 - 关键：指向ESP分区在驱动器中的起始LBA */
    bs->hidden_sectors = ESP_START_LBA;
    
    /* 总扇区数(32位) */
    bs->total_sectors_32 = total_sectors;
    
    /* FAT32特定 */
    uint32_t fat_size = (total_sectors + 0xFFF) / 0x1000;  /* 保守计算FAT表大小 */
    bs->sectors_per_fat_32 = fat_size;
    
    bs->ext_flags = 0x0000;
    bs->fs_version = 0x0000;
    bs->root_cluster = 2;           /* 根目录在簇2 */
    bs->fs_info_sector = 1;         /* FSInfo在启动扇区后 */
    bs->backup_boot_sector = 6;     /* 备份启动扇区 */
    
    /* 驱动器号 */
    bs->drive_number = 0x80;
    
    /* 启动签名 */
    bs->boot_signature = 0x29;
    bs->volume_id = 0x12345678;
    memcpy(bs->volume_label, "AETHEL OS  ", 11);
    memcpy(bs->fs_type, "FAT32   ", 8);
    
    /* 启动扇区签名 */
    bs->boot_sector_signature = 0xAA55;
    
    return 0;
}

/* 辅助：更新FAT表项 */
static int update_fat_entry(FILE *iso_file, uint32_t cluster_index, uint32_t value) {
    /* FAT32每个项4字节。FAT1起始于 ESP_START_LBA + 32 (保留扇区) */
    /* FAT2起始于 ESP_START_LBA + 32 + FAT_SIZE */
    
    uint32_t fat_offset_sectors = 32;
    uint64_t fat1_start_byte = (ESP_START_LBA + fat_offset_sectors) * SECTOR_SIZE;
    uint64_t entry_offset = cluster_index * 4;
    
    /* 更新 FAT1 */
    fseek(iso_file, fat1_start_byte + entry_offset, SEEK_SET);
    uint8_t buf[4];
    write_le32(buf, value);
    fwrite(buf, 1, 4, iso_file);
    
    /* 更新 FAT2 (镜像) - 假设FAT表大小为 512 扇区 */
    uint64_t fat2_start_byte = fat1_start_byte + (512 * SECTOR_SIZE);
    fseek(iso_file, fat2_start_byte + entry_offset, SEEK_SET);
    fwrite(buf, 1, 4, iso_file);
    
    return 0;
}

/**
 * [替换原函数] 生成初始FAT表
 * 必须初始化前三个簇：0(Media), 1(EOC), 2(Root Dir EOC)
 */
static int generate_fat_table(FILE *iso_file, uint32_t fat_size_sectors, uint32_t ignored) {
    fprintf(stderr, "[ADL] Initializing FAT Table...\n");

    /* 1. 先把整个 FAT 表区域清零 */
    uint8_t zero_sector[SECTOR_SIZE] = {0};
    /* FAT1 */
    if (seek_to_lba(iso_file, ESP_START_LBA + 32) != 0) return -1;
    for (uint32_t i = 0; i < fat_size_sectors; i++) fwrite(zero_sector, 1, SECTOR_SIZE, iso_file);
    /* FAT2 */
    if (seek_to_lba(iso_file, ESP_START_LBA + 32 + fat_size_sectors) != 0) return -1;
    for (uint32_t i = 0; i < fat_size_sectors; i++) fwrite(zero_sector, 1, SECTOR_SIZE, iso_file);

    /* 2. 初始化特殊项 */
    /* Entry 0: Media Type (0x0FFFFFF8) */
    update_fat_entry(iso_file, 0, 0x0FFFFFF8);
    
    /* Entry 1: EOC / Hard Error (0x0FFFFFFF) */
    update_fat_entry(iso_file, 1, 0x0FFFFFFF);
    
    /* Entry 2: Root Directory (Cluster 2) -> EOC (结束) */
    /* 注意：如果不标记为 EOC，UEFI 会试图读取 Cluster 3 作为根目录的延续，导致读取错误 */
    update_fat_entry(iso_file, 2, 0x0FFFFFFF);
    
    return 0;
}

/**
 * 在FAT32中创建根目录并添加BOOTX64.EFI条目
 */
static int create_fat32_root_directory(FILE *iso_file, const char *bootloader_file) {
    uint8_t root_dir_sector[SECTOR_SIZE];
    memset(root_dir_sector, 0, SECTOR_SIZE);
    
    FAT32_DirEntry *entry = (FAT32_DirEntry *)root_dir_sector;
    
    /* 第一个条目: BOOTX64.EFI */
    memcpy(entry->filename, "BOOTX64", 7);
    memcpy(entry->extension, "EFI", 3);
    entry->attributes = 0x20;  /* Archive */
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    entry->creation_date = ((tm_info->tm_year - 80) << 9) | ((tm_info->tm_mon + 1) << 5) | tm_info->tm_mday;
    entry->creation_time = (tm_info->tm_hour << 11) | (tm_info->tm_min << 5) | (tm_info->tm_sec >> 1);
    entry->high_cluster = 0;
    entry->low_cluster = 3;  /* 簇3 */
    
    uint64_t file_size = get_file_size(bootloader_file);
    if (file_size > 0xFFFFFFFF) {
        file_size = 0xFFFFFFFF;
    }
    entry->file_size = (uint32_t)file_size;
    
    /* 根目录位置: ESP_START_LBA + 32 + 2*FAT_SIZE + 64 */
    uint32_t root_dir_lba = ESP_START_LBA + 32 + 2 * 512 + 64;
    
    if (seek_to_lba(iso_file, root_dir_lba) != 0) {
        return -1;
    }
    
    if (write_sector(iso_file, root_dir_sector, SECTOR_SIZE) != 0) {
        return -1;
    }
    
    return 0;
}

/**
 * 初始化完整的ESP FAT32分区
 */
int adl_initialize_esp_fat32(FILE *iso_file, const char *bootloader_file) {
    fprintf(stderr, "[ADL] Initializing ESP FAT32 partition at LBA 1024\n");
    
    if (seek_to_lba(iso_file, ESP_START_LBA) != 0) {
        return -1;
    }
    
    /* 生成启动扇区 */
    uint8_t boot_sector[SECTOR_SIZE];
    if (generate_fat32_boot_sector(boot_sector, ESP_SIZE_SECTORS) != 0) {
        return -1;
    }
    
    if (write_sector(iso_file, boot_sector, SECTOR_SIZE) != 0) {
        return -1;
    }
    
    /* 生成FAT表 */
    if (generate_fat_table(iso_file, 512, 0) != 0) {
        return -1;
    }
    
    /* 创建根目录 */
    if (create_fat32_root_directory(iso_file, bootloader_file) != 0) {
        return -1;
    }
    
    /* 写入启动文件 */
    if (adl_write_to_fat32(iso_file, bootloader_file, NULL, 0) != 0) {
        fprintf(stderr, "[ADL] Warning: Failed to write bootloader to FAT32\n");
    }
    
    fprintf(stderr, "[ADL] ESP FAT32 initialization complete\n");
    return 0;
}

/**
 * 在FAT32中写入文件
 */
/**
 * [替换原函数] 写入文件数据并建立 FAT 链
 */
int adl_write_to_fat32(FILE *iso_file, const char *filename,
                       const uint8_t *ignore_data, uint32_t ignore_size) {
    FILE *src = fopen(filename, "rb");
    if (!src) return -1;
    
    fseek(src, 0, SEEK_END);
    uint32_t size = ftell(src);
    fseek(src, 0, SEEK_SET);
    
    /* 计算需要的簇数量 (4KB/簇) */
    uint32_t clusters_needed = (size + 4095) / 4096;
    uint32_t start_cluster = 3; /* 紧接在根目录(Cluster 2)之后 */
    
    /* 数据区起始 LBA */
    uint64_t data_start_lba = ESP_START_LBA + 32 + (512 * 2);
    
    uint8_t buf[4096];
    
    for (uint32_t i = 0; i < clusters_needed; i++) {
        uint32_t current_cluster = start_cluster + i;
        
        /* 1. 更新 FAT 表 */
        uint32_t next_cluster;
        if (i == clusters_needed - 1) {
            next_cluster = 0x0FFFFFFF; /* 链表结束 */
        } else {
            next_cluster = current_cluster + 1;
        }
        update_fat_entry(iso_file, current_cluster, next_cluster);
        
        /* 2. 写入数据到物理扇区 */
        /* 物理 LBA = DataStart + (ClusterN - 2) * 8 */
        uint64_t target_lba = data_start_lba + (current_cluster - 2) * 8;
        
        if (seek_to_lba(iso_file, target_lba) != 0) break;
        
        memset(buf, 0, 4096);
        size_t n = fread(buf, 1, 4096, src);
        if (n > 0) {
            fwrite(buf, 1, 4096, iso_file); /* 写入8个扇区 */
        }
    }
    
    fclose(src);
    return 0;
}

/* ============================================================================
 * AEFS 卷结构
 * ============================================================================ */

/**
 * 生成AEFS Checkpoint
 */
static int generate_aefs_checkpoint(FILE *iso_file, uint64_t checkpoint_lba) {
    AEFS_Checkpoint checkpoint;
    memset(&checkpoint, 0, sizeof(checkpoint));
    
    checkpoint.magic = 0x4B50434841455341ULL;  /* AEFS_CHECKPOINT_MAGIC */
    checkpoint.version = 1;
    checkpoint.timestamp = time(NULL);
    checkpoint.root_inode_lba = checkpoint_lba + 2;
    checkpoint.volume_tree_lba = checkpoint_lba + 3;
    checkpoint.free_blocks = 0;
    checkpoint.total_blocks = 0;
    
    if (seek_to_lba(iso_file, checkpoint_lba) != 0) {
        return -1;
    }
    
    if (write_sector(iso_file, (const uint8_t *)&checkpoint, sizeof(checkpoint)) != 0) {
        return -1;
    }
    
    return 0;
}

/**
 * 生成AEFS卷清单
 */
static int generate_aefs_volume_manifest(FILE *iso_file, uint64_t manifest_lba,
                                         const uint8_t *boot_uuid, const uint8_t *payload_uuid) {
    uint8_t manifest_sector[SECTOR_SIZE];
    memset(manifest_sector, 0, SECTOR_SIZE);
    
    AEFS_ADL_Volume_Entry *entry = (AEFS_ADL_Volume_Entry *)manifest_sector;
    
    /* >:BOOT 卷 */
    memcpy(entry[0].volume_uuid, boot_uuid, 16);
    entry[0].volume_root_lba = 0;  /* 稍后填充 */
    strcpy(entry[0].volume_path, ">:BOOT");
    
    /* >:PAYLOAD 卷 */
    memcpy(entry[1].volume_uuid, payload_uuid, 16);
    entry[1].volume_root_lba = 0;  /* 稍后填充 */
    strcpy(entry[1].volume_path, ">:PAYLOAD");
    
    if (seek_to_lba(iso_file, manifest_lba) != 0) {
        return -1;
    }
    
    if (write_sector(iso_file, manifest_sector, SECTOR_SIZE) != 0) {
        return -1;
    }
    
    return 0;
}

/**
 * 生成AEFS卷结构
 */
int adl_generate_aefs_volume(FILE *iso_file, uint64_t aefs_start_lba) {
    fprintf(stderr, "[ADL] Generating AEFS volume structure at LBA %llu\n",
            (unsigned long long)aefs_start_lba);
    
    uint8_t boot_uuid[16], payload_uuid[16];
    generate_uuid(boot_uuid);
    generate_uuid(payload_uuid);
    
    /* 生成Checkpoint */
    if (generate_aefs_checkpoint(iso_file, aefs_start_lba) != 0) {
        return -1;
    }
    
    /* 生成卷清单 */
    if (generate_aefs_volume_manifest(iso_file, aefs_start_lba + 1,
                                      boot_uuid, payload_uuid) != 0) {
        return -1;
    }
    
    fprintf(stderr, "[ADL] AEFS volume structure created\n");
    return 0;
}

/* ============================================================================
 * 完整ISO生成
 * ============================================================================ */

/**
 * 读取整个文件到缓冲区
 */
static uint8_t* read_file_to_buffer(const char *filename, uint64_t *out_size) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > BOOTX64_MAX_SIZE) {
        fprintf(stderr, "Error: Invalid file size: %ld\n", file_size);
        fclose(f);
        return NULL;
    }
    
    uint8_t *buffer = malloc(file_size);
    if (!buffer) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(f);
        return NULL;
    }
    
    if (fread(buffer, 1, file_size, f) != (size_t)file_size) {
        fprintf(stderr, "Error: Failed to read file\n");
        free(buffer);
        fclose(f);
        return NULL;
    }
    
    fclose(f);
    *out_size = file_size;
    return buffer;
}

/**
 * 完整的ISO镜像生成函数
 */
int adl_generate_complete_iso(const char *kernel_file, const char *efi_boot_file,
                              const char *output_iso, uint64_t iso_size_mb) {
    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "Generating Hybrid AEFS ISO Image (ADL Architecture)\n");
    fprintf(stderr, "================================================================================\n");
    
    /* 验证输入文件 */
    uint64_t kernel_size = get_file_size(kernel_file);
    uint64_t bootloader_size = get_file_size(efi_boot_file);
    
    if (kernel_size == 0) {
        fprintf(stderr, "Error: Kernel file not found or empty: %s\n", kernel_file);
        return -1;
    }
    
    if (bootloader_size == 0) {
        fprintf(stderr, "Error: Bootloader file not found or empty: %s\n", efi_boot_file);
        return -1;
    }
    
    fprintf(stderr, "[ADL] Kernel: %s (%llu bytes)\n", kernel_file, (unsigned long long)kernel_size);
    fprintf(stderr, "[ADL] Bootloader: %s (%llu bytes)\n", efi_boot_file, (unsigned long long)bootloader_size);
    fprintf(stderr, "[ADL] ISO Size: %llu MB\n", (unsigned long long)iso_size_mb);
    
    /* 创建ISO文件 */
    FILE *iso_file = fopen(output_iso, "w+b");
    if (!iso_file) {
        fprintf(stderr, "Error: Cannot create ISO file: %s\n", output_iso);
        return -1;
    }
    
    /* 计算总LBA数 */
    uint64_t total_lba = (iso_size_mb * 1024 * 1024) / SECTOR_SIZE;
    
    /* 生成UUIDs */
    uint8_t disk_uuid[16], aefs_pool_uuid[16];
    generate_uuid(disk_uuid);
    generate_uuid(aefs_pool_uuid);
    
    fprintf(stderr, "[ADL] Total LBA: %llu\n", (unsigned long long)total_lba);
    
    /* === 阶段1: ADL MBR + 分区表 === */
    fprintf(stderr, "\n[STAGE 1] Writing ADL MBR Header and Partition Table\n");
    
    uint8_t mbr_data[512];
    if (generate_adl_mbr_header(mbr_data, total_lba, disk_uuid, DISK_TYPE_SSD) != 0) {
        fclose(iso_file);
        return -1;
    }
    
    if (generate_mbr_partition_table(&mbr_data[440]) != 0) {
        fclose(iso_file);
        return -1;
    }
    
    if (seek_to_lba(iso_file, ADL_MBR_LBA) != 0) {
        fclose(iso_file);
        return -1;
    }
    
    if (write_sector(iso_file, mbr_data, SECTOR_SIZE) != 0) {
        fclose(iso_file);
        return -1;
    }
    
    /* === 阶段2: Shadow GPT === */
    fprintf(stderr, "[STAGE 2] Writing Shadow GPT\n");
    
    if (generate_shadow_gpt_header(iso_file, disk_uuid, total_lba) != 0) {
        fclose(iso_file);
        return -1;
    }
    
    /* 生成ESP UUID用于Shadow GPT */
    uint8_t esp_uuid[16];
    generate_uuid(esp_uuid);
    
    if (generate_shadow_gpt_entries(iso_file, esp_uuid) != 0) {
        fclose(iso_file);
        return -1;
    }
    
    /* === 阶段3: ADL分区表 === */
    fprintf(stderr, "[STAGE 3] Writing ADL Partition Table\n");
    
    uint64_t aefs_pool_size = total_lba - AEFS_POOL_START_LBA - 33;
    if (generate_adl_partition_entries(iso_file, aefs_pool_uuid,
                                       AEFS_POOL_START_LBA, aefs_pool_size) != 0) {
        fclose(iso_file);
        return -1;
    }
    
    /* === 阶段4: ESP FAT32 === */
    fprintf(stderr, "[STAGE 4] Writing ESP FAT32 Partition\n");
    
    if (adl_initialize_esp_fat32(iso_file, efi_boot_file) != 0) {
        fprintf(stderr, "Warning: ESP initialization incomplete\n");
    }
    
    /* === 阶段5: AEFS卷 === */
    fprintf(stderr, "[STAGE 5] Writing AEFS Volume Structure\n");
    
    if (adl_generate_aefs_volume(iso_file, AEFS_POOL_START_LBA) != 0) {
        fclose(iso_file);
        return -1;
    }
    
    /* === 阶段6: 备份Header === */
    fprintf(stderr, "[STAGE 6] Writing Backup Headers\n");
    
    /* 磁盘末尾-1: ADL Header备份 */
    if (seek_to_lba(iso_file, total_lba - 1) != 0) {
        fclose(iso_file);
        return -1;
    }
    
    if (write_sector(iso_file, mbr_data, SECTOR_SIZE) != 0) {
        fclose(iso_file);
        return -1;
    }
    
    /* 确保文件大小正确 */
    fseek(iso_file, 0, SEEK_END);
    uint64_t file_pos = ftell(iso_file);
    uint64_t expected_size = total_lba * SECTOR_SIZE;
    
    if (file_pos < expected_size) {
        fprintf(stderr, "[ADL] Padding ISO to %llu bytes\n", (unsigned long long)expected_size);
        uint8_t zero_sector[SECTOR_SIZE] = {0};
        while (file_pos < expected_size) {
            fwrite(zero_sector, 1, SECTOR_SIZE, iso_file);
            file_pos += SECTOR_SIZE;
        }
    }
    
    fclose(iso_file);
    
    /* 验证生成的ISO */
    uint64_t iso_file_size = get_file_size(output_iso);
    fprintf(stderr, "\n================================================================================\n");
    fprintf(stderr, "ISO Generation Complete\n");
    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "[ADL] ISO File: %s\n", output_iso);
    fprintf(stderr, "[ADL] ISO Size: %llu MB (%llu bytes)\n",
            iso_file_size / (1024 * 1024), (unsigned long long)iso_file_size);
    fprintf(stderr, "[ADL] Expected Size: %llu MB (%llu bytes)\n",
            iso_size_mb, expected_size);
    fprintf(stderr, "✓ ADL Hybrid ISO successfully generated\n");
    fprintf(stderr, "================================================================================\n");
    
    return 0;
}

/* ============================================================================
 * 磁盘类型识别
 * ============================================================================ */

/**
 * 识别磁盘类型 (SSD/HDD/RamDisk)
 */
int adl_detect_disk_type(const char *image_file) {
    /* 在Stage 1中，简单返回SSD类型 */
    /* Stage 2可以实现更复杂的检测逻辑 */
    return DISK_TYPE_SSD;
}
