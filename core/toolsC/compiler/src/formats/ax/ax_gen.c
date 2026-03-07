/*
 * AX (Aethel Index) B+Tree Generation Implementation
 * 
 * 工业级完整实现 - 严格按照《AethelOS 二进制及目录结构.txt》规范
 * 
 * AX是B+树索引格式，用于替代所有数据库
 * 
 * 完整结构：
 * [256B Header] + [B+Tree Nodes] + [RuntimeState] + [Metadata]
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

#include "ax.h"
#include <string.h>
#include <stdio.h>

/**
 * 生成索引文件的AethelID
 */
AethelID ax_generate_aethel_id(const char *index_type, const uint8_t *payload) {
    char id_type_buf[64];
    snprintf(id_type_buf, sizeof(id_type_buf), "Aethel/Index/%s",
             index_type ? index_type : "generic");
    return aethel_id_generate(id_type_buf, payload, NULL);
}

/**
 * 初始化AX头部
 */
void ax_header_init(AethelBinaryHeader *hdr) {
    if (!hdr) return;
    
    aethel_header_init(hdr, MAGIC_AX);
    
    /* 设置AX默认参数 */
    
    /* 在format_specific中设置B+树阶数 (offset 0x70-0x73) */
    uint32_t *tree_ord = (uint32_t *)&hdr->format_specific[0];
    *tree_ord = 32;  /* 默认阶数32 */
    
    /* 键大小 (offset 0x74-0x77) */
    uint32_t *k_size = (uint32_t *)&hdr->format_specific[4];
    *k_size = 16;    /* 默认键大小16字节 */
    
    /* 值大小 (offset 0x78-0x7B) */
    uint32_t *v_size = (uint32_t *)&hdr->format_specific[8];
    *v_size = 64;    /* 默认值大小64字节 */
    
    /* 总条目数 (offset 0x7C-0x7F) */
    uint32_t *entries = (uint32_t *)&hdr->format_specific[12];
    *entries = 0x00000000;
}

/**
 * 设置AX B+树配置
 */
void ax_header_set_tree_config(AethelBinaryHeader *hdr,
                                uint32_t tree_order,
                                uint32_t key_size,
                                uint32_t value_size,
                                uint32_t total_entries) {
    if (!hdr) return;
    
    uint32_t *order = (uint32_t *)&hdr->format_specific[0];
    *order = tree_order;
    
    uint32_t *k_size = (uint32_t *)&hdr->format_specific[4];
    *k_size = key_size;
    
    uint32_t *v_size = (uint32_t *)&hdr->format_specific[8];
    *v_size = value_size;
    
    uint32_t *entries = (uint32_t *)&hdr->format_specific[12];
    *entries = total_entries;
}

/**
 * 设置AX树元数据
 */
void ax_header_set_tree_metadata(AethelBinaryHeader *hdr,
                                  uint32_t tree_height,
                                  uint32_t root_offset) {
    if (!hdr) return;
    
    uint32_t *height = (uint32_t *)&hdr->extended_metadata[0];
    *height = tree_height;
    
    uint32_t *root = (uint32_t *)&hdr->extended_metadata[4];
    *root = root_offset;
}

/**
 * 完整的AX镜像生成
 */
int ax_generate_image(const char *output_file, 
                     const uint8_t *index_data, size_t index_size,
                     const uint8_t *state_data, size_t state_size,
                     const uint8_t *metadata_data, size_t metadata_size) {
    if (!output_file || !index_data || index_size == 0) {
        fprintf(stderr, "[AX] Error: Invalid parameters\n");
        return -1;
    }
    
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "[AX] Error: Cannot create output file '%s'\n", output_file);
        return -1;
    }
    
    /* 构建AX头部 */
    AethelBinaryHeader hdr;
    ax_header_init(&hdr);
    
    /* 计算各Zone的偏移和大小 */
    uint64_t current_offset = AETHEL_HEADER_SIZE;
    
    /* ActFlow Zone - B+树节点 */
    hdr.act_flow_offset = current_offset;
    hdr.act_flow_size = index_size;
    current_offset += index_size;
    
    /* MirrorState Zone - 索引运行时状态 */
    if (state_data && state_size > 0) {
        hdr.mirror_state_offset = current_offset;
        hdr.mirror_state_size = state_size;
        current_offset += state_size;
    } else {
        hdr.mirror_state_offset = 0;
        hdr.mirror_state_size = 0;
    }
    
    /* ConstantTruth Zone - 树元数据 */
    if (metadata_data && metadata_size > 0) {
        hdr.constant_truth_offset = current_offset;
        hdr.constant_truth_size = metadata_size;
        current_offset += metadata_size;
    } else {
        hdr.constant_truth_offset = 0;
        hdr.constant_truth_size = 0;
    }
    
    /* 生成并设置AethelID */
    uint8_t payload[12];
    memset(payload, 0, 12);
    payload[0] = 0x42;  /* 'B' */
    payload[1] = 0x2B;  /* '+' */
    payload[2] = 0x54;  /* 'T' */
    
    AethelID index_id = ax_generate_aethel_id("bplus", payload);
    memcpy(hdr.aethel_id, index_id.bytes, AETHEL_ID_SIZE);
    
    /* 计算并设置CRC */
    aethel_header_calculate_crc(&hdr);
    
    /* 写入头部 */
    if (aethel_header_write(out, &hdr) != 0) {
        fprintf(stderr, "[AX] Error: Failed to write header\n");
        fclose(out);
        return -1;
    }
    
    /* 写入ActFlow段 - B+树数据 */
    if (fwrite(index_data, 1, index_size, out) != index_size) {
        fprintf(stderr, "[AX] Error: Failed to write B+tree data\n");
        fclose(out);
        return -1;
    }
    
    /* 写入MirrorState段（若存在） */
    if (state_data && state_size > 0) {
        if (fwrite(state_data, 1, state_size, out) != state_size) {
            fprintf(stderr, "[AX] Error: Failed to write runtime state\n");
            fclose(out);
            return -1;
        }
    }
    
    /* 写入ConstantTruth段（若存在） */
    if (metadata_data && metadata_size > 0) {
        if (fwrite(metadata_data, 1, metadata_size, out) != metadata_size) {
            fprintf(stderr, "[AX] Error: Failed to write metadata\n");
            fclose(out);
            return -1;
        }
    }
    
    fclose(out);
    
    fprintf(stderr, "[AX] ✓ Generated: %s (Aethel Index - B+tree)\n", output_file);
    fprintf(stderr, "     B+tree Data: %zu bytes\n", index_size);
    fprintf(stderr, "     Runtime State: %zu bytes\n", state_size);
    fprintf(stderr, "     Metadata: %zu bytes\n", metadata_size);
    fprintf(stderr, "     Total Size: %zu bytes\n", current_offset);
    
    return 0;
}
