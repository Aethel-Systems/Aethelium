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
 * UEFI PE32+ Industrial Grade Generator
 * 工业级 UEFI PE32+ EFI Application Binary Generator
 * 
 * 严格按照《PE32+ Specification》和《UEFI Specification》规范实现
 * 
 * 与 pe_gen.c 的关键区别 (Current embedded AETB PE vs Industrial PE):
 * 
 * Current (pe_gen.c - 嵌入式AETB):
 *   - PE 作为 AETB 的容器
 *   - .text = 完整 AETB 结构 (包含256B头 + ActFlow + MirrorState + ConstantTruth)
 *   - 用于 UEFI Bootloader 启动 AethelOS 内核
 * 
 * Industrial (pe_industrial.c - 标准工业级):
 *   - PE 作为标准 UEFI Application
 *   - .text = Pure x86-64 machine code (从编译器后端直接提取)
 *   - .data = 初始化数据 (MirrorState zone)
 *   - .rodata = 只读常量数据 (ConstantTruth zone)
 *   - .reloc = 重定位表
 *   - 完整的导入/导出表用于 UEFI Boot Services 调用
 *   - 用于生成独立的 UEFI 应用程序
 */

#ifndef PE_INDUSTRIAL_H
#define PE_INDUSTRIAL_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
   PE32+ 工业级常量定义
   ====================================================================== */

/* DOS Header 固定大小 */
#define PE_IND_DOS_HEADER_SIZE              64

/* PE Signature */
#define PE_IND_SIGNATURE                    0x4550  /* "PE\0\0" */

/* COFF File Header 大小 */
#define PE_IND_COFF_HEADER_SIZE             20

/* PE32+ Optional Header 大小 */
#define PE_IND_OPTIONAL_HEADER_SIZE         240

/* Section Header 大小 */
#define PE_IND_SECTION_HEADER_SIZE          40

/* 标准对齐值 */
#define PE_IND_SECTION_ALIGNMENT            0x1000    /* 4 KB - 内存中对齐 */
#define PE_IND_FILE_ALIGNMENT               0x200     /* 512 B - 文件中对齐 */

/* UEFI 标准加载基址 (for x64) */
#define PE_IND_IMAGE_BASE                   0x0000000140000000ULL

/* 标准数据目录条目数 */
#define PE_IND_DATA_DIRECTORIES             16

/* ======================================================================
   PE32+ Machine 类型
   ====================================================================== */

#define PE_IND_MACHINE_I386                 0x014C
#define PE_IND_MACHINE_R3K_MIPS             0x0162
#define PE_IND_MACHINE_R4K_MIPS             0x0166
#define PE_IND_MACHINE_R10K_MIPS            0x0168
#define PE_IND_MACHINE_WCEMIPSV2            0x0169
#define PE_IND_MACHINE_ALPHA                0x0184
#define PE_IND_MACHINE_SH3                  0x01A2
#define PE_IND_MACHINE_SH3DSP               0x01A3
#define PE_IND_MACHINE_SH3E                 0x01A4
#define PE_IND_MACHINE_SH4                  0x01A6
#define PE_IND_MACHINE_SH5                  0x01A8
#define PE_IND_MACHINE_ARM                  0x01C0
#define PE_IND_MACHINE_THUMB                0x01C2
#define PE_IND_MACHINE_ARMV7                0x01C4
#define PE_IND_MACHINE_ARM64                0xAA64
#define PE_IND_MACHINE_MIPS16               0x0266
#define PE_IND_MACHINE_MIPS_FPU             0x0366
#define PE_IND_MACHINE_MIPS_FPU16           0x0466
#define PE_IND_MACHINE_TRICORE              0x0520
#define PE_IND_MACHINE_CEF                  0x0CEF
#define PE_IND_MACHINE_EBC                  0x0EBC
#define PE_IND_MACHINE_AMD64                0x8664  /* x86-64 */
#define PE_IND_MACHINE_M32R                 0x9041
#define PE_IND_MACHINE_ARM64EC              0xA641
#define PE_IND_MACHINE_RISCV32              0x5032
#define PE_IND_MACHINE_RISCV64              0x5064
#define PE_IND_MACHINE_RISCV128             0x5128
#define PE_IND_MACHINE_LOONGARCH32          0x6232
#define PE_IND_MACHINE_LOONGARCH64          0x6264

/* ======================================================================
   PE32+ Subsystem 类型
   ====================================================================== */

