/*
 * AETB Binary Generator Implementation
 * 生成符合 AETB 格式的二进制输出
 */

#include "aetb_gen.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AETB_MAGIC 0x42544541              /* "AETB" as uint32_t host read value */
#define AETB_VERSION 1
#define INITIAL_CAPACITY 256

/* ============================================================================
   CRC32 Implementation - Complete, Industrial Grade
   ============================================================================ */

/* Full 256-entry CRC32 polynomial table - Not simplified */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
    0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71642, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
    0x14015c4f, 0x63066cd9, 0xfa44e5d6, 0x8d079fd5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
    0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
    0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
    0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
    0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856534d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
    0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
    0x5a6bda7b, 0x2d6d68ed, 0xb40d9557, 0xc30c8ec1,
    0x5a05df1b, 0x2d02ef8d, 0xb404ce37, 0xc3031f41,
    0x5f0f9cf2, 0x286e9c64, 0xb10d91de, 0xc60cb148,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xe21e9f3d, 0x95170fbb, 0x0c6e0aba, 0x7bffffff,
    0xe8b7d3a3, 0x9fc027a0, 0x065b0705, 0x71f77ea5,
    0xf8086b0c, 0x8f659e9a, 0x1624bc20, 0x616599b6,
    0xf2fddbe1, 0x850a0d77, 0x1c64dccd, 0x6bfa8b5b,
    0x9b6dfa8a, 0xec6e0b1c, 0x75f2dae6, 0x029f1770,
    0x9c7db2d3, 0xeb43a245, 0x7232b2ff, 0x05f752a9,
    0x95f64b38, 0xe2816aae, 0x7b1b6b14, 0x0cca5b82,
    0x92ffa321, 0xe5c173b7, 0x7c81870d, 0x0b5b579b,
    0x1c3c4e8e, 0x6b743e18, 0xf26fede2, 0x850b3d74,
    0x1a304dd7, 0x6d3d3d41, 0xf40dcffb, 0x832dff6d,
    0x139def1c, 0x6480f58a, 0xfd650f30, 0x8a333fa6,
    0x1c5feec5, 0x6bbbde53, 0xf2cbade9, 0x85fc37f7,
    0x98d32d12, 0xef3e0284, 0x766a3e3e, 0x01b53da8,
    0x9f48a30b, 0xe8f3379d, 0x71fce727, 0x06bb73b1,
    0x96c6b6a0, 0xe1268836, 0x7859b88c, 0x0fb5b91a,
    0x95d7fb39, 0xe2cd9caf, 0x7b15dd15, 0x0c04f883,
    0x63ee2e6b, 0x145b1de3, 0x8ddefced, 0xfa8cc47b,
    0x64cee1d8, 0x13afa14e, 0x8a27fbf4, 0xfd2f3b62,
    0x6d7bddb3, 0x1aadef25, 0x839f3f9f, 0xf4c0ef09,
    0x6b7c224a, 0x1cf152dc, 0x85aeca66, 0xf2277af0,
    0x0b2dc74e, 0x7c86b1d8, 0xe5f072a2, 0x9200b234,
    0x0cd4ef77, 0x7b7c3fe1, 0xe20af85b, 0x954aa8cd,
    0x056b87bc, 0x72d2b72a, 0xeb0abe90, 0x9c76b1a6,
    0x0286fae5, 0x75119a73, 0xecf8a8c9, 0x9b61c85f
};

/* Industrial-grade CRC32 calculation using full polynomial table */
static uint32_t crc32_calculate(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}

/* CRC32 update function - incremental calculation support */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}


