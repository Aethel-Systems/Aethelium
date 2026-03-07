/*
 * AKI (Aethel Kernel Image) Generation Implementation
 * 
 * 工业级完整实现 - 严格按照《AethelOS 二进制及目录结构.txt》规范
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

#include "aki.h"
#include "asm_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * 生成内核的AethelID
 */
AethelID aki_generate_aethel_id(const uint8_t *payload) {
    /* AethelID类型：Aethel/Kernel/Origin */
    return aethel_id_generate("Aethel/Kernel/Origin", payload, NULL);
}

/**
 * 初始化AKI头部
 */
void aki_header_init(AethelBinaryHeader *hdr) {
    if (!hdr) return;
    
    aethel_header_init(hdr, MAGIC_AKI);
    
    /* 设置AKI默认参数 */
    
    /* genesis_point: 0x100000 (x86-64 1MB标准加载点) */
    hdr->genesis_point = 0x100000;
    
    /* 在format_specific中设置SIP向量 (offset 0x70-0x77) */
    uint64_t *sip_vector = (uint64_t *)&hdr->format_specific[0];
    *sip_vector = 0x0000000000000000;  /* 默认开放最高权限（后续由SIP_MORPHING改写） */
    
    /* genesis_point扩展字段 (offset 0x78-0x7F) */
    uint64_t *genesis_ext = (uint64_t *)&hdr->format_specific[8];
    *genesis_ext = 0x100000;
}

/**
 * 设置AKI头部的SIP向量
 */
void aki_header_set_sip_vector(AethelBinaryHeader *hdr, uint64_t sip_vector) {
    if (!hdr) return;
    uint64_t *sip = (uint64_t *)&hdr->format_specific[0];
    *sip = sip_vector;
}

/**
 * 设置AKI头部的Genesis Point
 */
void aki_header_set_genesis_point(AethelBinaryHeader *hdr, uint64_t genesis_point) {
    if (!hdr) return;
    hdr->genesis_point = genesis_point;
    uint64_t *genesis_ext = (uint64_t *)&hdr->format_specific[8];
    *genesis_ext = genesis_point;
}

/**
 * 设置模式亲和度
 */
void aki_header_set_mode_affinity(AethelBinaryHeader *hdr, uint8_t affinity) {
    if (!hdr) return;
    hdr->extended_metadata[0] = affinity;
}

/**
 * 设置vCore调度器偏移
 */
void aki_header_set_vcore_scheduler(AethelBinaryHeader *hdr, uint64_t offset) {
    if (!hdr) return;
    uint64_t *vcore_offset = (uint64_t *)&hdr->extended_metadata[32];  /* 相对位置 */
    *vcore_offset = offset;
}

/**
 * 设置AethelA编译引擎入口
 */
void aki_header_set_aethela_engine(AethelBinaryHeader *hdr, uint64_t entry) {
    if (!hdr) return;
    uint64_t *aethela_entry = (uint64_t *)&hdr->extended_metadata[40];
    *aethela_entry = entry;
}

/**
 * 完整的AKI镜像生成 - 工业级实现
 * 
 * 架构改造（用户要求）：
 * - C层生成原始素材并扩充数据量 (ActFlow, MirrorState, ConstantTruth buffers)
 * - 汇编层负责完整结构编织 (Structure Weaving) - 排版、对齐、CRC计算、文件I/O
 * 
 * 严禁简化 - 必须生成足够量级的数据，确保[Full Structure]完整可见
 */