#define PE_IND_SUBSYSTEM_UNKNOWN                    0
#define PE_IND_SUBSYSTEM_NATIVE                     1
#define PE_IND_SUBSYSTEM_WINDOWS_GUI                2
#define PE_IND_SUBSYSTEM_WINDOWS_CUI                3
#define PE_IND_SUBSYSTEM_OS2_CUI                    5
#define PE_IND_SUBSYSTEM_POSIX_CUI                  7
#define PE_IND_SUBSYSTEM_WINDOWS_CE_GUI             9
#define PE_IND_SUBSYSTEM_EFI_APPLICATION            10  /* UEFI Application */
#define PE_IND_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER    11  /* UEFI Boot Services Driver */
#define PE_IND_SUBSYSTEM_EFI_RUNTIME_DRIVER         12  /* UEFI Runtime Driver */
#define PE_IND_SUBSYSTEM_EFI_ROM                    13  /* UEFI ROM Image */
#define PE_IND_SUBSYSTEM_XBOX                       14
#define PE_IND_SUBSYSTEM_WINDOWS_BOOT_APPLICATION   16

/* ======================================================================
   PE32+ Characteristics (COFF File Header 中的Characteristics字段)
   ====================================================================== */

#define PE_IND_CHAR_RELOCS_STRIPPED         0x0001  /* No relocation info */
#define PE_IND_CHAR_EXECUTABLE_IMAGE        0x0002  /* Executable */
#define PE_IND_CHAR_LINE_NUMS_STRIPPED      0x0004  /* COFF line numbers stripped */
#define PE_IND_CHAR_LOCAL_SYMS_STRIPPED     0x0008  /* COFF symbol table stripped */
#define PE_IND_CHAR_AGGR_WS                 0x0010  /* Aggressively trim working set */
#define PE_IND_CHAR_LARGE_ADDRESS_AWARE     0x0020  /* Supports addresses > 2GB */
#define PE_IND_CHAR_RESERVED_0040           0x0040  /* Reserved */
#define PE_IND_CHAR_REVERSE_LO              0x0080  /* Little-endian */
#define PE_IND_CHAR_32BIT_MACHINE           0x0100  /* 32-bit machine */
#define PE_IND_CHAR_DEBUG_STRIPPED          0x0200  /* Debug info stripped */
#define PE_IND_CHAR_REMOVABLE_RUN_FROM_SWAP 0x0400  /* Removable run from swap */
#define PE_IND_CHAR_NET_RUN_FROM_SWAP       0x0800  /* Network run from swap */
#define PE_IND_CHAR_SYSTEM                  0x1000  /* System file */
#define PE_IND_CHAR_DLL                     0x2000  /* Dynamic library */
#define PE_IND_CHAR_UP_SYSTEM_ONLY          0x4000  /* Uniprocessor only */
#define PE_IND_CHAR_REVERSE_HI              0x8000  /* Big-endian */

/* ======================================================================
   PE32+ Optional Header DLL Characteristics
   ====================================================================== */

#define PE_IND_DLL_CHAR_HIGH_ENTROPY_VA              0x0020  /* High entropy VA */
#define PE_IND_DLL_CHAR_DYNAMIC_BASE                 0x0040  /* Dynamic base */
#define PE_IND_DLL_CHAR_FORCE_INTEGRITY              0x0080  /* Force integrity checks */
#define PE_IND_DLL_CHAR_NX_COMPAT                    0x0100  /* NX compatible */
#define PE_IND_DLL_CHAR_NO_ISOLATION                 0x0200  /* No isolation */
#define PE_IND_DLL_CHAR_NO_SEH                       0x0400  /* No SEH */
#define PE_IND_DLL_CHAR_NO_BIND                      0x0800  /* Do not bind */
#define PE_IND_DLL_CHAR_APPCONTAINER                 0x1000  /* AppContainer */
#define PE_IND_DLL_CHAR_WDM_DRIVER                   0x2000  /* WDM driver */
#define PE_IND_DLL_CHAR_GUARD_CF                     0x4000  /* Control Flow Guard */
#define PE_IND_DLL_CHAR_TERMINAL_SERVER_AWARE        0x8000  /* Terminal Server Aware */

/* ======================================================================
   PE32+ Section Characteristics
   ====================================================================== */

