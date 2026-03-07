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
 * UEFI PE32+ Industrial Grade Generator - Implementation
 * 工业级 UEFI PE32+ EFI Application Binary Generator 完整实现
 *
 * 严格按照以下规范实现：
 * 1. PE32+ Specification (Microsoft COFF and PE/COFF Specification)
 * 2. UEFI Specification (Unified Extensible Firmware Interface)
 * 3. x86-64 ABI (System V AMD64 ABI)
 *
 * 与现有 pe_gen.c 的根本区别：
 * - pe_gen.c: 嵌入AETB格式的PE Bootloader
 * - pe_industrial.c: 标准UEFI应用PE格式，支持编译器所有Zone的独立布局
 */

#include "pe_industrial.h"
#include "../common/format_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

/* ======================================================================
   PE32+ CheckSum 计算 - 工业级实现
   
   按照 PE 规范，CheckSum 的计算使用 Carrillo-Sahni 算法：
   1. 逐个读取文件中的 32-bit 无符号整数
   2. 累加所有 32-bit 值，跳过 CheckSum 字段本身（位于 Optional Header 中的 0x40）
   3. 最终加上 ImageSize
   4. 取低 32-bit 作为 CheckSum
   ====================================================================== */

uint32_t pe_industrial_calculate_checksum(const uint8_t *file_data,
                                          size_t file_size,
                                          uint32_t checksum_offset) {
    if (!file_data || file_size == 0) {
        return 0;
    }
    
    /* 转换为 uint32_t 指针进行逐字计算 */
    uint64_t sum = 0;
    const uint32_t *ptr32 = (const uint32_t *)file_data;
    size_t count = (file_size + 3) / 4;  /* 四舍五入到最近的 4 字节边界 */
    
    for (size_t i = 0; i < count; i++) {
        uint32_t offset = i * 4;
        
        /* 跳过 CheckSum 字段所在的位置（通常在 Optional Header 的 0x40 处） */
        if (offset >= checksum_offset && offset < checksum_offset + 4) {
            continue;
        }
        
        /* 读取 32-bit 字 */
        uint32_t word = 0;
        if (offset < file_size) {
            if (offset + 3 < file_size) {
                word = *(uint32_t *)&file_data[offset];
            } else {
                /* 处理文件末尾不完整的 4 字节 */
                for (int j = 0; j < 4 && offset + j < file_size; j++) {
                    word |= ((uint32_t)file_data[offset + j]) << (j * 8);
                }
            }
        }
        
        sum += word;
        
        /* 处理进位 */
        if (sum > 0xFFFFFFFF) {
            sum = (sum & 0xFFFFFFFF) + (sum >> 32);
        }
    }
    
    /* 最后加上文件大小 */
    sum = (sum & 0xFFFFFFFF) + (uint32_t)file_size;
    if (sum > 0xFFFFFFFF) {
        sum = (sum & 0xFFFFFFFF) + (sum >> 32);
    }
    
    return (uint32_t)(sum & 0xFFFFFFFF);
}

/* ======================================================================
   辅助函数：对齐值计算
   ====================================================================== */

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return ((value + alignment - 1) / alignment) * alignment;
}

static size_t align_up_size(size_t value, size_t alignment) {
    if (alignment == 0) return value;
    return ((value + alignment - 1) / alignment) * alignment;
}

/* ======================================================================
   辅助函数：Section 名字填充（最多 8 字节）
   ====================================================================== */

static void fill_section_name(uint8_t *name_field, const char *name) {
    memset(name_field, 0, 8);
    if (name) {
        size_t len = strlen(name);
        if (len > 8) len = 8;
        memcpy(name_field, name, len);
    }
}

/* ======================================================================
   工业级 PE32+ EFI 生成函数
   ====================================================================== */

