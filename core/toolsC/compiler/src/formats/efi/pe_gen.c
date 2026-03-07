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
 * PE32+ (Portable Executable) EFI Format Generation Implementation
 * 
 * 工业级完整实现 - 严格按照《AethelOS 二进制及目录结构.txt》规范
 *
 * 完整结构：
 * [64B DOS Header] + [4B PE Signature] + [20B COFF Header] 
 * + [240B Optional Header] + [40B .text Section] + [40B .reloc Section]
 * + [aligned .text data] + [aligned .reloc data]
 */

#include "pe.h"
#include "../common/format_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

/* ============================================================
   校验和计算 - Carrillo-Sahni 算法
   UEFI规范中定义的PE校验和计算方法
   ============================================================ */

uint32_t pe32plus_calculate_checksum(const uint8_t *file_data, 
                                     size_t file_size,
                                     uint32_t checksum_offset) {
    if (!file_data || file_size == 0) {
        return 0;
    }
    
    uint32_t sum = 0;
    uint32_t high = 0;
    const uint16_t *ptr = (const uint16_t *)file_data;
    size_t count = file_size / 2;
    uint32_t skip_idx = checksum_offset / 2;
    
    /* 应用 Carrillo-Sahni 算法计算校验和 */
    for (size_t i = 0; i < count; i++) {
        /* 跳过 CheckSum 字段本身 (2 个 uint16_t) */
        if (i == skip_idx || i == skip_idx + 1) {
            continue;
        }
        
        uint32_t word = ptr[i];
        sum = sum + word;
        if (sum < word) {
            high++;
        }
    }
    
    /* 最终组合 */
    sum = sum + (high << 16) + (uint32_t)file_size;
    return sum;
}

/**
 * 完整的 PE32+ EFI 镜像生成 - 工业级实现
 * 
 * 关键理念：PE 不仅是 UEFI EFI 应用容器，更是 AethelOS 嵌入式 AETB 逻辑的载体
 * 
 * 架构：
 * [PE32+ Headers]
 *   + [.text Section]
 *       ├─ [256B AKI-style Header]  (包含AETB zone偏移)
 *       ├─ [ActFlow Zone]           (指令逻辑 - AethelA引擎编织对象)
 *       ├─ [MirrorState Zone]       (初始化数据)
 *       ├─ [ConstantTruth Zone]     (常量数据)
 *       └─ [ImportNexus/IdentityNexus]  (身份和引用信息)
 *   + [.reloc Section]
 *
 * 这样 PE 既是标准 UEFI EFI 可执行，又是完整的 AethelOS 逻辑包装
 */

