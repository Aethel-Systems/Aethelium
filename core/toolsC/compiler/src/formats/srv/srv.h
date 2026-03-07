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
 * SRV (System Service Archive) Format Header
 * 
 * 系统服务二进制格式定义
 * SRV是提供功能接口的逻辑实体，无用户概念，仅有调用权限
 * 
 * 包含：
 * - Header (256字节)
 * - [Full Structure]:
 *   1. ActFlow Zone: 服务的内部业务逻辑
 *   2. MirrorState Zone: 服务维护的内存对象、运行时状态
 *   3. ConstantTruth Zone: Aethelium强类型定义、接口Schema
 */

#ifndef AETHELC_COMPILER_FORMATS_SRV_H
#define AETHELC_COMPILER_FORMATS_SRV_H

#include "../common/format_common.h"
#include <stdint.h>
#include <stdio.h>

/* SRV 魔数定义 */
#define MAGIC_SRV           0x21565253  /* "SRV!" */

/* ============================================================================
 * SRV头部格式定义 (基于AethelBinaryHeader的扩展)
 * ============================================================================ */

/**
 * SRV特定的头部扩展字段
 * 
 * 在AethelBinaryHeader的format_specific区域（0x70-0x7F）和
 * extended_metadata区域（0x80-0xD7）中的布局：
 */
typedef struct {
    /* 0x70-0x73: 服务ID (内部分配的实体ID) */
    uint32_t service_id;
    
    /* 0x74-0x77: 服务类型 (用于分类和发现) */
    uint32_t service_type;
    
    /* 0x78-0x7B: SIP策略位图 (决定哪些AethelID可以调用) */
    uint32_t sip_policy;
    
    /* 0x7C-0x7D: 服务的权限等级 */
    uint16_t privilege_level;
    
    /* 0x7E-0x7F: 模式行为 (0: 仅沙盒, 1: 仅架构师, 2: 共有) */
    uint8_t mode_behavior;
    uint8_t reserved_0;
    
    /* 0x80-0x87: 服务生命点 */
    uint64_t srv_genesis;
    
    /* 0x88-0x8F: 服务入口点 */
    uint64_t service_entry;
} SRV_SpecificHeader;

/* ============================================================================
 * SRV 生成接口 - 工业级完整实现
 * ============================================================================ */

/**
 * 初始化SRV头部
 */
void srv_header_init(AethelBinaryHeader *hdr);

/**
 * 生成系统服务的AethelID
 * 类型标记格式：Aethel/Srv/Domain/Function
 */
AethelID srv_generate_aethel_id(const char *domain, const char *function,
                               const uint8_t *payload);

/**
 * 设置SRV服务基本信息
 */
void srv_header_set_service_info(AethelBinaryHeader *hdr,
                                  uint32_t service_id,
                                  uint32_t service_type,
                                  uint16_t privilege_level);

/**
 * 设置SRV的SIP策略
 */
void srv_header_set_sip_policy(AethelBinaryHeader *hdr, uint32_t policy);

/**
 * 设置SRV的模式行为
 */
void srv_header_set_mode_behavior(AethelBinaryHeader *hdr, uint8_t behavior);

/**
 * 设置SRV的服务生命点和入口
 */
void srv_header_set_genesis(AethelBinaryHeader *hdr, uint64_t genesis);
void srv_header_set_entry(AethelBinaryHeader *hdr, uint64_t entry);

/**
 * 从编译产生的二进制生成SRV镜像
 * 
 * 完整的[Full Structure]实现：
 * - ActFlow Zone: 服务的内部业务逻辑
 * - MirrorState Zone: 服务维护的内存对象（可选）
 * - ConstantTruth Zone: 接口Schema、反射信息（可选）
 * 
 * @param output_file     输出SRV文件路径
 * @param code            编译产生的ActFlow业务代码
 * @param code_size       ActFlow大小
 * @param mirror_data     MirrorState数据 (NULL表示无)
 * @param mirror_size     MirrorState大小
 * @param constant_data   ConstantTruth数据 (NULL表示无)
 * @param constant_size   ConstantTruth大小
 * @return                成功返回0，失败返回非0
 */
int srv_generate_image(const char *output_file, 
                      const uint8_t *code, size_t code_size,
                      const uint8_t *mirror_data, size_t mirror_size,
                      const uint8_t *constant_data, size_t constant_size);

#endif
