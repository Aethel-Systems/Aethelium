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

 #include "wsys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define WSYS_MACHINE_AMD64 0x8664

typedef struct {
    const char *func_name;
    uint32_t marker;
    uint32_t iat_rva;
    uint32_t name_rva;
    int used;
} WsysImportSpec;

static WsysImportSpec g_wsys_imports[] = {
    {"DbgPrint", 0x77570001U, 0, 0, 0},
    {"ExAllocatePoolWithTag", 0x77570002U, 0, 0, 0},
    {"ExFreePoolWithTag", 0x77570003U, 0, 0, 0},
    {"IoCreateDevice", 0x77570004U, 0, 0, 0},
    {"IoCreateSymbolicLink", 0x77570005U, 0, 0, 0},
    {"IoDeleteDevice", 0x77570006U, 0, 0, 0},
    {"IoDeleteSymbolicLink", 0x77570007U, 0, 0, 0},
    {"IoCompleteRequest", 0x77570008U, 0, 0, 0},
    {"RtlInitUnicodeString", 0x77570009U, 0, 0, 0},
    {"KeDelayExecutionThread", 0x7757000AU, 0, 0, 0},
    {"MmGetSystemRoutineAddress", 0x7757000BU, 0, 0, 0}
};
#define NUM_WSYS_IMPORTS (sizeof(g_wsys_imports)/sizeof(g_wsys_imports[0]))

/* 工业级对齐 */
static uint32_t wsys_align_up(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return ((value + alignment - 1) / alignment) * alignment;
}

static void wsys_fill_section_name(uint8_t *name_field, const char *name) {
    memset(name_field, 0, 8);
    if (name) {
        size_t len = strlen(name);
        memcpy(name_field, name, len > 8 ? 8 : len);
    }
}

/* ======================================================================
 * Carrillo-Sahni Checksum (Microsoft PE mandatory checksum)
 * ====================================================================== */