#define PE_IND_SCN_TYPE_NO_PAD               0x00000008  /* Cannot be padded */
#define PE_IND_SCN_CNT_CODE                  0x00000020  /* Code */
#define PE_IND_SCN_CNT_INITIALIZED_DATA      0x00000040  /* Initialized data */
#define PE_IND_SCN_CNT_UNINITIALIZED_DATA    0x00000080  /* Uninitialized data */
#define PE_IND_SCN_LNK_OTHER                 0x00000100  /* Reserved */
#define PE_IND_SCN_LNK_INFO                  0x00000200  /* Comments or .drective */
#define PE_IND_SCN_LNK_REMOVE                0x00000800  /* Will not become part of image */
#define PE_IND_SCN_LNK_COMDAT                0x00001000  /* COMDAT */
#define PE_IND_SCN_NO_DEFER_SPEC_EXC         0x00004000  /* Speculative exception handling unsafe */
#define PE_IND_SCN_GPREL                     0x00008000  /* GP relative reference */
#define PE_IND_SCN_MEM_FARDATA               0x00008000  /* Reserved */
#define PE_IND_SCN_MEM_PURGEABLE             0x00020000  /* Reserved */
#define PE_IND_SCN_MEM_16BIT                 0x00020000  /* Reserved */
#define PE_IND_SCN_MEM_LOCKED                0x00040000  /* Reserved */
#define PE_IND_SCN_MEM_PRELOAD               0x00080000  /* Reserved */
#define PE_IND_SCN_ALIGN_1BYTES              0x00100000  /* 1-byte alignment */
#define PE_IND_SCN_ALIGN_2BYTES              0x00200000  /* 2-byte alignment */
#define PE_IND_SCN_ALIGN_4BYTES              0x00300000  /* 4-byte alignment */
#define PE_IND_SCN_ALIGN_8BYTES              0x00400000  /* 8-byte alignment */
#define PE_IND_SCN_ALIGN_16BYTES             0x00500000  /* 16-byte alignment */
#define PE_IND_SCN_ALIGN_32BYTES             0x00600000  /* 32-byte alignment */
#define PE_IND_SCN_ALIGN_64BYTES             0x00700000  /* 64-byte alignment */
#define PE_IND_SCN_ALIGN_128BYTES            0x00800000  /* 128-byte alignment */
#define PE_IND_SCN_ALIGN_256BYTES            0x00900000  /* 256-byte alignment */
#define PE_IND_SCN_ALIGN_512BYTES            0x00A00000  /* 512-byte alignment */
#define PE_IND_SCN_ALIGN_1024BYTES           0x00B00000  /* 1024-byte alignment */
#define PE_IND_SCN_ALIGN_2048BYTES           0x00C00000  /* 2048-byte alignment */
#define PE_IND_SCN_ALIGN_4096BYTES           0x00D00000  /* 4096-byte alignment */
#define PE_IND_SCN_ALIGN_8192BYTES           0x00E00000  /* 8192-byte alignment */
#define PE_IND_SCN_ALIGN_MASK                0x00F00000  /* Alignment mask */
#define PE_IND_SCN_LNK_NRELOC_OVFL           0x01000000  /* Relocation overflow */
#define PE_IND_SCN_MEM_DISCARDABLE           0x02000000  /* Can be discarded */
#define PE_IND_SCN_MEM_NOT_CACHED            0x04000000  /* Not cacheable */
#define PE_IND_SCN_MEM_NOT_PAGED             0x08000000  /* Not pageable */
#define PE_IND_SCN_MEM_SHARED                0x10000000  /* Shared in memory */
#define PE_IND_SCN_MEM_EXECUTE               0x20000000  /* Executable */
#define PE_IND_SCN_MEM_READ                  0x40000000  /* Readable */
#define PE_IND_SCN_MEM_WRITE                 0x80000000  /* Writable */

/* ======================================================================
   PE32+ 数据目录索引
   ====================================================================== */

#define PE_IND_DIR_EXPORT_TABLE             0
#define PE_IND_DIR_IMPORT_TABLE             1
#define PE_IND_DIR_RESOURCE_TABLE           2
#define PE_IND_DIR_EXCEPTION_TABLE          3
#define PE_IND_DIR_CERTIFICATE_TABLE        4
#define PE_IND_DIR_BASE_RELOCATION_TABLE    5
#define PE_IND_DIR_DEBUG                    6
#define PE_IND_DIR_ARCHITECTURE             7
#define PE_IND_DIR_GLOBAL_PTR               8
#define PE_IND_DIR_TLS_TABLE                9
#define PE_IND_DIR_LOAD_CONFIG_TABLE        10
#define PE_IND_DIR_BOUND_IMPORT             11
#define PE_IND_DIR_IAT                      12
#define PE_IND_DIR_DELAY_IMPORT_DESCRIPTOR  13
#define PE_IND_DIR_COM_PLUS_RUNTIME         14
#define PE_IND_DIR_RESERVED                 15

