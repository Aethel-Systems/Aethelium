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
 * ASN.1 DER Encoder Implementation
 * 完整的工业级实现，包括长度编码、TLV 三元组、树构造等
 */

#include "der_encoder.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ============================================================================
   内部辅助函数
   ============================================================================ */

/**
 * 检查是否过载
 */
static int check_offset_overflow(size_t offset, size_t add, size_t capacity) {
    return (offset > capacity || add > capacity - offset);
}

/**
 * 计算编码长度值所需的字节数
 */
static size_t der_length_encoded_size(size_t length) {
    if (length <= 127) {
        return 1;  /* 短形式 */
    }
    
    /* 长形式：1 字节标志 + N 字节长度 */
    if (length <= 0xFF) return 2;
    if (length <= 0xFFFF) return 3;
    if (length <= 0xFFFFFF) return 4;
    if (length <= 0xFFFFFFFF) return 5;
    if (length <= 0xFFFFFFFFFF) return 6;
    if (length <= 0xFFFFFFFFFFFF) return 7;
    if (length <= 0xFFFFFFFFFFFFFF) return 8;
    return 9;  /* 最多 9 字节（1 字节标志 + 8 字节长度） */
}

/* ============================================================================
   编码器上下文管理
   ============================================================================ */

der_encoder_context_t* der_encoder_create(size_t capacity, int verbose) {
    der_encoder_context_t *ctx;
    
    if (capacity == 0) {
        fprintf(stderr, "[ERROR] DER: Capacity must be > 0\n");
        return NULL;
    }
    
    ctx = (der_encoder_context_t*)malloc(sizeof(der_encoder_context_t));
    if (!ctx) {
        fprintf(stderr, "[ERROR] DER: Out of memory allocating encoder context\n");
        return NULL;
    }
    
    ctx->buffer = (uint8_t*)malloc(capacity);
    if (!ctx->buffer) {
        fprintf(stderr, "[ERROR] DER: Out of memory allocating buffer\n");
        free(ctx);
        return NULL;
    }
    
    ctx->capacity = capacity;
    ctx->offset = 0;
    ctx->verbose = verbose;
    memset(ctx->error_message, 0, sizeof(ctx->error_message));
    
    if (verbose) {
        fprintf(stderr, "[INFO] DER encoder created (capacity %zu bytes)\n", capacity);
    }
    
    return ctx;
}

void der_encoder_destroy(der_encoder_context_t *ctx) {
    if (!ctx) return;
    if (ctx->buffer) {
        free(ctx->buffer);
        ctx->buffer = NULL;
    }
    free(ctx);
}

/* ============================================================================
   DER 长度编码
   ============================================================================ */

/**
 * 将长度按 DER 规则编码
 * 短形式（0-127）：1 字节直接表示
 * 长形式（128+）：第一字节 = 0x80 | 字节数，后续字节大端序表示长度
 */
size_t der_encode_length(uint8_t *buffer, size_t offset, size_t capacity,
                        size_t length) {
    if (!buffer) {
        return (size_t)-1;
    }
    
    if (length <= 127) {
        /* 短形式 */
        if (check_offset_overflow(offset, 1, capacity)) {
            fprintf(stderr, "[ERROR] DER: overflow encoding short length\n");
            return (size_t)-1;
        }
        buffer[offset] = (uint8_t)length;
        return offset + 1;
    }
    
    /* 长形式：计算需要多少字节表示长度 */
    size_t length_bytes = 0;
    size_t temp = length;
    while (temp > 0) {
        length_bytes++;
        temp >>= 8;
    }
    
    if (length_bytes > 8) {
        fprintf(stderr, "[ERROR] DER: length %zu requires > 8 bytes (unsupported)\n", length);
        return (size_t)-1;
    }
    
    /* 写入长形式标记 + 字节数 */
    if (check_offset_overflow(offset, 1 + length_bytes, capacity)) {
        fprintf(stderr, "[ERROR] DER: overflow encoding long length\n");
        return (size_t)-1;
    }
    
    buffer[offset] = (uint8_t)(0x80 | length_bytes);
    
    /* 大端序写入长度值 */
    for (size_t i = 0; i < length_bytes; i++) {
        buffer[offset + 1 + length_bytes - 1 - i] = (uint8_t)(length >> (i * 8));
    }
    
    return offset + 1 + length_bytes;
}

