/*
 * AethelOS ISO 生成器 - 完整的ADL(Aethel Disk Layout)实现
 *
 * 本模块负责创建包含以下结构的混合ISO镜像：
 * 1. ADL (Aethel Disk Layout) - 原生的完整分区表结构
 * 2. EFI系统分区 (ESP) - 包含EFI引导文件  
 * 3. AEFS分区 - 完整的ÆFS日志结构文件系统
 *
 * ISO布局 (使用标准ADL格式):
 * - LBA 0: Aethel Disk Header (主头)
 * - LBA 1-127: 主ADL分区表 (Partition_Entry数组)
 * - LBA 128-257: ESP分区 (FAT32 EFI系统分区)
 * - LBA 258+: AEFS分区 (完整的日志结构文件系统)
 * - LBA (最后-128): 备份ADL分区表
 * - LBA (最后-1): Aethel Disk Header (备份头)
 *
 * 严格遵循AEFS.txt设计文档的ADL规范实现
 */

#include "aefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* Forward declarations */
extern uint32_t aefs_checksum_xxh3(const uint8_t *data, size_t len);
extern int aefs_generate_uuid(uint8_t *uuid);

/* ============================================================================
 * MBR和GPT支持 - 混合分区表格式
 * ============================================================================ */

/**
 * 生成MBR (Master Boot Record) 分区表
 * 这允许QEMU等标准固件识别ESP和AEFS分区
 * MBR位于LBA 0的最后512字节处（扇区0）
 */
static void create_mbr_partition_table(uint8_t *mbr_sector, 
                                      uint32_t esp_start_lba, 
                                      uint32_t esp_size_sectors,
                                      uint32_t aefs_start_lba, 
                                      uint32_t aefs_size_sectors) {
    if (!mbr_sector) return;
    
    memset(mbr_sector, 0, 512);
    
    /* Boot code (minimal) */
    mbr_sector[0] = 0xEB;
    mbr_sector[1] = 0x52;
    mbr_sector[2] = 0x90;
    
    /* OEM/Brand name */
    memcpy(&mbr_sector[3], "AETHELOS", 8);
    
    /* MBR分区表 - 4个分区条目，每个16字节 */
    uint8_t *pte = &mbr_sector[446];  /* 分区表条目从字节446开始 */
    
    /* 分区1: ESP (FAT32 EFI) */
    pte[0] = 0x00;           /* 状态: 非活跃 */
    pte[1] = 0x00;           /* CHS Start - Head */
    pte[2] = 0x02;           /* CHS Start - Sector */
    pte[3] = 0x00;           /* CHS Start - Cylinder */
    pte[4] = 0xEF;           /* 分区类型: EFI System (0xEF) */
    pte[5] = 0x00;           /* CHS End - Head */
    pte[6] = 0x02;           /* CHS End - Sector */
    pte[7] = 0x00;           /* CHS End - Cylinder */
    *(uint32_t *)&pte[8]  = esp_start_lba;           /* LBA Start */
    *(uint32_t *)&pte[12] = esp_size_sectors;        /* LBA Size */
    
    /* 分区2: AEFS (Linux/Unix) */
    pte = &mbr_sector[462];
    pte[0] = 0x00;           /* 状态: 非活跃 */
    pte[1] = 0x00;           /* CHS Start */
    pte[2] = 0x02;
    pte[3] = 0x00;
    pte[4] = 0x83;           /* 分区类型: Linux (0x83) */
    pte[5] = 0x00;           /* CHS End */
    pte[6] = 0x02;
    pte[7] = 0x00;
    *(uint32_t *)&pte[8]  = aefs_start_lba;          /* LBA Start */
    *(uint32_t *)&pte[12] = aefs_size_sectors;       /* LBA Size */
    
    /* MBR签名 */
    *(uint16_t *)&mbr_sector[510] = 0xAA55;
}

/* ============================================================================
 * ISO 9660 Primary Volume Descriptor (PVD) 生成
 * ============================================================================ */

/**
 * 生成ISO 9660 Primary Volume Descriptor
 * 这是CD/DVD光盘的标准格式，UEFI和QEMU都依赖于此
 */