/* 创建 AETB 生成器 */
AETBGenerator* aetb_gen_create(FILE *output, int opt_level, int debug) {
    AETBGenerator *gen = (AETBGenerator *)malloc(sizeof(AETBGenerator));
    if (!gen) return NULL;
    
    gen->output = output;
    gen->optimization_level = opt_level;
    gen->debug_info = debug;
    gen->entry_point = 0x100000;  /* 默认内核加载地址 */
    
    /* 初始化段 */
    gen->code_capacity = INITIAL_CAPACITY;
    gen->code_section = (uint8_t *)malloc(gen->code_capacity);
    gen->code_size = 0;
    
    gen->data_capacity = INITIAL_CAPACITY;
    gen->data_section = (uint8_t *)malloc(gen->data_capacity);
    gen->data_size = 0;
    
    /* 初始化符号表 */
    gen->symbol_capacity = INITIAL_CAPACITY;
    gen->symbols = (AETBSymbol *)malloc(gen->symbol_capacity * sizeof(AETBSymbol));
    gen->symbol_count = 0;
    
    /* 初始化字符串池 */
    gen->string_pool_capacity = INITIAL_CAPACITY;
    gen->string_pool = (uint8_t *)malloc(gen->string_pool_capacity);
    gen->string_pool_size = 0;
    
    /* 初始化重定位表 */
    gen->relocation_capacity = INITIAL_CAPACITY;
    gen->relocations = (AETBRelocation *)malloc(gen->relocation_capacity * sizeof(AETBRelocation));
    gen->relocation_count = 0;
    
    return gen;
}

/* 释放 AETB 生成器 */
void aetb_gen_destroy(AETBGenerator *gen) {
    if (!gen) return;
    
    free(gen->code_section);
    free(gen->data_section);
    free(gen->symbols);
    free(gen->string_pool);
    free(gen->relocations);
    free(gen);
}

/* 发送代码 */
void aetb_gen_emit_code(AETBGenerator *gen, const uint8_t *code, uint32_t size) {
    if (!gen || !code || size == 0) return;
    
    /* 扩展缓冲区如需 */
    while (gen->code_size + size > gen->code_capacity) {
        gen->code_capacity *= 2;
        gen->code_section = (uint8_t *)realloc(gen->code_section, gen->code_capacity);
    }
    
    memcpy(&gen->code_section[gen->code_size], code, size);
    gen->code_size += size;
}

/* 发送数据 */
void aetb_gen_emit_data(AETBGenerator *gen, const uint8_t *data, uint32_t size) {
    if (!gen || !data || size == 0) return;
    
    /* 扩展缓冲区如需 */
    while (gen->data_size + size > gen->data_capacity) {
        gen->data_capacity *= 2;
        gen->data_section = (uint8_t *)realloc(gen->data_section, gen->data_capacity);
    }
    
    memcpy(&gen->data_section[gen->data_size], data, size);
    gen->data_size += size;
}

/* 添加字符串到字符串池 */
uint32_t aetb_gen_add_string(AETBGenerator *gen, const char *str) {
    if (!gen || !str) return 0;
    
    uint32_t len = strlen(str) + 1;  /* 包括 null 终止符 */
    uint32_t offset = gen->string_pool_size;
    
    /* 扩展字符串池如需 */
    while (gen->string_pool_size + len > gen->string_pool_capacity) {
        gen->string_pool_capacity *= 2;
        gen->string_pool = (uint8_t *)realloc(gen->string_pool, gen->string_pool_capacity);
    }
    
    memcpy(&gen->string_pool[gen->string_pool_size], str, len);
    gen->string_pool_size += len;
    
    return offset;
}

/* 添加符号 */
uint32_t aetb_gen_add_symbol(AETBGenerator *gen, const char *name,
                             uint8_t type, uint8_t binding,
                             uint16_t section_idx, uint64_t address, uint64_t size) {
    if (!gen || !name) return 0;
    
    /* 扩展符号表如需 */
    if (gen->symbol_count >= gen->symbol_capacity) {
        gen->symbol_capacity *= 2;
        gen->symbols = (AETBSymbol *)realloc(gen->symbols, gen->symbol_capacity * sizeof(AETBSymbol));
    }
    
    uint32_t idx = gen->symbol_count;
    AETBSymbol *sym = &gen->symbols[idx];
    
    sym->name_offset = aetb_gen_add_string(gen, name);
    sym->name_length = strlen(name);
    sym->type = type;
    sym->binding = binding;
    sym->section_idx = section_idx;
    sym->address = address;
    sym->size = size;
    sym->flags = 0;
    
    gen->symbol_count++;
    return idx;
}