/* ============================================================================
   TLV 编码
   ============================================================================ */

size_t der_encode_tlv(uint8_t *buffer, size_t offset, size_t capacity,
                     uint8_t tag, const uint8_t *value, size_t value_length) {
    if (!buffer) {
        return (size_t)-1;
    }
    
    /* 写入标签 */
    if (check_offset_overflow(offset, 1, capacity)) {
        fprintf(stderr, "[ERROR] DER: overflow writing tag\n");
        return (size_t)-1;
    }
    buffer[offset] = tag;
    offset++;
    
    /* 写入长度 */
    offset = der_encode_length(buffer, offset, capacity, value_length);
    if (offset == (size_t)-1) {
        fprintf(stderr, "[ERROR] DER: failed to encode length\n");
        return (size_t)-1;
    }
    
    /* 写入值 */
    if (value_length > 0) {
        if (check_offset_overflow(offset, value_length, capacity)) {
            fprintf(stderr, "[ERROR] DER: overflow writing value\n");
            return (size_t)-1;
        }
        if (value) {
            memcpy(&buffer[offset], value, value_length);
        }
        offset += value_length;
    }
    
    return offset;
}

/* ============================================================================
   特定类型的编码器函数
   ============================================================================ */

size_t der_encode_ia5_string(uint8_t *buffer, size_t offset, size_t capacity,
                            const char *string) {
    size_t len;
    
    if (!buffer || !string) {
        return (size_t)-1;
    }
    
    len = strlen(string);
    
    /* 验证 IA5 字符集（7-bit ASCII） */
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)string[i] > 127) {
            fprintf(stderr, "[ERROR] DER: non-ASCII character in IA5String at offset %zu\n", i);
            return (size_t)-1;
        }
    }
    
    return der_encode_tlv(buffer, offset, capacity, DER_TAG_IA5_STRING,
                         (const uint8_t*)string, len);
}

size_t der_encode_octet_string(uint8_t *buffer, size_t offset, size_t capacity,
                              const uint8_t *data, size_t data_length) {
    if (!buffer) {
        return (size_t)-1;
    }
    
    return der_encode_tlv(buffer, offset, capacity, DER_TAG_OCTET_STRING,
                         data, data_length);
}

/* ============================================================================
   序列编码（两次遍历）
   ============================================================================ */

uint64_t der_encode_sequence_header(uint8_t *buffer, size_t offset,
                                   size_t capacity) {
    if (!buffer) {
        fprintf(stderr, "[ERROR] DER: NULL buffer\n");
        return (uint64_t)-1;
    }
    
    /* 写入 SEQUENCE 标签 */
    if (check_offset_overflow(offset, 1, capacity)) {
        fprintf(stderr, "[ERROR] DER: overflow writing sequence tag\n");
        return (uint64_t)-1;
    }
    
    buffer[offset] = DER_TAG_SEQUENCE;
    offset++;
    
    /* 记录长度字段起始位置 */
    size_t length_offset = offset;
    
    /* 跳过长度字段（假设最多 9 字节） */
    if (check_offset_overflow(offset, 9, capacity)) {
        fprintf(stderr, "[ERROR] DER: overflow allocating length space\n");
        return (uint64_t)-1;
    }
    
    offset += 9;  /* 预留足够空间（最坏情况：长形式 9 字节） */
    
    /* 返回：高 32 位 = 长度字段起始，低 32 位 = 内容起始 */
    return ((uint64_t)length_offset << 32) | (uint64_t)offset;
}