static void create_iso9660_pvd(uint8_t *pvd_sector) {
    if (!pvd_sector) return;
    
    memset(pvd_sector, 0, 2048);
    
    /* 偏移0: Volume Descriptor Type (1 = Primary) */
    pvd_sector[0] = 0x01;
    
    /* 偏移1-5: ISO 9660标准标识符 */
    memcpy(&pvd_sector[1], "CD001", 5);
    
    /* 偏移6: Version (必须为1) */
    pvd_sector[6] = 0x01;
    
    /* 偏移7-2047: PVD数据 (大多数留作0) */
    
    /* 偏移8-39: 系统标识符 */
    memcpy(&pvd_sector[8], "AETHEL OS           ", 32);
    
    /* 偏移40-71: 卷标识符 */
    memcpy(&pvd_sector[40], "AETHELISO           ", 32);
    
    /* 偏移80-87: 卷空间大小 (LBA, 小端/大端) */
    uint32_t total_blocks = 262144;  /* 1GB in 4K blocks = 262144 */
    *(uint32_t *)&pvd_sector[80] = total_blocks;
    *(uint32_t *)&pvd_sector[84] = total_blocks;  /* Big endian copy */
    pvd_sector[84] = (total_blocks >> 24) & 0xFF;
    pvd_sector[85] = (total_blocks >> 16) & 0xFF;
    pvd_sector[86] = (total_blocks >> 8) & 0xFF;
    pvd_sector[87] = total_blocks & 0xFF;
    
    /* 偏移120-123: 卷集大小 */
    pvd_sector[120] = 0x01;
    pvd_sector[121] = 0x00;
    pvd_sector[122] = 0x00;
    pvd_sector[123] = 0x01;
    
    /* 偏移124-125: 卷序列号 */
    pvd_sector[124] = 0x01;
    pvd_sector[125] = 0x00;
    
    /* 偏移126-127: 逻辑块大小 (2048字节) */
    pvd_sector[126] = 0x08;
    pvd_sector[127] = 0x00;
    
    /* 偏移156: 根目录记录位置 (让我们放在LBA 19) */
    pvd_sector[156] = 19;  /* 小端 */
    pvd_sector[157] = 0x00;
    pvd_sector[158] = 0x00;
    pvd_sector[159] = 0x00;
    pvd_sector[160] = 0x00;  /* 大端 */
    pvd_sector[161] = 0x00;
    pvd_sector[162] = 0x00;
    pvd_sector[163] = 19;
    
    /* 偏移190: El Torito Catalog位置 (LBA 17, in blocks of 2048) */
    /* 但我们要用扇区位置，所以LBA 17 * 8 = 136 */
    pvd_sector[190] = 136;     /* 小端 */
    pvd_sector[191] = 0x00;
    pvd_sector[192] = 0x00;
    pvd_sector[193] = 0x00;
    pvd_sector[194] = 0x00;    /* 大端 */
    pvd_sector[195] = 0x00;
    pvd_sector[196] = 0x00;
    pvd_sector[197] = 136;
}

/* ============================================================================
 * El Torito启动目录记录 (Boot Catalog) 生成
 * ============================================================================ */

/**
 * 生成El Torito启动目录记录 (Boot Catalog)
 * 这使ISO 9660光盘能够被UEFI固件识别为可启动的
 */
static void create_el_torito_boot_catalog(uint8_t *catalog_sector) {
    if (!catalog_sector) return;
    
    memset(catalog_sector, 0, 2048);
    
    /* ========================================================================
     * 验证条目 (Platform Validation Entry)
     * ======================================================================== */
    uint8_t *entry = &catalog_sector[0];
    
    entry[0] = 0x01;           /* 头指示符: 验证条目 */
    entry[1] = 0x00;           /* 平台ID: 0x00 = x86 */
    entry[2] = 0x00;           /* 保留 */
    entry[3] = 0x00;
    
    /* 检查和 (使得所有4字节之和= 0) */
    uint16_t check = *(uint16_t *)&entry[0];
    *(uint16_t *)&entry[4] = (uint16_t)(-(int16_t)check);
    
    /* ========================================================================
     * UEFI引导条目 (Initial/Default Boot Entry - UEFI Mode)
     * ======================================================================== */
    entry = &catalog_sector[32];
    
    entry[0] = 0xEF;           /* 头指示符: UEFI引导 */
    entry[1] = 0x00;           /* 平台ID: 0xEF */
    entry[2] = 0x01;           /* 引导镜像ID */
    entry[3] = 0x00;
    
    /* 引导镜像位置（指向ESP分区的第一个扇区）
     * ESP在LBA 256 (4K块) = 2048个512字节扇区
     * 但这里填的是镜像号，由Boot Catalog中的镜像定义指出
     */
    entry[4] = 0x88;           /* 引导指示符: 此扇区可引导 */
    entry[5] = 0x00;           /* 负载: 无 (UEFI模式) */
    entry[6] = 0x00;
    entry[7] = 0x00;
    
    /* 引导镜像起始地址 (指向UEFI/EFI固件) */
    /* 我们把EFI固件放在LBA 256 (4K块) = 2048个扇区 */
    *(uint32_t *)&entry[8] = 2048;  /* 小端格式的LBA */
    
    /* ========================================================================
     * 引导镜像定义条目 (Boot Image Definition)
     * ======================================================================== */
    entry = &catalog_sector[64];
    
    entry[0] = 0x90;           /* 头指示符: 引导镜像定义 */
    entry[1] = 0x4D;           /* 平台ID: UEFI */
    *(uint16_t *)&entry[2] = 1;  /* 此镜像的个数 */
    
    /* ID字符串 (可选) */
    memcpy(&entry[4], "AETHELOS UEFI", 13);
}

/**
 * 生成GUID分区表(GPT)头
 */
