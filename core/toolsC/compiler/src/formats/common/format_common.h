/*
 * AethelOS Format Common Header Definitions
 * 所有二进制格式的通用规范
 *
 * 设计原则：
 * - 256字节标准化头部
 * - 使用AethelID作为身份标识
 * - 完全模块化，零C代码混入业务逻辑
 * - 严格遵守《AethelOS 二进制及目录结构.txt》规范
 */

#ifndef AETHEL_FORMAT_COMMON_H
#define AETHEL_FORMAT_COMMON_H

#include <stdint.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include "../../../include/binary_format.h"

/* ============================================================================
 * 通用常数定义
 * ============================================================================ */

#define AETHEL_HEADER_SIZE      256     /* 标准化头部大小 */
#define AETHEL_MAGIC_SIZE       4       /* 魔数大小 */
#define AETHEL_ID_SIZE          32      /* AethelID大小 (256位) */
#define AETHEL_CRC_SIZE         4       /* CRC校验大小 */

/* AethelID 结构：256位（32字节）*/
#define AETHEL_ID_VERSION_BITS      4       /* 版本号位数 */
#define AETHEL_ID_TIMESTAMP_BITS    48      /* 时间戳位数 */
#define AETHEL_ID_ENTROPY_BITS      96      /* 随机熵位数 */
#define AETHEL_ID_PAYLOAD_BITS      96      /* 加密负载位数 */
#define AETHEL_ID_CHECKSUM_BITS     12      /* 校验位数 */

/* AethelOS纪元时间 (2026-01-06 00:00:00 UTC) */
#define AETHEL_EPOCH_TIMESTAMP      1735987200

/* 二进制格式魔数 */
#define MAGIC_AKI               0x21494B41  /* "AKI!" */
#define MAGIC_HDA               0x21414448  /* "HDA!" */
#define MAGIC_SRV               0x21565253  /* "SRV!" */
#define MAGIC_AETB              0x42544541  /* "AETB" as uint32_t host read value */
#define MAGIC_IYA               0x41594921  /* "IYA!" */
#define MAGIC_AX                0x21584541  /* "AEX!" - AX format */

/* 版本号 */
#define FORMAT_VERSION_V1       1

/* ============================================================================
 * 通用头部结构 - 256字节标准化
 * ============================================================================ */

typedef struct {
    /* 0x00-0x03: 魔数 (4 bytes) */
    uint32_t magic;
    
    /* 0x04-0x07: 版本号 (4 bytes) */
    uint32_t version;
    
    /* 0x08-0x0F: 保留 (8 bytes) */
    uint8_t  reserved_0[8];
    
    /* 0x10-0x1F: AethelID - 身份标识 (32 bytes) */
    uint8_t  aethel_id[AETHEL_ID_SIZE];
    
    /* 0x30-0x37: Genesis Point - 主入口地址 (8 bytes) */
    uint64_t genesis_point;
    
    /* 0x38-0x3F: 保留 (8 bytes) */
    uint8_t  reserved_1[8];
    
    /* 0x40-0x4F: ActFlow Zone [offset, size] (16 bytes) */
    uint64_t act_flow_offset;
    uint64_t act_flow_size;
    
    /* 0x50-0x5F: MirrorState Zone [offset, size] (16 bytes) */
    uint64_t mirror_state_offset;
    uint64_t mirror_state_size;
    
    /* 0x60-0x6F: ConstantTruth Zone [offset, size] (16 bytes) */
    uint64_t constant_truth_offset;
    uint64_t constant_truth_size;
    
    /* 0x70-0x7F: 格式特定区域 (16 bytes) - 由各格式自定义 */
    uint8_t  format_specific[16];
    
    /* 0x80-0xD7: 扩展元数据 (88 bytes) - 由各格式自定义 */
    uint8_t  extended_metadata[88];
    
    /* 0xD8-0xDB: Header CRC32 (4 bytes) */
    uint32_t header_crc;
    
    /* 0xDC-0xDF: Build Timestamp (4 bytes) */
    uint32_t build_timestamp;
    
    /* 0xE0-0xE3: Build Version (4 bytes) */
    uint32_t build_version;
    
    /* 0xE4-0xE7: Compiler Version (4 bytes) */
    uint32_t compiler_version;
    
    /* 0xE8-0xFF: 保留用于未来扩展 (24 bytes) */
    uint8_t  reserved_future[24];
} AethelBinaryHeader;