int der_backfill_length(uint8_t *buffer, size_t length_offset,
                       size_t content_length) {
    if (!buffer) {
        fprintf(stderr, "[ERROR] DER: NULL buffer in backfill\n");
        return -1;
    }
    
    /* 计算长度编码的大小 */
    size_t encoded_size = der_length_encoded_size(content_length);
    
    /* 编码长度值 */
    size_t temp_offset = der_encode_length(buffer, length_offset,
                                          length_offset + encoded_size + 100,
                                          content_length);
    
    if (temp_offset == (size_t)-1 || temp_offset > length_offset + 9) {
        fprintf(stderr, "[ERROR] DER: failed to backfill length\n");
        return -1;
    }
    
    return 0;
}

size_t der_calculate_tlv_size(uint8_t tag, size_t value_length) {
    /* tag (1 byte) + length + value */
    return 1 + der_length_encoded_size(value_length) + value_length;
}

/* ============================================================================
   DER 节点操作
   ============================================================================ */

der_node_t* der_node_create_sequence(void) {
    der_node_t *node = (der_node_t*)calloc(1, sizeof(der_node_t));
    if (!node) {
        fprintf(stderr, "[ERROR] DER: Out of memory creating sequence node\n");
        return NULL;
    }
    
    node->type = DER_NODE_SEQUENCE;
    node->child_capacity = 8;
    node->children = (der_node_t**)malloc(sizeof(der_node_t*) * node->child_capacity);
    if (!node->children) {
        fprintf(stderr, "[ERROR] DER: Out of memory allocating children array\n");
        free(node);
        return NULL;
    }
    
    return node;
}

der_node_t* der_node_create_octet_string(const uint8_t *data, size_t length) {
    der_node_t *node = (der_node_t*)calloc(1, sizeof(der_node_t));
    if (!node) {
        fprintf(stderr, "[ERROR] DER: Out of memory creating octet string node\n");
        return NULL;
    }
    
    node->type = DER_NODE_OCTET_STRING;
    node->data_length = length;
    
    if (length > 0 && data) {
        node->data = (uint8_t*)malloc(length);
        if (!node->data) {
            fprintf(stderr, "[ERROR] DER: Out of memory allocating octet data\n");
            free(node);
            return NULL;
        }
        memcpy(node->data, data, length);
    }
    
    return node;
}

der_node_t* der_node_create_ia5_string(const char *string) {
    der_node_t *node = (der_node_t*)calloc(1, sizeof(der_node_t));
    if (!node) {
        fprintf(stderr, "[ERROR] DER: Out of memory creating IA5 string node\n");
        return NULL;
    }
    
    node->type = DER_NODE_IA5_STRING;
    if (string) {
        node->data_length = strlen(string);
        node->data = (uint8_t*)malloc(node->data_length);
        if (!node->data) {
            fprintf(stderr, "[ERROR] DER: Out of memory allocating IA5 data\n");
            free(node);
            return NULL;
        }
        memcpy(node->data, string, node->data_length);
    }
    
    return node;
}

int der_node_add_child(der_node_t *parent, der_node_t *child) {
    if (!parent || !child) {
        fprintf(stderr, "[ERROR] DER: NULL parent or child\n");
        return -1;
    }
    
    if (parent->type != DER_NODE_SEQUENCE) {
        fprintf(stderr, "[ERROR] DER: Cannot add child to non-sequence node\n");
        return -1;
    }
    
    /* 扩展容量 */
    if (parent->child_count >= parent->child_capacity) {
        size_t new_capacity = parent->child_capacity * 2;
        der_node_t **new_children = (der_node_t**)realloc(parent->children,
                                                         sizeof(der_node_t*) * new_capacity);
        if (!new_children) {
            fprintf(stderr, "[ERROR] DER: Out of memory expanding children array\n");
            return -1;
        }
        parent->children = new_children;
        parent->child_capacity = new_capacity;
    }
    
    parent->children[parent->child_count++] = child;
    return 0;
}