static void create_gpt_header(uint8_t *gpt_buffer,
                             uint32_t disk_size_sectors,
                             uint32_t partition_entry_lba,
                             uint32_t num_partitions,
                             int is_backup) {
    if (!gpt_buffer) return;
    
    memset(gpt_buffer, 0, 512);
    
    uint32_t *p32 = (uint32_t *)gpt_buffer;
    uint64_t *p64 = (uint64_t *)gpt_buffer;
    
    /* GPT头签名 */
    *(uint32_t *)&gpt_buffer[0] = 0x20494645;  /* "EFI " */
    *(uint32_t *)&gpt_buffer[4] = 0x54524150;  /* "PART" */
    
    /* 版本 1.0 */
    *(uint32_t *)&gpt_buffer[8] = 0x00010000;
    
    /* 头大小 (92 bytes) */
    *(uint32_t *)&gpt_buffer[12] = 92;
    
    /* 头校验和 (设置为0，稍后计算) */
    *(uint32_t *)&gpt_buffer[16] = 0;
    
    /* 保留 */
    *(uint32_t *)&gpt_buffer[20] = 0;
    
    /* 当前LBA */
    if (is_backup) {
        *(uint64_t *)&gpt_buffer[24] = disk_size_sectors - 1;  /* 备份在最后 */
    } else {
        *(uint64_t *)&gpt_buffer[24] = 1;  /* 主GPT在LBA 1 */
    }
    
    /* 备份LBA */
    if (is_backup) {
        *(uint64_t *)&gpt_buffer[32] = 1;
    } else {
        *(uint64_t *)&gpt_buffer[32] = disk_size_sectors - 1;
    }
    
    /* 第一个可用的LBA */
    /* 标准GPT头在LBA 1，分区表在LBA 2-33。所以第一个可用的是LBA 34。*/
    /* 你的ESP在1024，34 < 1024，这样就是合法的。*/
    *(uint64_t *)&gpt_buffer[40] = 34;
    
    /* 最后可用的LBA */
    *(uint64_t *)&gpt_buffer[48] = disk_size_sectors - 2;
    
    /* 磁盘GUID (全0即可) */
    memset(&gpt_buffer[56], 0, 16);
    
    /* 分区条目数组起始LBA */
    *(uint64_t *)&gpt_buffer[72] = partition_entry_lba;
    
    /* 分区条目个数 */
    *(uint32_t *)&gpt_buffer[80] = num_partitions;
    
    /* 分区条目大小 (128 bytes) */
    *(uint32_t *)&gpt_buffer[84] = 128;
    
    /* 分区数组CRC32 (设置为0) */
    *(uint32_t *)&gpt_buffer[88] = 0;
}

/**
 * 生成GPT分区条目
 */
static void create_gpt_partition_entry(uint8_t *entry,
                                      const char *partition_type_guid,  /* "C12A7328-F81F-11D2-BA4B-00A0C93EC93B" for ESP */
                                      uint64_t start_lba,
                                      uint64_t end_lba,
                                      const char *partition_name) {
    if (!entry) return;
    
    memset(entry, 0, 128);
    
    /* 分区类型GUID */
    if (strcmp(partition_type_guid, "ESP") == 0) {
        /* ESP Type: C12A7328-F81F-11D2-BA4B-00A0C93EC93B */
        uint8_t esp_guid[] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
                              0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
        memcpy(entry, esp_guid, 16);
    } else if (strcmp(partition_type_guid, "LINUX") == 0) {
        /* Linux Type: 0FC63DAF-8483-4772-8E79-3D69D8477DE4 */
        uint8_t linux_guid[] = {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
                                0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};
        memcpy(entry, linux_guid, 16);
    }
    
    /* 分区GUID (随机) */
    for (int i = 16; i < 32; i++) {
        entry[i] = rand() & 0xFF;
    }
    
    /* 起始LBA */
    *(uint64_t *)&entry[32] = start_lba;
    
    /* 结束LBA */
    *(uint64_t *)&entry[40] = end_lba;
    
    /* 属性标志 */
    *(uint64_t *)&entry[48] = 0;
    
    /* 分区名称 (UTF-16LE) */
    if (partition_name) {
        for (int i = 0; i < 36 && partition_name[i]; i++) {
            entry[56 + i*2] = partition_name[i];
            entry[56 + i*2 + 1] = 0;
        }
    }
}

/* ============================================================================
 * 辅助函数
 * ============================================================================ */

/**
 * 生成FAT32引导扇区和FSInfo扇区的最小化实现
 * 这允许ESP分区被识别为FAT32格式
 */
