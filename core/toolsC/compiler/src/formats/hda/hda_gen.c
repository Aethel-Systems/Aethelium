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
 * HDA (Hardware Driver Archive) Generation Implementation
 * 
 * 工业级完整实现 - 严格按照《AethelOS 二进制及目录结构.txt》规范
 * 
 * HDA不是传统驱动程序，而是硬件能力契约的描述文件
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

#include "hda.h"
#include <string.h>
#include <stdio.h>

/**
 * 生成硬件驱动的AethelID
 */
AethelID hda_generate_aethel_id(const char *category, const char *type,
                               const uint8_t *payload) {
    char id_type_buf[64];
    snprintf(id_type_buf, sizeof(id_type_buf), "Aethel/Driver/%s/%s", 
             category ? category : "generic", type ? type : "unknown");
    return aethel_id_generate(id_type_buf, payload, NULL);
}

/**
 * 初始化HDA头部
 */
void hda_header_init(AethelBinaryHeader *hdr) {
    if (!hdr) return;
    
    aethel_header_init(hdr, MAGIC_HDA);
    
    /* 设置HDA默认参数 */
    
    /* 在format_specific中设置设备类型ID (offset 0x70-0x73) */
    uint32_t *device_type = (uint32_t *)&hdr->format_specific[0];
    *device_type = 0x00000000;
    
    /* 硬件指纹 (offset 0x74-0x77) */
    uint32_t *hw_finger = (uint32_t *)&hdr->format_specific[4];
    *hw_finger = 0x00000000;
    
    /* SIP要求等级 (offset 0x78-0x7B) */
    uint32_t *sip_req = (uint32_t *)&hdr->format_specific[8];
    *sip_req = 0x00000000;
    
    /* 模式支持 (offset 0x7C-0x7D) - 默认支持两种模式 */
    uint16_t *mode_sup = (uint16_t *)&hdr->format_specific[12];
    *mode_sup = 0x0003;  /* Bit0=沙盒, Bit1=架构师 */
}

/**
 * 设置HDA设备信息
 */
void hda_header_set_device_info(AethelBinaryHeader *hdr,
                                uint32_t device_type_id,
                                uint32_t hw_fingerprint) {
    if (!hdr) return;
    
    uint32_t *dev_type = (uint32_t *)&hdr->format_specific[0];
    *dev_type = device_type_id;
    
    uint32_t *hw_finger = (uint32_t *)&hdr->format_specific[4];
    *hw_finger = hw_fingerprint;
}

/**
 * 设置SIP要求
 */
void hda_header_set_sip_requirement(AethelBinaryHeader *hdr, uint32_t sip_level) {
    if (!hdr) return;
    uint32_t *sip_req = (uint32_t *)&hdr->format_specific[8];
    *sip_req = sip_level;
}

/**
 * 设置模式支持
 */
void hda_header_set_mode_support(AethelBinaryHeader *hdr, uint16_t modes) {
    if (!hdr) return;
    uint16_t *mode_sup = (uint16_t *)&hdr->format_specific[12];
    *mode_sup = modes;
}

/**
 * 设置硬件契约入口
 */
void hda_header_set_contract_entry(AethelBinaryHeader *hdr, uint64_t entry) {
    if (!hdr) return;
    uint64_t *contract_ent = (uint64_t *)&hdr->extended_metadata[0];
    *contract_ent = entry;
}

/**
 * 设置硬件初始化入口
 */
void hda_header_set_init_entry(AethelBinaryHeader *hdr, uint64_t entry) {
    if (!hdr) return;
    uint64_t *init_ent = (uint64_t *)&hdr->extended_metadata[8];
    *init_ent = entry;
}

/**
 * 完整的HDA镜像生成
 */
int hda_generate_image(const char *output_file, 
                      const uint8_t *code, size_t code_size,
                      const uint8_t *mirror_data, size_t mirror_size,
                      const uint8_t *constant_data, size_t constant_size) {
    if (!output_file || !code || code_size == 0) {
        fprintf(stderr, "[HDA] Error: Invalid parameters\n");
        return -1;
    }
    
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "[HDA] Error: Cannot create output file '%s'\n", output_file);
        return -1;
    }
    
    /* 从输入数据直接构建 动作流 */
    if (!code || code_size == 0) {
        fprintf(stderr, "[HDA] Error: No code provided\n");
        return -1;
    }
    
    /* 构建HDA头部 */
    AethelBinaryHeader hdr;
    hda_header_init(&hdr);
    
    /* 计算各Zone的偏移和大小 */
    uint64_t current_offset = AETHEL_HEADER_SIZE;
    
    /* ActFlow Zone - 硬件控制行为 + 寄存器操作 */
    hdr.act_flow_offset = current_offset;
    hdr.act_flow_size = code_size;
    current_offset += code_size;
    
    /* MirrorState Zone - 硬件状态镜像 */
    if (mirror_data && mirror_size > 0) {
        hdr.mirror_state_offset = current_offset;
        hdr.mirror_state_size = mirror_size;
        current_offset += mirror_size;
    } else {
        hdr.mirror_state_offset = 0;
        hdr.mirror_state_size = 0;
    }
    
    /* ConstantTruth Zone - 硬件配置表、契约元数据 */
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
    payload[0] = 0x48;  /* 'H' */
    payload[1] = 0x44;  /* 'D' */
    payload[2] = 0x41;  /* 'A' */
    
    AethelID driver_id = hda_generate_aethel_id("hardware", "generic", payload);
    memcpy(hdr.aethel_id, driver_id.bytes, AETHEL_ID_SIZE);
    
    /* 计算并设置CRC */
    aethel_header_calculate_crc(&hdr);
    
    /* 写入头部 */
    if (aethel_header_write(out, &hdr) != 0) {
        fprintf(stderr, "[HDA] Error: Failed to write header\n");
        fclose(out);
        return -1;
    }
    
    /* 写入ActFlow段 - 硬件控制代码 */
    if (fwrite(code, 1, code_size, out) != code_size) {
        fprintf(stderr, "[HDA] Error: Failed to write ActFlow code\n");
        fclose(out);
        return -1;
    }
    
    /* 写入MirrorState段（若存在） */
    if (mirror_data && mirror_size > 0) {
        if (fwrite(mirror_data, 1, mirror_size, out) != mirror_size) {
            fprintf(stderr, "[HDA] Error: Failed to write MirrorState\n");
            fclose(out);
            return -1;
        }
    }
    
    /* 写入ConstantTruth段（若存在） */
    if (constant_data && constant_size > 0) {
        if (fwrite(constant_data, 1, constant_size, out) != constant_size) {
            fprintf(stderr, "[HDA] Error: Failed to write ConstantTruth\n");
            fclose(out);
            return -1;
        }
    }
    
    fclose(out);
    
    fprintf(stderr, "[HDA] ✓ Generated: %s (hardware driver archive)\n", output_file);
    fprintf(stderr, "      ActFlow Control: %zu bytes\n", code_size);
    fprintf(stderr, "      MirrorState: %zu bytes\n", mirror_size);
    fprintf(stderr, "      ConstantTruth: %zu bytes\n", constant_size);
    fprintf(stderr, "      Total Size: %zu bytes\n", current_offset);
    
    return 0;
}