/* ======================================================================
   Relocation Type 定义
   ====================================================================== */

#define PE_IND_REL_BASED_ABSOLUTE           0   /* Absolute reference, no relocation */
#define PE_IND_REL_BASED_HIGH               1   /* High 16 bits */
#define PE_IND_REL_BASED_LOW                2   /* Low 16 bits */
#define PE_IND_REL_BASED_HIGHLOW            3   /* High/Low 32 bits */
#define PE_IND_REL_BASED_HIGHADJ            4   /* High+1/Low 32 bits */
#define PE_IND_REL_BASED_MIPS_JMPADDR       5   /* MIPS jmp addr */
#define PE_IND_REL_BASED_ARM_MOV32          5   /* ARM MOVW/MOVT */
#define PE_IND_REL_BASED_RISCV_HI20         5   /* RISC-V HI20 */
#define PE_IND_REL_BASED_SECTION            6   /* Image relative */
#define PE_IND_REL_BASED_REL32              7   /* 32-bit relative */
#define PE_IND_REL_BASED_MIPS_JMPADDR16     9   /* MIPS16 jmp addr */
#define PE_IND_REL_BASED_IA64_IMM64         9   /* IA-64 immediate 64-bit */
#define PE_IND_REL_BASED_DIR64              10  /* 64-bit relocation */
#define PE_IND_REL_BASED_HIGH3ADJ           11  /* High 3+1/Low */

/* ======================================================================
   导入表结构定义
   ====================================================================== */

/* Import Directory Entry */
typedef struct {
    uint32_t ImportLookupTableRVA;  /* RVA of ILT */
    uint32_t TimeDateStamp;          /* 0 for loaded module, -1 for bound */
    uint32_t ForwarderChain;         /* -1 if no forworders */
    uint32_t NameRVA;                /* RVA of DLL name string */
    uint32_t ImportAddressTableRVA;  /* RVA of IAT */
} PE_IDT_Entry;

/* 导入符号条目（64位） */
typedef struct {
    uint64_t imported_address;
    uint32_t rva_in_section;
    uint8_t  symbol_type;  /* 0=procedure, 1=data */
    uint8_t  reserved[3];
} PE_IND_ImportSymbol;

/* ======================================================================
   PE32+ 结构体定义 - 完整工业级
   ====================================================================== */

/* DOS Header (64 bytes) */
typedef struct PE_IDH {
    uint16_t e_magic;               /* 0x00: MZ */
    uint16_t e_cblp;                /* 0x02 */
    uint16_t e_cp;                  /* 0x04 */
    uint16_t e_crlc;                /* 0x06 */
    uint16_t e_cp_hdr;              /* 0x08 */
    uint16_t e_minalloc;            /* 0x0A */
    uint16_t e_maxalloc;            /* 0x0C */
    uint16_t e_ss;                  /* 0x0E */
    uint16_t e_sp;                  /* 0x10 */
    uint16_t e_csum;                /* 0x12 */
    uint16_t e_ip;                  /* 0x14 */
    uint16_t e_cs;                  /* 0x16 */
    uint16_t e_lfarlc;              /* 0x18 */
    uint16_t e_ovno;                /* 0x1A */
    uint16_t e_res[4];              /* 0x1C-0x23 */
    uint16_t e_oemid;               /* 0x24 */
    uint16_t e_oeminfo;             /* 0x26 */
    uint16_t e_res2[10];            /* 0x28-0x3B */
    uint32_t e_lfanew;              /* 0x3C: Offset to PE header */
} PE_IDH;

/* COFF File Header (20 bytes) */
typedef struct PE_IF {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} PE_IF;

/* PE32+ Optional Header (240 bytes) */
typedef struct PE_IOP {
    uint16_t Magic;                         /* 0x020B for PE32+ */
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    /* PE32+ specific */
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    /* DataDirectory[16] */
    struct {
        uint32_t VirtualAddress;
        uint32_t Size;
    } DataDirectories[PE_IND_DATA_DIRECTORIES];
} PE_IOP;

/* Section Header (40 bytes) */
typedef struct PE_ISH {
    uint8_t  Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} PE_ISH;