static void create_fat32_boot_sector(uint8_t *sector, uint32_t sector_count) {
    if (!sector) return;
    
    memset(sector, 0, 512);
    
    /* FAT32引导扇区参数 */
    sector[0] = 0xEB;       /* JMP short */
    sector[1] = 0x3C;
    sector[2] = 0x90;       /* NOP */
    
    /* OEM标识符 */
    memcpy(&sector[3], "AETHEL  ", 8);
    
    /* 字节每扇区 */
    *(uint16_t *)&sector[11] = 512;
    
    /* 每个分配单元的扇区数 */
    sector[13] = 8;
    
    /* 保留扇区数 */
    *(uint16_t *)&sector[14] = 1;
    
    /* FAT副本数 */
    sector[16] = 2;
    
    /* 根目录条目数 (FAT32中为0) */
    *(uint16_t *)&sector[17] = 0;
    
    /* 扇区总数 (16位, FAT32中为0) */
    *(uint16_t *)&sector[19] = 0;
    
    /* FAT类型 (FAT32为0) */
    sector[21] = 0;
    
    /* 扇区每磁道 */
    *(uint16_t *)&sector[24] = 63;
    
    /* 磁头数 */
    *(uint16_t *)&sector[26] = 255;
    
    /* 隐藏扇区数 */
    *(uint32_t *)&sector[28] = 0;
    
    /* 扇区总数 (32位) */
    *(uint32_t *)&sector[32] = sector_count;
    
    /* FAT32特定字段 */
    /* 每个FAT的扇区数 */
    uint32_t fat_sectors = (sector_count * 4 + 511) / 512;
    *(uint32_t *)&sector[36] = fat_sectors;
    
    /* 扩展标志 */
    *(uint16_t *)&sector[40] = 0;
    
    /* 文件系统版本 */
    *(uint16_t *)&sector[42] = 0;
    
    /* 根目录簇 */
    *(uint32_t *)&sector[44] = 2;
    
    /* FSInfo扇区号 */
    *(uint16_t *)&sector[48] = 1;
    
    /* 备份引导扇区号 */
    *(uint16_t *)&sector[50] = 6;
    
    /* 驱动号 */
    sector[64] = 0x80;
    
    /* 扩展引导签名 */
    sector[66] = 0x29;
    
    /* 卷序列号 */
    *(uint32_t *)&sector[67] = 0x12345678;
    
    /* 卷标签 */
    memcpy(&sector[71], "AETHELOSX  ", 11);
    
    /* 文件系统类型 */
    memcpy(&sector[82], "FAT32   ", 8);
    
    /* 引导签名 */
    *(uint16_t *)&sector[510] = 0xAA55;
}

/**
 * 创建最小化的FAT32文件分配表
 */
static void create_fat32_fat_table(uint8_t *fat_buffer, uint32_t fat_sectors) {
    if (!fat_buffer) return;
    
    memset(fat_buffer, 0, fat_sectors * 512);
    
    /* FAT[0]: 媒介描述符 */
    *(uint32_t *)&fat_buffer[0] = 0x0FFFFFF8;
    
    /* FAT[1]: 备用分配 */
    *(uint32_t *)&fat_buffer[4] = 0x0FFFFFFF;
    
    /* FAT[2]: 根目录簇 */
    *(uint32_t *)&fat_buffer[8] = 0x0FFFFFFF;
}

/**
 * 创建最小化的FAT32根目录
 */
static void create_fat32_root_directory(uint8_t *dir_buffer, uint32_t dir_size) {
    if (!dir_buffer) return;
    
    memset(dir_buffer, 0, dir_size);
    
    uint32_t offset = 0;
    
    /* 第1项: 卷标签 */
    memcpy(&dir_buffer[offset], "AETHELOSX  ", 11);
    dir_buffer[offset + 11] = 0x08;  /* 卷标签属性 */
    offset += 32;
    
    /* 第2项: EFI 目录条目 */
    memcpy(&dir_buffer[offset], "EFI        ", 11);  /* 大写，用空格填充到11字节 */
    dir_buffer[offset + 11] = 0x10;  /* 目录属性 */
    dir_buffer[offset + 20] = 0x00;  /* 簇号(低字) */
    dir_buffer[offset + 21] = 0x00;
    dir_buffer[offset + 26] = 0x04;  /* 簇号(高字) - 簇4 */
    dir_buffer[offset + 27] = 0x00;
    dir_buffer[offset + 28] = 0x00;  /* 文件大小 (0) */
    dir_buffer[offset + 29] = 0x10;
    dir_buffer[offset + 30] = 0x00;
    dir_buffer[offset + 31] = 0x00;
    offset += 32;
}

/* ============================================================================
 * 主ISO生成函数 - 完整的ADL实现
 * ============================================================================ */

/**
 * 生成混合格式的ISO镜像，包含完整的ADL分区表和AEFS文件系统
 * 
 * 参数:
 *   kernel_file: 内核文件路径
 *   efi_boot_file: EFI引导文件路径
 *   output_iso: 输出ISO文件路径
 *   iso_size_mb: ISO总大小(MB)
 *
 * 返回值:
 *   0: 成功
 *   -1: 失败
 */
