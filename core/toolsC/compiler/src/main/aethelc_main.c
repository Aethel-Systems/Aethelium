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
 * AethelOS Aethelium Compiler - Main Driver
 * aethelc: 编译器与链接器的主入口点
 *
 * 用法:
 * 编译: aethelc <input.ae> -o <output.aetb> [options]
 * 链接: aethelc --link -o <output> <obj1> <obj2> ... --entry <symbol>
 * ISO:  aethelc --iso -o <output.iso> --kernel <kernel> --efi <boot.efi> [--size <MB>]
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

#include "aec_lexer.h"
#include "aec_parser.h"
#include "semantic_checker.h"
#include "../frontend/import_resolver.h"
#include "../frontend/aecf_parser.h"
#include <libgen.h>
#include "aec_codegen.h"
#include "let_gen.h"
#include "let_weaver_bridge.h"
#include "../include/aefs.h"
#include "../include/binary_format.h"
#include "../frontend/preprocessor.h"
#include "../frontend/lexer/unix_strike.h"
#include "../formats.h"
#include "../formats/common/format_common.h"
#include "../formats/efi/pe.h"
#include "../formats/macho/macho_weaver.h"
#include "../formats/im4p/im4p_generator.h"
#include "../middleend/builtin_print.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#define MAX_INPUT_FILES 128

/* 编译目标格式 */
#define FORMAT_AETB 0
#define FORMAT_AKI  1
#define FORMAT_SRV  2
#define FORMAT_HDA  3
#define FORMAT_EFI  4        /* UEFI PE32+ 嵌入式 AETB Bootloader */
#define FORMAT_LET  5
#define FORMAT_BIN  6
#define FORMAT_PE   7        /* UEFI PE32+ 工业级应用（新增） */
#define FORMAT_ROM  8        /* ROM 固件镜像（可直接刷写） */
#define FORMAT_MACHO 9       /* 裸机 Mach-O 固件镜像 (Apple Silicon) */
#define FORMAT_IM4P 10       /* Image4 Payload 容器 (iBoot + Apple Silicon) */
#define FORMAT_EXE  11       /* Windows PE32+ Console Executable (独立完整实现) */
#define FORMAT_WSYS 12       /* Windows Kernel Driver (.sys) 格式 */

#define EXE_PORTAL_IAT_MARK_NTWRITEFILE               0x11F1E1A1U
#define EXE_PORTAL_IAT_MARK_NTTERMINATEPROCESS        0x22F2E2A2U
#define EXE_HOST_IAT_MARK_NTALLOCATEVIRTUALMEMORY     0x33F3E3B1U
#define EXE_HOST_IAT_MARK_NTALLOCATEVIRTUALMEMORYEX   0x33F3E3B2U
#define EXE_HOST_IAT_MARK_NTFREEVIRTUALMEMORY         0x33F3E3B3U
#define EXE_HOST_IAT_MARK_NTPROTECTVIRTUALMEMORY      0x33F3E3B4U
#define EXE_HOST_IAT_MARK_NTQUERYVIRTUALMEMORY        0x33F3E3B5U
#define EXE_HOST_IAT_MARK_NTLOCKVIRTUALMEMORY         0x33F3E3B6U
#define EXE_HOST_IAT_MARK_NTUNLOCKVIRTUALMEMORY       0x33F3E3B7U
#define EXE_HOST_IAT_MARK_NTCREATETHREADEX            0x33F3E3B8U
#define EXE_HOST_IAT_MARK_NTCREATEFILE                0x33F3E3B9U
#define EXE_HOST_IAT_MARK_NTREADFILE                  0x33F3E3BAU
#define EXE_HOST_IAT_MARK_NTCREATEEVENT               0x33F3E3BBU
#define EXE_HOST_IAT_MARK_NTWAITFORSINGLEOBJECT       0x33F3E3BCU
#define EXE_HOST_IAT_MARK_NTDELAYEXECUTION            0x33F3E3BDU
#define EXE_HOST_IAT_MARK_NTREMOVEIOCOMPLETION        0x33F3E3BEU
#define EXE_HOST_IAT_MARK_REGISTERCLASSA              0x44F4E4C1U
#define EXE_HOST_IAT_MARK_CREATEWINDOWEXA             0x44F4E4C2U
#define EXE_HOST_IAT_MARK_DEFWINDOWPROCA              0x44F4E4C3U
#define EXE_HOST_IAT_MARK_SHOWWINDOW                  0x44F4E4C4U
#define EXE_HOST_IAT_MARK_UPDATEWINDOW                0x44F4E4C5U
#define EXE_HOST_IAT_MARK_GETMESSAGEA                 0x44F4E4C6U
#define EXE_HOST_IAT_MARK_TRANSLATEMESSAGE            0x44F4E4C7U
#define EXE_HOST_IAT_MARK_DISPATCHMESSAGEA            0x44F4E4C8U
#define EXE_HOST_IAT_MARK_POSTQUITMESSAGE             0x44F4E4C9U
#define EXE_HOST_IAT_MARK_LOADCURSORA                 0x44F4E4CAU
#define EXE_HOST_IAT_MARK_SETTIMER                    0x44F4E4CBU
#define EXE_HOST_IAT_MARK_KILLTIMER                   0x44F4E4CCU
#define EXE_HOST_IAT_MARK_SETWINDOWTEXTA              0x44F4E4CDU
#define EXE_HOST_IAT_MARK_MESSAGEBOXA                 0x44F4E4CEU
#define EXE_HOST_IAT_MARK_NTSETEVENT                  0x55F5E5D1U
#define EXE_HOST_IAT_MARK_NTRESETEVENT                0x55F5E5D2U
#define EXE_HOST_IAT_MARK_NTWAITFORMULTIPLEOBJECTS    0x55F5E5D3U
#define EXE_HOST_IAT_MARK_NTSIGNALANDWAIT             0x55F5E5D4U
#define EXE_HOST_IAT_MARK_NTOPENPROCESS               0x55F5E5D5U
#define EXE_HOST_IAT_MARK_NTQUERYINFORMATIONPROCESS   0x55F5E5D6U
#define EXE_HOST_IAT_MARK_NTQUERYSYSTEMINFORMATION    0x55F5E5D7U
#define EXE_HOST_IAT_MARK_RTLGETVERSION               0x55F5E5D8U
#define EXE_HOST_IAT_MARK_NTCREATESECTION             0x55F5E5D9U
#define EXE_HOST_IAT_MARK_NTMAPVIEWOFSECTION          0x55F5E5DAU
#define EXE_HOST_IAT_MARK_NTUNMAPVIEWOFSECTION        0x55F5E5DBU
#define EXE_HOST_IAT_MARK_NTQUERYINFORMATIONFILE      0x55F5E5DCU
#define EXE_HOST_IAT_MARK_NTDEVICEIOCONTROLFILE       0x55F5E5DDU
#define EXE_HOST_IAT_MARK_NTOPENFILE                  0x55F5E5DEU
#define EXE_HOST_IAT_MARK_NTCREATEUSERPROCESS         0x55F5E5DFU
#define EXE_HOST_IAT_MARK_LDRLOADDLL                  0x66F6E6A1U
#define EXE_HOST_IAT_MARK_LDRGETPROCEDUREADDRESS      0x66F6E6A2U
#define EXE_HOST_IAT_MARK_RTLINITUNICODESTRING        0x66F6E6A3U
#define EXE_HOST_IAT_MARK_REGISTERHOTKEY              0x66F6E6A4U
#define EXE_HOST_IAT_MARK_UNREGISTERHOTKEY            0x66F6E6A5U
#define EXE_HOST_IAT_MARK_GETCURSORPOS                0x66F6E6A6U
#define EXE_HOST_IAT_MARK_SETWINDOWPOS                0x66F6E6A7U
#define EXE_HOST_IAT_MARK_SETFOREGROUNDWINDOW         0x66F6E6A8U
#define EXE_HOST_IAT_MARK_BRINGWINDOWTOTOP            0x66F6E6A9U
#define EXE_HOST_IAT_MARK_GETSYSTEMMETRICS            0x66F6E6AAU

typedef struct {
    const char *dll_name;
    const char *function_name;
    uint32_t marker;
    int gui_import;
} ExeImportPatchSpec;

static const ExeImportPatchSpec g_exe_import_patch_specs[] = {
    { EXE_IMPORT_NTDLL, "NtWriteFile", EXE_PORTAL_IAT_MARK_NTWRITEFILE, 0 },
    { EXE_IMPORT_NTDLL, "NtTerminateProcess", EXE_PORTAL_IAT_MARK_NTTERMINATEPROCESS, 0 },
    { EXE_IMPORT_NTDLL, "NtAllocateVirtualMemory", EXE_HOST_IAT_MARK_NTALLOCATEVIRTUALMEMORY, 0 },
    { EXE_IMPORT_NTDLL, "NtAllocateVirtualMemoryEx", EXE_HOST_IAT_MARK_NTALLOCATEVIRTUALMEMORYEX, 0 },
    { EXE_IMPORT_NTDLL, "NtFreeVirtualMemory", EXE_HOST_IAT_MARK_NTFREEVIRTUALMEMORY, 0 },
    { EXE_IMPORT_NTDLL, "NtProtectVirtualMemory", EXE_HOST_IAT_MARK_NTPROTECTVIRTUALMEMORY, 0 },
    { EXE_IMPORT_NTDLL, "NtQueryVirtualMemory", EXE_HOST_IAT_MARK_NTQUERYVIRTUALMEMORY, 0 },
    { EXE_IMPORT_NTDLL, "NtLockVirtualMemory", EXE_HOST_IAT_MARK_NTLOCKVIRTUALMEMORY, 0 },
    { EXE_IMPORT_NTDLL, "NtUnlockVirtualMemory", EXE_HOST_IAT_MARK_NTUNLOCKVIRTUALMEMORY, 0 },
    { EXE_IMPORT_NTDLL, "NtCreateThreadEx", EXE_HOST_IAT_MARK_NTCREATETHREADEX, 0 },
    { EXE_IMPORT_NTDLL, "NtCreateFile", EXE_HOST_IAT_MARK_NTCREATEFILE, 0 },
    { EXE_IMPORT_NTDLL, "NtReadFile", EXE_HOST_IAT_MARK_NTREADFILE, 0 },
    { EXE_IMPORT_NTDLL, "NtCreateEvent", EXE_HOST_IAT_MARK_NTCREATEEVENT, 0 },
    { EXE_IMPORT_NTDLL, "NtSetEvent", EXE_HOST_IAT_MARK_NTSETEVENT, 0 },
    { EXE_IMPORT_NTDLL, "NtResetEvent", EXE_HOST_IAT_MARK_NTRESETEVENT, 0 },
    { EXE_IMPORT_NTDLL, "NtWaitForSingleObject", EXE_HOST_IAT_MARK_NTWAITFORSINGLEOBJECT, 0 },
    { EXE_IMPORT_NTDLL, "NtWaitForMultipleObjects", EXE_HOST_IAT_MARK_NTWAITFORMULTIPLEOBJECTS, 0 },
    { EXE_IMPORT_NTDLL, "NtSignalAndWaitForSingleObject", EXE_HOST_IAT_MARK_NTSIGNALANDWAIT, 0 },
    { EXE_IMPORT_NTDLL, "NtDelayExecution", EXE_HOST_IAT_MARK_NTDELAYEXECUTION, 0 },
    { EXE_IMPORT_NTDLL, "NtRemoveIoCompletion", EXE_HOST_IAT_MARK_NTREMOVEIOCOMPLETION, 0 },
    { EXE_IMPORT_NTDLL, "NtOpenProcess", EXE_HOST_IAT_MARK_NTOPENPROCESS, 0 },
    { EXE_IMPORT_NTDLL, "NtQueryInformationProcess", EXE_HOST_IAT_MARK_NTQUERYINFORMATIONPROCESS, 0 },
    { EXE_IMPORT_NTDLL, "NtQuerySystemInformation", EXE_HOST_IAT_MARK_NTQUERYSYSTEMINFORMATION, 0 },
    { EXE_IMPORT_NTDLL, "RtlGetVersion", EXE_HOST_IAT_MARK_RTLGETVERSION, 0 },
    { EXE_IMPORT_NTDLL, "NtCreateSection", EXE_HOST_IAT_MARK_NTCREATESECTION, 0 },
    { EXE_IMPORT_NTDLL, "NtMapViewOfSection", EXE_HOST_IAT_MARK_NTMAPVIEWOFSECTION, 0 },
    { EXE_IMPORT_NTDLL, "NtUnmapViewOfSection", EXE_HOST_IAT_MARK_NTUNMAPVIEWOFSECTION, 0 },
    { EXE_IMPORT_NTDLL, "NtQueryInformationFile", EXE_HOST_IAT_MARK_NTQUERYINFORMATIONFILE, 0 },
    { EXE_IMPORT_NTDLL, "NtDeviceIoControlFile", EXE_HOST_IAT_MARK_NTDEVICEIOCONTROLFILE, 0 },
    { EXE_IMPORT_NTDLL, "NtOpenFile", EXE_HOST_IAT_MARK_NTOPENFILE, 0 },
    { EXE_IMPORT_NTDLL, "NtCreateUserProcess", EXE_HOST_IAT_MARK_NTCREATEUSERPROCESS, 0 },
    { EXE_IMPORT_NTDLL, "LdrLoadDll", EXE_HOST_IAT_MARK_LDRLOADDLL, 0 },
    { EXE_IMPORT_NTDLL, "LdrGetProcedureAddress", EXE_HOST_IAT_MARK_LDRGETPROCEDUREADDRESS, 0 },
    { EXE_IMPORT_NTDLL, "RtlInitUnicodeString", EXE_HOST_IAT_MARK_RTLINITUNICODESTRING, 0 },
    { EXE_IMPORT_USER32, "RegisterClassA", EXE_HOST_IAT_MARK_REGISTERCLASSA, 1 },
    { EXE_IMPORT_USER32, "CreateWindowExA", EXE_HOST_IAT_MARK_CREATEWINDOWEXA, 1 },
    { EXE_IMPORT_USER32, "DefWindowProcA", EXE_HOST_IAT_MARK_DEFWINDOWPROCA, 1 },
    { EXE_IMPORT_USER32, "ShowWindow", EXE_HOST_IAT_MARK_SHOWWINDOW, 1 },
    { EXE_IMPORT_USER32, "UpdateWindow", EXE_HOST_IAT_MARK_UPDATEWINDOW, 1 },
    { EXE_IMPORT_USER32, "GetMessageA", EXE_HOST_IAT_MARK_GETMESSAGEA, 1 },
    { EXE_IMPORT_USER32, "TranslateMessage", EXE_HOST_IAT_MARK_TRANSLATEMESSAGE, 1 },
    { EXE_IMPORT_USER32, "DispatchMessageA", EXE_HOST_IAT_MARK_DISPATCHMESSAGEA, 1 },
    { EXE_IMPORT_USER32, "PostQuitMessage", EXE_HOST_IAT_MARK_POSTQUITMESSAGE, 1 },
    { EXE_IMPORT_USER32, "LoadCursorA", EXE_HOST_IAT_MARK_LOADCURSORA, 1 },
    { EXE_IMPORT_USER32, "SetTimer", EXE_HOST_IAT_MARK_SETTIMER, 1 },
    { EXE_IMPORT_USER32, "KillTimer", EXE_HOST_IAT_MARK_KILLTIMER, 1 },
    { EXE_IMPORT_USER32, "SetWindowTextA", EXE_HOST_IAT_MARK_SETWINDOWTEXTA, 1 },
    { EXE_IMPORT_USER32, "MessageBoxA", EXE_HOST_IAT_MARK_MESSAGEBOXA, 1 },
    { EXE_IMPORT_USER32, "RegisterHotKey", EXE_HOST_IAT_MARK_REGISTERHOTKEY, 1 },
    { EXE_IMPORT_USER32, "UnregisterHotKey", EXE_HOST_IAT_MARK_UNREGISTERHOTKEY, 1 },
    { EXE_IMPORT_USER32, "GetCursorPos", EXE_HOST_IAT_MARK_GETCURSORPOS, 1 },
    { EXE_IMPORT_USER32, "SetWindowPos", EXE_HOST_IAT_MARK_SETWINDOWPOS, 1 },
    { EXE_IMPORT_USER32, "SetForegroundWindow", EXE_HOST_IAT_MARK_SETFOREGROUNDWINDOW, 1 },
    { EXE_IMPORT_USER32, "BringWindowToTop", EXE_HOST_IAT_MARK_BRINGWINDOWTOTOP, 1 },
    { EXE_IMPORT_USER32, "GetSystemMetrics", EXE_HOST_IAT_MARK_GETSYSTEMMETRICS, 1 }
};

