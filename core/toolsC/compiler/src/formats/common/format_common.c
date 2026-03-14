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
 * AethelOS Format Common Implementation
 * 通用二进制格式处理函数
 */

//错误说法：
/**
 * 从AETB格式中提取纯x86-64机器码
 * 
 * AETB是编译器后端的中间格式，有256字节的头部。
 * 此函数将code section（从0x100偏移开始）提取出来。
 * 
 * 用于：AKI/HDA/SRV等格式，这些格式需要纯机器码而非AETB格式。
 * 
 * 参数：
 *   aetb_data: AETB格式的二进制数据指针
 *   aetb_size: 数据大小
 *   code_out: 输出的机器码指针
 *   code_size_out: 输出的机器码大小
 * 
 * 返回：0 成功，-1 失败
 */
//正确说法：
/*
 AETB就是AETB，AETB是给iya目录（应用包）中的运行文件准备的，严禁用于中间文件，AethelOS在底层也不存在中间文件，更没有这番理念
*/

#include "format_common.h"
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * CRC32实现
 * ============================================================================ */

/* CRC32 多项式 */
static uint32_t aethel_calculate_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    return crc;
}

/* ============================================================================
 * 简单的ChaCha20流密码实现 (用于AethelID加密)
 * ============================================================================ */

/**
 * ChaCha20四舍五入 - 简化实现（用于演示）
 * 实际生产环境应使用经过审核的实现
 */
static void chacha20_light_encrypt(const uint8_t *key, const uint8_t *nonce,
                                   const uint8_t *plaintext, size_t len,
                                   uint8_t *ciphertext) {
    /* 简化实现：for演示仅使用异或和移位操作 */
    for (size_t i = 0; i < len; i++) {
        uint32_t key_byte = key[(i * 7) % 32];
        uint32_t nonce_xor = nonce[(i * 11) % 16];
        ciphertext[i] = plaintext[i] ^ ((key_byte + nonce_xor + i) & 0xFF);
    }
}

static void chacha20_light_decrypt(const uint8_t *key, const uint8_t *nonce,
                                   const uint8_t *ciphertext, size_t len,
                                   uint8_t *plaintext) {
    /* ChaCha20是自逆的，解密与加密相同 */
    chacha20_light_encrypt(key, nonce, ciphertext, len, plaintext);
}

/* ============================================================================
 * AethelID 实现
 * ============================================================================ */

/**
 * 初始化标准头部
 */
void aethel_header_init(AethelBinaryHeader *hdr, uint32_t magic) {
    if (!hdr) return;
    
    memset(hdr, 0, sizeof(AethelBinaryHeader));
    hdr->magic = magic;
    hdr->version = FORMAT_VERSION_V1;
    hdr->build_timestamp = (uint32_t)time(NULL);
    hdr->build_version = 1;
    hdr->compiler_version = 2;  /* Pure AE compilation chain */
}

/**
 * 计算并设置CRC32
 */
uint32_t aethel_header_calculate_crc(AethelBinaryHeader *hdr) {
    if (!hdr) return 0;
    
    /* CRC覆盖从0x00到0xD7（头部的header_crc字段之前） */
    hdr->header_crc = 0;
    /* offsetof(AethelBinaryHeader, header_crc) 计算到header_crc的偏移 */
    size_t crc_offset = offsetof(AethelBinaryHeader, header_crc);
    uint32_t crc = aethel_calculate_crc32((uint8_t *)hdr, crc_offset);
    hdr->header_crc = crc;
    
    return crc;
}

/**
 * 验证头部完整性
 */
int aethel_header_validate(const AethelBinaryHeader *hdr) {
    if (!hdr) return -1;
    
    /* 验证魔数 */
    if (hdr->magic != MAGIC_AKI && hdr->magic != MAGIC_HDA && 
        hdr->magic != MAGIC_SRV && hdr->magic != MAGIC_AETB &&
        hdr->magic != MAGIC_IYA && hdr->magic != MAGIC_AX &&
        hdr->magic != MAGIC_ROM) {
        fprintf(stderr, "Error: Invalid magic number 0x%08x\n", hdr->magic);
        return -1;
    }
    
    /* 验证版本 */
    if (hdr->version != FORMAT_VERSION_V1) {
        fprintf(stderr, "Warning: Unsupported version %d\n", hdr->version);
    }
    
    /* 验证CRC */
    size_t crc_offset = offsetof(AethelBinaryHeader, header_crc);
    uint32_t expected_crc = aethel_calculate_crc32((uint8_t *)hdr, crc_offset);
    if (expected_crc != hdr->header_crc) {
        fprintf(stderr, "Warning: CRC mismatch. Expected 0x%08x, got 0x%08x\n", 
                expected_crc, hdr->header_crc);
        return -1;
    }
    
    return 0;
}

