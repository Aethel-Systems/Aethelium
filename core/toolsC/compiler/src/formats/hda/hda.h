/*
 * HDA (Hardware Driver Archive) Format Header
 * 
 * 硬件驱动二进制格式定义
 * HDA是对硬件行为的"描述文件"，不是传统驱动程序
 * 
 * 包含：
 * - Header (256字节)
 * - [Full Structure]:
 *   1. ActFlow Zone: 寄存器级操作序列、硬件控制行为
 *   2. MirrorState Zone: 硬件状态镜像
 *   3. ConstantTruth Zone: 硬件特定配置表、契约元数据
 */

#ifndef AETHELC_COMPILER_FORMATS_HDA_H
#define AETHELC_COMPILER_FORMATS_HDA_H

#include "../common/format_common.h"
#include <stdint.h>
#include <stdio.h>

/* HDA 魔数定义 */
#define MAGIC_HDA           0x21414448  /* "HDA!" */

/* ============================================================================
 * HDA头部格式定义 (基于AethelBinaryHeader的扩展)
 * ============================================================================ */

/**
 * HDA特定的头部扩展字段
 * 
 * 在AethelBinaryHeader的format_specific区域（0x70-0x7F）和
 * extended_metadata区域（0x80-0xD7）中的布局：
 */
typedef struct {
    /* 0x70-0x73: 硬件设备类型ID */
    uint32_t device_type_id;
    
    /* 0x74-0x77: 硬件特征指纹（用于验证硬件） */
    uint32_t hw_fingerprint;
    
    /* 0x78-0x7B: 最低SIP保护等级 */
    uint32_t sip_requirement;
    
    /* 0x7C-0x7D: 模式支持位图 (Bit0: 沙盒, Bit1: 架构师) */
    uint16_t mode_support;
    
    /* 0x7E-0x7F: 保留 */
    uint16_t reserved_0;
    
    /* 0x80-0x87: 硬件契约协商入口点 */
    uint64_t contract_entry;
    
    /* 0x88-0x8F: 硬件初始化入口 */
    uint64_t hw_init_entry;
} HDA_SpecificHeader;

/* ============================================================================
 * HDA 生成接口 - 工业级完整实现
 * ============================================================================ */

/**
 * 初始化HDA头部
 */
void hda_header_init(AethelBinaryHeader *hdr);

/**
 * 生成硬件驱动的AethelID
 * 类型标记格式：Aethel/Driver/Category/Type
 */
AethelID hda_generate_aethel_id(const char *category, const char *type,
                               const uint8_t *payload);

/**
 * 设置HDA的硬件类型和指纹
 */
void hda_header_set_device_info(AethelBinaryHeader *hdr,
                                uint32_t device_type_id,
                                uint32_t hw_fingerprint);

/**
 * 设置HDA的SIP要求
 */
void hda_header_set_sip_requirement(AethelBinaryHeader *hdr, uint32_t sip_level);

/**
 * 设置硬件驱动支持的模式
 */
void hda_header_set_mode_support(AethelBinaryHeader *hdr, uint16_t modes);

/**
 * 设置硬件契约入口和初始化入口
 */
void hda_header_set_contract_entry(AethelBinaryHeader *hdr, uint64_t entry);
void hda_header_set_init_entry(AethelBinaryHeader *hdr, uint64_t entry);

/**
 * 从编译产生的二进制生成HDA镜像
 * 
 * 完整的[Full Structure]实现：
 * - ActFlow Zone: 寄存器级操作 + 硬件控制行为
 * - MirrorState Zone: 硬件运行时状态（可选）
 * - ConstantTruth Zone: 硬件配置表、契约元数据（可选）
 * 
 * @param output_file     输出HDA文件路径
 * @param code            编译产生的ActFlow硬件操作码
 * @param code_size       ActFlow大小
 * @param mirror_data     MirrorState数据 (NULL表示无)
 * @param mirror_size     MirrorState大小
 * @param constant_data   ConstantTruth数据 (NULL表示无)
 * @param constant_size   ConstantTruth大小
 * @return                成功返回0，失败返回非0
 */
int hda_generate_image(const char *output_file, 
                      const uint8_t *code, size_t code_size,
                      const uint8_t *mirror_data, size_t mirror_size,
                      const uint8_t *constant_data, size_t constant_size);

#endif