static int create_temp_file_path(char *path_out, size_t path_out_sz, const char *prefix) {
    if (!path_out || path_out_sz == 0 || !prefix) {
        return -1;
    }
#ifdef _WIN32
    {
        const char *tmp_dir;
        int n;
        int temp_handle;

        tmp_dir = getenv("TEMP");
        if (!tmp_dir || tmp_dir[0] == '\0') {
            tmp_dir = getenv("TMP");
        }
        if (!tmp_dir || tmp_dir[0] == '\0') {
            tmp_dir = ".";
        }
        n = snprintf(path_out, path_out_sz, "%s\\%s-XXXXXX", tmp_dir, prefix);
        if (n < 0 || (size_t)n >= path_out_sz) {
            return -1;
        }
        if (_mktemp_s(path_out, path_out_sz) != 0) {
            return -1;
        }
        temp_handle = _open(path_out, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
        if (temp_handle < 0) {
            unlink(path_out);
            return -1;
        }
        return temp_handle;
    }
#else
    {
        int n;
        n = snprintf(path_out, path_out_sz, "/tmp/%s-XXXXXX", prefix);
        if (n < 0 || (size_t)n >= path_out_sz) {
            return -1;
        }
        return mkstemp(path_out);
    }
#endif
}

static EXE_Section_Data *find_exe_section(EXE_Binary_Weaver_Context *ctx, const char *name) {
    uint32_t i;
    if (!ctx || !name) return NULL;
    for (i = 0; i < ctx->section_count; ++i) {
        if (strncmp(ctx->sections[i].name, name, sizeof(ctx->sections[i].name)) == 0) {
            return &ctx->sections[i];
        }
    }
    return NULL;
}

static uint32_t find_exe_import_iat_rva(const EXE_Binary_Weaver_Context *ctx,
                                        const char *dll_name,
                                        const char *function_name) {
    uint32_t i;
    if (!ctx || !dll_name || !function_name) return 0U;
    for (i = 0; i < ctx->import_library_count; ++i) {
        uint32_t j;
        const EXE_Import_Library *lib = &ctx->imports[i];
        if (strncmp(lib->dll_name, dll_name, sizeof(lib->dll_name)) != 0) continue;
        for (j = 0; j < lib->function_count; ++j) {
            if (strncmp(lib->functions[j].function_name, function_name, sizeof(lib->functions[j].function_name)) == 0) {
                return lib->functions[j].iat_rva;
            }
        }
    }
    return 0U;
}

static int exe_code_contains_marker(const uint8_t *code, size_t code_size, uint32_t marker) {
    size_t i;
    if (!code || code_size < 4) return 0;
    for (i = 0; i + 4 <= code_size; ++i) {
        uint32_t value = ((uint32_t)code[i]) |
                         ((uint32_t)code[i + 1] << 8) |
                         ((uint32_t)code[i + 2] << 16) |
                         ((uint32_t)code[i + 3] << 24);
        if (value == marker) return 1;
    }
    return 0;
}

static int exe_code_uses_gui_imports(const uint8_t *code, size_t code_size) {
    size_t i;
    for (i = 0; i < sizeof(g_exe_import_patch_specs) / sizeof(g_exe_import_patch_specs[0]); ++i) {
        if (!g_exe_import_patch_specs[i].gui_import) continue;
        if (exe_code_contains_marker(code, code_size, g_exe_import_patch_specs[i].marker)) {
            return 1;
        }
    }
    return 0;
}

static int patch_exe_portal_iat_displacements(uint8_t *code, size_t code_size, uint32_t text_rva, const EXE_Binary_Weaver_Context *ctx) {
    size_t i;
    size_t patched = 0;
    if (!code || code_size < 4 || text_rva == 0U || !ctx) {
        return -1;
    }
    for (i = 0; i + 4 <= code_size; ++i) {
        uint32_t marker = ((uint32_t)code[i]) |
                          ((uint32_t)code[i + 1] << 8) |
                          ((uint32_t)code[i + 2] << 16) |
                          ((uint32_t)code[i + 3] << 24);
        size_t j;
        for (j = 0; j < sizeof(g_exe_import_patch_specs) / sizeof(g_exe_import_patch_specs[0]); ++j) {
            if (marker == g_exe_import_patch_specs[j].marker) {
                uint32_t target_rva = find_exe_import_iat_rva(ctx,
                                                              g_exe_import_patch_specs[j].dll_name,
                                                              g_exe_import_patch_specs[j].function_name);
                int32_t disp32;
                if (target_rva == 0U) {
                    return -1;
                }
                disp32 = (int32_t)((int64_t)target_rva - ((int64_t)text_rva + (int64_t)i + 4));
                code[i + 0] = (uint8_t)(disp32 & 0xFF);
                code[i + 1] = (uint8_t)((disp32 >> 8) & 0xFF);
                code[i + 2] = (uint8_t)((disp32 >> 16) & 0xFF);
                code[i + 3] = (uint8_t)((disp32 >> 24) & 0xFF);
                patched++;
                i += 3;
                break;
            }
        }
    }
    /* 强制返回0：哪怕没有任何 IAT 魔数也意味着该程序没有直接依赖外部符号，这是完全合法的 */
    return 0;
}

// ============================================================================
// 二进制格式生成函数 - 直接从编译输出生成目标格式
// ============================================================================

// ============================================================================
// 从二进制数据中提取三个Zone的辅助函数
// ============================================================================

static int extract_zones_from_binary(const uint8_t *binary_data, size_t binary_size,
                                    const uint8_t **code_out, size_t *code_size_out,
                                    const uint8_t **mirror_out, size_t *mirror_size_out,
                                    const uint8_t **constant_out, size_t *constant_size_out) {
    if (!binary_data || binary_size < AETHEL_HEADER_SIZE) {
        fprintf(stderr, "Error: Invalid binary data size\n");
        return -1;
    }
    
    /* 从数据中读取header */
    AethelBinaryHeader *hdr = (AethelBinaryHeader *)binary_data;
    
    /* 提取ActFlow Zone */
    if (hdr->act_flow_size > 0 && hdr->act_flow_offset + hdr->act_flow_size <= binary_size) {
        *code_out = binary_data + hdr->act_flow_offset;
        *code_size_out = hdr->act_flow_size;
    } else {
        *code_out = NULL;
        *code_size_out = 0;
    }
    
    /* 提取MirrorState Zone */
    if (hdr->mirror_state_size > 0 && hdr->mirror_state_offset + hdr->mirror_state_size <= binary_size) {
        *mirror_out = binary_data + hdr->mirror_state_offset;
        *mirror_size_out = hdr->mirror_state_size;
    } else {
        *mirror_out = NULL;
        *mirror_size_out = 0;
    }
    
    /* 提取ConstantTruth Zone */
    if (hdr->constant_truth_size > 0 && hdr->constant_truth_offset + hdr->constant_truth_size <= binary_size) {
        *constant_out = binary_data + hdr->constant_truth_offset;
        *constant_size_out = hdr->constant_truth_size;
    } else {
        *constant_out = NULL;
        *constant_size_out = 0;
    }
    
    return 0;
}

/* PE 格式已迁移到模块化pe_gen.c中实现 */



/* 直接从缓冲区生成AKI格式 - 纯AE编译链（零C代码） */
static int generate_aki_image_from_buffer(const char *output_file, const uint8_t *code, size_t code_size) {
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "Error: Cannot create output file '%s'\n", output_file);
        return 1;
    }
    
    /* 检测输入是否为AETB格式 - 如果是则提取机器码 */
    const uint8_t *actflow_code = code;
    size_t actflow_size = code_size;
    int aetb_detected = 0;
    
    if (code_size >= 128) {
        uint32_t magic = *(uint32_t*)code;
        if (magic == AETB_MAGIC) {
            aetb_detected = 1;
            /* AETB格式：读取code_size从偏移0x14处 (小端序32位) */
            uint32_t aetb_code_size = *(uint32_t*)&code[0x14];
            
            fprintf(stderr, "[DEBUG] Detected AETB format; code_size_in_header=%u\n", aetb_code_size);
            
            /* 验证code_size不超过AETB文件大小 */
            if (0x80 + aetb_code_size <= code_size) {
                /* 提取实际的x86-64机器码（从偏移0x80 = 128字节之后） */
                actflow_code = &code[0x80];
                actflow_size = aetb_code_size;
                
                if (actflow_size == 0) {
                    /* 如果AETB内的code_size为0，使用整个剩余size */
                    actflow_size = code_size - 0x80;
                }
                fprintf(stderr, "[DEBUG] Extracted x86-64 code: %zu bytes\n", actflow_size);
            }
        }
    }
    
    if (!aetb_detected) {
        fprintf(stderr, "[DEBUG] No AETB detected; using code directly: %zu bytes\n", code_size);
    }
    
    /* 构建标准 256 字节 AKI 头部 */
    uint8_t header[256] = {0};
    
    /* 0x00-0x03: 魔数 */
    header[0] = 0x41;  /* A */
    header[1] = 0x4b;  /* K */
    header[2] = 0x49;  /* I */
    header[3] = 0x21;  /* ! */
    
    /* 0x04-0x07: 版本 */
    uint32_t version = 1;
    memcpy(&header[4], &version, 4);
    
    /* 0x40-0x47: ActFlow offset (紧跟头部) */
    uint64_t act_flow_offset = 256;
    memcpy(&header[0x40], &act_flow_offset, 8);
    
    /* 0x48-0x4F: ActFlow size (提取的机器码) */
    uint64_t act_flow_size = actflow_size;
    memcpy(&header[0x48], &act_flow_size, 8);
    
    /* 0x50-0x57: MirrorState offset (紧跟ActFlow) */
    uint64_t mirror_state_offset = 256 + actflow_size;
    memcpy(&header[0x50], &mirror_state_offset, 8);
    
    /* 0x58-0x5F: MirrorState size (暂为0，由.ae编译扩展) */
    uint64_t mirror_state_size = 0;
    memcpy(&header[0x58], &mirror_state_size, 8);
    
    /* 0x60-0x67: ConstantTruth offset (紧跟MirrorState) */
    uint64_t constant_truth_offset = 256 + actflow_size;
    memcpy(&header[0x60], &constant_truth_offset, 8);
    
    /* 0x68-0x6F: ConstantTruth size (暂为0，由.ae编译扩展) */
    uint64_t constant_truth_size = 0;
    memcpy(&header[0x68], &constant_truth_size, 8);
    
    /* 0xD8-0xDB: Header CRC */
    uint32_t header_crc = 0;
    for (int i = 0; i < 0xD8; i++) {
        header_crc ^= header[i];
    }
    memcpy(&header[0xD8], &header_crc, 4);
    
    /* 0xE0-0xE7: Build timestamp */
    uint64_t build_time = time(NULL);
    memcpy(&header[0xE0], &build_time, 8);
    
    /* 0xE8-0xEB: Build version */
    uint32_t build_version = 1;
    memcpy(&header[0xE8], &build_version, 4);
    
    /* 0xEC-0xEF: Compiler version */
    uint32_t compiler_version = 2;  /* Pure AE compilation chain */
    memcpy(&header[0xEC], &compiler_version, 4);
    
    /* 写入 256 字节头部 */
    if (fwrite(header, 1, 256, out) != 256) {
        fprintf(stderr, "Error: Failed to write AKI header\n");
        fclose(out);
        return 1;
    }
    
    /* 写入 ActFlow 段 (AE编译产生的x86-64机器码，零C代码) */
    if (actflow_code && actflow_size > 0) {
        if (fwrite(actflow_code, 1, actflow_size, out) != actflow_size) {
            fprintf(stderr, "Error: Failed to write ActFlow segment\n");
            fclose(out);
            return 1;
        }
    }
    
    /* MirrorState和ConstantTruth留待后续.ae编译扩展 */
    
    fclose(out);
    return 0;
}


/* 生成 AKI 格式镜像 - 纯AE编译链 */
static int generate_aki_image(const char *input_file, const char *output_file) {
    // 读取编译产生的二进制
    FILE *in = fopen(input_file, "rb");
    if (!in) {
        fprintf(stderr, "Error: Cannot read compiled binary '%s'\n", input_file);
        return 1;
    }
    
    // 获取大小
    fseek(in, 0, SEEK_END);
    size_t code_size = ftell(in);
    fseek(in, 0, SEEK_SET);
    
    // 读取二进制
    uint8_t *code = malloc(code_size);
    if (!code) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(in);
        return 1;
    }
    
    if (fread(code, 1, code_size, in) != code_size) {
        fprintf(stderr, "Error: Failed to read compiled binary\n");
        free(code);
        fclose(in);
        return 1;
    }
    fclose(in);
    
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "Error: Cannot create output file '%s'\n", output_file);
        free(code);
        return 1;
    }
    
    /* 检测输入是否为AETB格式 - 如果是则提取机器码 */
    const uint8_t *actflow_code = code;
    size_t actflow_size = code_size;
    
    if (code_size >= 128) {
        uint32_t magic = *(uint32_t*)code;
        if (magic == AETB_MAGIC) {
            /* AETB格式：读取code_size从偏移0x14处 (小端序32位) */
            uint32_t aetb_code_size = *(uint32_t*)&code[0x14];
            
            /* 验证code_size不超过AETB文件大小 */
            if (0x80 + aetb_code_size <= code_size) {
                /* 提取实际的x86-64机器码（从偏移0x80 = 128字节之后） */
                actflow_code = &code[0x80];
                actflow_size = aetb_code_size;
                
                if (actflow_size == 0) {
                    /* 如果AETB内的code_size为0，使用整个剩余size */
                    actflow_size = code_size - 0x80;
                }
            }
        }
    }
    
    /* 构建标准 256 字节 AKI 头部（按文档规范） */
    uint8_t header[256] = {0};
    
    /* 0x00-0x03: 魔数 */
    header[0] = 0x41;   /* A */
    header[1] = 0x4b;   /* K */
    header[2] = 0x49;   /* I */
    header[3] = 0x21;   /* ! */
    
    /* 0x04-0x07: 版本 */
    uint32_t version = 1;
    memcpy(&header[4], &version, 4);
    
    /* 0x40-0x47: ActFlow offset */
    uint64_t act_flow_offset = 256;
    memcpy(&header[0x40], &act_flow_offset, 8);
    
    /* 0x48-0x4F: ActFlow size */
    uint64_t act_flow_size = actflow_size;
    memcpy(&header[0x48], &act_flow_size, 8);
    
    /* 0x50-0x57: MirrorState offset */
    uint64_t mirror_state_offset = 256 + actflow_size;
    memcpy(&header[0x50], &mirror_state_offset, 8);
    
    /* 0x58-0x5F: MirrorState size (暂为0) */
    uint64_t mirror_state_size = 0;
    memcpy(&header[0x58], &mirror_state_size, 8);
    
    /* 0x60-0x67: ConstantTruth offset */
    uint64_t constant_truth_offset = 256 + actflow_size;
    memcpy(&header[0x60], &constant_truth_offset, 8);
    
    /* 0x68-0x6F: ConstantTruth size (暂为0) */
    uint64_t constant_truth_size = 0;
    memcpy(&header[0x68], &constant_truth_size, 8);
    
    /* 写入 256 字节头部 */
    if (fwrite(header, 1, 256, out) != 256) {
        fprintf(stderr, "Error: Failed to write AKI header\n");
        free(code);
        fclose(out);
        return 1;
    }
    
    /* 写入 ActFlow 段 (AE编译产生的x86-64机器码) */
    if (actflow_code && actflow_size > 0) {
        if (fwrite(actflow_code, 1, actflow_size, out) != actflow_size) {
            fprintf(stderr, "Error: Failed to write ActFlow segment\n");
            free(code);
            fclose(out);
            return 1;
        }
    }
    
    fclose(out);
    free(code);
    
    return 0;
}

