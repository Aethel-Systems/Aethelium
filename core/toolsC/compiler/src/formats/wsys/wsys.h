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

#ifndef AETHELC_COMPILER_FORMATS_WSYS_H
#define AETHELC_COMPILER_FORMATS_WSYS_H

#include <stdint.h>
#include <stddef.h>

#define WSYS_MAGIC 0x020B
#define WSYS_SUBSYSTEM_NATIVE 1
#define WSYS_SECTION_ALIGNMENT 0x1000
#define WSYS_FILE_ALIGNMENT 0x200
#define WSYS_IMAGE_BASE_DEFAULT 0x0000000140000000ULL

/* Microsoft x64 DriverEntry Shadow Space Requirement */
#define WSYS_DRIVER_ENTRY_SHADOW_SPACE 32

/* Section Protections: Strictly W^X */
#define WSYS_SCN_TEXT   0x60000020 /* CODE | EXECUTE | READ */
#define WSYS_SCN_DATA   0xC0000040 /* INITIALIZED_DATA | READ | WRITE */
#define WSYS_SCN_RDATA  0x40000040 /* INITIALIZED_DATA | READ */
#define WSYS_SCN_RELOC  0x42000040 /* INITIALIZED_DATA | READ | DISCARDABLE */

/* PE Data Directory Indexes */
#define WSYS_DIR_IMPORT 1
#define WSYS_DIR_RELOC  5
#define WSYS_DIR_IAT    12

/* Machine Types */
#define WSYS_MACHINE_AMD64 0x8664
#define WSYS_MACHINE_ARM64 0xAA64

typedef struct {
    uint16_t e_magic;
    uint8_t  e_padding[58];
    uint32_t e_lfanew;
} WSYS_DOS_Header;

typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} WSYS_COFF_Header;

typedef struct {
    uint16_t Magic;
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
    struct {
        uint32_t VirtualAddress;
        uint32_t Size;
    } DataDirectories[16];
} WSYS_Optional_Header;

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
} WSYS_Section_Header;

typedef struct {
    const char *output_filename;
    const uint8_t *code_section;
    size_t code_size;
    const uint8_t *data_section;
    size_t data_size;
    const uint8_t *rodata_section;
    size_t rodata_size;
    uint32_t entry_point_offset;
    uint16_t machine_type;          /* 新增：x86-64 (0x8664) 或 ARM64 (0xAA64) 动态传入 */
} WSYS_Input;

int wsys_generate_image(const WSYS_Input *input);

#endif /* AETHELC_COMPILER_FORMATS_WSYS_H */