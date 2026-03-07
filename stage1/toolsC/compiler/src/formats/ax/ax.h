/*
 * AX (Aethel Index) Format Header
 * 
 * B+树索引格式定义
 * 用于替代所有数据库，支持高效的范围查询和点查询
 * 
 * 包含：
 * - Header (256字节)
 * - [Full Structure]:
 *   1. ActFlow Zone: B+树节点数据
 *   2. MirrorState Zone: 索引运行时状态
 *   3. ConstantTruth Zone: 树元数据、配置
 */

#ifndef AETHELC_COMPILER_FORMATS_AX_H
#define AETHELC_COMPILER_FORMATS_AX_H

#include "../common/format_common.h"
#include <stdint.h>
#include <stdio.h>

/* AX 魔数定义 */
#define MAGIC_AX            0x21584541  /* "AEX!" - 正确拼写: "AX!" 但为保持4字节，使用"AEX!" */

/* ============================================================================
 * AX头部格式定义 (基于AethelBinaryHeader的扩展)
 * ============================================================================ */

/**
 * AX特定的头部扩展字段
 * 
 * 在AethelBinaryHeader的format_specific区域（0x70-0x7F）和
 * extended_metadata区域（0x80-0xD7）中的布局：
 */
typedef struct {
    /* 0x70-0x73: B+树的阶数 */
    uint32_t tree_order;
    
    /* 0x74-0x77: 键大小 (字节) */
    uint32_t key_size;
    
    /* 0x78-0x7B: 值大小 (字节) */
    uint32_t value_size;
    
    /* 0x7C-0x7F: 总条目数 */
    uint32_t total_entries;
    
    /* 0x80-0x83: 树高度 */
    uint32_t tree_height;
    
    /* 0x84-0x87: 根节点偏移 */
    uint32_t root_node_offset;
} AX_SpecificHeader;

/* ============================================================================
 * AX 生成接口 - 工业级完整实现
 * ============================================================================ */

/**
 * 初始化AX头部
 */
void ax_header_init(AethelBinaryHeader *hdr);

/**
 * 生成索引文件的AethelID
 * 类型标记格式：Aethel/Index/Type
 */
AethelID ax_generate_aethel_id(const char *index_type, const uint8_t *payload);

/**
 * 设置AX B+树配置
 */
void ax_header_set_tree_config(AethelBinaryHeader *hdr,
                                uint32_t tree_order,
                                uint32_t key_size,
                                uint32_t value_size,
                                uint32_t total_entries);

/**
 * 设置AX树元数据
 */
void ax_header_set_tree_metadata(AethelBinaryHeader *hdr,
                                  uint32_t tree_height,
                                  uint32_t root_offset);

/**
 * 从编译产生的索引数据生成AX镜像
 * 
 * 完整的[Full Structure]实现：
 * - ActFlow Zone: B+树节点（内部节点 + 叶子节点）
 * - MirrorState Zone: 索引运行时状态（可选）
 * - ConstantTruth Zone: 树元数据、配置（可选）
 * 
 * @param output_file     输出AX文件路径
 * @param index_data      编译产生的B+树数据（ActFlow）
 * @param index_size      数据大小
 * @param state_data      运行时状态数据 (NULL表示无)
 * @param state_size      状态大小
 * @param metadata_data   元数据 (NULL表示无)
 * @param metadata_size   元数据大小
 * @return                成功返回0，失败返回非0
 */
int ax_generate_image(const char *output_file, 
                     const uint8_t *index_data, size_t index_size,
                     const uint8_t *state_data, size_t state_size,
                     const uint8_t *metadata_data, size_t metadata_size);

#endif