/* 验证内核：禁止 C 导出 */
static int validate_no_foreign_c(const char *source_file, const char *target_mode) {
    if (strcmp(target_mode, "kernel") != 0) {
        return 0;  /* 仅在内核模式检查 */
    }
    
    FILE *f = fopen(source_file, "r");
    if (!f) return 0;
    
    char line[1024];
    int line_num = 0;
    int found_error = 0;
    
    while (fgets(line, sizeof(line), f)) {
        line_num++;
        
        /* 检查 @foreign("C") 模式 */
        if (strstr(line, "@foreign") && strstr(line, "\"C\"")) {
            fprintf(stderr, "ERROR [Line %d]: Kernel modules cannot export C interfaces!\n", line_num);
            fprintf(stderr, "  Found: @foreign(\"C\")\n");
            fprintf(stderr, "  Hint: Remove all C export interfaces from kernel code\n");
            found_error = 1;
        }
    }
    
    fclose(f);
    return found_error ? 1 : 0;
}


typedef struct {
    char *input_files[MAX_INPUT_FILES];
    int input_count;
    const char *output_file;
    const char *entry_point;
    const char *mode;         /* "compile" or "link" */
    int optimize_level;
    int verbose;
    int debug;                /* 新增：--debug 标志 */
    const char *output_format;  /* "aetb", "let", "aki", "hda", "srv", "efi", "bin", "rom" */
    const char *target_mode;    /* "application" or "kernel" */
    const char *emit_format;    /* --emit override */
    int machine_bits;           /* 16/32/64 */
    const char *isa;            /* x86|x86_64|aarch64 */
    int bin_flat;               /* --bin-flat */
    int bin_with_map;           /* --bin-with-map */
    const char *bin_entry;      /* --bin-entry <offset|symbol> */
    int has_bin_entry_offset;
    uint64_t bin_entry_offset;
    int verify_let_mode;        /* --verify-let-contract */
    const char *verify_let_file;
    int dump_reloc_mode;        /* --dump-reloc-dna */
    const char *dump_reloc_file;
    const char *dump_reloc_output;
    /* 新增：ISO生成选项 */
    int is_iso_mode;
    const char *kernel_file;
    const char *efi_boot_file;
    uint64_t iso_size_mb;
    /* 新增：编译标志（链接标志） */
    int freestanding;           /* --freestanding: 自由运行模式 */
    int no_stack_check;         /* --no-stack-check: 禁用堆栈检查 */
    int no_default_libs;        /* --no-default-libs: 不链接默认库 */
    int no_shared_libs;         /* --no-shared-libs: 不使用共享库 */
    int bundle_dependencies;    /* --bundle-dependencies: 绑定依赖 */
    int bundle_all_dependencies; /* --bundle-all-dependencies: 绑定所有依赖 */
    int static_only;            /* --static-only: 仅静态链接 */
    int static_complete;        /* --static-complete: 静态链接完整性 */
    int app_package;            /* --app-package: 应用包格式（IYA） */
    /* ROM 镜像选项 */
    int rom_mode;               /* --rom: 生成ROM镜像 */
    uint64_t rom_size_bytes;    /* --side <size> (default 8MB) */
    uint8_t rom_fill_byte;      /* ROM填充字节 (默认0xFF) */
    /* 新增：库包含支持 (for library source inlining) */
    char *include_libs[32];     /* 库名称列表（如 "std", "auraui"） */
    int include_lib_count;      /* 包含库的数量 */
    /* Mach-O 及 IM4P 支持 */
    uint64_t macho_phys_base;   /* --base <addr>: Mach-O 物理基址 (default: 0x800000000) */
    const char *im4p_identifier; /* --is im4p <name>: IM4P 标识符名称 (default: "krnl") */
    const char *config_file;     /* --config 或 /config */
} CompilerOptions;

static int compiler_append_input_file(CompilerOptions *opts, char *path) {
    if (!opts || !path) return -1;
    if (opts->input_count >= MAX_INPUT_FILES) {
        fprintf(stderr, "Error: Too many input files\n");
        return -1;
    }
    opts->input_files[opts->input_count++] = path;
    return 0;
}

static char *dup_lib_normalized_path(const char *path) {
    size_t i;
    size_t len;
    char *out;
    if (!path) return NULL;
    len = strlen(path);
    out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, path, len + 1);
    for (i = 0; i < len; ++i) {
        if (out[i] == '\\') out[i] = '/';
    }
    return out;
}

static char *join_lib_root_path(const char *root, const char *relative) {
    char *norm_root;
    char *norm_rel;
    char *joined;
    size_t root_len;
    size_t rel_len;
    int need_sep;
    if (!root || !relative) return NULL;
    norm_root = dup_lib_normalized_path(root);
    norm_rel = dup_lib_normalized_path(relative);
    if (!norm_root || !norm_rel) {
        free(norm_root);
        free(norm_rel);
        return NULL;
    }
    while (*norm_rel == '/' || *norm_rel == '.') {
        if (*norm_rel == '.' && norm_rel[1] == '/') {
            memmove(norm_rel, norm_rel + 2, strlen(norm_rel + 2) + 1);
            continue;
        }
        if (*norm_rel == '/') {
            memmove(norm_rel, norm_rel + 1, strlen(norm_rel + 1) + 1);
            continue;
        }
        break;
    }
    root_len = strlen(norm_root);
    rel_len = strlen(norm_rel);
    need_sep = (root_len > 0 && norm_root[root_len - 1] != '/');
    joined = (char *)malloc(root_len + rel_len + (need_sep ? 2 : 1));
    if (!joined) {
        free(norm_root);
        free(norm_rel);
        return NULL;
    }
    memcpy(joined, norm_root, root_len);
    if (need_sep) joined[root_len++] = '/';
    memcpy(joined + root_len, norm_rel, rel_len + 1);
    free(norm_root);
    free(norm_rel);
    return joined;
}

static int path_is_directory(const char *path) {
    struct stat st;
    if (!path) return 0;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int path_is_regular_file(const char *path) {
    struct stat st;
    if (!path) return 0;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode) ? 1 : 0;
}

static int path_has_ae_suffix(const char *path) {
    size_t len;
    if (!path) return 0;
    len = strlen(path);
    return len >= 3 && strcmp(path + len - 3, ".ae") == 0;
}

static int append_lib_directory_recursive(CompilerOptions *opts, const char *dir_path) {
    DIR *dir;
    struct dirent *ent;
    if (!opts || !dir_path) return -1;
    dir = opendir(dir_path);
    if (!dir) {
        fprintf(stderr, "Error: Cannot open library directory '%s'\n", dir_path);
        return -1;
    }
    while ((ent = readdir(dir)) != NULL) {
        char *child;
        size_t base_len;
        size_t name_len;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        base_len = strlen(dir_path);
        name_len = strlen(ent->d_name);
        child = (char *)malloc(base_len + name_len + 2);
        if (!child) {
            closedir(dir);
            return -1;
        }
        memcpy(child, dir_path, base_len);
        if (base_len == 0 || dir_path[base_len - 1] != '/') child[base_len++] = '/';
        memcpy(child + base_len, ent->d_name, name_len + 1);

        if (path_is_directory(child)) {
            if (append_lib_directory_recursive(opts, child) != 0) {
                free(child);
                closedir(dir);
                return -1;
            }
            free(child);
            continue;
        }
        if (path_is_regular_file(child) && path_has_ae_suffix(child)) {
            if (compiler_append_input_file(opts, child) != 0) {
                free(child);
                closedir(dir);
                return -1;
            }
            continue;
        }
        free(child);
    }
    closedir(dir);
    return 0;
}

static int append_env_library_path(CompilerOptions *opts, const char *lib_spec) {
    const char *lib_root;
    char *resolved;
    if (!opts || !lib_spec || lib_spec[0] == '\0') return -1;
    lib_root = getenv("AELibraryPATH");
    if (!lib_root || lib_root[0] == '\0') {
        lib_root = getenv("AELibraryPath");
    }
    if (!lib_root || lib_root[0] == '\0') {
        fprintf(stderr, "Error: --lib requires environment variable AELibraryPATH\n");
        return -1;
    }
    resolved = join_lib_root_path(lib_root, lib_spec);
    if (!resolved) return -1;
    if (path_is_directory(resolved)) {
        int rc = append_lib_directory_recursive(opts, resolved);
        free(resolved);
        return rc;
    }
    if (path_is_regular_file(resolved) && path_has_ae_suffix(resolved)) {
        return compiler_append_input_file(opts, resolved);
    }
    fprintf(stderr, "Error: --lib target '%s' was not found under AELibraryPATH\n", lib_spec);
    free(resolved);
    return -1;
}

static int parse_u64(const char *text, uint64_t *out) {
    char *endp = NULL;
    unsigned long long v;
    if (!text || !out || text[0] == '\0') return -1;
    v = strtoull(text, &endp, 0);
    if (!endp || *endp != '\0') return -1;
    *out = (uint64_t)v;
    return 0;
}

static int parse_rom_size_bytes(const char *text, uint64_t *out_bytes) {
    char buf[64];
    char *endp = NULL;
    double value;
    size_t len;
    if (!text || !out_bytes) return -1;
    len = strlen(text);
    if (len == 0 || len >= sizeof(buf)) return -1;
    snprintf(buf, sizeof(buf), "%s", text);
    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)tolower((unsigned char)buf[i]);
    }
    value = strtod(buf, &endp);
    if (!endp || endp == buf) return -1;
    while (*endp && isspace((unsigned char)*endp)) endp++;
    if (*endp == '\0') {
        *out_bytes = (uint64_t)(value * 1024.0 * 1024.0);
        return 0;
    }
    if (strcmp(endp, "k") == 0 || strcmp(endp, "kb") == 0) {
        *out_bytes = (uint64_t)(value * 1024.0);
        return 0;
    }
    if (strcmp(endp, "m") == 0 || strcmp(endp, "mb") == 0) {
        *out_bytes = (uint64_t)(value * 1024.0 * 1024.0);
        return 0;
    }
    if (strcmp(endp, "g") == 0 || strcmp(endp, "gb") == 0) {
        *out_bytes = (uint64_t)(value * 1024.0 * 1024.0 * 1024.0);
        return 0;
    }
    return -1;
}