int pe_industrial_generate_efi(const PE_Industrial_Input *input) {
    if (!input || !input->output_filename) {
        fprintf(stderr, "[PE-IND] Error: Invalid input\n");
        return -1;
    }
    
    /* ================================================================
       第一步：验证输入参数
       ================================================================ */
    
    if (!input->code_section || input->code_size == 0) {
        fprintf(stderr, "[PE-IND] Error: Invalid code section\n");
        fprintf(stderr, "         code_section: %p, size: %zu\n",
                (void*)input->code_section, input->code_size);
        return -1;
    }
    
    /* 数据段和只读数据段可以为空 */
    
    if (input->image_base == 0) {
        fprintf(stderr, "[PE-IND] Warning: ImageBase is 0, using default 0x140000000\n");
        /* 继续使用默认值 */
    }
    
    if (input->entry_point_offset >= input->code_size) {
        fprintf(stderr, "[PE-IND] Warning: Entry point offset (0x%x) >= code size (0x%zx)\n",
                input->entry_point_offset, input->code_size);
        /* 继续处理，使用提供的offset */
    }
    
    /* ================================================================
       第二步：计算文件布局参数
       ================================================================ */
    
    uint32_t file_alignment = PE_IND_FILE_ALIGNMENT;        /* 0x200 */
    uint32_t section_alignment = PE_IND_SECTION_ALIGNMENT;  /* 0x1000 */
    
    /* 动态计算需要的 Section 数量和大小（避免空 Section 导致 RVA 重叠引发 UEFI Load Error） */
    uint16_t num_sections = 1; /* 至少有 .text */
    if (input->data_size > 0) num_sections++;
    if (input->rodata_size > 0) num_sections++;
    num_sections++; /* .reloc 必须有 */
    
    /* 生成 Base Relocation Table
     * 必须至少包含一个 ABSOLUTE 填充条目，以保证 Block 符合 32-bit 对齐的 12 字节规范
     */
    uint8_t reloc_data[12] = {
        0x00, 0x10, 0x00, 0x00,  /* Page RVA: 0x1000 (第一个 section) */
        0x0C, 0x00, 0x00, 0x00,  /* Block Size: 12 bytes */
        0x00, 0x00,              /* Dummy Entry 1: Type 0 (ABSOLUTE), Offset 0 */
        0x00, 0x00               /* Dummy Entry 2: Padding (凑齐 32位对齐) */
    };
    uint32_t reloc_actual_size = sizeof(reloc_data);
    
    /* Headers 大小（对齐到 file_alignment）
     * Headers = DOS(64) + PESignature(4) + COFF(20) + Optional(240) + Sections(num_sections * 40)
     */
    uint32_t headers_size = align_up(64 + 4 + 20 + 240 + (num_sections * 40), file_alignment);
    
    uint32_t current_rva = section_alignment;
    uint32_t current_raw_ptr = headers_size;
    
    /* 计算 .text 布局 */
    uint32_t text_rva = current_rva;
    uint32_t text_raw_ptr = current_raw_ptr;
    uint32_t text_virtual_size = (uint32_t)input->code_size;
    uint32_t text_raw_size = align_up(text_virtual_size, file_alignment);
    current_rva += align_up(text_virtual_size, section_alignment);
    current_raw_ptr += text_raw_size;
    
    /* 计算 .data 布局 */
    uint32_t data_rva = 0, data_raw_ptr = 0, data_virtual_size = 0, data_raw_size = 0;
    if (input->data_size > 0) {
        data_rva = current_rva;
        data_raw_ptr = current_raw_ptr;
        data_virtual_size = (uint32_t)input->data_size;
        data_raw_size = align_up(data_virtual_size, file_alignment);
        current_rva += align_up(data_virtual_size, section_alignment);
        current_raw_ptr += data_raw_size;
    }
    
    /* 计算 .rodata 布局 */
    uint32_t rodata_rva = 0, rodata_raw_ptr = 0, rodata_virtual_size = 0, rodata_raw_size = 0;
    if (input->rodata_size > 0) {
        rodata_rva = current_rva;
        rodata_raw_ptr = current_raw_ptr;
        rodata_virtual_size = (uint32_t)input->rodata_size;
        rodata_raw_size = align_up(rodata_virtual_size, file_alignment);
        current_rva += align_up(rodata_virtual_size, section_alignment);
        current_raw_ptr += rodata_raw_size;
    }
    
    /* 计算 .reloc 布局 */
    uint32_t reloc_rva = current_rva;
    uint32_t reloc_raw_ptr = current_raw_ptr;
    uint32_t reloc_virtual_size = reloc_actual_size;
    uint32_t reloc_raw_size = align_up(reloc_virtual_size, file_alignment);
    current_rva += align_up(reloc_virtual_size, section_alignment);
    current_raw_ptr += reloc_raw_size;
    
    /* Entry Point RVA (相对于 .text section) */
    uint32_t entry_rva = text_rva + input->entry_point_offset;
    
    /* 总镜像大小与文件大小 */
    uint32_t image_size = current_rva;
    size_t file_size = current_raw_ptr;
    
    /* ================================================================
       第三步：分配 PE 文件缓冲区
       ================================================================ */
    
    uint8_t *file_buf = (uint8_t *)calloc(file_size, 1);
    if (!file_buf) {
        fprintf(stderr, "[PE-IND] Error: Out of memory for PE file (0x%zx bytes)\n", file_size);
        return -1;
    }
    
    /* ================================================================
       第四步：构造 DOS Header (64 bytes)
       ================================================================ */
    
    file_buf[0] = 'M';
    file_buf[1] = 'Z';
    
    *(uint16_t *)&file_buf[0x02] = 0x0090;          /* e_cblp */
    *(uint16_t *)&file_buf[0x04] = 0x0003;          /* e_cp */
    *(uint16_t *)&file_buf[0x08] = 0x0004;          /* e_cp_hdr */
    *(uint16_t *)&file_buf[0x0C] = 0xFFFF;          /* e_maxalloc */
    *(uint16_t *)&file_buf[0x10] = 0x00B8;          /* e_sp */
    *(uint16_t *)&file_buf[0x12] = 0x0040;          /* e_csum */
    *(uint16_t *)&file_buf[0x14] = 0x0000;          /* e_ip */
    *(uint16_t *)&file_buf[0x24] = 0x0040;          /* e_lfarlc */
    *(uint32_t *)&file_buf[0x3C] = 0x0040;          /* e_lfanew: PE header offset = 0x40 */
    
    /* ================================================================
       第五步：构造 PE Signature (4 bytes) + COFF Header (20 bytes)
       ================================================================ */
    
    *(uint32_t *)&file_buf[0x40] = 0x4550;
    
    uint8_t *coff_hdr = &file_buf[0x44];
    *(uint16_t *)&coff_hdr[0] = PE_IND_MACHINE_AMD64;                       
    *(uint16_t *)&coff_hdr[2] = num_sections;                               /* NumberOfSections (动态) */
    *(uint32_t *)&coff_hdr[4] = (uint32_t)time(NULL);                       
    *(uint32_t *)&coff_hdr[8] = 0;                                          
    *(uint32_t *)&coff_hdr[12] = 0;                                         
    *(uint16_t *)&coff_hdr[16] = 240;                                       
    
    uint16_t characteristics = PE_IND_CHAR_EXECUTABLE_IMAGE | PE_IND_CHAR_LARGE_ADDRESS_AWARE;
    *(uint16_t *)&coff_hdr[18] = characteristics;
    
    /* ================================================================
       第六步：构造 PE32+ Optional Header (240 bytes)
       ================================================================ */
    
    uint8_t *opt_hdr = &file_buf[0x58];
    
    *(uint16_t *)&opt_hdr[0] = 0x020B; /* Magic: PE32+ */
    opt_hdr[2] = 14; 
    opt_hdr[3] = 0;   
    
    *(uint32_t *)&opt_hdr[4] = text_raw_size;           /* SizeOfCode */
    *(uint32_t *)&opt_hdr[8] = data_raw_size + rodata_raw_size;  /* SizeOfInitializedData */
    *(uint32_t *)&opt_hdr[12] = 0;                      /* SizeOfUninitializedData */
    
    *(uint32_t *)&opt_hdr[16] = entry_rva;              /* AddressOfEntryPoint */
    *(uint32_t *)&opt_hdr[20] = text_rva;               /* BaseOfCode */
    
    uint64_t image_base = input->image_base ? input->image_base : PE_IND_IMAGE_BASE;
    *(uint64_t *)&opt_hdr[24] = image_base;
    
    *(uint32_t *)&opt_hdr[32] = section_alignment;     /* SectionAlignment */
    *(uint32_t *)&opt_hdr[36] = file_alignment;        /* FileAlignment */
    
    *(uint16_t *)&opt_hdr[40] = 6;  
    *(uint16_t *)&opt_hdr[42] = 0;  
    *(uint16_t *)&opt_hdr[44] = 0;  
    *(uint16_t *)&opt_hdr[46] = 0;  
    *(uint16_t *)&opt_hdr[48] = 0;  
    *(uint16_t *)&opt_hdr[50] = 0;  
    *(uint32_t *)&opt_hdr[52] = 0;  
    
    *(uint32_t *)&opt_hdr[56] = image_size;             /* SizeOfImage */
    *(uint32_t *)&opt_hdr[60] = headers_size;           /* SizeOfHeaders */
    *(uint32_t *)&opt_hdr[64] = 0;
    
    *(uint16_t *)&opt_hdr[68] = PE_IND_SUBSYSTEM_EFI_APPLICATION;  
    
    uint16_t dll_characteristics = PE_IND_DLL_CHAR_HIGH_ENTROPY_VA | PE_IND_DLL_CHAR_DYNAMIC_BASE | PE_IND_DLL_CHAR_NX_COMPAT;
    *(uint16_t *)&opt_hdr[70] = dll_characteristics;
    
    *(uint64_t *)&opt_hdr[72] = input->stack_reserve ? input->stack_reserve : 0x10000;
    *(uint64_t *)&opt_hdr[80] = input->stack_commit ? input->stack_commit : 0x1000;
    *(uint64_t *)&opt_hdr[88] = input->heap_reserve ? input->heap_reserve : 0x100000;
    *(uint64_t *)&opt_hdr[96] = input->heap_commit ? input->heap_commit : 0x1000;
    *(uint32_t *)&opt_hdr[104] = 0;
    *(uint32_t *)&opt_hdr[108] = PE_IND_DATA_DIRECTORIES;
    
    uint32_t *data_dirs = (uint32_t *)&opt_hdr[112];
    for (int i = 0; i < 32; i++) {
        data_dirs[i] = 0;
    }
    
    /* Entry 5: Base Relocation Table */
    data_dirs[10] = reloc_rva;                          /* RVA */
    data_dirs[11] = reloc_actual_size;                  /* 必须且只能是实际解析大小(12)，绝不能是对齐后的 Size，否则引发UEFI Load Error */
    
    /* ================================================================
       第七步：构造 Section Headers (动态数量分配)
       ================================================================ */
    
    /* COFF Header (0x44) -> Section Headers (0x44 + 20 + 240 = 0x148) */
    uint8_t *sect_hdrs = &file_buf[0x148];  
    int sect_idx = 0;
    
    /* .text Section Header */
    memset(&sect_hdrs[sect_idx * 40], 0, 40);
    fill_section_name(&sect_hdrs[sect_idx * 40], ".text");
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 8] = text_virtual_size;     /* VirtualSize */
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 12] = text_rva;             /* VirtualAddress */
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 16] = text_raw_size;        /* SizeOfRawData */
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 20] = text_raw_ptr;         /* PointerToRawData */
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 36] = PE_IND_SCN_CNT_CODE | PE_IND_SCN_MEM_EXECUTE | PE_IND_SCN_MEM_READ;
    sect_idx++;
    
    /* .data Section Header (如果存在数据才写入，杜绝 RVA 重叠冲突) */
    if (input->data_size > 0) {
        memset(&sect_hdrs[sect_idx * 40], 0, 40);
        fill_section_name(&sect_hdrs[sect_idx * 40], ".data");
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 8] = data_virtual_size;    /* VirtualSize */
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 12] = data_rva;             /* VirtualAddress */
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 16] = data_raw_size;        /* SizeOfRawData */
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 20] = data_raw_ptr;         /* PointerToRawData */
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 36] = PE_IND_SCN_CNT_INITIALIZED_DATA | PE_IND_SCN_MEM_READ | PE_IND_SCN_MEM_WRITE;
        sect_idx++;
    }
    
    /* .rodata Section Header */
    if (input->rodata_size > 0) {
        memset(&sect_hdrs[sect_idx * 40], 0, 40);
        fill_section_name(&sect_hdrs[sect_idx * 40], ".rodata");
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 8] = rodata_virtual_size;  /* VirtualSize */
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 12] = rodata_rva;           /* VirtualAddress */
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 16] = rodata_raw_size;      /* SizeOfRawData */
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 20] = rodata_raw_ptr;       /* PointerToRawData */
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 36] = PE_IND_SCN_CNT_INITIALIZED_DATA | PE_IND_SCN_MEM_READ;
        sect_idx++;
    }
    
    /* .reloc Section Header */
    memset(&sect_hdrs[sect_idx * 40], 0, 40);
    fill_section_name(&sect_hdrs[sect_idx * 40], ".reloc");
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 8] = reloc_virtual_size;  /* VirtualSize */
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 12] = reloc_rva;           /* VirtualAddress */
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 16] = reloc_raw_size;      /* SizeOfRawData */
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 20] = reloc_raw_ptr;       /* PointerToRawData */
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 36] = PE_IND_SCN_CNT_INITIALIZED_DATA | PE_IND_SCN_MEM_READ | PE_IND_SCN_MEM_DISCARDABLE;
    
    /* ================================================================
       第八步：填充 Section 数据
       ================================================================ */
    
    /* 填充 .text section */
    if (input->code_section && input->code_size > 0) {
        if (memcpy(&file_buf[text_raw_ptr], input->code_section, input->code_size) == NULL) {
            fprintf(stderr, "[PE-IND] Error: Failed to copy .text data\n");
            free(file_buf);
            return -1;
        }
    }
    
    /* 填充 .data section */
    if (input->data_section && input->data_size > 0) {
        if (memcpy(&file_buf[data_raw_ptr], input->data_section, input->data_size) == NULL) {
            fprintf(stderr, "[PE-IND] Error: Failed to copy .data data\n");
            free(file_buf);
            return -1;
        }
    }
    
    /* 填充 .rodata section */
    if (input->rodata_section && input->rodata_size > 0) {
        if (memcpy(&file_buf[rodata_raw_ptr], input->rodata_section, input->rodata_size) == NULL) {
            fprintf(stderr, "[PE-IND] Error: Failed to copy .rodata data\n");
            free(file_buf);
            return -1;
        }
    }
    
    /* 填充 .reloc section */
    if (memcpy(&file_buf[reloc_raw_ptr], reloc_data, sizeof(reloc_data)) == NULL) {
        fprintf(stderr, "[PE-IND] Error: Failed to copy .reloc data\n");
        free(file_buf);
        return -1;
    }
    
    /* ================================================================
       第九步：计算并设置 CheckSum
       ================================================================ */
    
    /* CheckSum 字段在 Optional Header 中的偏移
     * = DOS(64) + PESignature(4) + COFF(20) + OptHdrOffset(64) = 0xC0
     */
    uint32_t checksum_offset = 0x40 + 4 + 20 + 64;
    uint32_t checksum = pe_industrial_calculate_checksum(file_buf, file_size, checksum_offset);
    *(uint32_t *)&opt_hdr[64] = checksum;
    
    /* ================================================================
       第十步：写入输出文件
       ================================================================ */
    
    FILE *out = fopen(input->output_filename, "wb");
    if (!out) {
        fprintf(stderr, "[PE-IND] Error: Cannot create output file '%s'\n", input->output_filename);
        fprintf(stderr, "         Error: %s\n", strerror(errno));
        free(file_buf);
        return -1;
    }
    
    size_t written = fwrite(file_buf, 1, file_size, out);
    if (written != file_size) {
        fprintf(stderr, "[PE-IND] Error: Failed to write PE file\n");
        fprintf(stderr, "         Expected: 0x%zx bytes, Wrote: 0x%zx bytes\n", file_size, written);
        if (ferror(out)) {
            fprintf(stderr, "         I/O Error: %s\n", strerror(errno));
        }
        fclose(out);
        free(file_buf);
        return -1;
    }
    
    int close_result = fclose(out);
    if (close_result != 0) {
        fprintf(stderr, "[PE-IND] Error: Failed to close output file\n");
        fprintf(stderr, "         Error: %s\n", strerror(errno));
        free(file_buf);
        return -1;
    }
    
    /* ================================================================
       第十一步：生成完毕 - 输出诊断信息
       ================================================================ */
    
    fprintf(stderr, "[PE-IND] ✓ Generated UEFI PE32+ EFI Application\n");
    fprintf(stderr, "         Output: %s\n", input->output_filename);
    fprintf(stderr, "         File Size: 0x%zx bytes (%zu bytes)\n", file_size, file_size);
    fprintf(stderr, "         Image Base: 0x%llx\n", (unsigned long long)image_base);
    fprintf(stderr, "         Sections:\n");
    fprintf(stderr, "           .text   RVA=0x%x VSize=0x%x RSize=0x%x\n",
            text_rva, text_virtual_size, text_raw_size);
    fprintf(stderr, "           .data   RVA=0x%x VSize=0x%x RSize=0x%x\n",
            data_rva, data_virtual_size, data_raw_size);
    fprintf(stderr, "           .rodata RVA=0x%x VSize=0x%x RSize=0x%x\n",
            rodata_rva, rodata_virtual_size, rodata_raw_size);
    fprintf(stderr, "           .reloc  RVA=0x%x VSize=0x%x RSize=0x%x\n",
            reloc_rva, reloc_virtual_size, reloc_raw_size);
    fprintf(stderr, "         Image Size: 0x%x\n", image_size);
    fprintf(stderr, "         Entry Point RVA: 0x%x\n", entry_rva);
    fprintf(stderr, "         CheckSum: 0x%08x\n", checksum);
    fprintf(stderr, "[PE-IND] Build complete. Standard UEFI application ready.\n");
    
    free(file_buf);
    return 0;
}

/* ======================================================================
   便捷函数：从编译器三个 Zone 直接生成 PE
   ====================================================================== */

int pe_industrial_generate_from_zones(const char *output_file,
                                      const uint8_t *code, size_t code_size,
                                      const uint8_t *data, size_t data_size,
                                      const uint8_t *rodata, size_t rodata_size,
                                      uint32_t entry_offset) {
    if (!output_file || !code || code_size == 0) {
        fprintf(stderr, "[PE-IND] Error: Invalid parameters\n");
        return -1;
    }
    
    PE_Industrial_Input input = {
        .output_filename = output_file,
        .code_section = code,
        .code_size = code_size,
        .data_section = data,
        .data_size = data_size,
        .rodata_section = rodata,
        .rodata_size = rodata_size,
        .entry_point_offset = entry_offset,
        .image_base = PE_IND_IMAGE_BASE,
        .stack_reserve = 0x10000,
        .stack_commit = 0x1000,
        .heap_reserve = 0x100000,
        .heap_commit = 0x1000
    };
    
    return pe_industrial_generate_efi(&input);
}