static uint32_t wsys_calculate_checksum(const uint8_t *file_data, size_t file_size, uint32_t checksum_offset) {
    uint32_t sum = 0;
    const uint16_t *ptr = (const uint16_t *)file_data;
    size_t count = file_size / 2;
    uint32_t skip_idx = checksum_offset / 2;
    
    for (size_t i = 0; i < count; i++) {
        if (i == skip_idx || i == skip_idx + 1) continue;
        sum += ptr[i];
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    if (file_size % 2) {
        sum += file_data[file_size - 1];
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    sum = (sum & 0xFFFF) + (sum >> 16);
    sum += (uint32_t)file_size;
    return sum;
}

/* ======================================================================
 * DIR64 Base Relocation Builder (Page Grouping)
 * ====================================================================== */
static int wsys_build_reloc_section(const uint32_t* reloc_rvas, size_t count, uint8_t** out_buf, uint32_t* out_size) {
    if (count == 0) {
        /* 工业级修复：只需 12 字节即可。
           绝对禁止添加 Block Size = 0 的 Dummy 终止符！
           这会导致内核 LdrRelocateImageWithBias 报错 487 并拒绝加载！ */
        uint8_t* buf = (uint8_t*)calloc(1, 12);
        *(uint32_t*)(buf + 0) = 0x3000; /* 先用 .rdata 默认段地址占位，后续会被修正为 text_rva */
        *(uint32_t*)(buf + 4) = 12;     /* Block Size: 8 字节头部 + 4 字节数据(2个空Entry) = 12 */
        *(uint16_t*)(buf + 8) = 0;      /* Entry: Type=0 (ABSOLUTE), Offset=0 */
        *(uint16_t*)(buf + 10) = 0;     /* Entry: Padding 补齐 4 字节 */
        
        *out_buf = buf;
        *out_size = 12;                 /* 尺寸严格等于 12，禁止附带任何终止块 */
        return 0;
    }

    uint32_t *rvas = (uint32_t *)malloc(count * sizeof(uint32_t));
    memcpy(rvas, reloc_rvas, count * sizeof(uint32_t));
    for (size_t i = 0; i < count - 1; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (rvas[i] > rvas[j]) {
                uint32_t tmp = rvas[i]; rvas[i] = rvas[j]; rvas[j] = tmp;
            }
        }
    }
    
    size_t cap = count * 12 + 128;
    uint8_t* buf = (uint8_t*)malloc(cap);
    if (!buf) {
        free(rvas);
        return -1;
    }
    size_t size = 0;
    size_t i = 0;

    while (i < count) {
        uint32_t page_rva = rvas[i] & ~0xFFF;
        size_t block_start = size;
        
        *(uint32_t*)(buf + size) = page_rva; size += 4;
        size += 4; /* Placeholder for Block Size */

        while (i < count && (rvas[i] & ~0xFFF) == page_rva) {
            uint16_t entry = (10 << 12) | (rvas[i] & 0xFFF); /* 10 = IMAGE_REL_BASED_DIR64 */
            *(uint16_t*)(buf + size) = entry;
            size += 2;
            i++;
        }
        
        if ((size - block_start) % 4 != 0) {
            *(uint16_t*)(buf + size) = 0; /* IMAGE_REL_BASED_ABSOLUTE Padding */
            size += 2;
        }

        *(uint32_t*)(buf + block_start + 4) = (uint32_t)(size - block_start);
    }

    /* 工业级修复：去掉了原来写入 size + 8 的 0 RVA, 0 Size 终止块 */
    
    free(rvas);
    *out_buf = buf;
    *out_size = (uint32_t)size; /* 真实大小，交由 DataDirectory[5].Size 定界 */
    return 0;
}

/* ======================================================================
 * 驱动自动签名 (Windows Only)
 * ====================================================================== */
static void wsys_sign_driver(const char* sys_path) {
#ifdef _WIN32
    char cmd[4096];
    fprintf(stderr, "[WSYS] Auto-signing kernel driver '%s'...\n", sys_path);
    snprintf(cmd, sizeof(cmd),
        "powershell -NoProfile -ExecutionPolicy Bypass -Command \""
        "$cert = Get-ChildItem -Path Cert:\\CurrentUser\\My | Where-Object { $_.Subject -match 'AethelOS Kernel Dev' } | Select-Object -First 1; "
        "if (-not $cert) { "
        "  $cert = New-SelfSignedCertificate -Subject 'CN=AethelOS Kernel Dev' -CertStoreLocation 'Cert:\\CurrentUser\\My' -Type CodeSigningCert; "
        "} "
        "Set-AuthenticodeSignature -FilePath '%s' -Certificate $cert\"",
        sys_path);
    int ret = system(cmd);
    if (ret == 0) {
        fprintf(stderr, "[WSYS] Successfully signed driver '%s'.\n", sys_path);
    } else {
        fprintf(stderr, "[WSYS] Warning: Failed to sign driver. Run as Administrator or check PowerShell policies.\n");
    }
#else
    (void)sys_path;
    fprintf(stderr, "[WSYS] Driver signing is only supported on Windows hosts.\n");
#endif
}

/* ======================================================================
 * 核心驱动编织逻辑
 * ====================================================================== */
int wsys_generate_image(const WSYS_Input *input) {
    if (!input || !input->output_filename || !input->code_section || input->code_size == 0) return -1;

    uint8_t *code_buf = (uint8_t *)malloc(input->code_size);
    memcpy(code_buf, input->code_section, input->code_size);
    size_t used_count = 0;

    /* Scan for IAT markers and flag them */
    for (size_t i = 0; i + 4 <= input->code_size; ++i) {
        uint32_t marker = *(uint32_t*)&code_buf[i];
        for (size_t j = 0; j < NUM_WSYS_IMPORTS; j++) {
            if (marker == g_wsys_imports[j].marker) {
                if (!g_wsys_imports[j].used) {
                    g_wsys_imports[j].used = 1;
                    used_count++;
                }
            }
        }
    }
    
    /* 第一阶段：仅统计各数据段大小（不进行具体数据渲染） */
    uint32_t rdata_virtual_size = 0;
    if (used_count > 0) {
        size_t desc_size = 20 * 2; /* 1 descriptor + 1 null terminator */
        size_t ilt_size = (used_count + 1) * 8;
        size_t iat_size = (used_count + 1) * 8;
        size_t names_size = 0;
        for (size_t j = 0; j < NUM_WSYS_IMPORTS; j++) {
            if (g_wsys_imports[j].used) {
                names_size += 2 + strlen(g_wsys_imports[j].func_name) + 1;
                names_size += (names_size % 2); /* align */
            }
        }
        size_t dll_name_size = strlen("ntoskrnl.exe") + 1;
        dll_name_size += (dll_name_size % 2);
        rdata_virtual_size = (uint32_t)(desc_size + ilt_size + iat_size + names_size + dll_name_size);
    }

    uint32_t pdata_virtual_size = (input->machine_type == WSYS_MACHINE_ARM64) ? 8 : 16;

    /* 第二阶段：基于精确段大小，执行全局唯一的 RVA 和 RAW 内存布局计算（单通道计算，杜绝硬编码脱节与 Shadowing） */
    uint16_t num_sections = 1; /* .text */
    if (input->data_size > 0) num_sections++;
    if (input->rodata_size > 0) num_sections++;
    if (rdata_virtual_size > 0) num_sections++;
    if (pdata_virtual_size > 0) num_sections++;
    num_sections++; /* 预留 .reloc 节区 */

    uint32_t headers_size = wsys_align_up(64 + 4 + 20 + 240 + (num_sections * 40), WSYS_FILE_ALIGNMENT);
    
    uint32_t current_rva = WSYS_SECTION_ALIGNMENT;
    uint32_t current_raw = headers_size;

    uint32_t text_rva = current_rva, text_raw = current_raw;
    current_rva += wsys_align_up(input->code_size, WSYS_SECTION_ALIGNMENT);
    current_raw += wsys_align_up(input->code_size, WSYS_FILE_ALIGNMENT);

    uint32_t data_rva = 0, data_raw = 0;
    if (input->data_size > 0) {
        data_rva = current_rva; data_raw = current_raw;
        current_rva += wsys_align_up(input->data_size, WSYS_SECTION_ALIGNMENT);
        current_raw += wsys_align_up(input->data_size, WSYS_FILE_ALIGNMENT);
    }

    uint32_t rodata_rva = 0, rodata_raw = 0;
    if (input->rodata_size > 0) {
        rodata_rva = current_rva; rodata_raw = current_raw;
        current_rva += wsys_align_up(input->rodata_size, WSYS_SECTION_ALIGNMENT);
        current_raw += wsys_align_up(input->rodata_size, WSYS_FILE_ALIGNMENT);
    }
    
    uint32_t rdata_rva = 0, rdata_raw = 0;
    if (rdata_virtual_size > 0) {
        rdata_rva = current_rva; rdata_raw = current_raw;
        current_rva += wsys_align_up(rdata_virtual_size, WSYS_SECTION_ALIGNMENT);
        current_raw += wsys_align_up(rdata_virtual_size, WSYS_FILE_ALIGNMENT);
    }

    uint32_t pdata_rva = 0, pdata_raw = 0;
    if (pdata_virtual_size > 0) {
        pdata_rva = current_rva; pdata_raw = current_raw;
        current_rva += wsys_align_up(pdata_virtual_size, WSYS_SECTION_ALIGNMENT);
        current_raw += wsys_align_up(pdata_virtual_size, WSYS_FILE_ALIGNMENT);
    }

    /* 第三阶段：渲染 .rdata 结构与 .pdata 缓存 */
    uint8_t *rdata_buf = NULL;
    if (used_count > 0) {
        rdata_buf = (uint8_t *)calloc(1, rdata_virtual_size);
        size_t desc_size = 20 * 2;
        size_t ilt_size = (used_count + 1) * 8;
        size_t iat_size = (used_count + 1) * 8;
        size_t names_size = 0;
        for (size_t j = 0; j < NUM_WSYS_IMPORTS; j++) {
            if (g_wsys_imports[j].used) {
                names_size += 2 + strlen(g_wsys_imports[j].func_name) + 1;
                names_size += (names_size % 2);
            }
        }
        size_t dll_name_size = strlen("ntoskrnl.exe") + 1;
        dll_name_size += (dll_name_size % 2);

        uint32_t ilt_rva = rdata_rva + desc_size;
        uint32_t iat_rva = ilt_rva + ilt_size;
        uint32_t names_rva = iat_rva + iat_size;
        uint32_t dll_rva = names_rva + names_size;

        /* 填充导入描述符 */
        *(uint32_t*)(rdata_buf + 0) = ilt_rva;
        *(uint32_t*)(rdata_buf + 12) = dll_rva;
        *(uint32_t*)(rdata_buf + 16) = iat_rva;

        /* 写入 DLL 字符串与导入表项 */
        memcpy(rdata_buf + dll_rva - rdata_rva, "ntoskrnl.exe", 13);
        
        size_t curr_idx = 0;
        uint32_t curr_name_rva = names_rva;
        for (size_t j = 0; j < NUM_WSYS_IMPORTS; j++) {
            if (g_wsys_imports[j].used) {
                g_wsys_imports[j].iat_rva = iat_rva + curr_idx * 8;
                *(uint64_t*)(rdata_buf + desc_size + curr_idx * 8) = curr_name_rva;
                *(uint64_t*)(rdata_buf + desc_size + ilt_size + curr_idx * 8) = curr_name_rva;
                
                size_t len = strlen(g_wsys_imports[j].func_name);
                memcpy(rdata_buf + curr_name_rva - rdata_rva + 2, g_wsys_imports[j].func_name, len + 1);
                curr_name_rva += 2 + len + 1;
                curr_name_rva += (curr_name_rva % 2);
                curr_idx++;
            }
        }
    }
    
    uint8_t *reloc_buf = NULL;
    uint32_t reloc_virtual_size = 0;
    uint32_t reloc_rvas[1024];
    size_t reloc_count = 0;

    uint8_t *pdata_buf = (uint8_t *)calloc(1, pdata_virtual_size);
    
    /* Fill .pdata */
    if (input->machine_type == WSYS_MACHINE_ARM64) {
        *(uint32_t*)(pdata_buf + 0) = text_rva + input->entry_point_offset; /* 必须精确指向真实的 DriverEntry 起点 */
        uint32_t func_len = (input->code_size - input->entry_point_offset) / 4;
        if (func_len > 0x7FF) func_len = 0x7FF;
        /* 工业级修复：CR=3 (bits 15-18) 代表这是一个拥有标准栈帧 (stp x29,x30,[sp,#-16]!) 的函数，吻合机器码，通过验证！ */
        *(uint32_t*)(pdata_buf + 4) = 1 | (func_len << 2) | (3 << 15);
    } else {
        *(uint32_t*)(pdata_buf + 0) = text_rva + input->entry_point_offset; /* x86_64 同样必须对齐入口 */
        *(uint32_t*)(pdata_buf + 4) = text_rva + input->code_size;
        *(uint32_t*)(pdata_buf + 8) = pdata_rva + 12;
        pdata_buf[12] = 0x01;
        pdata_buf[13] = 0x00;
        pdata_buf[14] = 0x00;
        pdata_buf[15] = 0x00;
    }
    
    /* Patch .text IAT displacements & Collect relocation targets on ARM64 */
    for (size_t i = 0; i + 8 <= input->code_size; ++i) {
        uint32_t marker = *(uint32_t*)&code_buf[i];
        for (size_t j = 0; j < NUM_WSYS_IMPORTS; j++) {
            if (g_wsys_imports[j].used && marker == g_wsys_imports[j].marker) {
                if (input->machine_type == WSYS_MACHINE_ARM64) {
                    /* ARM64 使用 ADRP + LDR PC相对寻址以符合 HVCI 规范，无需绝对重定位 */
                    if (i >= 16) {
                        uint32_t iat_rva = g_wsys_imports[j].iat_rva;
                        uint32_t insn_rva = text_rva + (uint32_t)i - 16; /* ADRP instruction RVA */
                        
                        int32_t page_offset = (int32_t)((iat_rva & ~0xFFF) - (insn_rva & ~0xFFF));
                        int32_t imm21 = page_offset / 4096;
                        uint32_t immlo = (uint32_t)imm21 & 3;
                        uint32_t immhi = ((uint32_t)imm21 >> 2) & 0x7FFFF;
                        uint32_t adrp_insn = 0x90000009 | (immlo << 29) | (immhi << 5);
                        *(uint32_t*)&code_buf[i - 16] = adrp_insn;
                        
                        uint32_t page_offset_12 = iat_rva & 0xFFF;
                        uint32_t imm12 = page_offset_12 >> 3;
                        uint32_t ldr_insn = 0xF9400129 | (imm12 << 10);
                        *(uint32_t*)&code_buf[i - 12] = ldr_insn;
                    }
                    /* 跳过占位符，无需重定位 */
                    i += 7;
                } else {
                    /* x86-64 保持原有的 RIP 相对 32 位偏置 */
                    int32_t disp32 = (int32_t)((int64_t)g_wsys_imports[j].iat_rva - ((int64_t)text_rva + i + 4));
                    *(int32_t*)&code_buf[i] = disp32;
                    i += 3;
                }
                break;
            }
        }
    }
    
    headers_size = wsys_align_up(64 + 4 + 20 + 240 + (num_sections * 40), WSYS_FILE_ALIGNMENT);
    
    /* 重新微调由于重定位节增删而改变的所有段 RAW 指针 */
    current_raw = headers_size;
    text_raw = current_raw;
    current_raw += wsys_align_up(input->code_size, WSYS_FILE_ALIGNMENT);
    if (input->data_size > 0) {
        data_raw = current_raw;
        current_raw += wsys_align_up(input->data_size, WSYS_FILE_ALIGNMENT);
    }
    if (input->rodata_size > 0) {
        rodata_raw = current_raw;
        current_raw += wsys_align_up(input->rodata_size, WSYS_FILE_ALIGNMENT);
    }
    if (rdata_virtual_size > 0) {
        rdata_raw = current_raw;
        current_raw += wsys_align_up(rdata_virtual_size, WSYS_FILE_ALIGNMENT);
    }
    if (pdata_virtual_size > 0) {
        pdata_raw = current_raw;
        current_raw += wsys_align_up(pdata_virtual_size, WSYS_FILE_ALIGNMENT);
    }
    
    uint32_t reloc_rva = 0, reloc_raw = 0;
    wsys_build_reloc_section(reloc_rvas, reloc_count, &reloc_buf, &reloc_virtual_size);
    if (reloc_virtual_size > 0) {
        
        if (reloc_count == 0) {
        /*
         * 修正：使用 text_rva（必定为 0x1000 且必然存在于任何 PE 镜像中）替代硬编码 0x3000。
         * 因为 ABSOLUTE 重定位属于 NO-OP（不作实质读写），在任何只读或执行页面上均属合规操作，
         * 且不会因页超出边界而触发 STATUS_INVALID_IMAGE_FORMAT。
         */
        uint32_t safe_rva = text_rva;
        *(uint32_t*)(reloc_buf + 0) = safe_rva;
    }
        reloc_rva = current_rva; reloc_raw = current_raw;
        current_rva += wsys_align_up(reloc_virtual_size, WSYS_SECTION_ALIGNMENT);
        current_raw += wsys_align_up(reloc_virtual_size, WSYS_FILE_ALIGNMENT);
    }

    size_t file_size = current_raw;
    uint8_t *file_buf = (uint8_t *)calloc(file_size, 1);

    /* DOS + PE + COFF */
    file_buf[0] = 'M'; file_buf[1] = 'Z';
    *(uint32_t *)&file_buf[0x3C] = 0x40;
    *(uint32_t *)&file_buf[0x40] = 0x4550;
    
    WSYS_COFF_Header *coff = (WSYS_COFF_Header *)&file_buf[0x44];
    coff->Machine = input->machine_type;                     /* 动态配置 AMD64/ARM64 架构标志 */
    coff->NumberOfSections = num_sections;
    coff->TimeDateStamp = (uint32_t)time(NULL);
    coff->SizeOfOptionalHeader = 240;
    coff->Characteristics = 0x0022; /* EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE */
    
    WSYS_Optional_Header *opt = (WSYS_Optional_Header *)&file_buf[0x58];
    opt->Magic = WSYS_MAGIC;
    opt->MajorLinkerVersion = 14;
    opt->SizeOfCode = wsys_align_up(input->code_size, WSYS_FILE_ALIGNMENT);
    /* 修正：必须将重定位（.reloc）节的文件大小一并计入全局已初始化数据量大小 */
    opt->SizeOfInitializedData = wsys_align_up(input->data_size, WSYS_FILE_ALIGNMENT) +
                                 wsys_align_up(input->rodata_size, WSYS_FILE_ALIGNMENT) +
                                 wsys_align_up(rdata_virtual_size, WSYS_FILE_ALIGNMENT) +
                                 wsys_align_up(pdata_virtual_size, WSYS_FILE_ALIGNMENT) +
                                 wsys_align_up(reloc_virtual_size, WSYS_FILE_ALIGNMENT);
    opt->AddressOfEntryPoint = text_rva + input->entry_point_offset;
    opt->BaseOfCode = text_rva;
    
    /* 工业级修复：强行使用安全的内核空间高地址映射基址，绕过 487 (INVALID_ADDRESS) 的检查拦截 */
    opt->ImageBase = 0xFFFFF80000000000ULL;
    
    opt->SectionAlignment = WSYS_SECTION_ALIGNMENT;
    opt->FileAlignment = WSYS_FILE_ALIGNMENT;
    opt->MajorOperatingSystemVersion = 10;
    opt->MinorOperatingSystemVersion = 0;
    opt->MajorImageVersion = 10;
    opt->MinorImageVersion = 0;
    opt->MajorSubsystemVersion = 10;
    opt->MinorSubsystemVersion = 0;
    opt->SizeOfImage = current_rva;
    opt->SizeOfHeaders = headers_size;
    opt->Subsystem = WSYS_SUBSYSTEM_NATIVE;
    
    /* 工业级修复：驱动专属特性标志 (WDM_DRIVER | DYNAMIC_BASE | NX_COMPAT | HIGH_ENTROPY_VA)
       剔除了对内核无意义甚至会导致审核不过的 TERMINAL_SERVER_AWARE (0x8000) */
    opt->DllCharacteristics = 0x2160;
    opt->SizeOfStackReserve = 0x100000;
    opt->SizeOfStackCommit = 0x1000;
    opt->SizeOfHeapReserve = 0x100000;
    opt->SizeOfHeapCommit = 0x1000;
    opt->NumberOfRvaAndSizes = 16;
    
    if (rdata_virtual_size > 0) {
        opt->DataDirectories[WSYS_DIR_IMPORT].VirtualAddress = rdata_rva;
        opt->DataDirectories[WSYS_DIR_IMPORT].Size = 40; /* 工业级修复：必须包含 Null Terminator (20 * 2) 否则引发 Loader 解析灾难 */
        opt->DataDirectories[WSYS_DIR_IAT].VirtualAddress = rdata_rva + 40 + (used_count + 1) * 8;
        opt->DataDirectories[WSYS_DIR_IAT].Size = (used_count + 1) * 8;
    }
    if (pdata_virtual_size > 0) {
        opt->DataDirectories[3].VirtualAddress = pdata_rva; /* Exception Directory */
        opt->DataDirectories[3].Size = pdata_virtual_size;
    }
    if (reloc_virtual_size > 0) {
        opt->DataDirectories[WSYS_DIR_RELOC].VirtualAddress = reloc_rva;
        opt->DataDirectories[WSYS_DIR_RELOC].Size = reloc_virtual_size;
    }

    uint8_t *sect_hdrs = &file_buf[0x148];
    int sect_idx = 0;

    /* .text */
    wsys_fill_section_name(&sect_hdrs[sect_idx * 40], ".text");
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 8] = (uint32_t)input->code_size;
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 12] = text_rva;
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 16] = wsys_align_up(input->code_size, WSYS_FILE_ALIGNMENT);
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 20] = text_raw;
    *(uint32_t *)&sect_hdrs[sect_idx * 40 + 36] = WSYS_SCN_TEXT;
    sect_idx++;

    if (input->data_size > 0) {
        wsys_fill_section_name(&sect_hdrs[sect_idx * 40], ".data");
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 8] = (uint32_t)input->data_size;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 12] = data_rva;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 16] = wsys_align_up(input->data_size, WSYS_FILE_ALIGNMENT);
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 20] = data_raw;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 36] = WSYS_SCN_DATA;
        sect_idx++;
    }

    if (input->rodata_size > 0) {
        wsys_fill_section_name(&sect_hdrs[sect_idx * 40], ".rodata");
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 8] = (uint32_t)input->rodata_size;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 12] = rodata_rva;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 16] = wsys_align_up(input->rodata_size, WSYS_FILE_ALIGNMENT);
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 20] = rodata_raw;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 36] = WSYS_SCN_RDATA;
        sect_idx++;
    }

    if (rdata_virtual_size > 0) {
        wsys_fill_section_name(&sect_hdrs[sect_idx * 40], ".rdata");
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 8] = rdata_virtual_size;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 12] = rdata_rva;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 16] = wsys_align_up(rdata_virtual_size, WSYS_FILE_ALIGNMENT);
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 20] = rdata_raw;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 36] = WSYS_SCN_RDATA;
        sect_idx++;
    }

    if (pdata_virtual_size > 0) {
        wsys_fill_section_name(&sect_hdrs[sect_idx * 40], ".pdata");
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 8] = pdata_virtual_size;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 12] = pdata_rva;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 16] = wsys_align_up(pdata_virtual_size, WSYS_FILE_ALIGNMENT);
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 20] = pdata_raw;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 36] = WSYS_SCN_RDATA;
        sect_idx++;
    }
    
    if (reloc_virtual_size > 0) {
        wsys_fill_section_name(&sect_hdrs[sect_idx * 40], ".reloc");
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 8] = reloc_virtual_size;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 12] = reloc_rva;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 16] = wsys_align_up(reloc_virtual_size, WSYS_FILE_ALIGNMENT);
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 20] = reloc_raw;
        *(uint32_t *)&sect_hdrs[sect_idx * 40 + 36] = WSYS_SCN_RELOC;
        sect_idx++; /* 修正：确保节表项序号完整递增，保持与 NumberOfSections 的统计一致性 */
    }

    /* 写缓冲 */
    memcpy(&file_buf[text_raw], code_buf, input->code_size);
    if (input->data_size > 0) memcpy(&file_buf[data_raw], input->data_section, input->data_size);
    if (input->rodata_size > 0) memcpy(&file_buf[rodata_raw], input->rodata_section, input->rodata_size);
    if (rdata_virtual_size > 0) memcpy(&file_buf[rdata_raw], rdata_buf, rdata_virtual_size);
    if (pdata_virtual_size > 0) memcpy(&file_buf[pdata_raw], pdata_buf, pdata_virtual_size);
    if (reloc_virtual_size > 0) memcpy(&file_buf[reloc_raw], reloc_buf, reloc_virtual_size);

    /* 工业级修复：对于 PE32+ (64位) 格式，CheckSum 字段在 Optional Header 的绝对文件偏移量为 0x98。
           原代码使用 0xD8 会导致清零错误的字段并计算出无效的校验和，引发内核加载器拒绝加载。 */
    opt->CheckSum = wsys_calculate_checksum(file_buf, file_size, 0x98);

    FILE *out = fopen(input->output_filename, "wb");
    if (!out) {
        free(file_buf); free(code_buf); free(rdata_buf); free(pdata_buf); free(reloc_buf);
        return -1;
    }
    fwrite(file_buf, 1, file_size, out);
    fclose(out);

    fprintf(stderr, "[WSYS] ✓ Generated Windows Kernel Driver: %s\n", input->output_filename);
    fprintf(stderr, "         CheckSum: 0x%08X\n", opt->CheckSum);
    
    wsys_sign_driver(input->output_filename);

    free(file_buf); free(code_buf); free(rdata_buf); free(pdata_buf); free(reloc_buf);
    return 0;
}