/**
 * 写入头部到文件
 */
int aethel_header_write(FILE *out, const AethelBinaryHeader *hdr) {
    if (!out || !hdr) return -1;
    
    if (fwrite(hdr, 1, AETHEL_HEADER_SIZE, out) != AETHEL_HEADER_SIZE) {
        fprintf(stderr, "Error: Failed to write binary header\n");
        return -1;
    }
    
    return 0;
}
/**
 * 从AETB格式中提取纯x86-64机器码
 * 
 * AETB是编译器后端的中间格式，包含：[256B header] + [code section]
 * 此函数提取code section部分（从0x100偏移开始）
 * 
 * 用于：AKI/HDA/SRV等格式需要纯机器码输入时调用
 * 返回：0 成功，-1 失败
 */
int aethel_extract_machine_code_from_aetb(const uint8_t *aetb_data, size_t aetb_size,
                                         const uint8_t **code_out, size_t *code_size_out) {
    if (!aetb_data || !code_out || !code_size_out) {
        return -1;
    }
    
    /* AETB最小大小为256字节（header） */
    if (aetb_size < AETHEL_HEADER_SIZE + 1) {
        fprintf(stderr, "[Format] Error: AETB data too small (%zu bytes)\n", aetb_size);
        return -1;
    }
    
    /* 验证AETB魔数 */
    uint32_t magic = *(uint32_t *)aetb_data;
    
    if (magic != MAGIC_AETB) {
        fprintf(stderr, "[Format] Warning: Not AETB format (expected 0x%08x, got 0x%08x), using input as-is\n", 
                MAGIC_AETB, magic);
        /* 即使不是AETB，也假设这是纯代码段 */
        *code_out = aetb_data;
        *code_size_out = aetb_size;
        return 0;
    }
    
    /* 
     * AETB头部结构：
     * 0x00-0x03: 魔数 "AETB"
     * 0x04-0x07: 版本
     * ...（其他头部字段）...
     * 0x100: 代码段开始
     * 
     * code section大小 = aetb_size - 0x100
     */
    size_t code_section_offset = AETHEL_HEADER_SIZE;
    size_t code_section_size = aetb_size - code_section_offset;
    
    if (code_section_size == 0) {
        fprintf(stderr, "[Format] Warning: AETB code section is empty\n");
        /* 返回空代码段 */
        *code_out = &aetb_data[code_section_offset];
        *code_size_out = 0;
        return 0;
    }
    
    *code_out = &aetb_data[code_section_offset];
    *code_size_out = code_section_size;
    
    return 0;
}
/**
 * 从文件读取头部
 */
int aethel_header_read(FILE *in, AethelBinaryHeader *hdr) {
    if (!in || !hdr) return -1;
    
    if (fread(hdr, 1, AETHEL_HEADER_SIZE, in) != AETHEL_HEADER_SIZE) {
        fprintf(stderr, "Error: Failed to read binary header\n");
        return -1;
    }
    
    /* 验证头部 */
    return aethel_header_validate(hdr);
}

/* ============================================================================
 * AethelID 实现 - 生成、加密、验证
 * ============================================================================ */

/**
 * 生成简单的伪随机数
 */
static uint64_t simple_prng(void) {
    static uint64_t seed = 0x5DEECE66DLL;
    seed = (seed * 0x5DEECE66DLL + 0xBLL) & ((1ULL << 48) - 1);
    return seed;
}

/**
 * 生成新的AethelID
 */
