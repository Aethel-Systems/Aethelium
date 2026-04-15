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
 * ASN.1 DER (Distinguished Encoding Rules) Encoder
 * 用于构造 Image4 Payload (IM4P) 容器的 ASN.1 树
 * 
 * DER 编码标准：
 * - TLV 三元组：标签（1-4字节）+ 长度（1-9字节）+ 值（可变）
 * - 长形式编码：本字节最高位为1，低7位表示后续字节数
 * - 自底向上构造或两次遍历（计算大小 -> 回写长度）
 */

#ifndef AETHEL_DER_ENCODER_H
#define AETHEL_DER_ENCODER_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   ASN.1 标签定义
   ============================================================================ */

#define DER_TAG_CLASS_UNIVERSAL    0x00    /* 通用标签 */
#define DER_TAG_CLASS_APPLICATION  0x40    /* 应用标签 */
#define DER_TAG_CLASS_CONTEXT      0x80    /* 上下文标签 */
#define DER_TAG_CLASS_PRIVATE      0xC0    /* 私有标签 */

#define DER_TAG_CONSTRUCTED        0x20    /* 构造标签 (SET, SEQUENCE) */
#define DER_TAG_PRIMITIVE          0x00    /* 原始标签（叶子节点） */

/* ASN.1 基本类型标签 */
#define DER_TAG_SEQUENCE           0x30    /* SEQUENCE (构造) */
#define DER_TAG_SET                0x31    /* SET (构造) */
#define DER_TAG_INTEGER            0x02    /* INTEGER (原始) */
#define DER_TAG_OCTET_STRING       0x04    /* OCTET STRING (原始或构造) */
#define DER_TAG_IA5_STRING         0x16    /* IA5String (7-bit ASCII) */
#define DER_TAG_PRINTABLE_STRING   0x13    /* PrintableString */

/* 长度编码常数 */
#define DER_LENGTH_LONG_FORM_FLAG  0x80    /* 长形式标记 */
#define DER_LENGTH_MAX_SHORT       127     /* 短形式最大值 */

/* ============================================================================
   DER 编码器上下文
   ============================================================================ */

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t offset;
    int verbose;
    char error_message[512];
} der_encoder_context_t;

/* ============================================================================
   DER 节点类型定义（用于树构造）
   ============================================================================ */

typedef enum {
    DER_NODE_SEQUENCE,      /* 序列 (SEQUENCE) */
    DER_NODE_INTEGER,       /* 整数 */
    DER_NODE_OCTET_STRING,  /* 八位字节串 */
    DER_NODE_IA5_STRING,    /* IA5 字符串 */
} der_node_type_t;

typedef struct der_node {
    der_node_type_t type;
    uint8_t *data;
    size_t data_length;
    
    /* 对于序列节点 */
    struct der_node **children;
    size_t child_count;
    size_t child_capacity;
    
    /* 编码后的大小（两次遍历后填充） */
    size_t encoded_size;
} der_node_t;

/* ============================================================================
   公共接口函数
   ============================================================================ */

/**
 * der_encoder_create - 创建 DER 编码器上下文
 * 
 * 参数：
 *   capacity - 输出缓冲区容量
 *   verbose  - 是否输出详细日志
 * 
 * 返回：编码器上下文指针，失败返回 NULL
 */
der_encoder_context_t* der_encoder_create(size_t capacity, int verbose);

/**
 * der_encoder_destroy - 销毁 DER 编码器上下文
 */
void der_encoder_destroy(der_encoder_context_t *ctx);

/**
 * der_encode_length - 将长度值按 DER 规则编码到缓冲区
 * 
 * 返回下一个写入位置，失败返回 (size_t)-1
 */
size_t der_encode_length(uint8_t *buffer, size_t offset, size_t capacity,
                        size_t length);

/**
 * der_encode_tlv - 写入完整的 TLV 三元组
 * 
 * 返回下一个写入位置，失败返回 (size_t)-1
 */
size_t der_encode_tlv(uint8_t *buffer, size_t offset, size_t capacity,
                     uint8_t tag, const uint8_t *value, size_t value_length);

/**
 * der_encode_ia5_string - 编码 IA5String (7位 ASCII)
 * 
 * 返回下一个写入位置，失败返回 (size_t)-1
 */
size_t der_encode_ia5_string(uint8_t *buffer, size_t offset, size_t capacity,
                            const char *string);

/**
 * der_encode_octet_string - 编码 OCTET STRING
 * 
 * 返回下一个写入位置，失败返回 (size_t)-1
 */
size_t der_encode_octet_string(uint8_t *buffer, size_t offset, size_t capacity,
                              const uint8_t *data, size_t data_length);

/**
 * der_encode_sequence_header - 编码序列头（用于两次遍历算法）
 * 
 * 此函数写入标签和长度占位符，返回长度字段的起始偏移，
 * 以便后续可以回填正确的长度值。
 * 
 * 返回：(长度字段起始偏移 << 32) | 内容起始偏移
 * 失败返回 (uint64_t)-1
 */
uint64_t der_encode_sequence_header(uint8_t *buffer, size_t offset,
                                   size_t capacity);

/**
 * der_backfill_length - 回填序列长度（两次遍历）
 * 
 * 知道了序列内容的实际大小后，回填长度字段。
 * 
 * 返回值：
 *   0   - 成功
 *   -1  - 长度编码失败
 */
int der_backfill_length(uint8_t *buffer, size_t length_offset,
                       size_t content_length);

/**
 * der_calculate_tlv_size - 计算编码后的 TLV 大小
 * 
 * 用于两次遍历算法中的第一次遍历。
 */
size_t der_calculate_tlv_size(uint8_t tag, size_t value_length);

/**
 * der_node_create_sequence - 创建序列节点
 */
der_node_t* der_node_create_sequence(void);

/**
 * der_node_create_octet_string - 创建八位字节串节点
 */
der_node_t* der_node_create_octet_string(const uint8_t *data, size_t length);

/**
 * der_node_create_ia5_string - 创建 IA5String 节点
 */
der_node_t* der_node_create_ia5_string(const char *string);

/**
 * der_node_add_child - 向序列节点添加子节点
 * 
 * 返回值：
 *   0   - 成功
 *   -1  - 内存分配失败
 */
int der_node_add_child(der_node_t *parent, der_node_t *child);

/**
 * der_node_encode - 将树形节点编码为 DER 二进制
 * 
 * 返回下一个写入位置，失败返回 (size_t)-1
 */
size_t der_node_encode(der_node_t *node, uint8_t *buffer, size_t offset,
                      size_t capacity);

/**
 * der_node_destroy - 递归销毁 DER 节点树
 */
void der_node_destroy(der_node_t *node);

#ifdef __cplusplus
}
#endif

#endif /* AETHEL_DER_ENCODER_H */
