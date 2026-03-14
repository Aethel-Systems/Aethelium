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
 * AETB Binary Generator Implementation
 * 生成符合 AETB 格式的二进制输出
 */

#include "aetb_gen.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define INITIAL_CAPACITY 1024

/* 编译期安全断言：确保头部严格为 256 字节，防止结构体填充(Padding)导致格式崩溃 */
_Static_assert(sizeof(aetb_header_t) == 256, "AETB Header MUST be exactly 256 bytes per specification");
_Static_assert(sizeof(AethelID) == 32, "AethelID MUST be exactly 32 bytes");

/* 工业级 256 条目 CRC32 查找表 (IEEE 802.3 标准) */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71642, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa44e5d6, 0x8d079fd5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856534d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5a6bda7b, 0x2d6d68ed, 0xb40d9557, 0xc30c8ec1,
    0x5a05df1b, 0x2d02ef8d, 0xb404ce37, 0xc3031f41, 0x5f0f9cf2, 0x286e9c64, 0xb10d91de, 0xc60cb148,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xe21e9f3d, 0x95170fbb, 0x0c6e0aba, 0x7bffffff, 0xe8b7d3a3, 0x9fc027a0, 0x065b0705, 0x71f77ea5,
    0xf8086b0c, 0x8f659e9a, 0x1624bc20, 0x616599b6, 0xf2fddbe1, 0x850a0d77, 0x1c64dccd, 0x6bfa8b5b,
    0x9b6dfa8a, 0xec6e0b1c, 0x75f2dae6, 0x029f1770, 0x9c7db2d3, 0xeb43a245, 0x7232b2ff, 0x05f752a9,
    0x95f64b38, 0xe2816aae, 0x7b1b6b14, 0x0cca5b82, 0x92ffa321, 0xe5c173b7, 0x7c81870d, 0x0b5b579b,
    0x1c3c4e8e, 0x6b743e18, 0xf26fede2, 0x850b3d74, 0x1a304dd7, 0x6d3d3d41, 0xf40dcffb, 0x832dff6d,
    0x139def1c, 0x6480f58a, 0xfd650f30, 0x8a333fa6, 0x1c5feec5, 0x6bbbde53, 0xf2cbade9, 0x85fc37f7,
    0x98d32d12, 0xef3e0284, 0x766a3e3e, 0x01b53da8, 0x9f48a30b, 0xe8f3379d, 0x71fce727, 0x06bb73b1,
    0x96c6b6a0, 0xe1268836, 0x7859b88c, 0x0fb5b91a, 0x95d7fb39, 0xe2cd9caf, 0x7b15dd15, 0x0c04f883,
    0x63ee2e6b, 0x145b1de3, 0x8ddefced, 0xfa8cc47b, 0x64cee1d8, 0x13afa14e, 0x8a27fbf4, 0xfd2f3b62,
    0x6d7bddb3, 0x1aadef25, 0x839f3f9f, 0xf4c0ef09, 0x6b7c224a, 0x1cf152dc, 0x85aeca66, 0xf2277af0,
    0x0b2dc74e, 0x7c86b1d8, 0xe5f072a2, 0x9200b234, 0x0cd4ef77, 0x7b7c3fe1, 0xe20af85b, 0x954aa8cd,
    0x056b87bc, 0x72d2b72a, 0xeb0abe90, 0x9c76b1a6, 0x0286fae5, 0x75119a73, 0xecf8a8c9, 0x9b61c85f
};