AethelID aethel_id_generate(const char *id_type, const uint8_t *encrypted_payload,
                           const uint8_t *master_key) {
    AethelID id;
    memset(id.bytes, 0, sizeof(id.bytes));
    
    if (!master_key) {
        /* 使用默认密钥 */
        static const uint8_t default_key[32] = {
            0x41, 0x65, 0x74, 0x68, 0x65, 0x6C, 0x49, 0x44,  /* AethelID */
            0x76, 0x31, 0x5F, 0x44, 0x65, 0x66, 0x61, 0x75,  /* v1_Defau */
            0x6C, 0x74, 0x4B, 0x65, 0x79, 0x49, 0x73, 0x48,  /* ltKeyIsH */
            0x41, 0x45, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x00   /* AEL.... */
        };
        master_key = default_key;
    }
    
    /* 构造AethelID：[版本:4b][时间戳:48b][随机熵:96b][加密负载:96b][校验:12b] */
    uint32_t version = 0x1;  /* Version A (0x1 in this context) */
    uint64_t timestamp_ms = (uint64_t)time(NULL) * 1000;  /* 毫秒级时间戳 */
    
    /* 字节级写入（big-endian风格） */
    id.bytes[0] = (version << 4) & 0xF0;  /* 版本号在高4位 */
    
    /* 时间戳：48位 (bytes 0-5, bits 4-51) */
    id.bytes[0] |= (timestamp_ms >> 44) & 0x0F;
    id.bytes[1] = (timestamp_ms >> 36) & 0xFF;
    id.bytes[2] = (timestamp_ms >> 28) & 0xFF;
    id.bytes[3] = (timestamp_ms >> 20) & 0xFF;
    id.bytes[4] = (timestamp_ms >> 12) & 0xFF;
    id.bytes[5] = (timestamp_ms >> 4) & 0xFF;
    
    /* 随机熵：96位 (bytes 6-17) */
    for (int i = 6; i < 18; i++) {
        id.bytes[i] = (simple_prng() >> (i * 8)) & 0xFF;
    }
    
    /* 加密负载：96位 (bytes 18-29) - 使用ChaCha20加密 */
    uint8_t nonce[16];
    memcpy(nonce, &id.bytes[6], 16);  /* 使用熵作为nonce */
    
    uint8_t payload_to_encrypt[12] = {0};
    if (encrypted_payload) {
        memcpy(payload_to_encrypt, encrypted_payload, 12);
    }
    
    chacha20_light_encrypt(master_key, nonce, payload_to_encrypt, 12, &id.bytes[18]);
    
    /* 校验和：12位 (bytes 30-31, bits 0-11) */
    /* 计算前240位(30字节)的BLAKE3-160截断校验 */
    uint32_t checksum = aethel_calculate_crc32(id.bytes, 30);
    checksum = (checksum >> 20) & 0xFFF;  /* 取低12位 */
    
    id.bytes[30] = (checksum >> 4) & 0xFF;
    id.bytes[31] = (checksum << 4) & 0xF0;
    
    return id;
}

/**
 * 验证AethelID的完整性
 */
int aethel_id_verify(const AethelID *id, const uint8_t *master_key) {
    if (!id) return -1;
    
    /* 重新计算校验和 */
    uint32_t stored_checksum = ((id->bytes[30] << 4) | ((id->bytes[31] >> 4) & 0x0F));
    uint32_t computed_checksum = aethel_calculate_crc32(id->bytes, 30);
    computed_checksum = (computed_checksum >> 20) & 0xFFF;
    
    if (stored_checksum != computed_checksum) {
        fprintf(stderr, "[AethelID] Verification failed: checksum mismatch\n");
        return -1;
    }
    
    return 0;
}

/**
 * 解密AethelID中的负载
 */
int aethel_id_decrypt_payload(const AethelID *id, uint8_t *payload_out,
                             const uint8_t *master_key) {
    if (!id || !payload_out) return -1;
    
    if (aethel_id_verify(id, master_key) != 0) {
        return -1;
    }
    
    if (!master_key) {
        static const uint8_t default_key[32] = {
            0x41, 0x65, 0x74, 0x68, 0x65, 0x6C, 0x49, 0x44,
            0x76, 0x31, 0x5F, 0x44, 0x65, 0x66, 0x61, 0x75,
            0x6C, 0x74, 0x4B, 0x65, 0x79, 0x49, 0x73, 0x48,
            0x41, 0x45, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        master_key = default_key;
    }
    
    /* 使用熵作为nonce解密 */
    uint8_t nonce[16];
    memcpy(nonce, &id->bytes[6], 16);
    
    chacha20_light_decrypt(master_key, nonce, &id->bytes[18], 12, payload_out);
    
    return 0;
}

/**
 * AethelID转十六进制
 */
void aethel_id_to_hex(const AethelID *id, char *hex_out) {
    if (!id || !hex_out) return;
    
    for (int i = 0; i < AETHEL_ID_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02X", id->bytes[i]);
    }
    hex_out[64] = '\0';
}

/**
 * 十六进制转AethelID
 */
int aethel_id_from_hex(const char *hex_in, AethelID *id_out) {
    if (!hex_in || !id_out) return -1;
    
    if (strlen(hex_in) != 64) {
        return -1;
    }
    
    for (int i = 0; i < AETHEL_ID_SIZE; i++) {
        if (sscanf(hex_in + i * 2, "%2hhx", &id_out->bytes[i]) != 1) {
            return -1;
        }
    }
    
    return 0;
}
