/*
 * SRV (System Service Archive) Generation Implementation
 * 
 * 工业级完整实现 - 严格按照《AethelOS 二进制及目录结构.txt》规范
 * 
 * SRV是提供功能接口的逻辑实体
 * 
 * 完整结构：
 * [256B Header] + [ActFlow] + [MirrorState] + [ConstantTruth]
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

#include "srv.h"
#include <string.h>
#include <stdio.h>

/**
 * 生成系统服务的AethelID
 */
AethelID srv_generate_aethel_id(const char *domain, const char *function,
                               const uint8_t *payload) {
    char id_type_buf[64];
    snprintf(id_type_buf, sizeof(id_type_buf), "Aethel/Srv/%s/%s",
             domain ? domain : "system", function ? function : "service");
    return aethel_id_generate(id_type_buf, payload, NULL);
}

/**
 * 初始化SRV头部
 */
void srv_header_init(AethelBinaryHeader *hdr) {
    if (!hdr) return;
    
    aethel_header_init(hdr, MAGIC_SRV);
    
    /* 设置SRV默认参数 */
    
    /* 在format_specific中设置服务ID (offset 0x70-0x73) */
    uint32_t *srv_id = (uint32_t *)&hdr->format_specific[0];
    *srv_id = 0x00000000;
    
    /* 服务类型 (offset 0x74-0x77) */
    uint32_t *srv_type = (uint32_t *)&hdr->format_specific[4];
    *srv_type = 0x00000000;
    
    /* SIP策略 (offset 0x78-0x7B) */
    uint32_t *sip_pol = (uint32_t *)&hdr->format_specific[8];
    *sip_pol = 0x00000000;
    
    /* 权限等级 (offset 0x7C-0x7D) */
    uint16_t *priv_level = (uint16_t *)&hdr->format_specific[12];
    *priv_level = 0x0000;
    
    /* 模式行为 (offset 0x7E) - 默认共有 */
    hdr->format_specific[14] = 0x02;
}

/**
 * 设置SRV服务信息
 */
void srv_header_set_service_info(AethelBinaryHeader *hdr,
                                  uint32_t service_id,
                                  uint32_t service_type,
                                  uint16_t privilege_level) {
    if (!hdr) return;
    
    uint32_t *srv_id = (uint32_t *)&hdr->format_specific[0];
    *srv_id = service_id;
    
    uint32_t *srv_type = (uint32_t *)&hdr->format_specific[4];
    *srv_type = service_type;
    
    uint16_t *priv = (uint16_t *)&hdr->format_specific[12];
    *priv = privilege_level;
}

/**
 * 设置SRV的SIP策略
 */
void srv_header_set_sip_policy(AethelBinaryHeader *hdr, uint32_t policy) {
    if (!hdr) return;
    uint32_t *sip_pol = (uint32_t *)&hdr->format_specific[8];
    *sip_pol = policy;
}

/**
 * 设置SRV的模式行为
 */
void srv_header_set_mode_behavior(AethelBinaryHeader *hdr, uint8_t behavior) {
    if (!hdr) return;
    hdr->format_specific[14] = behavior;
}

/**
 * 设置SRV的服务生命点
 */
void srv_header_set_genesis(AethelBinaryHeader *hdr, uint64_t genesis) {
    if (!hdr) return;
    uint64_t *srv_gen = (uint64_t *)&hdr->extended_metadata[0];
    *srv_gen = genesis;
}

/**
 * 设置SRV的服务入口
 */
void srv_header_set_entry(AethelBinaryHeader *hdr, uint64_t entry) {
    if (!hdr) return;
    uint64_t *srv_ent = (uint64_t *)&hdr->extended_metadata[8];
    *srv_ent = entry;
}

/**
 * 完整的SRV镜像生成
 */
int srv_generate_image(const char *output_file, 
                      const uint8_t *code, size_t code_size,
                      const uint8_t *mirror_data, size_t mirror_size,
                      const uint8_t *constant_data, size_t constant_size) {
    if (!output_file || !code || code_size == 0) {
        fprintf(stderr, "[SRV] Error: Invalid parameters\n");
        return -1;
    }
    
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "[SRV] Error: Cannot create output file '%s'\n", output_file);
        return -1;
    }
    
    /* 从输入数据直接构建 服务逻辑 */
    if (!code || code_size == 0) {
        fprintf(stderr, "[SRV] Error: No code provided\n");
        return -1;
    }
    
    /* 构建SRV头部 */
    AethelBinaryHeader hdr;
    srv_header_init(&hdr);
    
    /* 计算各Zone的偏移和大小 */
    uint64_t current_offset = AETHEL_HEADER_SIZE;
    
    /* ActFlow Zone - 服务的业务逻辑 */
    hdr.act_flow_offset = current_offset;
    hdr.act_flow_size = code_size;
    current_offset += code_size;
    
    /* MirrorState Zone - 服务维护的内存对象 */
    if (mirror_data && mirror_size > 0) {
        hdr.mirror_state_offset = current_offset;
        hdr.mirror_state_size = mirror_size;
        current_offset += mirror_size;
    } else {
        hdr.mirror_state_offset = 0;
        hdr.mirror_state_size = 0;
    }
    
    /* ConstantTruth Zone - 接口Schema、反射信息 */
    if (constant_data && constant_size > 0) {
        hdr.constant_truth_offset = current_offset;
        hdr.constant_truth_size = constant_size;
        current_offset += constant_size;
    } else {
        hdr.constant_truth_offset = 0;
        hdr.constant_truth_size = 0;
    }
    
    /* 生成并设置AethelID */
    uint8_t payload[12];
    memset(payload, 0, 12);
    payload[0] = 0x53;  /* 'S' */
    payload[1] = 0x52;  /* 'R' */
    payload[2] = 0x56;  /* 'V' */
    
    AethelID service_id = srv_generate_aethel_id("system", "service", payload);
    memcpy(hdr.aethel_id, service_id.bytes, AETHEL_ID_SIZE);
    
    /* 计算并设置CRC */
    aethel_header_calculate_crc(&hdr);
    
    /* 写入头部 */
    if (aethel_header_write(out, &hdr) != 0) {
        fprintf(stderr, "[SRV] Error: Failed to write header\n");
        fclose(out);
        return -1;
    }
    
    /* 写入ActFlow段 - 服务业务逻辑 */
    if (fwrite(code, 1, code_size, out) != code_size) {
        fprintf(stderr, "[SRV] Error: Failed to write ActFlow code\n");
        fclose(out);
        return -1;
    }
    
    /* 写入MirrorState段（若存在） */
    if (mirror_data && mirror_size > 0) {
        if (fwrite(mirror_data, 1, mirror_size, out) != mirror_size) {
            fprintf(stderr, "[SRV] Error: Failed to write MirrorState\n");
            fclose(out);
            return -1;
        }
    }
    
    /* 写入ConstantTruth段（若存在） */
    if (constant_data && constant_size > 0) {
        if (fwrite(constant_data, 1, constant_size, out) != constant_size) {
            fprintf(stderr, "[SRV] Error: Failed to write ConstantTruth\n");
            fclose(out);
            return -1;
        }
    }
    
    fclose(out);
    
    fprintf(stderr, "[SRV] ✓ Generated: %s (system service archive)\n", output_file);
    fprintf(stderr, "      ActFlow Logic: %zu bytes\n", code_size);
    fprintf(stderr, "      MirrorState: %zu bytes\n", mirror_size);
    fprintf(stderr, "      ConstantTruth: %zu bytes\n", constant_size);
    fprintf(stderr, "      Total Size: %zu bytes\n", current_offset);
    
    return 0;
}