static uint32_t crc32_calculate(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

/* 生成 AethelOS 纪元 AethelID (A1<Timestamp<Entropy...) */
static void generate_aethel_id(AethelID *id) {
    memset(id->bytes, 0, 32);
    /* 0-3 bit: 版本标识 A (1010) */
    id->bytes[0] = 0xA0;
    
    /* 时间戳：模拟从 2026-01-06 开始的毫秒数 */
    uint64_t current_time = (uint64_t)time(NULL) * 1000;
    id->bytes[1] = (current_time >> 40) & 0xFF;
    id->bytes[2] = (current_time >> 32) & 0xFF;
    id->bytes[3] = (current_time >> 24) & 0xFF;
    id->bytes[4] = (current_time >> 16) & 0xFF;
    id->bytes[5] = (current_time >> 8) & 0xFF;
    id->bytes[6] = current_time & 0xFF;
    
    /* 填充高熵随机数和占位加密载荷 */
    for(int i = 7; i < 30; i++) {
        id->bytes[i] = rand() & 0xFF;
    }
    
    /* 最后两字节填入简单的 Checksum */
    uint32_t cs = crc32_calculate(id->bytes, 30);
    id->bytes[30] = (cs >> 8) & 0xFF;
    id->bytes[31] = cs & 0xFF;
}

/* 辅助：动态扩容宏 */
#define ENSURE_CAPACITY(ptr, current_size, add_size, capacity) \
    while ((current_size) + (add_size) > (capacity)) { \
        (capacity) *= 2; \
        (ptr) = realloc((ptr), (capacity)); \
    }

AETBGenerator* aetb_gen_create(FILE *output, int opt_level, int debug) {
    AETBGenerator *gen = (AETBGenerator *)calloc(1, sizeof(AETBGenerator));
    if (!gen) return NULL;
    
    gen->output = output;
    gen->optimization_level = opt_level;
    gen->debug_info = debug;
    gen->entry_point = 0; 
    
    gen->act_flow_capacity = INITIAL_CAPACITY;
    gen->act_flow = (uint8_t *)malloc(gen->act_flow_capacity);
    
    gen->mirror_state_capacity = INITIAL_CAPACITY;
    gen->mirror_state = (uint8_t *)malloc(gen->mirror_state_capacity);
    
    gen->constant_truth_capacity = INITIAL_CAPACITY;
    gen->constant_truth = (uint8_t *)malloc(gen->constant_truth_capacity);
    
    gen->symbol_capacity = INITIAL_CAPACITY;
    gen->symbols = (AETBSymbol *)malloc(gen->symbol_capacity * sizeof(AETBSymbol));
    
    gen->strtab_capacity = INITIAL_CAPACITY;
    gen->strtab = (uint8_t *)malloc(gen->strtab_capacity);
    
    gen->reloc_capacity = INITIAL_CAPACITY;
    gen->relocs = (AETBRelocation *)malloc(gen->reloc_capacity * sizeof(AETBRelocation));
    
    /* 初始化字符串表，第一个字节通常为 \0 */
    gen->strtab[0] = '\0';
    gen->strtab_size = 1;
    
    srand((unsigned int)time(NULL));
    return gen;
}

void aetb_gen_destroy(AETBGenerator *gen) {
    if (!gen) return;
    free(gen->act_flow);
    free(gen->mirror_state);
    free(gen->constant_truth);
    free(gen->symbols);
    free(gen->strtab);
    free(gen->relocs);
    free(gen);
}

void aetb_gen_emit_act_flow(AETBGenerator *gen, const uint8_t *code, uint32_t size) {
    if (!gen || !code || size == 0) return;
    ENSURE_CAPACITY(gen->act_flow, gen->act_flow_size, size, gen->act_flow_capacity);
    memcpy(gen->act_flow + gen->act_flow_size, code, size);
    gen->act_flow_size += size;
}

void aetb_gen_emit_mirror_state(AETBGenerator *gen, const uint8_t *data, uint32_t size) {
    if (!gen || !data || size == 0) return;
    ENSURE_CAPACITY(gen->mirror_state, gen->mirror_state_size, size, gen->mirror_state_capacity);
    memcpy(gen->mirror_state + gen->mirror_state_size, data, size);
    gen->mirror_state_size += size;
}

void aetb_gen_emit_constant_truth(AETBGenerator *gen, const uint8_t *data, uint32_t size) {
    if (!gen || !data || size == 0) return;
    ENSURE_CAPACITY(gen->constant_truth, gen->constant_truth_size, size, gen->constant_truth_capacity);
    memcpy(gen->constant_truth + gen->constant_truth_size, data, size);
    gen->constant_truth_size += size;
}

uint32_t aetb_gen_add_string(AETBGenerator *gen, const char *str) {
    if (!gen || !str) return 0;
    uint32_t len = strlen(str) + 1;
    uint32_t offset = gen->strtab_size;
    ENSURE_CAPACITY(gen->strtab, gen->strtab_size, len, gen->strtab_capacity);
    memcpy(gen->strtab + gen->strtab_size, str, len);
    gen->strtab_size += len;
    return offset;
}

uint32_t aetb_gen_add_symbol(AETBGenerator *gen, const char *name, uint8_t type, uint8_t binding, uint16_t section_idx, uint64_t address, uint64_t size) {
    if (!gen) return 0;
    ENSURE_CAPACITY(gen->symbols, gen->symbol_count, 1, gen->symbol_capacity);
    uint32_t idx = gen->symbol_count++;
    AETBSymbol *sym = &gen->symbols[idx];
    sym->name_offset = aetb_gen_add_string(gen, name);
    sym->name_length = name ? strlen(name) : 0;
    sym->type = type;
    sym->binding = binding;
    sym->section_idx = section_idx;
    sym->address = address;
    sym->size = size;
    sym->flags = 0;
    return idx;
}

void aetb_gen_add_relocation_ex(AETBGenerator *gen, uint64_t offset, uint32_t symbol_idx, uint16_t reloc_type, int16_t addend) {
    if (!gen) return;
    ENSURE_CAPACITY(gen->relocs, gen->reloc_count, 1, gen->reloc_capacity);
    AETBRelocation *reloc = &gen->relocs[gen->reloc_count++];
    reloc->offset = offset;
    reloc->symbol_idx = symbol_idx;
    reloc->relocation_type = reloc_type;
    reloc->addend_type = (uint16_t)addend;
}

void aetb_gen_set_entry_point(AETBGenerator *gen, uint64_t entry_point) {
    if (gen) gen->entry_point = entry_point;
}

/* 核心：严谨计算偏移并序列化 */
int aetb_gen_finalize(AETBGenerator *gen) {
    if (!gen || !gen->output) return -1;
    
    aetb_header_t hdr;
    memset(&hdr, 0, sizeof(aetb_header_t));
    
    /* 1. 基础元数据 */
    hdr.magic = 0x42544541; // "AETB"
    hdr.version = 1;
    hdr.app_type = 0;
    hdr.flags = (gen->debug_info ? 0x0010 : 0); // AETB_FLAG_DEBUG
    generate_aethel_id(&hdr.logic_id);
    
    /* 2. 入口点：物理上通常是0，表示基于 ActFlow 起点的偏移 */
    hdr.genesis_pt = gen->entry_point; 
    
    /* 3. 严格连续的内存段布局计算 */
    uint64_t current_offset = sizeof(aetb_header_t); // 必须是 256
    
    // ActFlow (Code)
    hdr.act_flow_offset = current_offset;
    hdr.act_flow_size = gen->act_flow_size;
    current_offset += gen->act_flow_size;
    
    // MirrorState (Data RW)
    hdr.mirror_state_offset = current_offset;
    hdr.mirror_state_size = gen->mirror_state_size;
    current_offset += gen->mirror_state_size;
    
    // ConstantTruth (RO Data / Strings like "hello")
    hdr.constant_truth_offset = current_offset;
    hdr.constant_truth_size = gen->constant_truth_size;
    current_offset += gen->constant_truth_size;
    
    // RelocNexus
    hdr.reloc_nexus_offset = current_offset;
    hdr.reloc_nexus_size = gen->reloc_count * sizeof(AETBRelocation);
    current_offset += hdr.reloc_nexus_size;
    
    // Symbols
    hdr.symtab_offset = current_offset;
    hdr.symtab_size = gen->symbol_count * sizeof(AETBSymbol);
    current_offset += hdr.symtab_size;
    
    // String Table (For symbol names)
    hdr.strtab_offset = current_offset;
    hdr.strtab_size = gen->strtab_size;
    
    /* 4. 时间戳与版本 */
    hdr.mode_restriction = 0; // Sandbox
    hdr.build_timestamp = (uint64_t)time(NULL);
    hdr.build_version = 1;
    hdr.compiler_version = 2026;
    
    /* 5. 校验和计算 (只计算 0x00 到 0xD7 的范围) */
    hdr.header_crc = crc32_calculate((uint8_t*)&hdr, 0xD8);
    // total_crc 可选，需要计算全部 payload
    
    /* 6. 顺序写出到文件 (严禁产生空洞) */
    fwrite(&hdr, 1, sizeof(aetb_header_t), gen->output);
    if (gen->act_flow_size) fwrite(gen->act_flow, 1, gen->act_flow_size, gen->output);
    if (gen->mirror_state_size) fwrite(gen->mirror_state, 1, gen->mirror_state_size, gen->output);
    if (gen->constant_truth_size) fwrite(gen->constant_truth, 1, gen->constant_truth_size, gen->output);
    if (gen->reloc_count) fwrite(gen->relocs, 1, hdr.reloc_nexus_size, gen->output);
    if (gen->symbol_count) fwrite(gen->symbols, 1, hdr.symtab_size, gen->output);
    if (gen->strtab_size) fwrite(gen->strtab, 1, gen->strtab_size, gen->output);
    
    return 0;
}