static void print_usage(const char *prog) {
#ifdef _WIN32
    fprintf(stderr, "Aethelium Compiler\n");
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  Compile: %s <input.ae> /o:<output> [options]\n", prog);
    fprintf(stderr, "  Verify:  %s /verify-let-contract:<input.let>\n", prog);
    fprintf(stderr, "  Dump:    %s /dump-reloc-dna:<input.let> [/o:<output.txt>]\n", prog);
    fprintf(stderr, "  ISO:     %s /iso /o:<output.iso> /kernel:<kernel> /efi:<boot.efi> [/size:<MB>]\n", prog);
    fprintf(stderr, "\nOutput Formats:\n");
    fprintf(stderr, "  /emit:<aetb|let|aki|srv|hda|efi|pe|exe|bin|rom|macho|im4p|wsys>\n");
    fprintf(stderr, "\nBare-metal Mach-O (Apple Silicon) Options:\n");
    fprintf(stderr, "  /emit:macho         Generate bare-metal Mach-O firmware image\n");
    fprintf(stderr, "  /base:<address>     Physical load address (default: 0x800000000)\n");
    fprintf(stderr, "\nIM4P Image4 Payload (iBoot) Options:\n");
    fprintf(stderr, "  /emit:im4p          Generate Image4 container for iBoot\n");
    fprintf(stderr, "  /is:im4p:<name>     Payload identifier (default: krnl)\n");
    fprintf(stderr, "\nCompiler Architecture:\n");
    fprintf(stderr, "  .ae -> AETB         Direct compile path for application runtime binaries\n");
    fprintf(stderr, "  .ae -> LET          Logical Embryo (the only intermediate format)\n");
    fprintf(stderr, "  .ae -> LET -> BIN   Contract-validated pure machine code path\n");
    fprintf(stderr, "  .let -> aki/srv/hda/aetb/bin  Assembly weaver path using Gene-Table metadata\n");
    fprintf(stderr, "  /verify-let-contract   Validate LET machine contract\n");
    fprintf(stderr, "  /dump-reloc-dna        Dump relocation DNA list from LET\n");
    fprintf(stderr, "\nTarget Modes:\n");
    fprintf(stderr, "  /target:application   Application mode (default)\n");
    fprintf(stderr, "  /target:kernel        Kernel mode (prohibits C exports)\n");
    fprintf(stderr, "\nMachine Contract:\n");
    fprintf(stderr, "  /machine-bits:16|32|64   (default 64)\n");
    fprintf(stderr, "  /isa:x86|x86_64|aarch64  (default x86)\n");
    fprintf(stderr, "  /bin-entry:<offset|symbol>\n");
    fprintf(stderr, "  /bin-flat\n");
    fprintf(stderr, "  /bin-with-map\n");
    fprintf(stderr, "\nROM Generation Options:\n");
    fprintf(stderr, "  /rom                Generate flashable ROM image (.rom)\n");
    fprintf(stderr, "  /side:<size>        ROM size (default: 8MB). Supports KB/MB/GB\n");
    fprintf(stderr, "  /lib:<path>         Include AE library file or directory from AELibraryPATH root\n");
    fprintf(stderr, "\nDebug and Help Options:\n");
    fprintf(stderr, "  /debug              Enable debug output during compilation\n");
    fprintf(stderr, "  /? or /help         Display this help message\n");
    fprintf(stderr, "\nOther Options:\n");
    fprintf(stderr, "  /o:<file>           Output file\n");
    fprintf(stderr, "  /mode:<mode>        sandbox or architect (default: sandbox)\n");
    fprintf(stderr, "  /O<level>           Optimization level (0-3)\n");
    fprintf(stderr, "  /optimize:<lvl>     Same as /O\n");
    fprintf(stderr, "  /v                  Verbose output\n");
    fprintf(stderr, "  /entry:<sym>        Set entry point symbol (link mode only)\n");
    fprintf(stderr, "\nProject Configuration:\n");
    fprintf(stderr, "  /config [file]      Use AECF configuration file (default: config.aecf)\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  Compile to EXE:\n");
    fprintf(stderr, "    aethelc app.ae /o:app.exe /emit:exe /lib:GUI\\aura\n");
    fprintf(stderr, "  Compile from AECF:\n");
    fprintf(stderr, "    aethelc /config\n");
#else
    fprintf(stderr, "Aethelium Compiler\n");
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  Compile: %s <input.ae> -o <output> [options]\n", prog);
    fprintf(stderr, "  Verify:  %s --verify-let-contract <input.let>\n", prog);
    fprintf(stderr, "  Dump:    %s --dump-reloc-dna <input.let> [-o <output.txt>]\n", prog);
    fprintf(stderr, "  ISO:     %s --iso -o <output.iso> --kernel <kernel> --efi <boot.efi> [--size <MB>]\n", prog);
    fprintf(stderr, "\nOutput Formats:\n");
    fprintf(stderr, "  --emit aetb|let|aki|srv|hda|efi|pe|exe|bin|rom|macho|im4p|wsys\n");
    fprintf(stderr, "\nBare-metal Mach-O (Apple Silicon) Options:\n");
    fprintf(stderr, "  --emit macho         Generate bare-metal Mach-O firmware image\n");
    fprintf(stderr, "  --base <address>     Physical load address (default: 0x800000000)\n");
    fprintf(stderr, "                       Example: aethelc kernel.ae --emit macho --base 0x800000000\n");
    fprintf(stderr, "\nIM4P Image4 Payload (iBoot) Options:\n");
    fprintf(stderr, "  --emit im4p          Generate Image4 container for iBoot\n");
    fprintf(stderr, "  --is im4p <name>     Payload identifier (default: krnl)\n");
    fprintf(stderr, "                       Example: aethelc kernel.ae --emit im4p --is im4p krnl\n");
    fprintf(stderr, "\nCompiler Architecture:\n");
    fprintf(stderr, "  .ae -> AETB         Direct compile path for application runtime binaries\n");
    fprintf(stderr, "  .ae -> LET          Logical Embryo (the only intermediate format)\n");
    fprintf(stderr, "  .ae -> LET -> BIN   Contract-validated pure machine code path\n");
    fprintf(stderr, "  .let -> aki/srv/hda/aetb/bin  Assembly weaver path using Gene-Table metadata\n");
    fprintf(stderr, "  --verify-let-contract  Validate LET machine contract\n");
    fprintf(stderr, "  --dump-reloc-dna       Dump relocation DNA list from LET\n");
    fprintf(stderr, "\nTarget Modes:\n");
    fprintf(stderr, "  --target application   Application mode (default)\n");
    fprintf(stderr, "  --target kernel        Kernel mode (prohibits C exports)\n");
    fprintf(stderr, "\nMachine Contract:\n");
    fprintf(stderr, "  --machine-bits 16|32|64   (default 64)\n");
    fprintf(stderr, "  --isa x86|x86_64|aarch64  (default x86)\n");
    fprintf(stderr, "  --bin-entry <offset|symbol>\n");
    fprintf(stderr, "  --bin-flat\n");
    fprintf(stderr, "  --bin-with-map\n");
    fprintf(stderr, "\nROM Generation Options:\n");
    fprintf(stderr, "  --rom                Generate flashable ROM image (.rom)\n");
    fprintf(stderr, "  --side <size>        ROM size (default: 8MB). Supports KB/MB/GB; no suffix means MB\n");
    fprintf(stderr, "  --lib <path>         Include AE library file or directory from AELibraryPATH root\n");
    fprintf(stderr, "                       Use Windows-style relative paths, e.g. --lib GUI\\\\aura or --lib IO\\\\win\\\\winDiskIo.ae\n");
    fprintf(stderr, "\nDebug and Help Options:\n");
    fprintf(stderr, "  --debug              Enable debug output during compilation\n");
    fprintf(stderr, "  -h, --help           Display this help message\n");
    fprintf(stderr, "\nOther Options:\n");
    fprintf(stderr, "  -o <file>        Output file\n");
    fprintf(stderr, "  --mode <mode>    sandbox or architect (default: sandbox)\n");
    fprintf(stderr, "  -O <level>       Optimization level (0-3)\n");
    fprintf(stderr, "  --optimize <lvl> Same as -O (e.g. --optimize 2)\n");
    fprintf(stderr, "  -v               Verbose output\n");
    fprintf(stderr, "  --entry <sym>    Set entry point symbol (link mode only)\n");
#endif
}

static int parse_args(int argc, char **argv, CompilerOptions *opts) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    // 初始化选项
    opts->input_count = 0;
    opts->output_file = "a.out";
    opts->entry_point = "main";
    opts->mode = "sandbox";
    opts->optimize_level = 2;
    opts->verbose = 0;
    opts->debug = 0;
    opts->output_format = "aetb";
    opts->target_mode = "application";
    opts->emit_format = NULL;
    opts->machine_bits = 64;
    opts->isa = "x86";
    opts->bin_flat = 0;
    opts->bin_with_map = 0;
    opts->bin_entry = NULL;
    opts->has_bin_entry_offset = 0;
    opts->bin_entry_offset = 0;
    opts->verify_let_mode = 0;
    opts->verify_let_file = NULL;
    opts->dump_reloc_mode = 0;
    opts->dump_reloc_file = NULL;
    opts->dump_reloc_output = NULL;
    opts->is_iso_mode = 0;
    opts->kernel_file = NULL;
    opts->efi_boot_file = NULL;
    opts->iso_size_mb = 512;
    opts->freestanding = 0;
    opts->no_stack_check = 0;
    opts->no_default_libs = 0;
    opts->no_shared_libs = 0;
    opts->bundle_dependencies = 0;
    opts->bundle_all_dependencies = 0;
    opts->static_only = 0;
    opts->static_complete = 0;
    opts->app_package = 0;
    opts->rom_mode = 0;
    opts->rom_size_bytes = 8ULL * 1024ULL * 1024ULL;
    opts->rom_fill_byte = 0xFF;
    opts->include_lib_count = 0;
    memset(opts->include_libs, 0, sizeof(opts->include_libs));
    opts->macho_phys_base = 0x800000000ULL;
    opts->im4p_identifier = "krnl";
    opts->config_file = NULL; // [新增此行] 显式初始化为 NULL
    
    for (int i = 1; i < argc; i++) {
#ifdef _WIN32
        if (strcmp(argv[i], "/?") == 0 || strcmp(argv[i], "/help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 1;
        } else if (strcmp(argv[i], "/debug") == 0) {
            opts->debug = 1;
            opts->verbose = 1;
        } else if (strcmp(argv[i], "/iso") == 0) {
            opts->is_iso_mode = 1;
        } else if (strcmp(argv[i], "/rom") == 0) {
            opts->rom_mode = 1;
            opts->output_format = "rom";
            opts->emit_format = "rom";
        } else if (strncmp(argv[i], "/verify-let-contract:", 21) == 0) {
            opts->verify_let_mode = 1;
            opts->verify_let_file = argv[i] + 21;
        } else if (strncmp(argv[i], "/dump-reloc-dna:", 16) == 0) {
            opts->dump_reloc_mode = 1;
            opts->dump_reloc_file = argv[i] + 16;
            if (i + 1 < argc && strncmp(argv[i+1], "/o:", 3) == 0) {
                opts->dump_reloc_output = argv[++i] + 3;
            } else if (i + 1 < argc && strncmp(argv[i+1], "/out:", 5) == 0) {
                opts->dump_reloc_output = argv[++i] + 5;
            }
        } else if (strncmp(argv[i], "/o:", 3) == 0) {
            opts->output_file = argv[i] + 3;
        } else if (strncmp(argv[i], "/out:", 5) == 0) {
            opts->output_file = argv[i] + 5;
        } else if (strncmp(argv[i], "/kernel:", 8) == 0) {
            opts->kernel_file = argv[i] + 8;
        } else if (strncmp(argv[i], "/efi:", 5) == 0) {
            opts->efi_boot_file = argv[i] + 5;
        } else if (strncmp(argv[i], "/size:", 6) == 0) {
            opts->iso_size_mb = atoll(argv[i] + 6);
        } else if (strncmp(argv[i], "/side:", 6) == 0) {
            uint64_t bytes = 0;
            if (parse_rom_size_bytes(argv[i] + 6, &bytes) != 0 || bytes == 0) {
                fprintf(stderr, "Error: /side: expects size like 8MB/16MB/512KB\n");
                return 1;
            }
            opts->rom_size_bytes = bytes;
        } else if (strncmp(argv[i], "/rom-size:", 10) == 0) {
            uint64_t bytes = 0;
            if (parse_rom_size_bytes(argv[i] + 10, &bytes) != 0 || bytes == 0) {
                fprintf(stderr, "Error: /rom-size: expects size like 8MB/16MB/512KB\n");
                return 1;
            }
            opts->rom_size_bytes = bytes;
        } else if (strncmp(argv[i], "/entry:", 7) == 0) {
            opts->entry_point = argv[i] + 7;
        } else if (strncmp(argv[i], "/mode:", 6) == 0) {
            opts->mode = argv[i] + 6;
        } else if (strncmp(argv[i], "/emit:", 6) == 0) {
            const char *fmt = argv[i] + 6;
            if (strcmp(fmt, "aetb") == 0 || strcmp(fmt, "let") == 0 || strcmp(fmt, "aki") == 0 ||
                strcmp(fmt, "efi") == 0 || strcmp(fmt, "uefi_app") == 0 || strcmp(fmt, "pe") == 0 ||
                strcmp(fmt, "hda") == 0 || strcmp(fmt, "srv") == 0 || strcmp(fmt, "bin") == 0 ||
                strcmp(fmt, "rom") == 0 || strcmp(fmt, "macho") == 0 || strcmp(fmt, "im4p") == 0 || strcmp(fmt, "exe") == 0 || strcmp(fmt, "wsys") == 0) {
                opts->output_format = fmt;
                opts->emit_format = fmt;
            } else {
                fprintf(stderr, "Error: unsupported emit format '%s'\n", fmt);
                return 1;
            }
        } else if (strncmp(argv[i], "/format:", 8) == 0) {
            const char *fmt = argv[i] + 8;
            opts->output_format = fmt;
            opts->emit_format = fmt;
        } else if (strncmp(argv[i], "/machine-bits:", 14) == 0) {
            opts->machine_bits = atoi(argv[i] + 14);
        } else if (strncmp(argv[i], "/isa:", 5) == 0) {
            opts->isa = argv[i] + 5;
        } else if (strncmp(argv[i], "/bin-entry:", 11) == 0) {
            opts->bin_entry = argv[i] + 11;
            if (parse_u64(opts->bin_entry, &opts->bin_entry_offset) == 0) {
                opts->has_bin_entry_offset = 1;
            } else {
                opts->has_bin_entry_offset = 0;
            }
        } else if (strcmp(argv[i], "/bin-flat") == 0) {
            opts->bin_flat = 1;
        } else if (strcmp(argv[i], "/bin-with-map") == 0) {
            opts->bin_with_map = 1;
        } else if (strcmp(argv[i], "/config") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '/' && argv[i+1][0] != '-') {
                opts->config_file = argv[++i];
            } else {
                opts->config_file = "config.aecf";
            }
        } else if (strncmp(argv[i], "/config:", 8) == 0) {
            opts->config_file = argv[i] + 8;
        } else if (strncmp(argv[i], "/target:", 8) == 0) {
            opts->target_mode = argv[i] + 8;
        } else if (strcmp(argv[i], "/freestanding") == 0) {
            opts->freestanding = 1;
        } else if (strcmp(argv[i], "/no-stack-check") == 0) {
            opts->no_stack_check = 1;
        } else if (strcmp(argv[i], "/no-default-libs") == 0) {
            opts->no_default_libs = 1;
        } else if (strcmp(argv[i], "/no-shared-libs") == 0) {
            opts->no_shared_libs = 1;
        } else if (strcmp(argv[i], "/bundle-dependencies") == 0) {
            opts->bundle_dependencies = 1;
        } else if (strcmp(argv[i], "/bundle-all-dependencies") == 0) {
            opts->bundle_all_dependencies = 1;
        } else if (strcmp(argv[i], "/static-only") == 0) {
            opts->static_only = 1;
        } else if (strcmp(argv[i], "/static-complete") == 0) {
            opts->static_complete = 1;
        } else if (strcmp(argv[i], "/app-package") == 0) {
            opts->app_package = 1;
        } else if (strncmp(argv[i], "/include-lib:", 13) == 0) {
            if (opts->include_lib_count < 32) {
                opts->include_libs[opts->include_lib_count++] = argv[i] + 13;
            } else {
                fprintf(stderr, "Error: Too many libraries to include (max 32)\n");
                return 1;
            }
        } else if (strncmp(argv[i], "/lib:", 5) == 0) {
            if (append_env_library_path(opts, argv[i] + 5) != 0) {
                return 1;
            }
        } else if (strncmp(argv[i], "/is:im4p:", 9) == 0) {
            opts->emit_format = "im4p";
            opts->output_format = "im4p";
            opts->im4p_identifier = argv[i] + 9;
        } else if (strncmp(argv[i], "/base:", 6) == 0) {
            if (parse_u64(argv[i] + 6, &opts->macho_phys_base) != 0) {
                fprintf(stderr, "Error: /base: requires a valid hexadecimal address\n");
                return 1;
            }
        } else if (strncmp(argv[i], "/O", 2) == 0) {
            opts->optimize_level = atoi(argv[i] + 2);
        } else if (strncmp(argv[i], "/optimize:", 10) == 0) {
            opts->optimize_level = atoi(argv[i] + 10);
        } else if (strcmp(argv[i], "/v") == 0 || strcmp(argv[i], "/verbose") == 0) {
            opts->verbose = 1;
        } else if (argv[i][0] == '/') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        } else {
            if (opts->input_count < MAX_INPUT_FILES) {
                opts->input_files[opts->input_count++] = argv[i];
            } else {
                fprintf(stderr, "Error: Too many input files\n");
                return 1;
            }
        }
