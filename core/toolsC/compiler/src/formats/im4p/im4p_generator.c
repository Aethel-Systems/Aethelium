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
 * Aethelium IM4P Container Generator Implementation
 * 完整的工业级实现，包括 ASN.1 树构造、DER 编码、文件写出
 */

#include "im4p_generator.h"
#include "der_encoder.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
   IM4P 容器内部结构
   ============================================================================ */

typedef struct {
    der_node_t *root_sequence;
    uint8_t *encoded_data;
    size_t encoded_size;
} im4p_container_t;

/* ============================================================================
   ASN.1 树构造函数
   ============================================================================ */

void* im4p_container_create(const uint8_t *macho_data,
                           size_t macho_size,
                           const char *identifier) {
    im4p_container_t *container;
    der_node_t *root;
    der_node_t *magic_node;
    der_node_t *type_node;
    der_node_t *desc_node;
    der_node_t *image_node;
    
    /* 前置条件检查 */
    if (!macho_data || macho_size == 0) {
        fprintf(stderr, "[ERROR] IM4P: Mach-O data is NULL or empty\n");
        return NULL;
    }
    
    if (!identifier || identifier[0] == '\0') {
        fprintf(stderr, "[ERROR] IM4P: Identifier is NULL or empty\n");
        return NULL;
    }
    
    /* 验证标识符长度 (通常很短，如 "krnl") */
    if (strlen(identifier) > 256) {
        fprintf(stderr, "[ERROR] IM4P: Identifier too long (max 256 characters)\n");
        return NULL;
    }
    
    /* 分配容器结构 */
    container = (im4p_container_t*)calloc(1, sizeof(im4p_container_t));
    if (!container) {
        fprintf(stderr, "[ERROR] IM4P: Out of memory allocating container\n");
        return NULL;
    }
    
    /* 创建根 SEQUENCE 节点 */
    root = der_node_create_sequence();
    if (!root) {
        fprintf(stderr, "[ERROR] IM4P: Failed to create root sequence\n");
        free(container);
        return NULL;
    }
    
    /* ========== 第 1 个子节点：magic IA5String = "IM4P" ========== */
    magic_node = der_node_create_ia5_string("IM4P");
    if (!magic_node) {
        fprintf(stderr, "[ERROR] IM4P: Failed to create magic node\n");
        der_node_destroy(root);
        free(container);
        return NULL;
    }
    
    if (der_node_add_child(root, magic_node) != 0) {
        fprintf(stderr, "[ERROR] IM4P: Failed to add magic node\n");
        der_node_destroy(magic_node);
        der_node_destroy(root);
        free(container);
        return NULL;
    }
    
    /* ========== 第 2 个子节点：type IA5String = identifier ========== */
    type_node = der_node_create_ia5_string(identifier);
    if (!type_node) {
        fprintf(stderr, "[ERROR] IM4P: Failed to create type node\n");
        der_node_destroy(root);
        free(container);
        return NULL;
    }
    
    if (der_node_add_child(root, type_node) != 0) {
        fprintf(stderr, "[ERROR] IM4P: Failed to add type node\n");
        der_node_destroy(type_node);
        der_node_destroy(root);
        free(container);
        return NULL;
    }
    
    /* ========== 第 3 个子节点：description IA5String ========== */
    /* 生成描述文本，说明这是由 Aethelium 编译器生成的 */
    char description[256];
    snprintf(description, sizeof(description),
            "Aethelium Bare-metal Firmware Image (Compiled %s)",
            identifier);
    
    desc_node = der_node_create_ia5_string(description);
    if (!desc_node) {
        fprintf(stderr, "[ERROR] IM4P: Failed to create description node\n");
        der_node_destroy(root);
        free(container);
        return NULL;
    }
    
    if (der_node_add_child(root, desc_node) != 0) {
        fprintf(stderr, "[ERROR] IM4P: Failed to add description node\n");
        der_node_destroy(desc_node);
        der_node_destroy(root);
        free(container);
        return NULL;
    }
    
    /* ========== 第 4 个子节点：image OCTET STRING ========== */
    image_node = der_node_create_octet_string(macho_data, macho_size);
    if (!image_node) {
        fprintf(stderr, "[ERROR] IM4P: Failed to create image node\n");
        der_node_destroy(root);
        free(container);
        return NULL;
    }
    
    if (der_node_add_child(root, image_node) != 0) {
        fprintf(stderr, "[ERROR] IM4P: Failed to add image node\n");
        der_node_destroy(image_node);
        der_node_destroy(root);
        free(container);
        return NULL;
    }
    
    container->root_sequence = root;
    container->encoded_data = NULL;
    container->encoded_size = 0;
    
    fprintf(stderr, "[INFO] IM4P container created with identifier '%s' (%zu bytes Mach-O)\n",
            identifier, macho_size);
    
    return (void*)container;
}

/* ============================================================================
   DER 编码与输出
   ============================================================================ */