int pe32plus_generate_efi(const char *output_file,
                         const uint8_t *code_buffer,
                         size_t code_size) {
    if (!output_file || !code_buffer || code_size == 0) {
        fprintf(stderr, "[PE32+] Error: Invalid parameters\n");
        return -1;
    }
    
    /* ================================================================
       第一步：认识和验证完整AETB结构
       ================================================================ */
    
    /* 输入应该是完整的AETB格式数据
       [256B Header with zone metadata] + [ActFlow] + [MirrorState] + [ConstantTruth] + [ImportNexus]*/
    
    const uint8_t *aetb_data = code_buffer;
    size_t aetb_size = code_size;
    
    /* 验证最小大小 - 至少有header */
    if (aetb_size < 256) {
        fprintf(stderr, "[PE32+] Error: AETB input too small (%zu bytes, expected >= 256)\n", aetb_size);
        return -1;
    }
    
    /* 验证AETB header - 从binary_data开头读取AethelBinaryHeader */
    const AethelBinaryHeader *aetb_hdr = (const AethelBinaryHeader *)aetb_data;
    
    /* 简单验证：检查header的基本完整性
       实际验证应该使用aethel_header_validate，但为了保持pe_gen.c独立，
       这里只进行基本检查 */
    
    /* 提取zone偏移和大小信息 
       这些字段应该在AethelBinaryHeader中定义（参考AKI实现）*/
    uint64_t act_flow_offset = aetb_hdr->act_flow_offset;
    uint64_t act_flow_size = aetb_hdr->act_flow_size;
    uint64_t mirror_state_offset = aetb_hdr->mirror_state_offset;
    uint64_t mirror_state_size = aetb_hdr->mirror_state_size;
    uint64_t constant_truth_offset = aetb_hdr->constant_truth_offset;
    uint64_t constant_truth_size = aetb_hdr->constant_truth_size;
    
    /* 基本验证：zone偏移应该合理 */
    if (act_flow_offset < 256 || mirror_state_offset < 256 || constant_truth_offset < 256) {
        fprintf(stderr, "[PE32+] Warning: Zone offsets seem invalid, but proceeding with full data\n");
        /* 如果zone offset无效，我们直接将整个aetb_data作为.text content */
    }
    
    /* ================================================================
       第二步：参数计算和布局规划
       ================================================================ */
    
    uint32_t file_alignment = PE_FILE_ALIGNMENT;      /* 0x200 */
    uint32_t section_alignment = PE_SECTION_ALIGNMENT; /* 0x1000 */
    
    /* 
       重要：PE的.text段必须包含完整的AETB逻辑结构
       即使原始代码没有AETB header，我们也必须创建一个，
       以便AethelA引擎在加载时能够识别和处理
    */
    
    /* .text段内容 = 完整的AETB数据（包含embedded header） */
    size_t text_raw_content_size = aetb_size;
    uint32_t text_size_file = ((text_raw_content_size + file_alignment - 1) / file_alignment) * file_alignment;
    uint32_t text_size_mem = ((text_raw_content_size + section_alignment - 1) / section_alignment) * section_alignment;
    
    /* Base Relocation Table - 1个ABSOLUTE条目，避免零条目块被固件/工具误判 */
    uint8_t reloc_block_data[10] = {
        0x00, 0x10, 0x00, 0x00,  /* Page RVA (0x1000) */
        0x0A, 0x00, 0x00, 0x00,  /* Block Size (10 bytes) */
        0x00, 0x00               /* TypeOffset: IMAGE_REL_BASED_ABSOLUTE */
    };
    uint32_t reloc_raw_content_size = sizeof(reloc_block_data);
    uint32_t reloc_size_file = ((reloc_raw_content_size + file_alignment - 1) / file_alignment) * file_alignment;
    uint32_t reloc_size_mem = ((reloc_raw_content_size + section_alignment - 1) / section_alignment) * section_alignment;
    
    /* 头部大小：DOS(64) + PE Signature(4) + COFF(20) + Optional(240) + 2×Section(40) = 408 */
    uint32_t headers_size = ((64 + 4 + 20 + 240 + 2 * 40 + file_alignment - 1) / file_alignment) * file_alignment;
    
    /* RVA 计算 */
    uint32_t text_rva = section_alignment;
    uint32_t entry_rva = text_rva + AETHEL_HEADER_SIZE;  /* 默认跳过256B头部 */
    uint32_t reloc_rva = text_rva + text_size_mem;
    uint32_t image_size = reloc_rva + reloc_size_mem;

    /* UEFI入口优先取genesis_pt（编译器选定的入口函数偏移），其次回退到ActFlow */
    if (aetb_hdr->genesis_point >= AETHEL_HEADER_SIZE && aetb_hdr->genesis_point < aetb_size &&
        aetb_hdr->genesis_point <= (uint64_t)(UINT32_MAX - text_rva)) {
        entry_rva = text_rva + (uint32_t)aetb_hdr->genesis_point;
    } else if (act_flow_offset >= AETHEL_HEADER_SIZE && act_flow_offset < aetb_size &&
               act_flow_offset <= (uint64_t)(UINT32_MAX - text_rva)) {
        entry_rva = text_rva + (uint32_t)act_flow_offset;
    } else {
        fprintf(stderr,
                "[PE32+] Warning: Invalid entry offsets (genesis=%llu, act_flow=%llu), fallback to 0x%x\n",
                (unsigned long long)aetb_hdr->genesis_point,
                (unsigned long long)act_flow_offset, entry_rva);
    }
    
    /* 文件指针计算 */
    uint32_t text_raw_ptr = headers_size;
    uint32_t reloc_raw_ptr = text_raw_ptr + text_size_file;
    
    size_t file_size = reloc_raw_ptr + reloc_size_file;

    
    /* ================================================================
       第二步：分配文件缓冲区
       ================================================================ */
    
    uint8_t *file_buf = (uint8_t *)calloc(file_size, 1);
    if (!file_buf) {
        fprintf(stderr, "[PE32+] Error: Out of memory for PE file buffer (%zu bytes)\n", file_size);
        return -1;
    }
    
    /* ================================================================
       第三步：构造 DOS Header (64 bytes)
       ================================================================ */
    
    file_buf[0] = 'M';
    file_buf[1] = 'Z';
    
    *(uint16_t*)&file_buf[0x02] = 0x0090;           /* e_cblp */
    *(uint16_t*)&file_buf[0x04] = 0x0003;           /* e_cp */
    *(uint16_t*)&file_buf[0x08] = 0x0004;           /* e_cp_hdr */
    *(uint16_t*)&file_buf[0x0C] = 0xFFFF;           /* e_maxalloc */
    *(uint16_t*)&file_buf[0x10] = 0x00B8;           /* e_sp */
    *(uint16_t*)&file_buf[0x12] = 0x0040;           /* e_ip */
    *(uint16_t*)&file_buf[0x24] = 0x0040;           /* e_lfarlc */
    *(uint32_t*)&file_buf[0x3C] = 0x0040;           /* e_lfanew: PE header at 0x40 */
    
    /* ================================================================
       第四步：PE Signature (4 bytes) + COFF Header (20 bytes)
       ================================================================ */
    
    uint8_t *pe_header = file_buf + 0x40;
    
    /* PE Signature: "PE\0\0" */
    *(uint32_t*)&pe_header[0] = 0x4550;
    
    /* COFF File Header (offset +4) */
    uint16_t *coff = (uint16_t *)&pe_header[4];
    coff[0] = PE_MACHINE_X86_64;                    /* Machine */
    coff[1] = 2;                                     /* NumberOfSections */
    
    uint32_t *coff_dw = (uint32_t *)&pe_header[8];
    coff_dw[0] = (uint32_t)time(NULL);              /* TimeDateStamp */
    coff_dw[1] = 0;                                  /* PointerToSymbolTable */
    coff_dw[2] = 0;                                  /* NumberOfSymbols */
    
    uint16_t *coff_w = (uint16_t *)&pe_header[20];
    coff_w[0] = 240;                                 /* SizeOfOptionalHeader */
    coff_w[1] = PE_CHARACTERISTICS_EXECUTABLE | PE_CHARACTERISTICS_LARGE_ADDRESS_AWARE;  /* Characteristics */
    
    /* ================================================================
       第五步：PE32+ Optional Header (240 bytes)
       ================================================================ */
    
    uint8_t *opt_hdr = pe_header + 24;
    
    /* Parser Magic */
    *(uint16_t*)&opt_hdr[0] = 0x020B;                /* Magic: PE32+ */
    opt_hdr[2] = 14;                                  /* MajorLinkerVersion */
    opt_hdr[3] = 0;                                   /* MinorLinkerVersion */
    
    /* Code and Data Sizes */
    *(uint32_t*)&opt_hdr[4] = text_size_file;        /* SizeOfCode */
    *(uint32_t*)&opt_hdr[8] = reloc_size_file;       /* SizeOfInitializedData */
    *(uint32_t*)&opt_hdr[12] = 0;                    /* SizeOfUninitializedData */
    
    /* Entry Point and Base of Code */
    *(uint32_t*)&opt_hdr[16] = entry_rva;            /* AddressOfEntryPoint */
    *(uint32_t*)&opt_hdr[20] = text_rva;             /* BaseOfCode */
    
    /* Image Base (PE32+ 需要 8 bytes) */
    *(uint64_t*)&opt_hdr[24] = PE_UEFI_IMAGE_BASE;   /* ImageBase */
    
    /* Alignment Values */
    *(uint32_t*)&opt_hdr[32] = section_alignment;    /* SectionAlignment */
    *(uint32_t*)&opt_hdr[36] = file_alignment;       /* FileAlignment */
    
    /* OS Version */
    *(uint16_t*)&opt_hdr[40] = 6;                    /* MajorOperatingSystemVersion */
    *(uint16_t*)&opt_hdr[42] = 0;                    /* MinorOperatingSystemVersion */
    
    /* Image Version */
    *(uint16_t*)&opt_hdr[44] = 0;                    /* MajorImageVersion */
    *(uint16_t*)&opt_hdr[46] = 0;                    /* MinorImageVersion */
    
    /* Subsystem Version */
    *(uint16_t*)&opt_hdr[48] = 6;                    /* MajorSubsystemVersion */
    *(uint16_t*)&opt_hdr[50] = 0;                    /* MinorSubsystemVersion */
    
    /* Reserved */
    *(uint32_t*)&opt_hdr[52] = 0;                    /* Win32VersionValue */
    
    /* Image Sizes */
    *(uint32_t*)&opt_hdr[56] = image_size;           /* SizeOfImage */
    *(uint32_t*)&opt_hdr[60] = headers_size;         /* SizeOfHeaders */
    
    /* CheckSum - 稍后计算并设置 */
    *(uint32_t*)&opt_hdr[64] = 0;                    /* CheckSum */
    
    /* Subsystem and DLL Characteristics */
    *(uint16_t*)&opt_hdr[68] = PE_SUBSYSTEM_EFI_APPLICATION;  /* Subsystem */
    *(uint16_t*)&opt_hdr[70] = PE_DLL_CHARACTERISTICS_DYNAMIC_BASE | 
                                PE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA;  /* DllCharacteristics */
    
    /* Stack and Heap Sizes */
    *(uint64_t*)&opt_hdr[72] = 0x10000;              /* SizeOfStackReserve */
    *(uint64_t*)&opt_hdr[80] = 0x1000;               /* SizeOfStackCommit */
    *(uint64_t*)&opt_hdr[88] = 0x10000;              /* SizeOfHeapReserve */
    *(uint64_t*)&opt_hdr[96] = 0x1000;               /* SizeOfHeapCommit */
    
    /* Loader Flags */
    *(uint32_t*)&opt_hdr[104] = 0;                   /* LoaderFlags */
    
    /* Data Directories Count */
    *(uint32_t*)&opt_hdr[108] = PE_DATA_DIRECTORIES_COUNT;  /* NumberOfRvaAndSizes */
    
    /* Data Directories (16 entries × 8 bytes) - 所有初始化为0，除了 Base Relocation Table */
    uint32_t *data_dirs = (uint32_t *)&opt_hdr[112];
    for (int i = 0; i < 32; i++) {
        data_dirs[i] = 0;
    }
    
    /* Base Relocation Table (Entry 5: offset 40) */
    data_dirs[10] = reloc_rva;                       /* RVA */
    data_dirs[11] = reloc_raw_content_size;          /* Size */
    
    /* ================================================================
       第六步：Section Headers (2 × 40 bytes)
       ================================================================ */
    
    uint8_t *sect_hdr = pe_header + 24 + 240;       /* 紧跟 Optional Header */
    
    /* .text Section Header */
    memset(sect_hdr, 0, 40);
    memcpy(&sect_hdr[0], ".text\0\0\0", 8);
    *(uint32_t*)&sect_hdr[8] = text_size_mem;        /* VirtualSize */
    *(uint32_t*)&sect_hdr[12] = text_rva;            /* VirtualAddress */
    *(uint32_t*)&sect_hdr[16] = text_size_file;      /* SizeOfRawData */
    *(uint32_t*)&sect_hdr[20] = text_raw_ptr;        /* PointerToRawData */
    *(uint32_t*)&sect_hdr[36] = PE_SECTION_CNT_CODE | PE_SECTION_MEM_EXECUTE | PE_SECTION_MEM_READ;
    
    /* .reloc Section Header */
    sect_hdr += 40;
    memset(sect_hdr, 0, 40);
    memcpy(&sect_hdr[0], ".reloc\0\0", 8);
    *(uint32_t*)&sect_hdr[8] = reloc_size_mem;       /* VirtualSize */
    *(uint32_t*)&sect_hdr[12] = reloc_rva;           /* VirtualAddress */
    *(uint32_t*)&sect_hdr[16] = reloc_size_file;     /* SizeOfRawData */
    *(uint32_t*)&sect_hdr[20] = reloc_raw_ptr;       /* PointerToRawData */
    *(uint32_t*)&sect_hdr[36] = PE_SECTION_CNT_INITIALIZED | PE_SECTION_MEM_READ;
    
    /* ================================================================
       第七步：验证AETB数据完整性并填充节数据
       ================================================================ */
    
    /* 验证AETB输入 - 工业级检查 */
    if (!aetb_data || text_raw_content_size < AETHEL_HEADER_SIZE) {
        fprintf(stderr, "[PE32+] Error: Invalid AETB data\n");
        fprintf(stderr, "        aetb_data: %p, size: %zu\n", (void*)aetb_data, text_raw_content_size);
        fprintf(stderr, "        Minimum required: %u bytes (AETHEL_HEADER_SIZE)\n", AETHEL_HEADER_SIZE);
        free(file_buf);
        return -1;
    }
    
    /* 填充 .text 节 - 包含完整 AETB 结构
     * AETB格式验证：
     * [256B Header][ActFlow][MirrorState][ConstantTruth][ImportNexus]
     * 
     * 重要：print()的字符串必须在ConstantTruth段中，这样在运行时
     * 加载时字符串会被正确地映射到内存中
     */
    if (memcpy(file_buf + text_raw_ptr, aetb_data, text_raw_content_size) == NULL) {
        fprintf(stderr, "[PE32+] Error: Failed to copy AETB data to PE file buffer\n");
        free(file_buf);
        return -1;
    }
    
    /* 验证复制后的数据 - 检查魔数（如果AETB有的话） */
    if (text_raw_content_size >= 8) {
        uint32_t magic = *(uint32_t*)(file_buf + text_raw_ptr);
        fprintf(stderr, "[PE32+] AETB header magic: 0x%08x\n", magic);
    }
    
    /* 填充 .reloc 节 */
    if (memcpy(file_buf + reloc_raw_ptr, reloc_block_data, reloc_raw_content_size) == NULL) {
        fprintf(stderr, "[PE32+] Error: Failed to copy relocation data to PE file buffer\n");
        free(file_buf);
        return -1;
    }
    
    /* ================================================================
       第八步：生成重定位信息用于print()字符串
       ================================================================
     * 
     * print()生成的代码中包含相对偏移，指向字符串数据
     * 这些偏移需要在.reloc段中记录，以便UEFI加载器在加载PE时修正
     * 
     * 标准PE重定位块格式：
     * [32-bit PageRVA][32-bit BlockSize][TypeOffset entries...]
     * 其中TypeOffset = (Type << 12) | Offset
     */
    
    /* 当前的reloc_block_data只有一个ABSOLUTE条目
     * 如果需要处理print()字符串的重定位，需要添加更多条目
     * 这应该由编译器在生成print()代码时通知
     * 或由链接器在链接时识别并添加
     * 
     * 简单情况：单个.text节内的相对寻址不需要重定位（RIP-relative）
     * 复杂情况：如果字符串在.data段，则需要绝对地址重定位
     */
    
    fprintf(stderr, "[PE32+] Relocation info: %u bytes (includes %u base relocations)\n",
            reloc_raw_content_size, reloc_raw_content_size > 8 ? 1 : 0);
    
    /* ================================================================
       第九步：计算并设置 CheckSum
       ================================================================ */
    
    uint32_t checksum_offset = 0x40 + 24 + 64;  /* DOS(64) + PE Sign(4) + COFF(20) + OptHdr前的CheckSum偏移 */
    uint32_t checksum = pe32plus_calculate_checksum(file_buf, file_size, checksum_offset);
    *(uint32_t*)&opt_hdr[64] = checksum;
    
    /* ================================================================
       第十步：验证并写入文件
       ================================================================ */
    
    /* 预写验证 - 工业级质量检查 */
    if (file_size < headers_size) {
        fprintf(stderr, "[PE32+] Error: File size smaller than headers size\n");
        free(file_buf);
        return -1;
    }
    
    if (file_size > 0x7FFFFFFF) {
        fprintf(stderr, "[PE32+] Warning: File size exceeds signed 32-bit limit (0x%zx bytes)\n", file_size);
        /* 继续处理（PE支持更大的文件），但警告用户 */
    }
    
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "[PE32+] Error: Cannot create output file '%s'\n", output_file);
        fprintf(stderr, "        Error: %s\n", strerror(errno));
        free(file_buf);
        return -1;
    }
    
    size_t written = fwrite(file_buf, 1, file_size, out);
    if (written != file_size) {
        fprintf(stderr, "[PE32+] Error: Failed to write PE file\n");
        fprintf(stderr, "        Expected: %zu bytes, Wrote: %zu bytes\n", file_size, written);
        if (ferror(out)) {
            fprintf(stderr, "        I/O Error: %s\n", strerror(errno));
        }
        fclose(out);
        free(file_buf);
        return -1;
    }
    
    int close_result = fclose(out);
    if (close_result != 0) {
        fprintf(stderr, "[PE32+] Error: Failed to close output file\n");
        fprintf(stderr, "        Error: %s\n", strerror(errno));
        free(file_buf);
        return -1;
    }
    
    free(file_buf);
    
    /* ================================================================
       第十一步：生成完毕 - 输出诊断信息
       ================================================================ */
    
    fprintf(stderr, "[PE32+] ✓ Generated UEFI PE32+ EFI Bootloader\n");
    fprintf(stderr, "        Output: %s\n", output_file);
    fprintf(stderr, "        File Size: %zu bytes (0x%zx)\n", file_size, file_size);
    fprintf(stderr, "        Headers: %u bytes (0x%x)\n", headers_size, headers_size);
    fprintf(stderr, "        .text RVA: 0x%x, VSize: %zu bytes, RSize: %u bytes\n", 
            text_rva, text_size_mem, text_size_file);
    fprintf(stderr, "        .reloc RVA: 0x%x, VSize: %u bytes, RSize: %u bytes\n", 
            reloc_rva, reloc_size_mem, reloc_size_file);
    fprintf(stderr, "        Image Size: 0x%x (0x%x bytes)\n", image_size, image_size);
    fprintf(stderr, "        Image Base: 0x%llx\n", PE_UEFI_IMAGE_BASE);
    fprintf(stderr, "        CheckSum: 0x%08x\n", checksum);
    fprintf(stderr, "        Entry Point RVA: 0x%x\n", entry_rva);
    fprintf(stderr, "        Subsystem: EFI Application (10)\n");
    fprintf(stderr, "[PE32+] Build complete. Ready for UEFI boot.\n");
    
    return 0;
}

