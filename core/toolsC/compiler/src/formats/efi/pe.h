/*
 * PE32+ (Portable Executable) Format Header
 * 工业级 UEFI EFI Application 生成模块
 * 
 * 严格按照《AethelOS 二进制及目录结构.txt》规范
 * PE32+ EFI 加载器启动格式的规范实现
 */

#ifndef PE_FORMAT_H
#define PE_FORMAT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
   PE32+ 文件结构常量
   ====================================================================== */

/* DOS Header 大小 */
#define PE_DOS_HEADER_SIZE       64

/* PE Signature ("PE\0\0") */
#define PE_SIGNATURE             0x4550

/* COFF File Header 大小 */
#define PE_COFF_HEADER_SIZE      20

/* PE32+ Optional Header 大小 */
#define PE_OPTIONAL_HEADER_SIZE  240

/* 标准对齐值 */
#define PE_SECTION_ALIGNMENT     0x1000    /* 内存中对齐 */
#define PE_FILE_ALIGNMENT        0x200     /* 文件中对齐 */

/* 标准 UEFI 加载基址 */
#define PE_UEFI_IMAGE_BASE       0x0000000140000000ULL

/* 数据目录条目数 */
#define PE_DATA_DIRECTORIES_COUNT  16

/* ======================================================================
   PE32+ 枚举和常量
   ====================================================================== */

/* Machine 类型 */
#define PE_MACHINE_X86_64        0x8664

/* Subsystem 类型 */
#define PE_SUBSYSTEM_EFI_APPLICATION  10

/* Characteristics 标志 */
#define PE_CHARACTERISTICS_EXECUTABLE  0x0002
#define PE_CHARACTERISTICS_LARGE_ADDRESS_AWARE  0x0020

/* DLL Characteristics */
#define PE_DLL_CHARACTERISTICS_DYNAMIC_BASE      0x0040
#define PE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA   0x0020

/* Section Characteristics */
#define PE_SECTION_CNT_CODE          0x00000020
#define PE_SECTION_CNT_INITIALIZED   0x00000040
#define PE_SECTION_MEM_EXECUTE       0x20000000
#define PE_SECTION_MEM_READ          0x40000000
#define PE_SECTION_MEM_WRITE         0x80000000

/* ======================================================================
   PE32+ 结构体定义
   ====================================================================== */

/* DOS Header (64 bytes) */
typedef struct {
    uint16_t e_magic;           /* 0x00: MZ signature */
    uint16_t e_cblp;            /* 0x02 */
    uint16_t e_cp;              /* 0x04 */
    uint16_t e_crlc;            /* 0x06 */
    uint16_t e_cp_hdr;          /* 0x08 */
    uint16_t e_minalloc;        /* 0x0A */
    uint16_t e_maxalloc;        /* 0x0C */
    uint16_t e_ss;              /* 0x0E */
    uint16_t e_sp;              /* 0x10 */
    uint16_t e_csum;            /* 0x12 */
    uint16_t e_ip;              /* 0x14 */
    uint16_t e_cs;              /* 0x16 */
    uint16_t e_lfarlc;          /* 0x18 */
    uint16_t e_ovno;            /* 0x1A */
    uint16_t e_res[4];          /* 0x1C-0x23 */
    uint16_t e_oemid;           /* 0x24 */
    uint16_t e_oeminfo;         /* 0x26 */
    uint16_t e_res2[10];        /* 0x28-0x3B */
    uint32_t e_lfanew;          /* 0x3C: PE header offset */
} DOS_Header;

/* COFF File Header (20 bytes) */
typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} COFF_Header;

/* PE32+ Optional Header (240 bytes) */
typedef struct {
    uint16_t Magic;                      /* 0x020B for PE32+ */
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
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
} PE32Plus_OptionalHeader;

/* Section Header (40 bytes) */
typedef struct {
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
} Section_Header;

/* Base Relocation Block Header (8 bytes) */
typedef struct {
    uint32_t VirtualAddress;
    uint32_t SizeOfBlock;
} BaseRelocation_Block;

/* ======================================================================
   PE32+ 生成结构体 - 汇编层的输入
   ====================================================================== */

/* PE 编织结构输入 - 供汇编层构造PE完整结构 */
typedef struct {
    const char *output_filename;     /* 输出文件名 */
    const uint8_t *code_buffer;      /* .text 段数据 */
    size_t code_size;                /* .text 段大小 */
    uint64_t image_base;             /* PE ImageBase (通常 0x140000000) */
    uint32_t entry_point;            /* 入口点 RVA */
} PE32Plus_Weave_Input;

/* ======================================================================
   PE32+ 生成函数接口
   ====================================================================== */

/**
 * 计算 PE32+ 校验和 (CheckSum 字段)
 * 
 * PE CheckSum 是对整个文件的一个特殊计算，用于验证文件完整性
 * 计算时跳过 CheckSum 字段本身 (位于 Optional Header 的 0x40 处)
 * 
 * @param file_data    PE文件数据指针
 * @param file_size    PE文件总大小
 * @param checksum_offset  PE文件中 CheckSum 字段的偏移
 * @return             计算得到的校验和
 */
uint32_t pe32plus_calculate_checksum(const uint8_t *file_data, 
                                     size_t file_size,
                                     uint32_t checksum_offset);

/**
 * 完整的 PE32+ EFI 镜像生成 - 工业级实现
 * 
 * 从编译后的二进制代码生成 UEFI 兼容的 PE32+ EFI 可执行文件
 * 
 * 完整生成流程：
 * 1. 从输入二进制提取代码段
 * 2. 构造 DOS Header + PE Signature + COFF Header + Optional Header
 * 3. 创建 .text 和 .reloc 两个标准节
 * 4. 计算所有偏移、大小、RVA 值
 * 5. 构建 Base Relocation Table
 * 6. 计算全文件校验和
 * 7. 写入文件
 * 
 * @param output_file      输出文件路径 (e.g. "BOOTX64.EFI")
 * @param code_buffer      编译后的代码数据指针
 * @param code_size        代码数据大小
 * @return                 0 成功，-1 失败
 */
int pe32plus_generate_efi(const char *output_file,
                         const uint8_t *code_buffer,
                         size_t code_size);

/**
 * PE32+ 汇编层完整结构编织函数 (供汇编调用)
 * 
 * 汇编层负责：
 * - 完整的 PE Header 构造（DOS + PE + COFF + Optional Header）
 * - Section Headers 布局
 * - 对齐计算
 * - 校验和计算
 * - 文件 I/O
 * 
 * @param input    PE编织输入结构
 * @return         0 成功，-1 失败
 */
int weave_pe32plus_structure(const PE32Plus_Weave_Input *input);

#ifdef __cplusplus
}
#endif

#endif /* PE_FORMAT_H */