int im4p_container_encode(void *container_ptr, uint8_t *output, size_t *output_size) {
    im4p_container_t *container = (im4p_container_t*)container_ptr;
    size_t offset;
    
    /* 前置条件检查 */
    if (!container) {
        fprintf(stderr, "[ERROR] IM4P encode: NULL container\n");
        return -1;
    }
    
    if (!output || !output_size || *output_size == 0) {
        fprintf(stderr, "[ERROR] IM4P encode: invalid output parameters\n");
        return -1;
    }
    
    if (!container->root_sequence) {
        fprintf(stderr, "[ERROR] IM4P encode: root sequence is NULL\n");
        return -1;
    }
    
    /* 编码树为 DER 二进制 */
    offset = der_node_encode(container->root_sequence, output, 0, *output_size);
    
    if (offset == (size_t)-1) {
        fprintf(stderr, "[ERROR] IM4P encode: DER encoding failed\n");
        return -4;
    }
    
    /* 验证编码后的大小 */
    if (offset == 0 || offset > *output_size) {
        fprintf(stderr, "[ERROR] IM4P encode: Invalid encoded size %zu\n", offset);
        return -4;
    }
    
    /* 更新输出大小 */
    *output_size = offset;
    container->encoded_size = offset;
    
    fprintf(stderr, "[INFO] IM4P encoded successfully (%zu bytes)\n", offset);
    
    return 0;
}

void im4p_container_destroy(void *container_ptr) {
    im4p_container_t *container = (im4p_container_t*)container_ptr;
    
    if (!container) return;
    
    if (container->root_sequence) {
        der_node_destroy(container->root_sequence);
    }
    
    if (container->encoded_data) {
        free(container->encoded_data);
    }
    
    free(container);
}

/* ============================================================================
   顶级生成函数
   ============================================================================ */

int im4p_generate(const char *output_file,
                 const uint8_t *macho_data,
                 size_t macho_size,
                 const char *identifier) {
    void *container;
    uint8_t *buffer;
    size_t buffer_size;
    FILE *f;
    size_t written;
    int result;
    
    /* 前置条件验证 */
    if (!output_file || output_file[0] == '\0') {
        fprintf(stderr, "[ERROR] IM4P generate: NULL or empty output filename\n");
        return -1;
    }
    
    if (!macho_data || macho_size == 0) {
        fprintf(stderr, "[ERROR] IM4P generate: Mach-O data is NULL or empty\n");
        return -1;
    }
    
    if (!identifier || identifier[0] == '\0') {
        fprintf(stderr, "[ERROR] IM4P generate: Identifier is NULL or empty\n");
        return -1;
    }
    
    /* 验证 Mach-O 大小不超过 512MB（合理的固件上限） */
    if (macho_size > 512 * 1024 * 1024) {
        fprintf(stderr, "[ERROR] IM4P generate: Mach-O size %zu exceeds 512MB limit\n", macho_size);
        return -1;
    }
    
    fprintf(stderr, "[INFO] IM4P generation starting (Mach-O %zu bytes, identifier '%s')\n",
            macho_size, identifier);
    
    /* ========== 第 1 步：构造 IM4P 容器 ASN.1 树 ========== */
    container = im4p_container_create(macho_data, macho_size, identifier);
    if (!container) {
        fprintf(stderr, "[ERROR] IM4P generate: Failed to create container\n");
        return -3;
    }
    
    /* ========== 第 2 步：分配足够大的输出缓冲区 ========== */
    /* 估计大小：4 节点的 TLV 头部 + 所有数据 + 安全裕度 */
    buffer_size = 1024 + macho_size + 10240;  /* 10KB 安全裕度用于 DER 开销 */
    
    buffer = (uint8_t*)malloc(buffer_size);
    if (!buffer) {
        fprintf(stderr, "[ERROR] IM4P generate: Out of memory allocating buffer (%zu bytes)\n",
                buffer_size);
        im4p_container_destroy(container);
        return -2;
    }
    
    memset(buffer, 0, buffer_size);
    
    /* ========== 第 3 步：DER 编码容器 ========== */
    size_t encoded_size = buffer_size;
    result = im4p_container_encode(container, buffer, &encoded_size);
    if (result != 0) {
        fprintf(stderr, "[ERROR] IM4P generate: Encoding failed\n");
        free(buffer);
        im4p_container_destroy(container);
        return -4;
    }
    
    /* ========== 第 4 步：写出文件 ========== */
    f = fopen(output_file, "wb");
    if (!f) {
        fprintf(stderr, "[ERROR] IM4P generate: Failed to open '%s' for writing\n", output_file);
        free(buffer);
        im4p_container_destroy(container);
        return -5;
    }
    
    written = fwrite(buffer, 1, encoded_size, f);
    if (written != encoded_size) {
        fprintf(stderr, "[ERROR] IM4P generate: Partial write to '%s' (%zu / %zu bytes)\n",
                output_file, written, encoded_size);
        fclose(f);
        free(buffer);
        im4p_container_destroy(container);
        return -5;
    }
    
    fclose(f);
    
    fprintf(stderr, "[INFO] IM4P container generated successfully\n");
    fprintf(stderr, "[INFO] Output file: %s (%zu bytes)\n", output_file, encoded_size);
    
    /* ========== 清理 ========== */
    free(buffer);
    im4p_container_destroy(container);
    
    return 0;
}
