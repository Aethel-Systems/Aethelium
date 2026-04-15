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
 * AKI (Aethel Kernel Image) Generation Implementation
 * 
 * 工业级完整实现 - 严格按照《AethelOS 二进制及目录结构.txt》规范
 * 
 * 完整结构：
 * [256B Header] + [ActFlow] + [MirrorState] + [ConstantTruth]
 */
//错误说法：{
/**
 * 从AETB格式中提取纯x86-64机器码 -- 错误
 * 
 * AETB是编译器后端的中间格式，有256字节的头部。 -- 错误
 * 此函数将code section（从0x100偏移开始）提取出来。 -- 错误
 *  -- 错误
 * 用于：AKI/HDA/SRV等格式，这些格式需要纯机器码而非AETB格式。 -- 错误
 * 
 * 参数：
 *   aetb_data: AETB格式的二进制数据指针 -- 错误
 *   aetb_size: 数据大小 -- 错误
 *   code_out: 输出的机器码指针 -- 错误
 *   code_size_out: 输出的机器码大小 -- 错误
 * 
 * 返回：0 成功，-1 失败  -- 错误
 * }
 */
//正确说法：{
/*
 AETB就是AETB，AETB是给iya目录（应用包）中的运行文件准备的，严禁用于中间文件，AethelOS在底层也不存在中间文件，更没有这番理念 -- 正确
 }
*/

#include "aki.h"
#include "asm_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t version;
    uint16_t entry_count;
    uint32_t bytes_used;
    uint8_t kernel_id[32];
    uint64_t genesis_point;
    uint64_t sip_vector;
    uint64_t mode_affinity;
} AKITruthStaticHeader;

typedef struct __attribute__((packed)) {
    uint8_t aethel_id[32];
    uint64_t value;
    uint64_t span;
    uint32_t flags;
    char label[20];
} AKITruthStaticEntry;

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t version;
    uint16_t component_count;
    uint32_t bytes_used;
    uint64_t flags;
} AKIAethelALogicHeader;

typedef struct __attribute__((packed)) {
    uint8_t component_id[32];
    uint32_t role;
    uint32_t flags;
    uint64_t payload_offset;
    uint64_t payload_size;
    char label[16];
} AKIAethelALogicEntry;

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t version;
    uint16_t entry_count;
    uint32_t bytes_used;
    uint32_t reserved;
} AKINexusHeader;

typedef struct __attribute__((packed)) {
    uint8_t aethel_id[32];
    uint64_t target_offset;
    uint64_t target_size;
    uint32_t kind;
    uint32_t flags;
    char label[16];
} AKINexusEntry;

enum {
    AKI_TRUTH_FLAG_DICTIONARY = 1u << 0,
    AKI_TRUTH_FLAG_SECTION = 1u << 1,
    AKI_NEXUS_KIND_GENESIS = 1u,
    AKI_NEXUS_KIND_SECTION = 2u,
    AKI_NEXUS_KIND_ENGINE = 3u,
    AKI_ENGINE_ROLE_SOURCE = 1u,
    AKI_ENGINE_ROLE_SIP = 2u,
    AKI_ENGINE_ROLE_MORPH = 3u
};

static uint64_t aki_align16_u64(uint64_t value) {
    return (value + 15ull) & ~15ull;
}

static void aki_fill_tag_payload(uint8_t payload[12], const char *tag) {
    memset(payload, 0, 12);
    if (!tag) {
        return;
    }

    size_t tag_len = strlen(tag);
    if (tag_len > 12) {
        tag_len = 12;
    }
    memcpy(payload, tag, tag_len);
}

static AethelID aki_generate_named_id(const char *id_type, const char *tag) {
    uint8_t payload[12];
    aki_fill_tag_payload(payload, tag);
    return aethel_id_generate(id_type, payload, NULL);
}

/**
 * [修复]: 执行 x86_64 规范地址检查与扩展 (Canonical Address Sign-Extension)
 * 如果地址的第47位为1，高16位必须全为1；否则高16位必须全为0。
 */