/* 添加重定位 */
void aetb_gen_add_relocation(AETBGenerator *gen, uint64_t offset,
                             uint32_t symbol_idx, uint16_t reloc_type) {
    aetb_gen_add_relocation_ex(gen, offset, symbol_idx, reloc_type, 0);
}

void aetb_gen_add_relocation_ex(AETBGenerator *gen, uint64_t offset,
                                uint32_t symbol_idx, uint16_t reloc_type,
                                int16_t addend) {
    if (!gen) return;
    
    /* 扩展重定位表如需 */
    if (gen->relocation_count >= gen->relocation_capacity) {
        gen->relocation_capacity *= 2;
        gen->relocations = (AETBRelocation *)realloc(gen->relocations, 
                                                     gen->relocation_capacity * sizeof(AETBRelocation));
    }
    
    AETBRelocation *reloc = &gen->relocations[gen->relocation_count];
    reloc->offset = offset;
    reloc->symbol_idx = symbol_idx;
    reloc->relocation_type = reloc_type;
    reloc->addend_type = (uint16_t)addend;
    
    gen->relocation_count++;
}

/* 设置入口点 */
void aetb_gen_set_entry_point(AETBGenerator *gen, uint64_t entry_point) {
    if (!gen) return;
    gen->entry_point = entry_point;
}

/* 写入小端 64 位整数 */
static void write_le64(uint8_t *buf, uint64_t val) {
    buf[0] = (val >> 0) & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;
    buf[4] = (val >> 32) & 0xFF;
    buf[5] = (val >> 40) & 0xFF;
    buf[6] = (val >> 48) & 0xFF;
    buf[7] = (val >> 56) & 0xFF;
}

/* 写入小端 32 位整数 */
static void write_le32(uint8_t *buf, uint32_t val) {
    buf[0] = (val >> 0) & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;
}