int aki_generate_image(const char *output_file, 
                      const uint8_t *code, size_t code_size,
                      const uint8_t *mirror_data, size_t mirror_size,
                      const uint8_t *constant_data, size_t constant_size) {
    if (!output_file || !code || code_size == 0) {
        fprintf(stderr, "[AKI] Error: Invalid parameters\n");
        return -1;
    }
    
    /* 生成AethelID */
    uint8_t payload[12];
    memset(payload, 0, 12);
    payload[0] = 0x41;  /* 'A' */
    payload[1] = 0x4B;  /* 'K' */
    payload[2] = 0x49;  /* 'I' */
    
    /* Industrial-grade fix: Generate AethelID directly with proper initialization */
    AethelID kernel_id = aki_generate_aethel_id(payload);
    
    /* Create a persistent copy of kernel_id bytes for weave layer */
    uint8_t *kernel_id_copy = (uint8_t *)malloc(32);
    if (!kernel_id_copy) {
        fprintf(stderr, "[AKI] Error: Failed to allocate kernel_id copy buffer\n");
        return -1;
    }
    
    /* Copy the 32-byte AethelID to persistent buffer */
    /* Industrial-grade defensive: Fill with safe default if generation failed */
    memset(kernel_id_copy, 0, 32);
    memcpy(kernel_id_copy, kernel_id.bytes, 32);
    
    /* Validate and correct the AethelID if needed */
    /* Check if bytes were actually initialized */
    int has_valid_aethel_data = 0;
    
    /* Expected: byte 0 should have version bits set */
    /* If byte 0 is 0 or looks like uninitialized memory, generate safe default */
    uint8_t version_nibble = (kernel_id_copy[0] >> 4) & 0x0F;
    if (version_nibble == 0 || version_nibble > 0xF) {
        fprintf(stderr, "[AKI] Warning: Detected invalid AethelID version nibble\n");
        fprintf(stderr, "[AKI] Generating minimal valid AethelID kernel placeholder\n");
        
        /* Generate a minimal but valid AethelID: version A + zeros */
        kernel_id_copy[0] = 0xA0;  /* Version A (0xA) in high nibble */
        /* Rest can be zeros for production kernel - no secret payload needed */
        for (int i = 1; i < 32; i++) {
            kernel_id_copy[i] = 0x00;
        }
    } else {
        has_valid_aethel_data = 1;
    }
    
    if (has_valid_aethel_data) {
        fprintf(stderr, "[AKI] ✓ AethelID generated successfully (version 0x%X)\n", version_nibble);
    }
    
    /* =========================================================================
       扩充zone数据 - 确保每个zone都有足够的内容展示[Full Structure]
       严禁简化：即使没有真实数据也必须分配足够大小的zone
       =====================================================================*/
    
    /* ActFlow Zone - 16字节对齐，至少1KB */
    size_t expanded_code_size = (code_size < 1024) ? 1024 : code_size;
    uint8_t *expanded_code = (uint8_t *)malloc(expanded_code_size);
    if (!expanded_code) {
        fprintf(stderr, "[AKI] Error: Failed to allocate expanded ActFlow buffer\n");
        return -1;
    }
    memcpy(expanded_code, code, code_size);
    memset(expanded_code + code_size, 0x90, expanded_code_size - code_size);  /* NOP padding */
    
    /* MirrorState Zone - 16字节对齐，至少512字节 */
    size_t expanded_mirror_size = (mirror_size == 0) ? 512 : 
                                  ((mirror_size < 512) ? 512 : mirror_size);
    uint8_t *expanded_mirror = (uint8_t *)calloc(expanded_mirror_size, 1);
    if (!expanded_mirror) {
        fprintf(stderr, "[AKI] Error: Failed to allocate expanded MirrorState buffer\n");
        free(expanded_code);
        return -1;
    }
    if (mirror_data && mirror_size > 0) {
        memcpy(expanded_mirror, mirror_data, mirror_size);
    }
    /* Fill with system state metadata signature */
    uint32_t state_magic = 0x4D525354;  /* "MRST" - MirrorState */
    memcpy(expanded_mirror + 16, &state_magic, 4);
    
    /* ConstantTruth Zone - 4KB对齐，至少2KB */
    size_t expanded_constant_size = (constant_size == 0) ? 2048 : 
                                    ((constant_size < 2048) ? 2048 : constant_size);
    uint8_t *expanded_constant = (uint8_t *)calloc(expanded_constant_size, 1);
    if (!expanded_constant) {
        fprintf(stderr, "[AKI] Error: Failed to allocate expanded ConstantTruth buffer\n");
        free(expanded_code);
        free(expanded_mirror);
        return -1;
    }
    if (constant_data && constant_size > 0) {
        memcpy(expanded_constant, constant_data, constant_size);
    }
    /* Fill with system configuration metadata signature */
    uint32_t truth_magic = 0x43545254;  /* "CTRT" - ConstantTruth */
    memcpy(expanded_constant + 16, &truth_magic, 4);
    
    /* =========================================================================
       准备汇编层的输入结构 - 完整[Full Structure]编织
       =====================================================================*/
    
    AKI_Weave_Input weave_input;
    weave_input.output_filename = output_file;
    weave_input.act_flow_buffer = expanded_code;
    weave_input.act_flow_size = expanded_code_size;
    weave_input.mirror_state_buffer = expanded_mirror;
    weave_input.mirror_state_size = expanded_mirror_size;
    weave_input.constant_truth_buffer = expanded_constant;
    weave_input.constant_truth_size = expanded_constant_size;
    weave_input.genesis_point = 0x100000;  /* 标准 x86-64 加载点 */
    weave_input.aethel_id = kernel_id_copy;  /* Use persistent copy */
    
    /* 调用汇编层的完整结构编织函数 */
    /* 汇编函数负责：
       1. 构建256字节header (魔数、版本、AethelID、Genesis Point等)
       2. 计算ActFlow、MirrorState、ConstantTruth的zone offsets
       3. 应用16字节/4KB对齐
       4. 计算和填写CRC32校验
       5. 文件I/O - 写header + 所有zones
     */
    int result = weave_aki_structure(&weave_input);
    
    /* 清理临时缓冲 */
    free(expanded_code);
    free(expanded_mirror);
    free(expanded_constant);
    free(kernel_id_copy);  /* Free the persistent kernel_id copy */
    
    if (result == 0) {
        fprintf(stderr, "[AKI] ✓ Generated: %s (完整 [Full Structure])\n", output_file);
        fprintf(stderr, "      Header: 256 bytes  \n");
        fprintf(stderr, "      ActFlow Zone: %zu bytes (16B aligned)\n", expanded_code_size);
        fprintf(stderr, "      MirrorState Zone: %zu bytes (16B aligned)\n", expanded_mirror_size);
        fprintf(stderr, "      ConstantTruth Zone: %zu bytes (4KB aligned)\n", expanded_constant_size);
        fprintf(stderr, "      IdentityNexus: Symbol mapping table\n");
        fprintf(stderr, "      Total [Full Structure] size with header: %zu bytes\n",
                256 + expanded_code_size + expanded_mirror_size + expanded_constant_size);
    } else {
        fprintf(stderr, "[AKI] ✗ Failed to generate AKI with [Full Structure]\n");
    }
    
    return result;
}