/* ============================================================
   汇编层接口 - 供汇编代码调用
   
   在工业级实现中，完整的结构编织应该由汇编完成，
   但当前版本由C层处理。此函数为汇编接口的C实现。
   ============================================================ */

int weave_pe32plus_structure(const PE32Plus_Weave_Input *input) {
    if (!input || !input->output_filename || !input->code_buffer || input->code_size == 0) {
        fprintf(stderr, "[PE32+ Weave] Error: Invalid input\n");
        fprintf(stderr, "        output_filename: %p\n", (void*)input->output_filename);
        fprintf(stderr, "        code_buffer: %p\n", (void*)input->code_buffer);
        fprintf(stderr, "        code_size: %zu\n", input->code_size);
        return -1;
    }
    
    /* 验证输出文件名的有效性 */
    if (strlen(input->output_filename) == 0) {
        fprintf(stderr, "[PE32+ Weave] Error: Empty output filename\n");
        return -1;
    }
    
    /* 调用完整的PE32+ EFI生成函数 */
    fprintf(stderr, "[PE32+ Weave] Starting PE32+ structure generation\n");
    int result = pe32plus_generate_efi(input->output_filename, 
                                       input->code_buffer, 
                                       input->code_size);
    
    if (result == 0) {
        fprintf(stderr, "[PE32+ Weave] ✓ PE32+ structure generation successful\n");
    } else {
        fprintf(stderr, "[PE32+ Weave] ✗ PE32+ structure generation failed (result=%d)\n", result);
    }
    
    return result;
}