int aefs_generate_iso_image(const char *kernel_file, const char *efi_boot_file,
                            const char *output_iso, uint64_t iso_size_mb) {
    if (!kernel_file || !efi_boot_file || !output_iso || iso_size_mb < 100) {
        fprintf(stderr, "ERROR: Invalid ISO generation parameters\n");
        fprintf(stderr, "  kernel_file: %s\n", kernel_file ? kernel_file : "NULL");
        fprintf(stderr, "  efi_boot_file: %s\n", efi_boot_file ? efi_boot_file : "NULL");
        fprintf(stderr, "  output_iso: %s\n", output_iso ? output_iso : "NULL");
        fprintf(stderr, "  iso_size_mb: %llu (minimum 100)\n", iso_size_mb);
        return -1;
    }
    
    /* 打开输出ISO文件 */
    FILE *iso_file = fopen(output_iso, "wb");
    if (!iso_file) {
        fprintf(stderr, "ERROR: Cannot open output ISO file for writing: %s\n", output_iso);
        return -1;
    }
    
    printf("\n[AethelOS ISO Generator] Creating Hybrid ADL ISO Image\n");
    printf("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
    printf("Output:    %s\n", output_iso);
    printf("Total Size: %llu MB\n\n", iso_size_mb);
    
    /* 计算布局参数 */
    uint64_t iso_size_bytes = iso_size_mb * 1024ULL * 1024ULL;
    uint64_t iso_size_blocks = iso_size_bytes / AEFS_BLOCK_SIZE;
    
    /* ESP和AEFS分区大小 */
    uint64_t esp_size_mb = 256;  /* EFI系统分区: 256MB */
    uint64_t esp_size_blocks = (esp_size_mb * 1024ULL * 1024ULL) / AEFS_BLOCK_SIZE;
    
    uint64_t aefs_size_mb = iso_size_mb - esp_size_mb - 8;  /* 预留8MB用于元数据 */
    uint64_t aefs_size_blocks = (aefs_size_mb * 1024ULL * 1024ULL) / AEFS_BLOCK_SIZE;
    
    printf("Partition Layout:\n");
    printf("  ADL Header:        LBA 0 (%d bytes)\n", AEFS_BLOCK_SIZE);
    printf("  ADL Partition Tbl: LBA 1-127 (%d blocks)\n", 127);
    printf("  ESP Partition:     LBA 128-%llu (%llu blocks, %llu MB)\n", 
           128 + esp_size_blocks - 1, esp_size_blocks, esp_size_mb);
    printf("  AEFS Partition:    LBA %llu+ (%llu blocks, %llu MB)\n", 
           128 + esp_size_blocks, aefs_size_blocks, aefs_size_mb);
    printf("\n");
    
    /* ========================================================================
     * 第1步: 生成混合分区表 (MBR + ADL + GPT)
     * ======================================================================== */
    
    printf("[Step 1/9] Generating Hybrid Partition Tables (MBR + ADL + GPT)...\n");
    
    /* 计算扇区大小(512字节) */
    uint32_t esp_size_sectors = esp_size_blocks * AEFS_BLOCK_SIZE / 512;
    uint32_t aefs_size_sectors = aefs_size_blocks * AEFS_BLOCK_SIZE / 512;
    uint32_t total_sectors = iso_size_blocks * AEFS_BLOCK_SIZE / 512;
    
    /* 生成MBR分区表并写入物理扇区0(前512字节) */
    uint8_t *mbr_sector = malloc(512);
    create_mbr_partition_table(mbr_sector, 256 * 8, esp_size_sectors, 
                              (256 + esp_size_blocks) * 8, aefs_size_sectors);
    
    fseek(iso_file, 0, SEEK_SET);
    fwrite(mbr_sector, 1, 512, iso_file);
    
    /* 跳到LBA 0的剩余部分，填充零 */
    uint8_t *zero_fill = malloc(AEFS_BLOCK_SIZE - 512);
    memset(zero_fill, 0, AEFS_BLOCK_SIZE - 512);
    fwrite(zero_fill, 1, AEFS_BLOCK_SIZE - 512, iso_file);
    
    printf("  MBR written to sector 0 (LBA 0, 0-511 bytes)\n");
    printf("    Partition 1: ESP at LBA %u, size %u sectors\n", 256 * 8, esp_size_sectors);
    printf("    Partition 2: AEFS at LBA %u, size %u sectors\n", (256 + esp_size_blocks) * 8, aefs_size_sectors);
    
    /* 生成GPT头 (主GPT在LBA 1) */
    uint8_t *gpt_header = malloc(512);
    create_gpt_header(gpt_header, total_sectors, 2, 128, 0);  /* 主GPT */
    
    fseek(iso_file, 1 * AEFS_BLOCK_SIZE, SEEK_SET);
    fwrite(gpt_header, 1, 512, iso_file);
    
    printf("  GPT Header written to LBA 1\n");
    
    /* 生成GPT分区条目 (2个分区) */
    uint8_t *gpt_entries = malloc(128 * 128);  /* 128个条目 */
    memset(gpt_entries, 0, 128 * 128);
    
    create_gpt_partition_entry(&gpt_entries[0], "ESP", 128 * 8, (128 + esp_size_blocks) * 8 - 1, "EFI System");
    create_gpt_partition_entry(&gpt_entries[128], "LINUX", (128 + esp_size_blocks) * 8, 
                              (128 + esp_size_blocks + aefs_size_blocks) * 8 - 1, "AEFS Root");
    
    /* 写入GPT分区表 (从LBA 2开始) */
    fseek(iso_file, 2 * AEFS_BLOCK_SIZE, SEEK_SET);
    fwrite(gpt_entries, 1, 128 * 128, iso_file);
    
    printf("  GPT Partition Entries written to LBA 2+\n");
    
    free(mbr_sector);
    free(gpt_header);
    free(gpt_entries);
    
    /* ========================================================================
     * Step 1.5: ISO 9660 Primary Volume Descriptor + El Torito
     * ======================================================================== */
    
    printf("[Step 1.5/9] Generating ISO 9660 + El Torito Boot Structures...\n");
    
    /* ISO 9660要求: System Area (LBA 0-15) 已占用, PVD从LBA 16开始 */
    uint8_t *iso9660_pvd = malloc(2048);
    create_iso9660_pvd(iso9660_pvd);
    
    fseek(iso_file, 16 * AEFS_BLOCK_SIZE, SEEK_SET);
    fwrite(iso9660_pvd, 1, 2048, iso_file);
    printf("  ISO 9660 Primary Volume Descriptor written to LBA 16\n");
    
    /* El Torito Boot Catalog 位于 LBA 17 */
    uint8_t *el_torito_catalog = malloc(2048);
    create_el_torito_boot_catalog(el_torito_catalog);
    
    fseek(iso_file, 17 * AEFS_BLOCK_SIZE, SEEK_SET);
    fwrite(el_torito_catalog, 1, 2048, iso_file);
    printf("  El Torito Boot Catalog written to LBA 17\n");
    
    free(iso9660_pvd);
    free(el_torito_catalog);
    
    /* ========================================================================
     * 第2步: 生成Aethel Disk Header (ADL主头)
     * ======================================================================== */
    
    printf("[Step 2/9] Generating Aethel Disk Header (ADL)...\n");
    
    uint8_t disk_uuid[16];
    aefs_generate_uuid(disk_uuid);
    
    Aethel_Disk_Header *disk_header = malloc(sizeof(Aethel_Disk_Header));
    aefs_init_disk_header(disk_header, iso_size_blocks, AEFS_MEDIA_SSD_NVME);
    memcpy(disk_header->disk_uuid, disk_uuid, 16);
    disk_header->partition_table_lba = 8;   /* ADL分区表从LBA 8开始 */
    disk_header->partition_table_backup_lba = iso_size_blocks - 128;
    
    /* 写入主ADL头到LBA 0, 块内偏移512字节处(MBR之后) */
    fseek(iso_file, 512, SEEK_SET);
    fwrite(disk_header, 1, sizeof(Aethel_Disk_Header), iso_file);
    
    /* 填充到块大小 */
    uint8_t *padding_buffer = malloc(AEFS_BLOCK_SIZE);
    memset(padding_buffer, 0, AEFS_BLOCK_SIZE);
    uint32_t remaining = AEFS_BLOCK_SIZE - 512 - sizeof(Aethel_Disk_Header);
    fwrite(padding_buffer, 1, remaining, iso_file);
    free(zero_fill);  /* 释放之前的zero_fill */
    
    /* ========================================================================
     * 第3步: 生成主ADL分区表 (LBA 128-255)
     * ======================================================================== */
    
    printf("[Step 3/9] Generating ADL Partition Table (main)...\n");
    
    uint8_t esp_uuid[16], aefs_uuid[16];
    aefs_generate_uuid(esp_uuid);
    aefs_generate_uuid(aefs_uuid);
    
    /* 创建ESP分区条目 (现在在LBA 256) */
    Partition_Entry esp_entry;
    aefs_init_partition_entry(&esp_entry, esp_uuid,
                             256, 256 + esp_size_blocks - 1,
                             AEFS_PTYPE_BOOT, "ESP");
    esp_entry.flags = ADL_FLAG_BOOTABLE;
    
    /* 创建AEFS分区条目 (现在在LBA 256+esp_size_blocks) */
    Partition_Entry aefs_entry;
    aefs_init_partition_entry(&aefs_entry, aefs_uuid,
                             256 + esp_size_blocks, 256 + esp_size_blocks + aefs_size_blocks - 1,
                             AEFS_PTYPE_POOL, "AethelOS Root");
    
    /* 写入分区表 (从LBA 8开始) */
    fseek(iso_file, 8 * AEFS_BLOCK_SIZE, SEEK_SET);
    fwrite(&esp_entry, 1, sizeof(Partition_Entry), iso_file);
    fwrite(&aefs_entry, 1, sizeof(Partition_Entry), iso_file);
    
    /* 填充剩余空间到128个块 */
    memset(padding_buffer, 0, AEFS_BLOCK_SIZE);
    for (int i = 2; i < 128; i++) {
        fwrite(padding_buffer, 1, AEFS_BLOCK_SIZE, iso_file);
    }
    
    /* ========================================================================
     * 第4步: 生成ESP分区 (FAT32格式)
     * ======================================================================== */
    
    printf("[Step 4/9] Generating EFI System Partition (FAT32)...\n");
    printf("  Location: LBA 256, Size: %llu blocks\n", esp_size_blocks);
    
    /* 创建引导扇区 */
    uint8_t *boot_sector = malloc(512);
    create_fat32_boot_sector(boot_sector, (uint32_t)(esp_size_blocks * AEFS_BLOCK_SIZE / 512));
    
    fseek(iso_file, 256 * AEFS_BLOCK_SIZE, SEEK_SET);
    fwrite(boot_sector, 1, 512, iso_file);
    
    /* 创建FSInfo扇区 */
    uint8_t *fsinfo_sector = malloc(512);
    memset(fsinfo_sector, 0, 512);
    *(uint32_t *)&fsinfo_sector[0] = 0x41615252;    /* 签名1 */
    *(uint32_t *)&fsinfo_sector[484] = 0x61417272;  /* 签名2 */
    *(uint32_t *)&fsinfo_sector[488] = 0x0FFFFFFF;  /* 空闲簇数 */
    *(uint32_t *)&fsinfo_sector[492] = 0xFFFFFFFF;  /* 最后分配簇 */
    *(uint16_t *)&fsinfo_sector[510] = 0xAA55;
    
    fseek(iso_file, 256 * AEFS_BLOCK_SIZE + 512, SEEK_SET);
    fwrite(fsinfo_sector, 1, 512, iso_file);
    
    /* 创建FAT表 */
    uint32_t fat_sectors = ((esp_size_blocks * AEFS_BLOCK_SIZE / 512) * 4 + 511) / 512;
    uint8_t *fat_table = malloc(fat_sectors * 512);
    create_fat32_fat_table(fat_table, fat_sectors);
    
    fseek(iso_file, 256 * AEFS_BLOCK_SIZE + 1024, SEEK_SET);
    fwrite(fat_table, 1, fat_sectors * 512, iso_file);
    
    /* 创建备用FAT表 */
    fseek(iso_file, 256 * AEFS_BLOCK_SIZE + 1024 + fat_sectors * 512, SEEK_SET);
    fwrite(fat_table, 1, fat_sectors * 512, iso_file);
    
    /* 创建根目录 */
    uint32_t root_dir_sectors = 1;
    uint8_t *root_dir = malloc(root_dir_sectors * 512);
    create_fat32_root_directory(root_dir, root_dir_sectors * 512);
    
    fseek(iso_file, 256 * AEFS_BLOCK_SIZE + 1024 + 2 * fat_sectors * 512, SEEK_SET);
    fwrite(root_dir, 1, root_dir_sectors * 512, iso_file);
    
    /* 读取并写入EFI引导文件到ESP */
    FILE *efi_file = fopen(efi_boot_file, "rb");
    if (efi_file) {
        fseek(efi_file, 0, SEEK_END);
        long efi_size = ftell(efi_file);
        fseek(efi_file, 0, SEEK_SET);
        
        if (efi_size > 0 && efi_size < (long)(esp_size_blocks * AEFS_BLOCK_SIZE)) {
            uint8_t *efi_buffer = malloc(efi_size);
            if (efi_buffer && fread(efi_buffer, 1, efi_size, efi_file) == efi_size) {
                fseek(iso_file, 256 * AEFS_BLOCK_SIZE + 1024 + 2 * fat_sectors * 512 + 
                               root_dir_sectors * 512, SEEK_SET);
                fwrite(efi_buffer, 1, efi_size, iso_file);
                printf("  Embedded EFI firmware: %ld bytes\n", efi_size);
            }
            free(efi_buffer);
        }
        fclose(efi_file);
    } else {
        printf("  WARNING: EFI boot file not found, ESP will be empty\n");
    }
    
    free(boot_sector);
    free(fsinfo_sector);
    free(fat_table);
    free(root_dir);
    
    /* ========================================================================
     * 第5步: 生成AEFS分区头信息
     * ======================================================================== */
    
    printf("[Step 5/9] Generating AEFS Partition Metadata...\n");
    printf("  Location: LBA %llu\n", 256 + esp_size_blocks);
    
    uint64_t aefs_base_lba = 256 + esp_size_blocks;
    
    /* 生成Anchor Block */
    AEFS_Anchor_Block *anchor = malloc(sizeof(AEFS_Anchor_Block));
    aefs_init_anchor_block(anchor, aefs_uuid, aefs_base_lba + 1, aefs_base_lba + 10);
    
    fseek(iso_file, aefs_base_lba * AEFS_BLOCK_SIZE, SEEK_SET);
    fwrite(anchor, 1, sizeof(AEFS_Anchor_Block), iso_file);
    memset(padding_buffer, 0, AEFS_BLOCK_SIZE);
    fwrite(padding_buffer, 1, AEFS_BLOCK_SIZE - sizeof(AEFS_Anchor_Block), iso_file);
    
    /* 生成Checkpoint */
    AEFS_Checkpoint *checkpoint = malloc(sizeof(AEFS_Checkpoint));
    aefs_init_checkpoint(checkpoint, aefs_base_lba + 10, aefs_base_lba + 20,
                        aefs_size_blocks / 8, aefs_size_blocks);
    
    fwrite(checkpoint, 1, sizeof(AEFS_Checkpoint), iso_file);
    fwrite(padding_buffer, 1, AEFS_BLOCK_SIZE - sizeof(AEFS_Checkpoint), iso_file);
    
    /* 生成Segment Summary */
    AEFS_Segment_Summary *ssa = malloc(sizeof(AEFS_Segment_Summary));
    aefs_init_segment_summary(ssa, 0, aefs_base_lba + 10);
    
    fwrite(ssa, 1, sizeof(AEFS_Segment_Summary), iso_file);
    fwrite(padding_buffer, 1, AEFS_BLOCK_SIZE - sizeof(AEFS_Segment_Summary), iso_file);
    
    /* 生成Volume Descriptor */
    AEFS_Volume_Descriptor *vol_desc = malloc(sizeof(AEFS_Volume_Descriptor));
    aefs_init_volume_descriptor(vol_desc, aefs_uuid, "root", aefs_base_lba + 10);
    
    fwrite(vol_desc, 1, sizeof(AEFS_Volume_Descriptor), iso_file);
    fwrite(padding_buffer, 1, AEFS_BLOCK_SIZE - sizeof(AEFS_Volume_Descriptor), iso_file);
    
    /* 生成根Inode */
    AEFS_Node *root_inode = malloc(sizeof(AEFS_Node));
    const uint8_t system_app_id[] = "sys.kernel.root";
    aefs_init_inode(root_inode, aefs_uuid, AEFS_INODE_TYPE_DIRECTORY,
                   system_app_id, sizeof(system_app_id), 0x01);  /* 0x01 = AEFS_FLAG_SYSTEM_IMMUTABLE */
    
    fwrite(root_inode, 1, sizeof(AEFS_Node), iso_file);
    fwrite(padding_buffer, 1, AEFS_BLOCK_SIZE - sizeof(AEFS_Node), iso_file);
    
    /* ========================================================================
     * 第6步: 嵌入内核到AEFS分区
     * ======================================================================== */
    
    printf("[Step 6/9] Embedding Kernel into AEFS...\n");
    
    FILE *kernel_file_handle = fopen(kernel_file, "rb");
    if (kernel_file_handle) {
        fseek(kernel_file_handle, 0, SEEK_END);
        long kernel_size = ftell(kernel_file_handle);
        fseek(kernel_file_handle, 0, SEEK_SET);
        
        if (kernel_size > 0 && kernel_size < (long)(aefs_size_blocks * AEFS_BLOCK_SIZE)) {
            uint8_t *kernel_buffer = malloc(kernel_size);
            if (kernel_buffer && fread(kernel_buffer, 1, kernel_size, kernel_file_handle) == kernel_size) {
                /* 写入内核到AEFS日志区域 (LBA aefs_base_lba + 20) */
                fseek(iso_file, (aefs_base_lba + 20) * AEFS_BLOCK_SIZE, SEEK_SET);
                fwrite(kernel_buffer, 1, kernel_size, iso_file);
                printf("  Embedded kernel: %ld bytes\n", kernel_size);
            }
            free(kernel_buffer);
        }
        fclose(kernel_file_handle);
    } else {
        printf("  WARNING: Kernel file not found\n");
    }
    
    /* ========================================================================
     * 第7步: 生成备份分区表 (LBA iso_size_blocks - 128)
     * ======================================================================== */
    
    printf("[Step 7/9] Generating ADL Partition Table (backup)...\n");
    
    fseek(iso_file, (iso_size_blocks - 128) * AEFS_BLOCK_SIZE, SEEK_SET);
    fwrite(&esp_entry, 1, sizeof(Partition_Entry), iso_file);
    fwrite(&aefs_entry, 1, sizeof(Partition_Entry), iso_file);
    
    memset(padding_buffer, 0, AEFS_BLOCK_SIZE);
    for (int i = 2; i < 128; i++) {
        fwrite(padding_buffer, 1, AEFS_BLOCK_SIZE, iso_file);
    }
    
    /* ========================================================================
     * 第8步: 生成备份ADL头 (LBA iso_size_blocks - 1)
     * ======================================================================== */
    
    printf("[Step 8/9] Generating Aethel Disk Header (backup)...\n");
    
    Aethel_Disk_Header *backup_header = malloc(sizeof(Aethel_Disk_Header));
    memcpy(backup_header, disk_header, sizeof(Aethel_Disk_Header));
    backup_header->partition_table_lba = iso_size_blocks - 128;
    backup_header->partition_table_backup_lba = 1;
    
    /* 重新计算备份头的校验和 */
    backup_header->header_checksum = 0;
    backup_header->header_checksum = aefs_checksum_xxh3((uint8_t *)backup_header,
                                                        sizeof(Aethel_Disk_Header));
    
    fseek(iso_file, (iso_size_blocks - 1) * AEFS_BLOCK_SIZE, SEEK_SET);
    fwrite(backup_header, 1, sizeof(Aethel_Disk_Header), iso_file);
    
    /* ========================================================================
     * 第9步: 填充ISO到最终大小
     * ======================================================================== */
    
    printf("[Step 9/9] Padding ISO to final size...\n");
    
    long current_pos = ftell(iso_file);
    if (current_pos < (long)iso_size_bytes) {
        memset(padding_buffer, 0, AEFS_BLOCK_SIZE);
        while (current_pos < (long)iso_size_bytes) {
            size_t write_size = AEFS_BLOCK_SIZE;
            if (current_pos + (long)write_size > (long)iso_size_bytes) {
                write_size = iso_size_bytes - current_pos;
            }
            fwrite(padding_buffer, 1, write_size, iso_file);
            current_pos += write_size;
        }
    }
    
    /* 清理资源 */
    fclose(iso_file);
    free(disk_header);
    free(backup_header);
    free(anchor);
    free(checkpoint);
    free(ssa);
    free(vol_desc);
    free(root_inode);
    free(padding_buffer);
    
    printf("\n" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "\n");
    printf("[SUCCESS] Hybrid ADL ISO Image Created\n");
    printf("Output:     %s\n", output_iso);
    printf("Total Size: %llu bytes (%llu MB)\n", iso_size_bytes, iso_size_mb);
    printf("Format:     ADL (Aethel Disk Layout) with FAT32 ESP + AEFS Root\n");
    printf("\n");
    
    return 0;
}