static inline uint64_t aki_canonicalize(uint64_t addr) {
    if (addr & (1ULL << 47)) {
        return addr | 0xFFFF000000000000ULL; /* 高半核符号扩展 */
    }
    return addr & 0x0000FFFFFFFFFFFFULL; /* 低半核安全截断 */
}

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
    
    /* --- 修改处 2: 应用符号扩展修复 --- */
    uint64_t safe_addr = aki_canonicalize(genesis_point);
    
    hdr->genesis_point = safe_addr;
    uint64_t *genesis_ext = (uint64_t *)&hdr->format_specific[8];
    *genesis_ext = safe_addr;
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
 * - C层生成原始素材并扩充数据量 (ActFlow, TruthStatic, AethelA Logic buffers)
 * - 汇编层负责完整结构编织 (Structure Weaving) - 排版、对齐、CRC计算、文件I/O
 * 
 * 严禁简化 - 必须生成足够量级的数据，确保[Full Structure]完整可见
 */
/* --- 修改处 3: 增加 genesis_point 参数 --- */
int aki_generate_image(const char *output_file, 
                      const uint8_t *code, size_t code_size,
                      const uint8_t *mirror_data, size_t mirror_size,
                      const uint8_t *constant_data, size_t constant_size,
                      uint64_t genesis_point) {
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
       扩充并实体化 zone 数据
       - ActFlow: 最小 1KB 的行为流
       - TruthStatic: 带字典头和预设 AethelID 条目
       - AethelA Logic: 带组件清单和组件 manifest
       - IdentityNexus: 带真实记录的强绑定映射表
       =====================================================================*/

    AethelID act_flow_id = aki_generate_named_id("Aethel/Kernel/ActFlow", "ACTFLOW");
    AethelID truth_static_id = aki_generate_named_id("Aethel/Kernel/TruthStatic", "TRUTH");
    AethelID aethela_logic_id = aki_generate_named_id("Aethel/Kernel/AethelA", "AETHELA");
    AethelID nexus_id = aki_generate_named_id("Aethel/Kernel/Nexus", "NEXUS");
    AethelID source_id = aki_generate_named_id("Aethel/Kernel/SourceInterpretor", "SOURCE");
    AethelID sip_weaver_id = aki_generate_named_id("Aethel/Kernel/SipWeaver", "SIP");
    AethelID target_morpher_id = aki_generate_named_id("Aethel/Kernel/TargetMorpher", "MORPH");

    const char source_manifest[] =
        "SourceInterpretor: parse live Aethelium scripts for kernel shadow weaving.\n";
    const char sip_manifest[] =
        "SipWeaver: inject or remove safety checks based on sandbox or architect mode.\n";
    const char morph_manifest[] =
        "TargetMorpher: retarget hot ActFlow for the active ISA and machine contract.\n";

    /* ActFlow Zone - 16字节对齐，至少1KB */
    size_t expanded_code_size = (code_size < 1024) ? 1024 : code_size;
    uint8_t *expanded_code = (uint8_t *)malloc(expanded_code_size);
    if (!expanded_code) {
        fprintf(stderr, "[AKI] Error: Failed to allocate expanded ActFlow buffer\n");
        return -1;
    }
    memcpy(expanded_code, code, code_size);
    memset(expanded_code + code_size, 0x90, expanded_code_size - code_size);

    const size_t truth_entry_count = 4;
    size_t truth_used_size = sizeof(AKITruthStaticHeader) +
                             truth_entry_count * sizeof(AKITruthStaticEntry) +
                             mirror_size;
    size_t expanded_mirror_size = (truth_used_size < 512) ? 512 : truth_used_size;
    uint8_t *expanded_mirror = (uint8_t *)calloc(expanded_mirror_size, 1);
    if (!expanded_mirror) {
        fprintf(stderr, "[AKI] Error: Failed to allocate expanded TruthStatic buffer\n");
        free(expanded_code);
        return -1;
    }

    AKITruthStaticHeader *truth_header = (AKITruthStaticHeader *)expanded_mirror;
    memcpy(truth_header->magic, "TRTH", 4);
    truth_header->version = 1;
    truth_header->entry_count = truth_entry_count;
    truth_header->bytes_used = (uint32_t)truth_used_size;
    memcpy(truth_header->kernel_id, kernel_id_copy, sizeof(truth_header->kernel_id));
    truth_header->genesis_point = aki_canonicalize(genesis_point);
    truth_header->sip_vector = 0;
    truth_header->mode_affinity = 1;

    AKITruthStaticEntry *truth_entries =
        (AKITruthStaticEntry *)(expanded_mirror + sizeof(AKITruthStaticHeader));
    memcpy(truth_entries[0].aethel_id, kernel_id.bytes, 32);
    truth_entries[0].value = aki_canonicalize(genesis_point);
    truth_entries[0].span = expanded_code_size;
    truth_entries[0].flags = AKI_TRUTH_FLAG_DICTIONARY;
    memcpy(truth_entries[0].label, "kernel/origin", 14);

    memcpy(truth_entries[1].aethel_id, act_flow_id.bytes, 32);
    truth_entries[1].value = 0;
    truth_entries[1].span = expanded_code_size;
    truth_entries[1].flags = AKI_TRUTH_FLAG_SECTION;
    memcpy(truth_entries[1].label, "act/flow", 8);

    memcpy(truth_entries[2].aethel_id, truth_static_id.bytes, 32);
    truth_entries[2].value = 0;
    truth_entries[2].span = expanded_mirror_size;
    truth_entries[2].flags = AKI_TRUTH_FLAG_SECTION;
    memcpy(truth_entries[2].label, "truth/static", 12);

    memcpy(truth_entries[3].aethel_id, aethela_logic_id.bytes, 32);
    truth_entries[3].value = 0;
    truth_entries[3].span = 0;
    truth_entries[3].flags = AKI_TRUTH_FLAG_SECTION;
    memcpy(truth_entries[3].label, "aethela/logic", 13);

    if (mirror_data && mirror_size > 0) {
        memcpy(expanded_mirror + sizeof(AKITruthStaticHeader) +
                   truth_entry_count * sizeof(AKITruthStaticEntry),
               mirror_data,
               mirror_size);
    }

    const size_t aethela_component_count = 3;
    size_t source_size = sizeof(source_manifest) - 1;
    size_t sip_size = sizeof(sip_manifest) - 1;
    size_t morph_size = sizeof(morph_manifest) - 1;
    size_t aethela_manifest_base = sizeof(AKIAethelALogicHeader) +
                                   aethela_component_count * sizeof(AKIAethelALogicEntry);
    size_t aethela_used_size = aethela_manifest_base + source_size + sip_size +
                               morph_size + constant_size;
    size_t expanded_constant_size = (aethela_used_size < 2048) ? 2048 : aethela_used_size;
    uint8_t *expanded_constant = (uint8_t *)calloc(expanded_constant_size, 1);
    if (!expanded_constant) {
        fprintf(stderr, "[AKI] Error: Failed to allocate expanded AethelA Logic buffer\n");
        free(expanded_code);
        free(expanded_mirror);
        return -1;
    }

    AKIAethelALogicHeader *aethela_header = (AKIAethelALogicHeader *)expanded_constant;
    memcpy(aethela_header->magic, "AETH", 4);
    aethela_header->version = 1;
    aethela_header->component_count = aethela_component_count;
    aethela_header->bytes_used = (uint32_t)aethela_used_size;
    aethela_header->flags = 0x1;

    AKIAethelALogicEntry *aethela_entries =
        (AKIAethelALogicEntry *)(expanded_constant + sizeof(AKIAethelALogicHeader));

    size_t source_offset = aethela_manifest_base;
    size_t sip_offset = source_offset + source_size;
    size_t morph_offset = sip_offset + sip_size;
    size_t user_payload_offset = morph_offset + morph_size;

    memcpy(aethela_entries[0].component_id, source_id.bytes, 32);
    aethela_entries[0].role = AKI_ENGINE_ROLE_SOURCE;
    aethela_entries[0].flags = 0x1;
    aethela_entries[0].payload_offset = source_offset;
    aethela_entries[0].payload_size = source_size;
    memcpy(aethela_entries[0].label, "source", 6);

    memcpy(aethela_entries[1].component_id, sip_weaver_id.bytes, 32);
    aethela_entries[1].role = AKI_ENGINE_ROLE_SIP;
    aethela_entries[1].flags = 0x1;
    aethela_entries[1].payload_offset = sip_offset;
    aethela_entries[1].payload_size = sip_size;
    memcpy(aethela_entries[1].label, "sip/weaver", 10);

    memcpy(aethela_entries[2].component_id, target_morpher_id.bytes, 32);
    aethela_entries[2].role = AKI_ENGINE_ROLE_MORPH;
    aethela_entries[2].flags = 0x1;
    aethela_entries[2].payload_offset = morph_offset;
    aethela_entries[2].payload_size = morph_size;
    memcpy(aethela_entries[2].label, "target/morph", 12);

    memcpy(expanded_constant + source_offset, source_manifest, source_size);
    memcpy(expanded_constant + sip_offset, sip_manifest, sip_size);
    memcpy(expanded_constant + morph_offset, morph_manifest, morph_size);
    if (constant_data && constant_size > 0) {
        memcpy(expanded_constant + user_payload_offset, constant_data, constant_size);
    }

    uint64_t act_flow_offset = AETHEL_HEADER_SIZE;
    uint64_t truth_static_offset = aki_align16_u64(act_flow_offset + expanded_code_size);
    uint64_t aethela_logic_offset = aki_align16_u64(truth_static_offset + expanded_mirror_size);

    const size_t nexus_entry_count = 8;
    size_t nexus_size = sizeof(AKINexusHeader) + nexus_entry_count * sizeof(AKINexusEntry);
    uint8_t *nexus_buffer = (uint8_t *)calloc(nexus_size, 1);
    if (!nexus_buffer) {
        fprintf(stderr, "[AKI] Error: Failed to allocate IdentityNexus buffer\n");
        free(expanded_code);
        free(expanded_mirror);
        free(expanded_constant);
        return -1;
    }
    uint64_t nexus_offset = aki_align16_u64(aethela_logic_offset + expanded_constant_size);

    AKINexusHeader *nexus_header = (AKINexusHeader *)nexus_buffer;
    memcpy(nexus_header->magic, "NXUS", 4);
    nexus_header->version = 1;
    nexus_header->entry_count = nexus_entry_count;
    nexus_header->bytes_used = (uint32_t)nexus_size;

    AKINexusEntry *nexus_entries =
        (AKINexusEntry *)(nexus_buffer + sizeof(AKINexusHeader));

    memcpy(nexus_entries[0].aethel_id, kernel_id.bytes, 32);
    nexus_entries[0].target_offset = aki_canonicalize(genesis_point);
    nexus_entries[0].target_size = expanded_code_size;
    nexus_entries[0].kind = AKI_NEXUS_KIND_GENESIS;
    memcpy(nexus_entries[0].label, "kernel", 6);

    memcpy(nexus_entries[1].aethel_id, act_flow_id.bytes, 32);
    nexus_entries[1].target_offset = act_flow_offset;
    nexus_entries[1].target_size = expanded_code_size;
    nexus_entries[1].kind = AKI_NEXUS_KIND_SECTION;
    memcpy(nexus_entries[1].label, "act/flow", 8);

    memcpy(nexus_entries[2].aethel_id, truth_static_id.bytes, 32);
    nexus_entries[2].target_offset = truth_static_offset;
    nexus_entries[2].target_size = expanded_mirror_size;
    nexus_entries[2].kind = AKI_NEXUS_KIND_SECTION;
    memcpy(nexus_entries[2].label, "truth", 5);

    memcpy(nexus_entries[3].aethel_id, aethela_logic_id.bytes, 32);
    nexus_entries[3].target_offset = aethela_logic_offset;
    nexus_entries[3].target_size = expanded_constant_size;
    nexus_entries[3].kind = AKI_NEXUS_KIND_SECTION;
    memcpy(nexus_entries[3].label, "aethela", 7);

    memcpy(nexus_entries[4].aethel_id, nexus_id.bytes, 32);
    nexus_entries[4].target_offset = nexus_offset;
    nexus_entries[4].target_size = nexus_size;
    nexus_entries[4].kind = AKI_NEXUS_KIND_SECTION;
    memcpy(nexus_entries[4].label, "nexus", 5);

    memcpy(nexus_entries[5].aethel_id, source_id.bytes, 32);
    nexus_entries[5].target_offset = aethela_logic_offset + source_offset;
    nexus_entries[5].target_size = source_size;
    nexus_entries[5].kind = AKI_NEXUS_KIND_ENGINE;
    memcpy(nexus_entries[5].label, "source", 6);

    memcpy(nexus_entries[6].aethel_id, sip_weaver_id.bytes, 32);
    nexus_entries[6].target_offset = aethela_logic_offset + sip_offset;
    nexus_entries[6].target_size = sip_size;
    nexus_entries[6].kind = AKI_NEXUS_KIND_ENGINE;
    memcpy(nexus_entries[6].label, "sip", 3);

    memcpy(nexus_entries[7].aethel_id, target_morpher_id.bytes, 32);
    nexus_entries[7].target_offset = aethela_logic_offset + morph_offset;
    nexus_entries[7].target_size = morph_size;
    nexus_entries[7].kind = AKI_NEXUS_KIND_ENGINE;
    memcpy(nexus_entries[7].label, "morph", 5);

    truth_entries[3].value = aethela_logic_offset;
    truth_entries[3].span = expanded_constant_size;
    
    /* =========================================================================
       准备汇编层的输入结构 - 完整[Full Structure]编织
       =====================================================================*/
    
    AKI_Weave_Input weave_input;
    memset(&weave_input, 0, sizeof(weave_input));
    weave_input.output_filename = output_file;
    weave_input.act_flow_buffer = expanded_code;
    weave_input.act_flow_size = expanded_code_size;
    weave_input.mirror_state_buffer = expanded_mirror;
    weave_input.mirror_state_size = expanded_mirror_size;
    weave_input.constant_truth_buffer = expanded_constant;
    weave_input.constant_truth_size = expanded_constant_size;
    /* --- 修改处 4: 移除硬编码 0x100000，改用传入参数并进行符号扩展 --- */
    weave_input.genesis_point = aki_canonicalize(genesis_point);
    weave_input.aethel_id = kernel_id_copy;
    weave_input.identity_table_ptr = nexus_buffer;
    weave_input.identity_count = nexus_size;

    /* 直接 AKI 编译路径没有 LET Gene-Table；必须显式置零这些尾部字段，
       否则汇编编织层会读到未初始化栈数据，污染 sip/vector、mode/affinity
       和 nexus/point 一带的头部内容。 */
    weave_input.mode_affinity = 1;   /* 架构师优先 */
    weave_input.machine_bits = 64;
    
    /* 调用汇编层的完整结构编织函数 */
    /* 汇编函数负责：
       1. 构建256字节header (魔数、版本、AethelID、Genesis Point等)
       2. 计算ActFlow、TruthStatic、AethelA Logic的zone offsets
       3. 应用必要的区段对齐
       4. 计算和填写CRC32校验
       5. 文件I/O - 写header + 所有zones
     */
    int result = weave_aki_structure(&weave_input);
    
    /* 清理临时缓冲 */
    free(expanded_code);
    free(expanded_mirror);
    free(expanded_constant);
    free(nexus_buffer);
    free(kernel_id_copy);  /* Free the persistent kernel_id copy */
    
    if (result == 0) {
        fprintf(stderr, "[AKI] ✓ Generated: %s (完整 [Full Structure])\n", output_file);
        fprintf(stderr, "      Header: 256 bytes  \n");
        fprintf(stderr, "      ActFlow Zone: %zu bytes (16B aligned)\n", expanded_code_size);
        fprintf(stderr, "      TruthStatic Zone: %zu bytes (16B aligned)\n", expanded_mirror_size);
        fprintf(stderr, "      AethelA Logic Zone: %zu bytes\n", expanded_constant_size);
        fprintf(stderr, "      IdentityNexus: %zu bytes, %zu entries\n", nexus_size, nexus_entry_count);
        fprintf(stderr, "      Total [Full Structure] size with header: %zu bytes\n",
                (size_t)(nexus_offset + nexus_size));
    } else {
        fprintf(stderr, "[AKI] ✗ Failed to generate AKI with [Full Structure]\n");
    }
    
    return result;
}