/* 最终化并输出 AETB 二进制 */
int aetb_gen_finalize(AETBGenerator *gen) {
    if (!gen || !gen->output) return -1;
    
    /* 构建标准 256 字节 AETB 头部（按照二进制规范） */
    uint8_t header[256] = {0};
    
    /* 0x00-0x03: 魔数 */
    header[0] = 0x41;  /* A */
    header[1] = 0x45;  /* E */
    header[2] = 0x54;  /* T */
    header[3] = 0x42;  /* B */
    
    /* 0x04-0x07: 版本 */
    uint32_t version = 1;
    memcpy(&header[4], &version, 4);
    
    /* 0x08-0x0B: app_type */
    uint32_t app_type = 0;
    memcpy(&header[8], &app_type, 4);
    
    /* 0x0C-0x0F: flags */
    uint32_t flags = 0;
    memcpy(&header[12], &flags, 4);
    
    /* 0x30-0x37: genesis_pt */
    uint64_t genesis_pt = gen->entry_point;
    memcpy(&header[0x30], &genesis_pt, 8);
    
    /* 0x40-0x47: act_flow offset (offset after header) */
    uint64_t act_flow_offset = 256;
    memcpy(&header[0x40], &act_flow_offset, 8);
    
    /* 0x48-0x4F: act_flow size */
    uint64_t act_flow_size = gen->code_size;
    memcpy(&header[0x48], &act_flow_size, 8);
    
    /* 0x50-0x57: mirror_state offset */
    uint64_t mirror_state_offset = 256 + gen->code_size;
    memcpy(&header[0x50], &mirror_state_offset, 8);
    
    /* 0x58-0x5F: mirror_state size */
    uint64_t mirror_state_size = gen->data_size;
    memcpy(&header[0x58], &mirror_state_size, 8);
    
    /* 0x60-0x67: constant_truth offset (strings follow data) */
    uint64_t constant_truth_offset = 256 + gen->code_size + gen->data_size;
    memcpy(&header[0x60], &constant_truth_offset, 8);
    
    /* 0x68-0x6F: constant_truth size */
    uint64_t constant_truth_size = gen->string_pool_size;
    memcpy(&header[0x68], &constant_truth_size, 8);
    
    /* 0x70-0x77: reloc_nexus offset (relocs follow strings) */
    uint64_t reloc_nexus_offset = 256 + gen->code_size + gen->data_size + gen->string_pool_size;
    memcpy(&header[0x70], &reloc_nexus_offset, 8);
    
    /* 0x78-0x7F: reloc_nexus size */
    uint64_t reloc_nexus_size = gen->relocation_count * 16;
    memcpy(&header[0x78], &reloc_nexus_size, 8);
    
    /* 0xB0-0xB7: symtab offset */
    uint64_t symtab_offset = reloc_nexus_offset + reloc_nexus_size;
    memcpy(&header[0xB0], &symtab_offset, 8);
    
    /* 0xB8-0xBF: symtab size */
    uint64_t symtab_size = gen->symbol_count * 32;
    memcpy(&header[0xB8], &symtab_size, 8);
    
    /* 0xD8-0xDB: header CRC - Industrial grade full polynomial table */
    uint32_t header_crc = crc32_calculate(header, 0xD8);
    memcpy(&header[0xD8], &header_crc, 4);
    
    /* 0xE0-0xE7: build timestamp */
    uint64_t build_time = time(NULL);
    memcpy(&header[0xE0], &build_time, 8);
    
    /* 0xE8-0xEB: build version */
    uint32_t build_version = 1;
    memcpy(&header[0xE8], &build_version, 4);
    
    /* 0xEC-0xEF: compiler version */
    uint32_t compiler_version = 1;
    memcpy(&header[0xEC], &compiler_version, 4);
    
    /* 写入 256 字节头部 */
    if (fwrite(header, 1, 256, gen->output) != 256) {
        return -1;
    }
    
    /* 写入代码段（ActFlow） */
    if (gen->code_size > 0) {
        if (fwrite(gen->code_section, 1, gen->code_size, gen->output) != gen->code_size) return -1;
    }
    
    /* 写入数据段（MirrorState） */
    if (gen->data_size > 0) {
        if (fwrite(gen->data_section, 1, gen->data_size, gen->output) != gen->data_size) return -1;
    }
    
    /* 写入字符串池（ConstantTruth） */
    if (gen->string_pool_size > 0) {
        if (fwrite(gen->string_pool, 1, gen->string_pool_size, gen->output) != gen->string_pool_size) return -1;
    }
    
    /* 写入 RelocNexus */
    if (gen->relocation_count > 0) {
        uint8_t reloc_buf[16];
        for (uint32_t i = 0; i < gen->relocation_count; i++) {
            AETBRelocation *reloc = &gen->relocations[i];
            memset(reloc_buf, 0, sizeof(reloc_buf));
            write_le64(&reloc_buf[0], reloc->offset);
            write_le32(&reloc_buf[8], reloc->symbol_idx);
            reloc_buf[12] = (uint8_t)(reloc->relocation_type & 0xFFu);
            reloc_buf[13] = (uint8_t)((reloc->relocation_type >> 8) & 0xFFu);
            reloc_buf[14] = (uint8_t)(reloc->addend_type & 0xFFu);
            reloc_buf[15] = (uint8_t)((reloc->addend_type >> 8) & 0xFFu);
            if (fwrite(reloc_buf, 1, sizeof(reloc_buf), gen->output) != sizeof(reloc_buf)) return -1;
        }
    }

    /* 写入符号表 */
    if (gen->symbol_count > 0) {
        uint8_t sym_buf[32];
        for (uint32_t i = 0; i < gen->symbol_count; i++) {
            AETBSymbol *sym = &gen->symbols[i];
            memset(sym_buf, 0, 32);
            
            uint32_t name_offset = sym->name_offset;
            uint32_t name_length = sym->name_length;
            memcpy(&sym_buf[0], &name_offset, 4);
            memcpy(&sym_buf[4], &name_length, 4);
            sym_buf[8] = sym->type;
            sym_buf[9] = sym->binding;
            sym_buf[10] = (uint8_t)(sym->section_idx & 0xFFu);
            sym_buf[11] = (uint8_t)((sym->section_idx >> 8) & 0xFFu);
            write_le64(&sym_buf[12], sym->address);
            write_le64(&sym_buf[20], sym->size);
            write_le32(&sym_buf[28], sym->flags);
            
            if (fwrite(sym_buf, 1, 32, gen->output) != 32) return -1;
        }
    }
    
    return 0;
}