_Static_assert(sizeof(AethelBinaryHeader) == AETHEL_HEADER_SIZE, 
               "AethelBinaryHeader must be exactly 256 bytes");

/* ============================================================================
 * AethelID 结构与接口
 * ============================================================================ */

/**
 * AethelID 256位唯一标识符已在binary_format.h中定义
 * 
 * 格式：[版本:4b][时间戳:48b][随机熵:96b][加密负载:96b][校验:12b] = 256b
 * 
 * 设计目标：
 * - 完全自创，不参考UUID/GUID等
 * - 用于IPC直接通信
 * - 包含可加密信息
 * - 支持验证和防篡改
 */

/* ============================================================================
 * 通用函数接口
 * ============================================================================ */

/**
 * 初始化标准头部
 */
void aethel_header_init(AethelBinaryHeader *hdr, uint32_t magic);

/**
 * 计算并设置CRC32
 */
uint32_t aethel_header_calculate_crc(AethelBinaryHeader *hdr);

/**
 * 验证头部完整性
 */
int aethel_header_validate(const AethelBinaryHeader *hdr);

/**
 * 从缓冲区写入头部
 */
int aethel_header_write(FILE *out, const AethelBinaryHeader *hdr);

/**
 * 从缓冲区读取头部
 */
int aethel_header_read(FILE *in, AethelBinaryHeader *hdr);

/* ============================================================================
 * AethelID 生成、加密与验证接口
 * ============================================================================ */

/**
 * 生成新的AethelID
 * 
 * 参数：
 *   id_type: 类型标记字符串，例如 "Aethel/Kernel/Origin"
 *   encrypted_payload: 要加密的12字节负载数据
 *   master_key: 256位主密钥 (NULL则使用系统默认密钥)
 * 
 * 返回：生成的AethelID结构体
 */
AethelID aethel_id_generate(const char *id_type, const uint8_t *encrypted_payload,
                           const uint8_t *master_key);

/**
 * 从现有AethelID中提取和解密负载
 * 
 * 参数：
 *   id: 源AethelID
 *   payload_out: 输出缓冲区 (12字节)
 *   master_key: 256位主密钥 (NULL则使用系统默认密钥)
 * 
 * 返回：0成功，-1失败
 */
int aethel_id_decrypt_payload(const AethelID *id, uint8_t *payload_out,
                             const uint8_t *master_key);

/**
 * 验证AethelID的完整性和真伪
 * 
 * 返回：0有效，-1无效或篡改
 */
int aethel_id_verify(const AethelID *id, const uint8_t *master_key);

/**
 * 将AethelID转换为十六进制字符串表示 (64字符)
 * 
 * 参数：
 *   id: 源AethelID
 *   hex_out: 输出缓冲区 (至少65字节，包括\0)
 */
void aethel_id_to_hex(const AethelID *id, char *hex_out);

/**
 * 从十六进制字符串解析AethelID
 * 
 * 返回：0成功，-1失败
 */
int aethel_id_from_hex(const char *hex_in, AethelID *id_out);

/*
 * AETB 语义约束：
 * 1) AETB 是 IYA 应用包内的运行二进制格式，不是中间格式。
 * 2) AethelOS 唯一允许的编译中间格式是 LET。
 * 3) 严禁将 AETB 作为“机器码提取中间层”使用。
 */

#endif  /* AETHEL_FORMAT_COMMON_H */