/**
 * 计算节点编码后的大小（递归）
 */
static size_t der_node_calculate_size(der_node_t *node) {
    if (!node) return 0;
    
    switch (node->type) {
        case DER_NODE_OCTET_STRING:
            return der_calculate_tlv_size(DER_TAG_OCTET_STRING, node->data_length);
        
        case DER_NODE_IA5_STRING:
            return der_calculate_tlv_size(DER_TAG_IA5_STRING, node->data_length);
        
        case DER_NODE_SEQUENCE: {
            size_t content_size = 0;
            for (size_t i = 0; i < node->child_count; i++) {
                content_size += der_node_calculate_size(node->children[i]);
            }
            return der_calculate_tlv_size(DER_TAG_SEQUENCE, content_size);
        }
        
        default:
            fprintf(stderr, "[ERROR] DER: Unknown node type %d\n", node->type);
            return 0;
    }
}

size_t der_node_encode(der_node_t *node, uint8_t *buffer, size_t offset,
                      size_t capacity) {
    if (!node || !buffer) {
        fprintf(stderr, "[ERROR] DER: NULL node or buffer in encode\n");
        return (size_t)-1;
    }
    
    switch (node->type) {
        case DER_NODE_OCTET_STRING:
            return der_encode_octet_string(buffer, offset, capacity,
                                          node->data, node->data_length);
        
        case DER_NODE_IA5_STRING:
            return der_encode_tlv(buffer, offset, capacity, DER_TAG_IA5_STRING,
                                 node->data, node->data_length);
        
        case DER_NODE_SEQUENCE: {
            /* 写入标签 */
            if (check_offset_overflow(offset, 1, capacity)) {
                fprintf(stderr, "[ERROR] DER: overflow in sequence tag\n");
                return (size_t)-1;
            }
            buffer[offset] = DER_TAG_SEQUENCE;
            offset++;
            
            /* 记录长度位置 */
            size_t length_offset = offset;
            
            /* 预留长度字段空间 */
            if (check_offset_overflow(offset, 9, capacity)) {
                fprintf(stderr, "[ERROR] DER: overflow in sequence length space\n");
                return (size_t)-1;
            }
            offset += 9;
            
            /* 编码所有子节点 */
            size_t content_start = offset;
            for (size_t i = 0; i < node->child_count; i++) {
                offset = der_node_encode(node->children[i], buffer, offset, capacity);
                if (offset == (size_t)-1) {
                    fprintf(stderr, "[ERROR] DER: failed to encode child %zu\n", i);
                    return (size_t)-1;
                }
            }
            
            /* 回填长度 */
            size_t content_length = offset - content_start;
            if (der_backfill_length(buffer, length_offset, content_length) != 0) {
                fprintf(stderr, "[ERROR] DER: failed to backfill sequence length\n");
                return (size_t)-1;
            }
            
            /* 移除长度预留的多余字节 */
            size_t actual_length_size = der_length_encoded_size(content_length);
            if (actual_length_size < 9) {
                memmove(&buffer[content_start - (9 - actual_length_size)],
                       &buffer[content_start],
                       offset - content_start);
                offset -= (9 - actual_length_size);
            }
            
            return offset;
        }
        
        default:
            fprintf(stderr, "[ERROR] DER: Unknown node type in encode\n");
            return (size_t)-1;
    }
}

void der_node_destroy(der_node_t *node) {
    if (!node) return;
    
    if (node->data) {
        free(node->data);
    }
    
    if (node->type == DER_NODE_SEQUENCE && node->children) {
        for (size_t i = 0; i < node->child_count; i++) {
            der_node_destroy(node->children[i]);
        }
        free(node->children);
    }
    
    free(node);
}