/* Base Relocation Block Header (8 bytes) */
typedef struct PE_BRB {
    uint32_t VirtualAddress;
    uint32_t SizeOfBlock;
} PE_BRB;

/* Relocation Entry (2 bytes) */
typedef struct PE_RE {
    uint16_t TypeOffset;  /* Type (4 bits) | Offset (12 bits) */
} PE_RE;

/* ======================================================================
   工业级PE生成输入结构体
   ====================================================================== */

/* PE 工业级生成输入 */
typedef struct {
    const char *output_filename;      /* 输出 EFI 文件名 */
    const uint8_t *code_section;      /* .text 段数据（x86-64机器码） */
    size_t code_size;                 /* .text 段大小 */
    const uint8_t *data_section;      /* .data 段数据（初始化数据） */
    size_t data_size;                 /* .data 段大小 */
    const uint8_t *rodata_section;    /* .rodata 段数据（只读常量） */
    size_t rodata_size;               /* .rodata 段大小 */
    uint32_t entry_point_offset;      /* 入口点相对于 .text 开始的偏移 */
    uint64_t image_base;              /* ImageBase (通常 0x140000000) */
    uint32_t stack_reserve;           /* 栈保留大小 */
    uint32_t stack_commit;            /* 栈提交大小 */
    uint32_t heap_reserve;            /* 堆保留大小 */
    uint32_t heap_commit;             /* 堆提交大小 */
} PE_Industrial_Input;

/* ======================================================================
   PE32+ 校验和计算
   ====================================================================== */

/**
 * 计算 PE32+ CheckSum 字段值
 * 
 * PE CheckSum 是使用 Carrillo-Sahni 算法计算的 32-bit 和
 * CheckSum 字段本身（位于 Optional Header 的 0x40 偏移）需要被跳过
 * 
 * 公式: CheckSum = (ImageSize + SumOfAllWordsExceptChecksum) % 2^32
 *   + (SumOverflow << 16)
 * 
 * @param file_data      PE文件完整数据
 * @param file_size      PE文件总大小
 * @param checksum_offset    CheckSum 字段的文件偏移
 * @return              计算得到的 CheckSum 值
 */
uint32_t pe_industrial_calculate_checksum(const uint8_t *file_data,
                                          size_t file_size,
                                          uint32_t checksum_offset);

/* ======================================================================
   PE32+ 工业级生成函数
   ====================================================================== */

/**
 * 生成符合 UEFI 规范的 PE32+ EFI Application
 * 
 * 完整的 PE32+ 结构：
 * [64B DOS Header]
 * + [4B PE Signature]
 * + [20B COFF Header]
 * + [240B Optional Header]
 * + [40B .text Section Header]
 * + [40B .data Section Header]
 * + [40B .rodata Section Header]
 * + [40B .reloc Section Header]
 * + [aligned .text section]
 * + [aligned .data section]
 * + [aligned .rodata section]
 * + [aligned .reloc section]
 * 
 * 关键特性：
 * - 标准 UEFI Application Subsystem (10)
 * - 64-bit ImageBase (0x140000000)
 * - 完整的 Import/Export tables（当需要调用UEFI Boot Services时）
 * - Base Relocation 表用于 UEFI 加载器的镜像重定位
 * - 标准 PE32+ CheckSum 计算
 * 
 * @param input       PE 生成输入参数结构体
 * @return           0 成功，-1 失败
 */
int pe_industrial_generate_efi(const PE_Industrial_Input *input);

/**
 * 从编译器后端的三个 Zone 生成 PE 应用
 * 
 * 便捷函数，直接从编译器输出的三个 Zone 生成 PE
 * 
 * @param output_file      输出文件名
 * @param code             ActFlow Zone (机器码)
 * @param code_size        ActFlow 大小
 * @param data             MirrorState Zone (初始化数据)
 * @param data_size        MirrorState 大小
 * @param rodata           ConstantTruth Zone (常量)
 * @param rodata_size      ConstantTruth 大小
 * @param entry_offset     入口点偏移（相对于 code 开始）
 * @return                 0 成功，-1 失败
 */
int pe_industrial_generate_from_zones(const char *output_file,
                                      const uint8_t *code, size_t code_size,
                                      const uint8_t *data, size_t data_size,
                                      const uint8_t *rodata, size_t rodata_size,
                                      uint32_t entry_offset);

#ifdef __cplusplus
}
#endif

#endif /* PE_INDUSTRIAL_H */
