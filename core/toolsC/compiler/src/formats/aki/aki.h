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
 * AKI (Aethel Kernel Image) Format Header
 * 
 * AKI 包含：
 * - Header (256字节) - 标准AethelBinaryHeader + AKI特定扩展
 * - [Full Structure]:
 *   1. ActFlow Zone: x86-64机器码 + vCore调度器、SIP实时拦截逻辑
 *   2. MirrorState Zone: 系统运行时状态、内存对象
 *   3. ConstantTruth Zone: 系统预设的AethelID字典、配置表
 *   4. AethelA Compiler Engine: SourceInterpretor、SipWeaver、TargetMorpher等
 */

#ifndef AETHEL_FORMAT_AKI_H
#define AETHEL_FORMAT_AKI_H

#include "../common/format_common.h"
#include <stdint.h>
#include <stdio.h>

/* AKI 魔数定义 */
#define MAGIC_AKI           0x21494B41  /* "AKI!" */

/* ============================================================================
 * AKI头部格式定义 (基于AethelBinaryHeader的扩展)
 * ============================================================================ */

/**
 * AKI特定的头部扩展字段
 * 
 * 在AethelBinaryHeader的format_specific区域（0x70-0x7F）和
 * extended_metadata区域（0x80-0xD7）中的布局：
 */
typedef struct {
    /* 0x70-0x77: SIP核心权限向量 */
    uint64_t sip_vector;
    
    /* 0x78-0x7F: 物理加载原点 (通常0x100000) */
    uint64_t genesis_point;
    
    /* 0x80-0x87: 模式亲和度 (0=强制沙盒, 1=架构师优先) */
    uint8_t mode_affinity;
    
    /* 0x88-0x8F: AethelA编译器标记 */
    uint8_t aethela_flags;
    
    /* 0x90-0x9F: 预留 */
    uint8_t reserved_0[16];
    
    /* 0xA0-0xA7: vCore调度器偏移 */
    uint64_t vcore_scheduler_offset;
    
    /* 0xA8-0xAF: AethelA引擎入口点 */
    uint64_t aethela_engine_entry;
} AKI_SpecificHeader;

/* ============================================================================
 * AKI 生成接口 - 工业级完整实现
 * ============================================================================ */

/**
 * 初始化AKI头部
 */
void aki_header_init(AethelBinaryHeader *hdr);

/**
 * 生成内核的AethelID
 * 类型标记：Aethel/Kernel/Origin
 */
AethelID aki_generate_aethel_id(const uint8_t *payload);

/**
 * 设置AKI头部的SIP向量
 * SIP向量决定全局权限基准
 */
void aki_header_set_sip_vector(AethelBinaryHeader *hdr, uint64_t sip_vector);

/**
 * 设置AKI头部的Genesis Point
 * 物理加载原点，通常为0x100000
 */
void aki_header_set_genesis_point(AethelBinaryHeader *hdr, uint64_t genesis_point);

/**
 * 设置AKI的模式亲和度
 * 0: 强制沙盒  1: 架构师优先
 */
void aki_header_set_mode_affinity(AethelBinaryHeader *hdr, uint8_t affinity);

/**
 * 设置vCore调度器偏移
 */
void aki_header_set_vcore_scheduler(AethelBinaryHeader *hdr, uint64_t offset);

/**
 * 设置AethelA编译引擎入口点
 */
void aki_header_set_aethela_engine(AethelBinaryHeader *hdr, uint64_t entry);

/**
 * 从编译产生的二进制生成AKI镜像
 * 
 * 完整的[Full Structure]实现：
 * - ActFlow Zone: x86-64机器码 + vCore调度器 + SIP拦截逻辑
 * - MirrorState Zone: 系统运行时状态（可选）
 * - ConstantTruth Zone: AethelID字典、配置（可选）
 * 
 * @param output_file     输出AKI文件路径
 * @param code            编译产生的ActFlow机器码
 * @param code_size       ActFlow大小
 * @param mirror_data     MirrorState数据 (NULL表示无)
 * @param mirror_size     MirrorState大小
 * @param constant_data   ConstantTruth数据 (NULL表示无)
 * @param constant_size   ConstantTruth大小
 * @return                成功返回0，失败返回非0
 */
int aki_generate_image(const char *output_file, 
                       const uint8_t *code, size_t code_size,
                       const uint8_t *mirror_data, size_t mirror_size,
                       const uint8_t *constant_data, size_t constant_size,
                       uint64_t genesis_point);

/**
 * 从AETB中提取纯机器码（如果输入是AETB格式）
 */
int aki_extract_code_from_aetb(const uint8_t *input, size_t input_size,
                                const uint8_t **output, size_t *output_size);

#endif /* AETHEL_FORMAT_AKI_H */