#else
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 1;
        } else if (strcmp(argv[i], "--debug") == 0) {
            opts->debug = 1;
            opts->verbose = 1;
        } else if (strcmp(argv[i], "--iso") == 0) {
            opts->is_iso_mode = 1;
        } else if (strcmp(argv[i], "--rom") == 0) {
            opts->rom_mode = 1;
            opts->output_format = "rom";
            opts->emit_format = "rom";
        } else if (strcmp(argv[i], "--verify-let-contract") == 0) {
            if (i + 1 < argc) {
                opts->verify_let_mode = 1;
                opts->verify_let_file = argv[++i];
            } else {
                fprintf(stderr, "Error: --verify-let-contract requires a .let file\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--dump-reloc-dna") == 0) {
            if (i + 1 < argc) {
                opts->dump_reloc_mode = 1;
                opts->dump_reloc_file = argv[++i];
            } else {
                fprintf(stderr, "Error: --dump-reloc-dna requires a .let file\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) opts->output_file = argv[++i];
        } else if (strcmp(argv[i], "--kernel") == 0) {
            if (i + 1 < argc) opts->kernel_file = argv[++i];
        } else if (strcmp(argv[i], "--efi") == 0) {
            if (i + 1 < argc) opts->efi_boot_file = argv[++i];
        } else if (strcmp(argv[i], "--size") == 0) {
            if (i + 1 < argc) opts->iso_size_mb = atoll(argv[++i]);
        } else if (strcmp(argv[i], "--side") == 0 || strcmp(argv[i], "--rom-size") == 0) {
            if (i + 1 < argc) {
                uint64_t bytes = 0;
                if (parse_rom_size_bytes(argv[++i], &bytes) != 0 || bytes == 0) {
                    fprintf(stderr, "Error: --side expects size like 8MB/16MB/512KB\n");
                    return 1;
                }
                opts->rom_size_bytes = bytes;
            }
        } else if (strcmp(argv[i], "--entry") == 0) {
            if (i + 1 < argc) opts->entry_point = argv[++i];
        } else if (strcmp(argv[i], "--mode") == 0) {
            if (i + 1 < argc) opts->mode = argv[++i];
        } else if (strcmp(argv[i], "--emit") == 0 || strcmp(argv[i], "--format") == 0) {
            if (i + 1 < argc) {
                const char *fmt = argv[++i];
                if (strcmp(fmt, "aetb") == 0 || strcmp(fmt, "let") == 0 || strcmp(fmt, "aki") == 0 ||
                    strcmp(fmt, "efi") == 0 || strcmp(fmt, "uefi_app") == 0 || strcmp(fmt, "pe") == 0 ||
                    strcmp(fmt, "hda") == 0 || strcmp(fmt, "srv") == 0 || strcmp(fmt, "bin") == 0 ||
                    strcmp(fmt, "rom") == 0 || strcmp(fmt, "macho") == 0 || strcmp(fmt, "im4p") == 0 || strcmp(fmt, "exe") == 0 || strcmp(fmt, "wsys") == 0) {
                    opts->output_format = fmt;
                    opts->emit_format = fmt;
                } else {
                    fprintf(stderr, "Error: unsupported emit format '%s'\n", fmt);
                    return 1;
                }
            }
        } else if (strcmp(argv[i], "--machine-bits") == 0) {
            if (i + 1 < argc) {
                opts->machine_bits = atoi(argv[++i]);
                if (!(opts->machine_bits == 16 || opts->machine_bits == 32 || opts->machine_bits == 64)) {
                    fprintf(stderr, "Error: --machine-bits must be 16/32/64\n");
                    return 1;
                }
            }
        } else if (strcmp(argv[i], "--isa") == 0) {
            if (i + 1 < argc) {
                opts->isa = argv[++i];
                if (strcmp(opts->isa, "x86") != 0 &&
                    strcmp(opts->isa, "x86_64") != 0 &&
                    strcmp(opts->isa, "aarch64") != 0) {
                    fprintf(stderr, "Error: --isa must be x86|x86_64|aarch64\n");
                    return 1;
                }
            }
        } else if (strcmp(argv[i], "--bin-entry") == 0) {
            if (i + 1 < argc) {
                opts->bin_entry = argv[++i];
                if (parse_u64(opts->bin_entry, &opts->bin_entry_offset) == 0) {
                    opts->has_bin_entry_offset = 1;
                } else {
                    opts->has_bin_entry_offset = 0;
                }
            }
        } else if (strcmp(argv[i], "--bin-flat") == 0) {
            opts->bin_flat = 1;
        } else if (strcmp(argv[i], "--bin-with-map") == 0) {
            opts->bin_with_map = 1;
        } else if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
                opts->config_file = argv[++i];
            } else {
                opts->config_file = "config.aecf";
            }
        } else if (strcmp(argv[i], "--target") == 0) {
            if (i + 1 < argc) {
                const char *target = argv[++i];
                if (strcmp(target, "application") == 0 || strcmp(target, "kernel") == 0) {
                    opts->target_mode = target;
                } else {
                    fprintf(stderr, "Error: Invalid target '%s'. Valid: application, kernel\n", target);
                    return 1;
                }
            }
        } else if (strcmp(argv[i], "--freestanding") == 0) {
            opts->freestanding = 1;
        } else if (strcmp(argv[i], "--no-stack-check") == 0) {
            opts->no_stack_check = 1;
        } else if (strcmp(argv[i], "--no-default-libs") == 0) {
            opts->no_default_libs = 1;
        } else if (strcmp(argv[i], "--no-shared-libs") == 0) {
            opts->no_shared_libs = 1;
        } else if (strcmp(argv[i], "--bundle-dependencies") == 0) {
            opts->bundle_dependencies = 1;
        } else if (strcmp(argv[i], "--bundle-all-dependencies") == 0) {
            opts->bundle_all_dependencies = 1;
        } else if (strcmp(argv[i], "--static-only") == 0) {
            opts->static_only = 1;
        } else if (strcmp(argv[i], "--static-complete") == 0) {
            opts->static_complete = 1;
        } else if (strcmp(argv[i], "--app-package") == 0) {
            opts->app_package = 1;
        } else if (strcmp(argv[i], "--include-lib") == 0) {
            if (i + 1 < argc && opts->include_lib_count < 32) {
                opts->include_libs[opts->include_lib_count++] = argv[++i];
            } else if (opts->include_lib_count >= 32) {
                fprintf(stderr, "Error: Too many libraries to include (max 32)\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--lib") == 0) {
            if (i + 1 < argc) {
                if (append_env_library_path(opts, argv[++i]) != 0) {
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: --lib requires a file or directory path\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--is") == 0) {
            /* --is im4p <identifier>: 指定 IM4P 标识符 */
            if (i + 2 < argc && strcmp(argv[i+1], "im4p") == 0) {
                opts->emit_format = "im4p";
                opts->output_format = "im4p";
                opts->im4p_identifier = argv[i+2];
                i += 2;
            } else {
                fprintf(stderr, "Error: --is requires 'im4p <identifier>'\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--base") == 0) {
            /* --base <address>: Mach-O 物理基址 (16进制格式) */
            if (i + 1 < argc) {
                if (parse_u64(argv[++i], &opts->macho_phys_base) != 0) {
                    fprintf(stderr, "Error: --base requires a valid hexadecimal address\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: --base requires an address argument\n");
                return 1;
            }
        } else if (strncmp(argv[i], "-O", 2) == 0) {
            // 处理 -O2
            if (strlen(argv[i]) > 2) {
                opts->optimize_level = atoi(&argv[i][2]);
            } else if (i + 1 < argc) {
                opts->optimize_level = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--optimize") == 0) {
            // 处理 --optimize O2
            if (i + 1 < argc) {
                char *val = argv[++i];
                if (val[0] == 'O') {
                    opts->optimize_level = atoi(val + 1);
                } else {
                    opts->optimize_level = atoi(val);
                }
            }
        } else if (strcmp(argv[i], "-v") == 0) {
            opts->verbose = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        } else {
            // 输入文件
            if (opts->input_count < MAX_INPUT_FILES) {
                opts->input_files[opts->input_count++] = argv[i];
            } else {
                fprintf(stderr, "Error: Too many input files\n");
                return 1;
            }
        }
#endif
    }
    return 0;
}

static char* read_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        fprintf(stderr, "Error: Out of memory\n");
        return NULL;
    }
    
    if (fread(content, 1, size, f) != (size_t)size) {
        free(content);
        fclose(f);
        fprintf(stderr, "Error: Failed to read file '%s'\n", filename);
        return NULL;
    }
    
    content[size] = '\0';
    fclose(f);
    
    return content;
}

static char *trim_inplace(char *s) {
    char *end;
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static int append_asm_line(char ***lines, size_t *count, size_t *cap, const char *line) {
    char **grown;
    char *dup;
    if (!lines || !count || !cap || !line) return -1;
    if (*count >= *cap) {
        size_t new_cap = (*cap == 0) ? 64 : (*cap * 2);
        grown = (char **)realloc(*lines, new_cap * sizeof(char *));
        if (!grown) return -1;
        *lines = grown;
        *cap = new_cap;
    }
    dup = strdup(line);
    if (!dup) return -1;
    (*lines)[(*count)++] = dup;
    return 0;
}

static void free_asm_lines(char **lines, size_t count) {
    size_t i;
    if (!lines) return;
    for (i = 0; i < count; i++) free(lines[i]);
    free(lines);
}

static int is_word_boundary_char(char c) {
    return !(isalnum((unsigned char)c) || c == '_');
}

static int extract_asm_strings_from_source(const char *src, char ***out_lines, size_t *out_count) {
    const char *p;
    char **lines = NULL;
    size_t count = 0, cap = 0;
    if (!src || !out_lines || !out_count) return -1;
    *out_lines = NULL;
    *out_count = 0;
    p = src;
    while (*p) {
        if ((p == src || is_word_boundary_char(*(p - 1))) &&
            p[0] == 'a' && p[1] == 's' && p[2] == 'm' &&
            is_word_boundary_char(p[3])) {
            const char *q = p + 3;
            int depth = 0;
            while (*q && isspace((unsigned char)*q)) q++;
            if (*q != '{') {
                p++;
                continue;
            }
            depth = 1;
            q++;
            while (*q && depth > 0) {
                if (*q == '/' && q[1] == '/') {
                    q += 2;
                    while (*q && *q != '\n') q++;
                    continue;
                }
                if (*q == '/' && q[1] == '*') {
                    q += 2;
                    while (*q && !(q[0] == '*' && q[1] == '/')) q++;
                    if (*q) q += 2;
                    continue;
                }
                if (*q == '"') {
                    char buf[8192];
                    size_t bi = 0;
                    q++;
                    while (*q && *q != '"') {
                        if (*q == '\\' && q[1] != '\0') {
                            if (q[1] == 'n') {
                                if (bi + 1 < sizeof(buf)) buf[bi++] = '\n';
                                q += 2;
                                continue;
                            }
                            if (bi + 1 < sizeof(buf)) buf[bi++] = q[1];
                            q += 2;
                            continue;
                        }
                        if (bi + 1 < sizeof(buf)) buf[bi++] = *q;
                        q++;
                    }
                    buf[bi] = '\0';
                    if (bi > 0) {
                        char *trimmed;
                        char tmp[8192];
                        snprintf(tmp, sizeof(tmp), "%s", buf);
                        trimmed = trim_inplace(tmp);
                        if (*trimmed != '\0') {
                            if (append_asm_line(&lines, &count, &cap, trimmed) != 0) {
                                free_asm_lines(lines, count);
                                return -1;
                            }
                        }
                    }
                    if (*q == '"') q++;
                    continue;
                }
                if (*q == '{') depth++;
                else if (*q == '}') depth--;
                q++;
            }
            p = q;
            continue;
        }
        p++;
    }
    *out_lines = lines;
    *out_count = count;
    return 0;
}

static void replace_slash_with_underscore(char *s) {
    char *p;
    if (!s) return;
    p = s;
    while (*p) {
        if (*p == '/') {
            char prev = (p == s) ? ' ' : *(p - 1);
            if (p == s ||
                prev == ' ' || prev == '\t' || prev == ',' ||
                prev == '[' || prev == '+' || prev == '-' ||
                prev == '(' || prev == ':') {
                memmove(p, p + 1, strlen(p));
                continue;
            }
            *p = '_';
        }
        p++;
    }
}

static int asm_line_needs_64(const char *line) {
    static const char *needles[] = {
        " rsp", " rbp", " rsi", " rdi", " rip", " r8", " r9", " r10", " r11", " r12", " r13", " r14", " r15"
    };
    size_t i;
    if (!line) return 0;
    for (i = 0; i < sizeof(needles) / sizeof(needles[0]); i++) {
        if (strstr(line, needles[i]) != NULL) return 1;
    }
    if (strstr(line, "aethel2_long_mode_entry:") != NULL) return 1;
    return 0;
}

static int normalize_asm_line(const char *in, char *out, size_t out_sz) {
    char buf[8192];
    char *t;
    char symbuf[8192];
    size_t si = 0, i = 0;
    if (!in || !out || out_sz == 0) return -1;
    snprintf(buf, sizeof(buf), "%s", in);
    t = trim_inplace(buf);
    if (*t == '\0') {
        out[0] = '\0';
        return 0;
    }
    replace_slash_with_underscore(t);
    if (strncmp(t, ".org", 4) == 0 && isspace((unsigned char)t[4])) {
        snprintf(out, out_sz, "org%s", t + 4);
        return 0;
    }
    if (strncmp(t, ".byte", 5) == 0 && isspace((unsigned char)t[5])) {
        snprintf(out, out_sz, "db%s", t + 5);
        return 0;
    }
    if (strncmp(t, ".word", 5) == 0 && isspace((unsigned char)t[5])) {
        snprintf(out, out_sz, "dw%s", t + 5);
        return 0;
    }
    if (strncmp(t, ".long", 5) == 0 && isspace((unsigned char)t[5])) {
        snprintf(out, out_sz, "dd%s", t + 5);
        return 0;
    }
    if (strncmp(t, ".align", 6) == 0 && isspace((unsigned char)t[6])) {
        char *arg = trim_inplace(t + 6);
        snprintf(out, out_sz, "align %s, db 0", arg);
        return 0;
    }
    while (t[i] != '\0' && si + 1 < sizeof(symbuf)) {
        if (t[i] == '.' &&
            (i == 0 || t[i - 1] == ' ' || t[i - 1] == '\t' || t[i - 1] == ',' || t[i - 1] == ':') &&
            (isalnum((unsigned char)t[i + 1]) || t[i + 1] == '_')) {
            i++;
            continue;
        }
        symbuf[si++] = t[i++];
    }
    symbuf[si] = '\0';
    snprintf(out, out_sz, "%s", symbuf);
    return 0;
}

static long file_size_or_neg(const char *path) {
    struct stat st;
    if (!path) return -1;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

static int compile_inline_asm_to_bin(const char *input_file,
                                     const char *output_file,
                                     int machine_bits,
                                     int verbose) {
    char *source = NULL;
    char **lines = NULL;
    size_t line_count = 0;
    char asm_tmp[512];
    int asm_fd = -1;
    FILE *asm_fp = NULL;
    int rc = -1;
    int status = 0;
    int switched64 = 0;
    size_t i;
#ifndef _WIN32
    pid_t child_handle;
#endif

    if (!input_file || !output_file) return 0;
    source = read_file(input_file);
    if (!source) return -1;
    if (extract_asm_strings_from_source(source, &lines, &line_count) != 0) {
        free(source);
        return -1;
    }
    free(source);
    if (line_count == 0) {
        free_asm_lines(lines, line_count);
        return 0;
    }

    asm_fd = create_temp_file_path(asm_tmp, sizeof(asm_tmp), "aasm");
    if (asm_fd < 0) {
        free_asm_lines(lines, line_count);
        return -1;
    }
    asm_fp = fdopen(asm_fd, "w");
    if (!asm_fp) {
        close(asm_fd);
        unlink(asm_tmp);
        free_asm_lines(lines, line_count);
        return -1;
    }

    fprintf(asm_fp, "; auto-generated from %s\n", input_file);
    if (machine_bits == 16) fprintf(asm_fp, "bits 16\n");
    else if (machine_bits == 32) fprintf(asm_fp, "bits 32\n");
    else fprintf(asm_fp, "bits 64\n");

    for (i = 0; i < line_count; i++) {
        char norm[8192];
        if (normalize_asm_line(lines[i], norm, sizeof(norm)) != 0) {
            fclose(asm_fp);
            unlink(asm_tmp);
            free_asm_lines(lines, line_count);
            return -1;
        }
        if (norm[0] == '\0') continue;
        if (!switched64 && machine_bits != 64 && asm_line_needs_64(norm)) {
            fprintf(asm_fp, "bits 64\n");
            switched64 = 1;
        }
        fprintf(asm_fp, "%s\n", norm);
    }
    fclose(asm_fp);

    if (verbose) {
        fprintf(stderr, "[ASM-BIN] direct NASM pipeline: %s -> %s\n", input_file, output_file);
    }

#ifdef _WIN32
    {
        char cmd[8192];
        int n = snprintf(cmd, sizeof(cmd), "nasm -f bin -o \"%s\" \"%s\"", output_file, asm_tmp);
        if (n < 0 || (size_t)n >= sizeof(cmd)) {
            unlink(asm_tmp);
            free_asm_lines(lines, line_count);
            return -1;
        }
        status = system(cmd);
        if (status == 0 && file_size_or_neg(output_file) >= 0) {
            rc = 1;
        } else {
            unlink(output_file);
            rc = -1;
        }
    }
#else
    child_handle = fork();
    if (child_handle == 0) {
        execlp("nasm", "nasm", "-f", "bin", "-o", output_file, asm_tmp, (char *)NULL);
        _exit(127);
    }
    if (child_handle < 0) {
        unlink(asm_tmp);
        free_asm_lines(lines, line_count);
        return -1;
    }
    if (waitpid(child_handle, &status, 0) < 0) {
        unlink(asm_tmp);
        free_asm_lines(lines, line_count);
        return -1;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && file_size_or_neg(output_file) >= 0) {
        rc = 1;
    } else {
        unlink(output_file);
        rc = -1;
    }
#endif

    unlink(asm_tmp);
    free_asm_lines(lines, line_count);
    return rc;
}

static int merge_semantic_result(SemanticResult *dst, const SemanticResult *src) {
    size_t i;
    if (!dst || !src) return -1;

    dst->error_count += src->error_count;
    dst->warning_count += src->warning_count;
    dst->has_metal_block |= src->has_metal_block;
    dst->requires_architect_mode |= src->requires_architect_mode;
    dst->has_rimport |= src->has_rimport;
    dst->trap_hint_count += src->trap_hint_count;
    dst->reloc_dna_count += src->reloc_dna_count;
    dst->ipc_contract_count += src->ipc_contract_count;
    if (src->identity_contract_min_sip > dst->identity_contract_min_sip) {
        dst->identity_contract_min_sip = src->identity_contract_min_sip;
    }

    for (i = 0; i < src->import_count; i++) {
        if (dst->import_count >= 128) {
            fprintf(stderr, "Error: Semantic import table overflow while merging\n");
            return -1;
        }
        dst->imports[dst->import_count++] = src->imports[i];
    }

    return 0;
}

static int merge_program_ast(ASTNode *dst, ASTNode *src) {
    ASTNode **merged_decls;
    int new_count;
    int i;

    if (!dst || !src) return -1;
    if (dst->type != AST_PROGRAM || src->type != AST_PROGRAM) return -1;
    if (src->data.program.decl_count == 0) return 0;

    new_count = dst->data.program.decl_count + src->data.program.decl_count;
    merged_decls = realloc(dst->data.program.declarations, sizeof(ASTNode*) * (size_t)new_count);
    if (!merged_decls) {
        return -1;
    }

    dst->data.program.declarations = merged_decls;
    for (i = 0; i < src->data.program.decl_count; i++) {
        dst->data.program.declarations[dst->data.program.decl_count + i] =
            src->data.program.declarations[i];
    }
    dst->data.program.decl_count = new_count;
    return 0;
}

static uint16_t map_isa_to_contract(const char *isa, int machine_bits) {
    if (!isa) return (machine_bits == 64) ? LET_ISA_X86_64 : LET_ISA_X86;
    if (strcmp(isa, "x86") == 0) {
        return (machine_bits == 64) ? LET_ISA_X86_64 : LET_ISA_X86;
    }
    if (strcmp(isa, "aarch64") == 0) return LET_ISA_AARCH64;
    if (strcmp(isa, "x86_64") == 0 && machine_bits == 64) return LET_ISA_X86_64;
    if (strcmp(isa, "x86_64") == 0) return LET_ISA_X86;
    return LET_ISA_UNKNOWN;
}

static uint8_t default_reloc_width_for_bits(int machine_bits) {
    if (machine_bits <= 16) return 16;
    if (machine_bits <= 32) return 32;
    return 64;
}

static int run_compiler(CompilerOptions *opts) {
    const char *effective_mode = opts->mode;
    const char *effective_target_mode = opts->target_mode;
    LetEmitOptions let_emit_opts;
    LetWeaveOptions let_weave_opts;
    // 编译模式：支持多个输入文件用于内核构建
    if (opts->input_count == 0) {
        fprintf(stderr, "Error: No input files specified.\n");
        return 1;
    }

    if (opts->debug) {
        printf("[DEBUG] Compiler Debug Mode Enabled\n");
        printf("[DEBUG] Input files: %d\n", opts->input_count);
        for (int i = 0; i < opts->input_count; i++) {
            printf("[DEBUG]   [%d] %s\n", i + 1, opts->input_files[i]);
        }
        printf("[DEBUG] Output file: %s\n", opts->output_file);
        printf("[DEBUG] Target mode: %s\n", opts->target_mode);
        printf("[DEBUG] Optimization level: O%d\n", opts->optimize_level);
        printf("[DEBUG] Debug flags enabled\n");
    }

    if (opts->verbose) {
        printf("[INFO] 编译中: %d 个文件 -> %s\n", opts->input_count, opts->output_file);
        printf("[INFO] 目标模式: %s\n", opts->target_mode);
        printf("[INFO] 优化级别: O%d\n", opts->optimize_level);
        if (opts->freestanding) printf("[INFO] 模式: 裸机 (无 libc)\n");
        if (opts->no_default_libs) printf("[INFO] 标志: 不使用默认库\n");
        if (opts->no_shared_libs) printf("[INFO] 标志: 不使用共享库\n");
        if (opts->bundle_dependencies) printf("[INFO] 标志: 打包依赖\n");
        if (opts->bundle_all_dependencies) printf("[INFO] 标志: 打包所有依赖\n");
        if (opts->app_package) printf("[INFO] 标志: 应用程序包 (IYA) 格式\n");
        printf("[INFO] 机器契约: isa=%s bits=%d\n", opts->isa, opts->machine_bits);
        if (opts->debug) printf("[INFO] 模式: 调试信息已启用\n");
    }
    
    /* 检测目标格式 */
    const char *output_file = opts->output_file;
    int target_format = FORMAT_AETB;  /* 默认 */
    if (opts->emit_format) {
        if (strcmp(opts->emit_format, "aki") == 0) target_format = FORMAT_AKI;
        else if (strcmp(opts->emit_format, "let") == 0) target_format = FORMAT_LET;
        else if (strcmp(opts->emit_format, "srv") == 0) target_format = FORMAT_SRV;
        else if (strcmp(opts->emit_format, "hda") == 0) target_format = FORMAT_HDA;
        else if (strcmp(opts->emit_format, "efi") == 0 || strcmp(opts->emit_format, "uefi_app") == 0) target_format = FORMAT_EFI;
        else if (strcmp(opts->emit_format, "pe") == 0) target_format = FORMAT_PE;
        else if (strcmp(opts->emit_format, "exe") == 0) target_format = FORMAT_EXE;
        else if (strcmp(opts->emit_format, "bin") == 0) target_format = FORMAT_BIN;
        else if (strcmp(opts->emit_format, "rom") == 0) target_format = FORMAT_ROM;
        else if (strcmp(opts->emit_format, "macho") == 0) target_format = FORMAT_MACHO;
        else if (strcmp(opts->emit_format, "im4p") == 0) target_format = FORMAT_IM4P;
        else if (strcmp(opts->emit_format, "wsys") == 0) target_format = FORMAT_WSYS;
        else target_format = FORMAT_AETB;
    } else if (strstr(output_file, ".aki") != NULL) {
        target_format = FORMAT_AKI;
    } else if (strstr(output_file, ".let") != NULL) {
        target_format = FORMAT_LET;
    } else if (strstr(output_file, ".srv") != NULL) {
        target_format = FORMAT_SRV;
    } else if (strstr(output_file, ".hda") != NULL) {
        target_format = FORMAT_HDA;
    } else if (strstr(output_file, ".EFI") != NULL || strstr(output_file, ".efi") != NULL) {
        target_format = FORMAT_EFI;
    } else if (strstr(output_file, ".exe") != NULL || strstr(output_file, ".EXE") != NULL) {
        target_format = FORMAT_EXE;
    } else if (strstr(output_file, ".bin") != NULL) {
        target_format = FORMAT_BIN;
    } else if (strstr(output_file, ".rom") != NULL) {
        target_format = FORMAT_ROM;
    } else if (strstr(output_file, ".macho") != NULL) {
        target_format = FORMAT_MACHO;
    } else if (strstr(output_file, ".im4p") != NULL) {
        target_format = FORMAT_IM4P;
    } else if (strstr(output_file, ".sys") != NULL || strstr(output_file, ".SYS") != NULL) {
        target_format = FORMAT_WSYS;
    }
    
    if (opts->verbose) {
        const char *fmt_name = "AETB";
        switch (target_format) {
            case FORMAT_LET: fmt_name = "LET"; break;
            case FORMAT_AKI: fmt_name = "AKI"; break;
            case FORMAT_SRV: fmt_name = "SRV"; break;
            case FORMAT_HDA: fmt_name = "HDA"; break;
            case FORMAT_EFI: fmt_name = "PE32+ EFI (Embedded AETB)"; break;
            case FORMAT_PE: fmt_name = "PE32+ (Industrial Grade UEFI)"; break;
            case FORMAT_EXE: fmt_name = "EXE+ (Windows Console Executable)"; break;
            case FORMAT_BIN: fmt_name = "BIN"; break;
            case FORMAT_ROM: fmt_name = "ROM (Flash Image)"; break;
            case FORMAT_MACHO: fmt_name = "Mach-O (Bare-metal Apple Silicon)"; break;
            case FORMAT_IM4P: fmt_name = "IM4P (Image4 Payload for iBoot)"; break;
            case FORMAT_WSYS: fmt_name = "WSYS (Windows Kernel Driver)"; break;
        }
        printf("[INFO] 目标格式: %s\n", fmt_name);
    }

    if ((target_format == FORMAT_EFI ||
         target_format == FORMAT_PE ||
         target_format == FORMAT_EXE ||
         target_format == FORMAT_WSYS ||
         target_format == FORMAT_AKI ||
         target_format == FORMAT_HDA ||
         target_format == FORMAT_SRV ||
         target_format == FORMAT_BIN ||
         target_format == FORMAT_ROM ||
         target_format == FORMAT_MACHO ||
         target_format == FORMAT_IM4P) &&
        strcmp(opts->mode, "architect") != 0) {
        effective_mode = "architect";
        if (opts->verbose) {
            printf("[INFO] 系统二进制目标自动切换语义模式: sandbox -> architect\n");
        }
    }
    if ((target_format == FORMAT_EFI ||
         target_format == FORMAT_PE ||
         target_format == FORMAT_WSYS ||
         target_format == FORMAT_AKI ||
         target_format == FORMAT_HDA ||
         target_format == FORMAT_SRV ||
         target_format == FORMAT_BIN ||
         target_format == FORMAT_ROM ||
         target_format == FORMAT_MACHO ||
         target_format == FORMAT_IM4P) &&
        strcmp(opts->target_mode, "kernel") != 0) {
        effective_target_mode = "kernel";
        if (opts->verbose) {
            printf("[INFO] 系统二进制目标自动切换 target: application -> kernel\n");
        }
    }

    if (target_format == FORMAT_BIN && opts->input_count == 1) {
        int asm_direct = compile_inline_asm_to_bin(opts->input_files[0],
                                                   output_file,
                                                   opts->machine_bits,
                                                   opts->verbose);
        if (asm_direct == 1) {
            if (opts->verbose) {
                printf("✓ 编译成功\n");
                printf("  输出: %s\n", opts->output_file);
            }
            return 0;
        }
        if (asm_direct < 0) {
            fprintf(stderr, "Error: direct asm->bin pipeline failed for '%s'\n", opts->input_files[0]);
            return 1;
        }
    }

    memset(&let_emit_opts, 0, sizeof(let_emit_opts));
    let_emit_opts.target_isa = map_isa_to_contract(opts->isa, opts->machine_bits);
    let_emit_opts.machine_bits = (uint16_t)opts->machine_bits;
    let_emit_opts.endianness = LET_ENDIAN_LITTLE;
    let_emit_opts.abi_kind = opts->freestanding ? LET_ABI_FREESTANDING : LET_ABI_SYSV;
    let_emit_opts.code_model = LET_CODE_MODEL_FLAT;
    let_emit_opts.reloc_width = default_reloc_width_for_bits(opts->machine_bits);
    let_emit_opts.entry_encoding = LET_ENTRY_ENCODING_OFFSET;
    let_emit_opts.bin_flags = LET_BIN_FLAG_EXPORTABLE | LET_BIN_FLAG_RELOC_COMPLETE_REQUIRED;
    if (opts->bin_flat) {
        let_emit_opts.bin_flags |= LET_BIN_FLAG_FLAT_DEFAULT;
    }
    let_emit_opts.reloc_encoding_version = 1u;
    let_emit_opts.instruction_profile = 1u;
    let_emit_opts.syscall_profile = opts->freestanding ? 1u : 2u;
    let_emit_opts.sandbox_patch_required = (strcmp(effective_mode, "architect") == 0) ? 0u : 1u;

    /* LET weaving path: .let -> AKI/HDA/SRV/AETB via assembly weaver */
    memset(&let_weave_opts, 0, sizeof(let_weave_opts));
    let_weave_opts.bin_flat = opts->bin_flat;
    let_weave_opts.bin_with_map = opts->bin_with_map;
    let_weave_opts.has_bin_entry_offset = opts->has_bin_entry_offset;
    let_weave_opts.bin_entry_offset = (unsigned long long)opts->bin_entry_offset;
    let_weave_opts.rom_size_bytes = (unsigned long long)opts->rom_size_bytes;
    let_weave_opts.rom_fill_byte = opts->rom_fill_byte;

    if (opts->input_count == 1 &&
        strstr(opts->input_files[0], ".let") != NULL &&
        target_format != FORMAT_LET &&
        target_format != FORMAT_EFI) {
        int weave_target = 0;
        switch (target_format) {
            case FORMAT_AKI: weave_target = LET_WEAVE_TARGET_AKI; break;
            case FORMAT_SRV: weave_target = LET_WEAVE_TARGET_SRV; break;
            case FORMAT_HDA: weave_target = LET_WEAVE_TARGET_HDA; break;
            case FORMAT_AETB: weave_target = LET_WEAVE_TARGET_AETB; break;
            case FORMAT_BIN: weave_target = LET_WEAVE_TARGET_BIN; break;
            case FORMAT_ROM: weave_target = LET_WEAVE_TARGET_ROM; break;
            default: weave_target = 0; break;
        }
        if (weave_target == 0) {
            fprintf(stderr, "Error: Unsupported weave target for LET input\n");
            return 1;
        }
        if (opts->verbose) {
            printf("[INFO] LET weaving: %s -> %s\n", opts->input_files[0], opts->output_file);
        }
        if (let_weave_to_target(opts->input_files[0], opts->output_file, weave_target, opts->verbose, &let_weave_opts) != 0) {
            fprintf(stderr, "Error: LET weaving failed\n");
            return 1;
        }
        if (opts->verbose) {
            printf("✓ 编译成功\n");
            printf("  输出: %s\n", opts->output_file);
        }
        return 0;
    }
    
    /* 创建临时内存文件用于生成代码 */
    FILE *temp_output = tmpfile();
    if (!temp_output) {
        fprintf(stderr, "Error: Failed to create temporary file\n");
        return 1;
    }
    
    CodeGenerator *gen = codegen_create(temp_output);
    if (!gen) {
        fprintf(stderr, "Error: Failed to create code generator\n");
        fclose(temp_output);
        return 1;
    }
    
    /* 设置编译目标格式（用于print()机器码生成）*/
    gen->target_format = target_format;
    gen->use_uefi = (target_format == FORMAT_EFI || target_format == FORMAT_PE);
    gen->use_syscall = (target_format == FORMAT_AETB || target_format == FORMAT_AKI || 
                        target_format == FORMAT_SRV || target_format == FORMAT_HDA ||
                        target_format == FORMAT_ROM);
    if (opts->entry_point && opts->entry_point[0] != '\0' && strcmp(opts->entry_point, "main") != 0) {
        gen->entry_point_name = opts->entry_point;
    }

    if (codegen_set_target(gen, opts->isa, opts->machine_bits) != 0) {
        fprintf(stderr, "Error: unsupported Stage1 target combination: isa=%s bits=%d\n", opts->isa, opts->machine_bits);
        codegen_destroy(gen);
        fclose(temp_output);
        return 1;
    }
    
    ASTNode *merged_ast = NULL;
    SemanticResult semantic_agg;
    int frontend_failed = 0;
    semantic_result_init(&semantic_agg);
    
    /* 处理所有输入文件（含递归导入解析） */
    for (int file_idx = 0; file_idx < opts->input_count; file_idx++) {
        if (opts->verbose) {
            printf("[INFO] Processing file %d/%d: %s\n", file_idx + 1, opts->input_count, opts->input_files[file_idx]);
        }
        
        /* 新增：验证禁止 C 导出（仅内核模式） */
        if (validate_no_foreign_c(opts->input_files[file_idx], effective_target_mode) != 0) {
            codegen_destroy(gen);
            return 1;
        }
        
        char *source = read_file(opts->input_files[file_idx]);
        if (!source) {
            codegen_destroy(gen);
            return 1;
        }

        if (target_format == FORMAT_ROM) {
            char **rom_asm_lines = NULL;
            size_t rom_asm_count = 0;
            if (extract_asm_strings_from_source(source, &rom_asm_lines, &rom_asm_count) != 0) {
                free(source);
                codegen_destroy(gen);
                return 1;
            }
            if (rom_asm_count > 0) {
                fprintf(stderr, "Error: ROM build forbids asm blocks; use hardware/primal layer instead (%s)\n",
                        opts->input_files[file_idx]);
                free_asm_lines(rom_asm_lines, rom_asm_count);
                free(source);
                codegen_destroy(gen);
                return 1;
            }
            free_asm_lines(rom_asm_lines, rom_asm_count);
        }
        
        /* [任务实现-03] 预处理阶段：检查禁忌头文件 */
        if (preprocessor_scan_for_forbidden_includes(source) > 0) {
            fprintf(stderr, "编译已被罢工，退出码: %d\n", COMPILER_STRIKE_CODE);
            free(source);
            codegen_destroy(gen);
            return COMPILER_STRIKE_CODE;
        }
        
        // 1. 词法分析
        Lexer *lexer = lexer_create(source);
        Token *tokens = lexer_tokenize(lexer);
        const char *lex_err = lexer_get_error(lexer);
        if (lex_err && lex_err[0] != '\0') {
            fprintf(stderr, "[FATAL] Lexer error in file '%s': %s\n",
                    opts->input_files[file_idx], lex_err);
            frontend_failed = 1;
        }
        
        // 2. 语法分析
        Parser *parser = parser_create(tokens, opts->debug);

        ASTNode *file_ast = parser_parse_program(parser);

        const char *parse_err = parser_get_error(parser);
        int parse_err_count = parser_get_error_count(parser);
        int parse_panic = parser_is_panic(parser);
        if ((parse_err && parse_err[0] != '\0') || parse_err_count > 0 || parse_panic) {
            fprintf(stderr,
                    "[FATAL] Parser failed in file '%s' (errors=%d, panic=%d): %s\n",
                    opts->input_files[file_idx],
                    parse_err_count,
                    parse_panic,
                    (parse_err && parse_err[0] != '\0') ? parse_err : "(no parser message)");
            frontend_failed = 1;
        }

        if (!frontend_failed && file_ast) {
            char sem_error[512] = {0};
            SemanticResult file_sem;
            int sem_ret;
            sem_ret = semantic_analyze_program(file_ast,
                                               opts->input_files[file_idx],
                                               effective_mode,
                                               &file_sem,
                                               sem_error,
                                               sizeof(sem_error));
            if (sem_ret != 0) {
                fprintf(stderr,
                        "[FATAL] Semantic check failed in '%s': %s\n",
                        opts->input_files[file_idx],
                        sem_error[0] ? sem_error : "semantic violation");
                frontend_failed = 1;
            } else if (merge_semantic_result(&semantic_agg, &file_sem) != 0) {
                fprintf(stderr,
                        "[FATAL] Failed to merge semantic result for '%s'\n",
                        opts->input_files[file_idx]);
                frontend_failed = 1;
            }
        }
        
        /* 合并AST：将文件的声明添加到合并AST中 */
        if (file_ast) {
            if (merged_ast == NULL) {
                merged_ast = file_ast;
            } else if (merge_program_ast(merged_ast, file_ast) != 0) {
                fprintf(stderr,
                        "[FATAL] Failed to merge AST for '%s'\n",
                        opts->input_files[file_idx]);
                frontend_failed = 1;
            }
        }
        
        parser_destroy(parser);
        lexer_destroy(lexer);
        free(source);

        /* 递归解析导入：仅收集，解析和编译留给上层构建系统通过输入列表提供
         * （工业级：保留显式输入控制，避免隐式文件注入） */
        ImportList imports = {0};
        import_list_collect(file_ast, opts->input_files[file_idx], &imports);

        /* 仅告警提示缺失输入，由调用层更新 mk 依赖；不进行隐式编译以保持可控性 */
        for (size_t ii = 0; ii < imports.count; ii++) {
            char resolved[512];
            char from_buf[512];
            char root_buf[512];
            char *from_dir = NULL;
            char *root_dir = NULL;

            strncpy(from_buf, imports.items[ii].from_file, sizeof(from_buf) - 1);
            from_buf[sizeof(from_buf) - 1] = '\0';
            if (from_buf[0]) {
                from_dir = dirname(from_buf);
            }

            if (opts->input_count > 0) {
                strncpy(root_buf, opts->input_files[0], sizeof(root_buf) - 1);
                root_buf[sizeof(root_buf) - 1] = '\0';
                root_dir = dirname(root_buf);
            }

            if (import_resolve_path(imports.items[ii].module,
                                    from_dir,
                                    root_dir,
                                    resolved,
                                    sizeof(resolved)) == 0) {
                int already_listed = 0;
                for (int k = 0; k < opts->input_count; k++) {
                    if (strcmp(opts->input_files[k], resolved) == 0) {
                        already_listed = 1;
                        break;
                    }
                }
                if (!already_listed) {
                    fprintf(stderr,
                            "[WARN] Import '%s' resolved to '%s' (from %s) is not in build graph; add to input list for full compilation.\n",
                            imports.items[ii].module, resolved, imports.items[ii].from_file);
                }
            } else {
                fprintf(stderr,
                        "[WARN] Import '%s' (from %s) could not be resolved automatically; ensure mk translates naming per AethelOS rules.\n",
                        imports.items[ii].module,
                        imports.items[ii].from_file);
            }
        }

        if (frontend_failed) {
            break;
        }
    }

    if (frontend_failed) {
        fprintf(stderr, "[FATAL] Frontend compilation failed. Output file '%s' will NOT be generated.\n",
                output_file);
        codegen_destroy(gen);
        fclose(temp_output);
        return 1;
    }
    
    /* 使用合并的AST进行代码生成 */
    int result = 0;
    if (!merged_ast) {
        fprintf(stderr, "[FATAL] No valid AST generated from input sources. Aborting.\n");
        codegen_destroy(gen);
        fclose(temp_output);
        return 1;
    }
    result = codegen_generate(gen, merged_ast);
    
    if (result != 0) {
        const char *cg_error = codegen_get_error(gen);
        if (cg_error && cg_error[0] != '\0') {
            fprintf(stderr, "[FATAL] Code generation failed: %s\n", cg_error);
        } else {
            fprintf(stderr, "[FATAL] Code generation failed with error code %d\n", result);
        }
        codegen_destroy(gen);
        fclose(temp_output);
        return result;
    }

    codegen_destroy(gen);
    
    /* 从临时文件读取编译的二进制数据 */
    fseek(temp_output, 0, SEEK_END);
    size_t binary_size = ftell(temp_output);
    fseek(temp_output, 0, SEEK_SET);
    
    uint8_t *binary_data = malloc(binary_size);
    if (!binary_data) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(temp_output);
        return 1;
    }
    
    if (fread(binary_data, 1, binary_size, temp_output) != binary_size) {
        fprintf(stderr, "Error: Failed to read compiled binary\n");
        free(binary_data);
        fclose(temp_output);
        return 1;
    }
    
    fclose(temp_output);
    
    /* 根据目标格式直接生成最终文件 */
    int format_result = 0;
    
    /* 从完整二进制数据中提取三个Zone */
    const uint8_t *code = NULL, *mirror_data = NULL, *constant_data = NULL;
    size_t code_size = 0, mirror_size = 0, constant_size = 0;
    uint32_t entry_offset = 0;  /* PE格式所需的EFI入口点偏移 */
    uint64_t rom_entry_offset = 0;
    
    if (extract_zones_from_binary(binary_data, binary_size, 
                                 &code, &code_size,
                                 &mirror_data, &mirror_size,
                                 &constant_data, &constant_size) != 0) {
        fprintf(stderr, "Error: Failed to extract zones from binary data\n");
        free(binary_data);
        return 1;
    }
    
    /* 对于PE格式，计算entry_point_offset */
    /* 定位到 gen_expression 的循环之后，修改 entry_point_offset 的 logic */
    if (target_format == FORMAT_PE || target_format == FORMAT_EXE || target_format == FORMAT_WSYS) {
        if (binary_size >= sizeof(AethelBinaryHeader)) {
            const AethelBinaryHeader *hdr = (const AethelBinaryHeader *)binary_data;
            /* 
            * 修复：必须提取编译器阶段生成的 genesis_point 偏移量，
            * 而不是简单地假设 entry point 为 0。
            * 这确保了 PE Header 的 AddressOfEntryPoint 指向正确的函数地址。
            */
            if (opts->has_bin_entry_offset) {
                entry_offset = (uint32_t)opts->bin_entry_offset;
            } else {
                entry_offset = (uint32_t)hdr->genesis_point;
            }
        }
    }

    if (target_format == FORMAT_ROM) {
        rom_entry_offset = 0;
        if (binary_size >= sizeof(AethelBinaryHeader)) {
            const AethelBinaryHeader *hdr = (const AethelBinaryHeader *)binary_data;
            rom_entry_offset = hdr->genesis_point;
        }
    }
    
    if (target_format == FORMAT_AKI) {
        if (opts->verbose) {
            printf("[INFO] 使用模块化后端生成 AKI 内核镜像: %s\n", output_file);
        }
        format_result = aki_generate_image(output_file, code, code_size, 
                                          mirror_data, mirror_size, 
                                          constant_data, constant_size, 0xFFFFF1B779CD1910ULL);
    }
    else if (target_format == FORMAT_LET) {
        if (opts->verbose) {
            printf("[INFO] 生成 LET 逻辑胚胎: %s\n", output_file);
        }
        format_result = let_generate_image(output_file,
                                          code, code_size,
                                          mirror_data, mirror_size,
                                          constant_data, constant_size,
                                          &semantic_agg,
                                          &let_emit_opts);
    }
    else if (target_format == FORMAT_BIN) {
        char let_tmp[512];
        int let_fd;
        if (opts->verbose) {
            printf("[INFO] 生成 BIN: AE -> LET -> BIN\n");
        }
        let_fd = create_temp_file_path(let_tmp, sizeof(let_tmp), "alet");
        if (let_fd < 0) {
            fprintf(stderr, "Error: failed to create temporary LET file\n");
            free(binary_data);
            return 1;
        }
        close(let_fd);

        format_result = let_generate_image(let_tmp,
                                          code, code_size,
                                          mirror_data, mirror_size,
                                          constant_data, constant_size,
                                          &semantic_agg,
                                          &let_emit_opts);
        if (format_result == 0) {
            format_result = let_weave_to_target(let_tmp,
                                                output_file,
                                                LET_WEAVE_TARGET_BIN,
                                                opts->verbose,
                                                &let_weave_opts);
        }
        unlink(let_tmp);
    }
    else if (target_format == FORMAT_ROM) {
        if (opts->verbose) {
            printf("[INFO] 生成 ROM 固件镜像: %s (size=%llu bytes)\n",
                   output_file,
                   (unsigned long long)opts->rom_size_bytes);
        }
        format_result = rom_generate_image(output_file,
                                           code, code_size,
                                           mirror_data, mirror_size,
                                           constant_data, constant_size,
                                           rom_entry_offset,
                                           (uint16_t)opts->machine_bits,
                                           (uint16_t)map_isa_to_contract(opts->isa, opts->machine_bits),
                                           NULL,
                                           opts->rom_size_bytes,
                                           opts->rom_fill_byte);
    }
    else if (target_format == FORMAT_SRV) {
        if (opts->verbose) {
            printf("[INFO] 使用模块化后端生成 SRV 系统服务: %s\n", output_file);
        }
        format_result = srv_generate_image(output_file, code, code_size,
                                          mirror_data, mirror_size,
                                          constant_data, constant_size);
    }
    else if (target_format == FORMAT_HDA) {
        if (opts->verbose) {
            printf("[INFO] 使用模块化后端生成 HDA 硬件驱动: %s\n", output_file);
        }
        format_result = hda_generate_image(output_file, code, code_size,
                                          mirror_data, mirror_size,
                                          constant_data, constant_size);
    }
    else if (target_format == FORMAT_EFI) {
        if (opts->verbose) {
            printf("[INFO] 使用模块化后端生成 PE32+ EFI UEFI 加载器: %s\n", output_file);
        }
        
        /* PE32+ 必须包含完整的AETB逻辑结构，就像AKI一样 
           传入binary_data（完整AETB）而不是单个zone */
        format_result = pe32plus_generate_efi(output_file, binary_data, binary_size);
    }
    else if (target_format == FORMAT_PE) {
        if (opts->verbose) {
            printf("[INFO] 使用工业级后端生成 PE32+ UEFI 应用: %s\n", output_file);
        }
        
        /* 工业级PE生成器从三个独立的Zone生成标准UEFI应用
         * 不包含AETB结构，而是使用标准的.text/.data/.rodata sections
         */
        
        PE_Industrial_Input pe_input = {
            .output_filename = output_file,
            .code_section = code,
            .code_size = code_size,
            .data_section = mirror_data,
            .data_size = mirror_size,
            .rodata_section = constant_data,
            .rodata_size = constant_size,
            .entry_point_offset = entry_offset,
            .image_base = PE_IND_IMAGE_BASE,
            .stack_reserve = 0x10000,
            .stack_commit = 0x1000,
            .heap_reserve = 0x100000,
            .heap_commit = 0x1000
        };
        
        if (opts->verbose) {
            printf("[INFO] Entry point offset: 0x%x (code size: 0x%zx)\n", entry_offset, code_size);
        }
        
        format_result = pe_industrial_generate_efi(&pe_input);
    }
    else if (target_format == FORMAT_EXE) {
        uint8_t *probe_image = NULL;
        uint32_t probe_image_size = 0U;
        EXE_Section_Data *text_section;
        uint16_t exe_subsystem;
        size_t import_index;
        if (opts->verbose) {
            printf("[INFO] 使用 EXE Binary Weaver 生成 Windows 控制台应用: %s\n", output_file);
        }
        
        /* Windows PE32+ Console Executable (独立完整实现)
         * 从三个独立的Zone生成标准Windows EXE格式
         */
        
        EXE_Binary_Weaver_Context weaver_ctx;
        exe_subsystem = exe_code_uses_gui_imports(code, code_size) ?
                        EXE_WIN64_SUBSYSTEM_WINDOWS_GUI :
                        EXE_WIN64_SUBSYSTEM_WINDOWS_CUI;
        if (EXE_Weaver_Initialize(&weaver_ctx, 
                                 EXE_WIN64_IMAGE_BASE_DEFAULT,
                                 exe_subsystem) != 0) {
            fprintf(stderr, "Error: Failed to initialize EXE weaver context\n");
            free(binary_data);
            return 1;
        }

        for (import_index = 0;
             import_index < sizeof(g_exe_import_patch_specs) / sizeof(g_exe_import_patch_specs[0]);
             ++import_index) {
            if (!exe_code_contains_marker(code, code_size, g_exe_import_patch_specs[import_index].marker)) {
                continue;
            }
            if (EXE_Weaver_AddImport(&weaver_ctx,
                                     g_exe_import_patch_specs[import_index].dll_name,
                                     g_exe_import_patch_specs[import_index].function_name,
                                     NULL) != 0) {
                fprintf(stderr, "Error: Failed to register required import '%s!%s' for EXE image\n",
                        g_exe_import_patch_specs[import_index].dll_name,
                        g_exe_import_patch_specs[import_index].function_name);
                EXE_Weaver_Cleanup(&weaver_ctx);
                free(binary_data);
                return 1;
            }
        }
        
        /* Add code section (.text|actflow) */
        if (EXE_Weaver_AddSection(&weaver_ctx,
                                 ".text",
                                 code,
                                 code_size,
                                 EXE_WIN64_SECT_TEXT,
                                 EXE_WIN64_SECTION_ALIGNMENT) != 0) {
            fprintf(stderr, "Error: Failed to add .text section\n");
            free(binary_data);
            return 1;
        }
        
        /* Add data section (.data|state) */
        if (mirror_data && mirror_size > 0) {
            if (EXE_Weaver_AddSection(&weaver_ctx,
                                     ".data",
                                     mirror_data,
                                     mirror_size,
                                     EXE_WIN64_SECT_DATA,
                                     EXE_WIN64_SECTION_ALIGNMENT) != 0) {
                fprintf(stderr, "Error: Failed to add .data section\n");
                free(binary_data);
                return 1;
            }
        }
        
        /* Add constant section (.rdata|mirror) */
        if (constant_data && constant_size > 0) {
            if (EXE_Weaver_AddSection(&weaver_ctx,
                                     ".rdata",
                                     constant_data,
                                     constant_size,
                                     EXE_WIN64_SECT_DATA, /* 修复：设为DATA权限，允许全局变量读写 */
                                     EXE_WIN64_SECTION_ALIGNMENT) != 0) {
                fprintf(stderr, "Error: Failed to add .rdata section\n");
                free(binary_data);
                return 1;
            }
        }
        
        /* Set entry point (from genesis_point in binary header) */
        EXE_Weaver_SetEntryPoint(&weaver_ctx, entry_offset);
        
        /* Set DLL characteristics */
        EXE_Weaver_SetDllCharacteristics(&weaver_ctx,
                                        EXE_WIN64_DLLCHAR_AETHELIUM_DEFAULT);

        if (EXE_Weaver_Finalize(&weaver_ctx, &probe_image, &probe_image_size) != 0) {
            fprintf(stderr, "Error: Failed to finalize EXE layout for portal bootstrap patching\n");
            EXE_Weaver_Cleanup(&weaver_ctx);
            free(binary_data);
            return 1;
        }
        free(probe_image);

        text_section = find_exe_section(&weaver_ctx, ".text");
        if (!text_section ||
           patch_exe_portal_iat_displacements(text_section->raw_data,
                                              text_section->raw_size,
                                              text_section->virtual_address,
                                              &weaver_ctx) != 0) {
            fprintf(stderr, "Error: Failed to patch EXE portal bootstrap imports into .text\n");
            EXE_Weaver_Cleanup(&weaver_ctx);
            free(binary_data);
            return 1;
        }

        /* Write to file */
        if (EXE_Weaver_WriteToFile(&weaver_ctx, output_file) != 0) {
            fprintf(stderr, "Error: Failed to write EXE file\n");
            EXE_Weaver_Cleanup(&weaver_ctx);
            free(binary_data);
            return 1;
        }
        
        EXE_Weaver_Cleanup(&weaver_ctx);
        format_result = 0;
        
        if (opts->verbose) {
            printf("[INFO] EXE 二进制文件生成成功\n");
        }
    }
    else if (target_format == FORMAT_WSYS) {
        if (opts->verbose) {
            printf("[INFO] 使用工业级后端生成 Windows 内核驱动: %s\n", output_file);
        }
        WSYS_Input wsys_input = {
            .output_filename = output_file,
            .code_section = code,
            .code_size = code_size,
            .data_section = mirror_data,
            .data_size = mirror_size,
            .rodata_section = constant_data,
            .rodata_size = constant_size,
            .entry_point_offset = entry_offset,
            .machine_type = (opts->machine_bits == 64 && strcmp(opts->isa, "aarch64") == 0) ? WSYS_MACHINE_ARM64 : WSYS_MACHINE_AMD64
        };
        format_result = wsys_generate_image(&wsys_input);
    }
    else if (target_format == FORMAT_MACHO) {
        if (opts->verbose) {
            printf("[INFO] 生成裸机 Mach-O 固件镜像（Apple Silicon）: %s\n", output_file);
            printf("[INFO] Physical load address: 0x%llx\n", (unsigned long long)opts->macho_phys_base);
        }
        
        /* 完整性检查：ActFlow 必须存在 */
        if (!code || code_size == 0) {
            fprintf(stderr, "Error: ActFlow (code) zone is required for Mach-O generation\n");
            free(binary_data);
            return 1;
        }
        
        format_result = macho_generate_image(output_file,
                                            code, code_size,
                                            mirror_data, mirror_size,
                                            constant_data, constant_size,
                                            opts->macho_phys_base);
        
        if (format_result == 0 && opts->verbose) {
            printf("[INFO] Mach-O firmware image generated successfully\n");
        }
    }
    else if (target_format == FORMAT_IM4P) {
        if (opts->verbose) {
            printf("[INFO] 生成 IM4P Image4 容器（iBoot）: %s\n", output_file);
            printf("[INFO] Payload identifier: '%s'\n", opts->im4p_identifier);
        }
        
        /* IM4P 工作流：
         * 1. 从三个区块生成 Mach-O
         * 2. 将 Mach-O 包装在 ASN.1 DER IM4P 容器中
         */
        
        /* 完整性检查 */
        if (!code || code_size == 0) {
            fprintf(stderr, "Error: ActFlow (code) zone is required for IM4P generation\n");
            free(binary_data);
            return 1;
        }
        
        if (!opts->im4p_identifier || opts->im4p_identifier[0] == '\0') {
            fprintf(stderr, "Error: IM4P identifier not set (use --is im4p <name>)\n");
            free(binary_data);
            return 1;
        }
        
        /* 第 1 步：生成临时 Mach-O */
        char temp_macho[512];
        int temp_fd = create_temp_file_path(temp_macho, sizeof(temp_macho), "amacho");
        if (temp_fd < 0) {
            fprintf(stderr, "Error: Failed to create temporary Mach-O file\n");
            free(binary_data);
            return 1;
        }
        close(temp_fd);
        
        format_result = macho_generate_image(temp_macho,
                                            code, code_size,
                                            mirror_data, mirror_size,
                                            constant_data, constant_size,
                                            opts->macho_phys_base);
        
        if (format_result != 0) {
            fprintf(stderr, "Error: Mach-O generation failed during IM4P workflow\n");
            unlink(temp_macho);
            free(binary_data);
            return 1;
        }
        
        /* 第 2 步：读取临时 Mach-O 文件 */
        FILE *temp_macho_file = fopen(temp_macho, "rb");
        if (!temp_macho_file) {
            fprintf(stderr, "Error: Failed to read temporary Mach-O file\n");
            unlink(temp_macho);
            free(binary_data);
            return 1;
        }
        
        fseek(temp_macho_file, 0, SEEK_END);
        size_t macho_file_size = ftell(temp_macho_file);
        fseek(temp_macho_file, 0, SEEK_SET);
        
        uint8_t *macho_buffer = (uint8_t*)malloc(macho_file_size);
        if (!macho_buffer) {
            fprintf(stderr, "Error: Out of memory reading Mach-O\n");
            fclose(temp_macho_file);
            unlink(temp_macho);
            free(binary_data);
            return 1;
        }
        
        size_t macho_read = fread(macho_buffer, 1, macho_file_size, temp_macho_file);
        fclose(temp_macho_file);
        
        if (macho_read != macho_file_size) {
            fprintf(stderr, "Error: Partial read of Mach-O file (%zu / %zu bytes)\n",
                    macho_read, macho_file_size);
            free(macho_buffer);
            unlink(temp_macho);
            free(binary_data);
            return 1;
        }
        
        /* 第 3 步：生成 IM4P 容器 */
        format_result = im4p_generate(output_file,
                                     macho_buffer, macho_file_size,
                                     opts->im4p_identifier);
        
        free(macho_buffer);
        unlink(temp_macho);
        
        if (format_result == 0 && opts->verbose) {
            printf("[INFO] IM4P container generated successfully\n");
        }
    }
    else {
        /* 格式为AETB - 直接复制到最终文件 */
        if (opts->verbose) {
            printf("[INFO] 直接生成 AETB 格式: %s\n", output_file);
        }
        
        FILE *out = fopen(output_file, "wb");
        if (!out) {
            fprintf(stderr, "Error: Cannot create output file '%s'\n", output_file);
            free(binary_data);
            return 1;
        }
        
        if (fwrite(binary_data, 1, binary_size, out) != binary_size) {
            fprintf(stderr, "Error: Failed to write output file\n");
            fclose(out);
            free(binary_data);
            return 1;
        }
        
        fclose(out);
        format_result = 0;
    }
    
    free(binary_data);
    
    if (format_result != 0) {
        unlink(output_file);
        fprintf(stderr, "Error: Failed to generate output format\n");
        return 1;
    }
    
    if (opts->verbose) {
        printf("✓ 编译成功\n");
        printf("  输出: %s\n", opts->output_file);
    }
    
    return 0;
}

int main(int argc, char **argv) {
    CompilerOptions opts = {0}; // [修改后] 安全地初始化全部字段为0或NULL

    if (parse_args(argc, argv, &opts) != 0) {
        return 1;
    }

    AecfConfig aecf;
    int aecf_loaded = 0;
    
    if (opts.config_file) {
        aecf_config_init(&aecf);
        if (aecf_parse_file(opts.config_file, &aecf) == 0) {
            aecf_loaded = 1;
            /* 智能覆盖层：将 AECF 配置熔接至 CompilerOptions */
            if (aecf.target_format && !opts.emit_format) {
                opts.output_format = aecf.target_format;
                opts.emit_format = aecf.target_format;
            }
            if (aecf.output_file && strcmp(opts.output_file, "a.out") == 0) {
                opts.output_file = aecf.output_file;
            }
            if (aecf.isa && strcmp(opts.isa, "x86") == 0) {
                opts.isa = aecf.isa;
            }
            if (aecf.machine_bits > 0) {
                opts.machine_bits = aecf.machine_bits;
            }
            if (aecf.mode && strcmp(opts.mode, "sandbox") == 0) {
                opts.mode = aecf.mode;
            }
            if (aecf.opt_level >= 0) {
                opts.optimize_level = aecf.opt_level;
            }
            if (aecf.bin_flat) opts.bin_flat = aecf.bin_flat;
            if (aecf.freestanding) opts.freestanding = aecf.freestanding;
            if (aecf.rom_mode) {
                opts.rom_mode = 1;
                opts.output_format = "rom";
                opts.emit_format = "rom";
            }
            
            for (int i = 0; i < aecf.input_count; i++) {
                if (opts.input_count < MAX_INPUT_FILES) {
                    opts.input_files[opts.input_count++] = aecf.input_files[i];
                }
            }
            
            /* AELibrary 自动化装配装甲 */
            if (aecf.use_lib) {
                const char *lib_env = getenv("AELibraryPATH");
                if (!lib_env || lib_env[0] == '\0') {
                    lib_env = getenv("AELibraryPath");
                }
                
                if (!lib_env || lib_env[0] == '\0') {
                    fprintf(stderr, "[AECF] AELibraryPATH not found in env. Initializing Auto-Fetcher...\n");
                    int ret = system("git clone https://github.com/Aethel-Systems/AELibrary.git .aelibrary");
                    if (ret == 0) {
#ifdef _WIN32
                        _putenv("AELibraryPATH=.aelibrary");
#else
                        setenv("AELibraryPATH", ".aelibrary", 1);
#endif
                        fprintf(stderr, "[AECF] Successfully securely cloned AELibrary to Local Context '.aelibrary'\n");
                    } else {
                        fprintf(stderr, "[FATAL] Failed to fetch AELibrary payload. Verify network capability or manually export AELibraryPATH.\n");
                        aecf_config_destroy(&aecf);
                        return 1;
                    }
                }
                
                if (aecf.lib_model && opts.include_lib_count < 32) {
                    append_env_library_path(&opts, aecf.lib_model);
                }
            }
            
            if (opts.verbose) {
                fprintf(stderr, "[AECF] Master Configuration Matrix applied successfully from %s\n", opts.config_file);
            }
        } else {
            fprintf(stderr, "[FATAL] Failure parsing Configuration Manifest: %s\n", opts.config_file);
            aecf_config_destroy(&aecf);
            return 1;
        }
    }

    if (opts.verify_let_mode) {
        int ret = let_verify_contract(opts.verify_let_file, opts.verbose) == 0 ? 0 : 1;
        if (aecf_loaded) aecf_config_destroy(&aecf);
        return ret;
    }

    if (opts.dump_reloc_mode) {
        int ret = let_dump_reloc_dna(opts.dump_reloc_file, opts.dump_reloc_output) == 0 ? 0 : 1;
        if (aecf_loaded) aecf_config_destroy(&aecf);
        return ret;
    }
    
    if (opts.is_iso_mode) {
        if (opts.verbose) {
            printf("[INFO] ISO 生成模式\n");
            printf("[INFO] 内核: %s\n", opts.kernel_file);
            printf("[INFO] EFI 引导: %s\n", opts.efi_boot_file);
            printf("[INFO] 输出: %s\n", opts.output_file);
            printf("[INFO] 大小: %llu MB\n", opts.iso_size_mb);
        }
        
        return aefs_generate_iso_image(opts.kernel_file, opts.efi_boot_file,
                                      opts.output_file, opts.iso_size_mb);
    }
    
    int rc = run_compiler(&opts);
    if (aecf_loaded) {
        aecf_config_destroy(&aecf);
    }
    return rc;
}
