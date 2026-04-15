/*
 * =========================================================================
 * Aethelium EXE Binary Weaver - Industrial Grade Implementation
 * =========================================================================
 *
 * Titan Arum / Windows Ultra-Deep Spec aligned PE32+ weaver.
 * The image is built fully in memory, emits deterministic headers, injects
 * Aethelium Quick Launch metadata, and reserves the structural surfaces
 * required by a native NT execution path.
 * =========================================================================
 */

#ifndef AETHELIUM_EXE_BINARY_WEAVER_H
#define AETHELIUM_EXE_BINARY_WEAVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EXE_WIN64_DOS_HEADER_SIZE                64U
#define EXE_WIN64_SIGNATURE_SIZE                 4U
#define EXE_WIN64_COFF_HEADER_SIZE               20U
#define EXE_WIN64_OPT_HEADER_SIZE                240U
#define EXE_WIN64_SECTION_HEADER_SIZE            40U
#define EXE_WIN64_DATA_DIRECTORIES_COUNT         16U
#define EXE_WIN64_MAX_SECTIONS                   8U
#define EXE_WIN64_MAX_IMPORT_LIBRARIES           8U
#define EXE_WIN64_MAX_IMPORTS_PER_LIBRARY        32U
#define EXE_WIN64_MAX_UNWIND_RECORDS             64U
#define EXE_WIN64_MAX_RELOCATIONS                256U

#define EXE_WIN64_SECTION_ALIGNMENT              0x1000U
#define EXE_WIN64_FILE_ALIGNMENT                 0x200U
#define EXE_WIN64_IMAGE_BASE_DEFAULT             0x0000000140000000ULL
#define EXE_WIN64_IMAGE_MAX_SIZE                 0x100000000ULL
#define EXE_WIN64_STACK_RESERVE                  0x00200000ULL
#define EXE_WIN64_STACK_COMMIT                   0x00001000ULL
#define EXE_WIN64_HEAP_RESERVE                   0x00100000ULL
#define EXE_WIN64_HEAP_COMMIT                    0x00001000ULL

#define EXE_WIN64_MACHINE_X64                    0x8664U
#define EXE_WIN64_SUBSYSTEM_WINDOWS_GUI          2U
#define EXE_WIN64_SUBSYSTEM_WINDOWS_CUI          3U

#define EXE_WIN64_CHAR_EXECUTABLE_IMAGE          0x0002U
#define EXE_WIN64_CHAR_LARGE_ADDRESS_AWARE       0x0020U
#define EXE_WIN64_CHAR_CONSOLE_DEFAULT           (EXE_WIN64_CHAR_EXECUTABLE_IMAGE | EXE_WIN64_CHAR_LARGE_ADDRESS_AWARE)
#define EXE_WIN64_CHAR_GUI_DEFAULT               (EXE_WIN64_CHAR_EXECUTABLE_IMAGE | EXE_WIN64_CHAR_LARGE_ADDRESS_AWARE)

#define EXE_WIN64_DLLCHAR_DYNAMIC_BASE           0x0040U
#define EXE_WIN64_DLLCHAR_NX_COMPAT              0x0100U
#define EXE_WIN64_DLLCHAR_NO_SEH                 0x0400U
#define EXE_WIN64_DLLCHAR_AETHELIUM_DEFAULT      (EXE_WIN64_DLLCHAR_DYNAMIC_BASE | EXE_WIN64_DLLCHAR_NX_COMPAT | EXE_WIN64_DLLCHAR_NO_SEH)

#define EXE_WIN64_SECT_TEXT                      0x60000020U
#define EXE_WIN64_SECT_RDATA                     0x40000040U
#define EXE_WIN64_SECT_DATA                      0xC0000040U
#define EXE_WIN64_SECT_PDATA                     0x40000040U
#define EXE_WIN64_SECT_IDATA                     0xC0000040U
#define EXE_WIN64_SECT_RELOC                     0x42000040U

#define EXE_WIN64_MAGIC_PE32PLUS                 0x020BU

#define EXE_WIN64_DATADIR_EXPORT                 0U
#define EXE_WIN64_DATADIR_IMPORT                 1U
#define EXE_WIN64_DATADIR_RESOURCE               2U
#define EXE_WIN64_DATADIR_EXCEPTION              3U
#define EXE_WIN64_DATADIR_CERTIFICATE            4U
#define EXE_WIN64_DATADIR_BASERELOC              5U
#define EXE_WIN64_DATADIR_DEBUG                  6U
#define EXE_WIN64_DATADIR_ARCHITECTURE           7U
#define EXE_WIN64_DATADIR_GLOBALPTR              8U
#define EXE_WIN64_DATADIR_TLS                    9U
#define EXE_WIN64_DATADIR_LOAD_CONFIG            10U
#define EXE_WIN64_DATADIR_BOUND_IMPORT           11U
#define EXE_WIN64_DATADIR_IAT                    12U
#define EXE_WIN64_DATADIR_DELAY_IMPORT           13U
#define EXE_WIN64_DATADIR_COM_PLUS_RUNTIME       14U
#define EXE_WIN64_DATADIR_RESERVED               15U

#define EXE_IMPORT_NTDLL                         "ntdll.dll"
#define EXE_IMPORT_USER32                        "user32.dll"

typedef struct {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;
} EXE_DOS_Header;

typedef struct {
    uint32_t signature;
    uint32_t stack_size;
    uint32_t entry_point_crc;
    uint32_t metadata_version;
    char section_alias_text[16];
    char section_alias_rdata[16];
    char section_alias_data[16];
    char section_alias_pdata[16];
} Aethelium_Quick_Launch_Descriptor;

typedef struct {
    uint16_t machine;
    uint16_t number_of_sections;
    uint32_t time_date_stamp;
    uint32_t pointer_to_symbol_table;
    uint32_t number_of_symbols;
    uint16_t size_of_optional_header;
    uint16_t characteristics;
} EXE_COFF_Header;

typedef struct {
    uint32_t virtual_address;
    uint32_t size;
} EXE_Data_Dir_Entry;

typedef struct {
    uint16_t magic;
    uint8_t major_linker_version;
    uint8_t minor_linker_version;
    uint32_t size_of_code;
    uint32_t size_of_initialized_data;
    uint32_t size_of_uninitialized_data;
    uint32_t address_of_entry_point;
    uint32_t base_of_code;
    uint64_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint16_t major_operating_system_version;
    uint16_t minor_operating_system_version;
    uint16_t major_image_version;
    uint16_t minor_image_version;
    uint16_t major_subsystem_version;
    uint16_t minor_subsystem_version;
    uint32_t win32_version_value;
    uint32_t size_of_image;
    uint32_t size_of_headers;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint64_t size_of_stack_reserve;
    uint64_t size_of_stack_commit;
    uint64_t size_of_heap_reserve;
    uint64_t size_of_heap_commit;
    uint32_t loader_flags;
    uint32_t number_of_rva_and_sizes;
    EXE_Data_Dir_Entry data_directories[EXE_WIN64_DATA_DIRECTORIES_COUNT];
} EXE_Optional_Header;

typedef struct {
    uint8_t name[8];
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data;
    uint32_t pointer_to_relocations;
    uint32_t pointer_to_linenumbers;
    uint16_t number_of_relocations;
    uint16_t number_of_linenumbers;
    uint32_t characteristics;
} EXE_Section_Header;

typedef struct {
    EXE_DOS_Header dos_header;
    Aethelium_Quick_Launch_Descriptor quick_launch;
    uint32_t pe_signature;
    EXE_COFF_Header coff_header;
    EXE_Optional_Header optional_header;
    EXE_Section_Header section_headers[EXE_WIN64_MAX_SECTIONS];
} EXE_PE_Headers;

typedef struct {
    char name[9];
    uint8_t *raw_data;
    uint32_t raw_size;
    uint32_t virtual_size;
    uint32_t characteristics;
    uint32_t alignment;
    uint32_t virtual_address;
    uint32_t pointer_to_raw_data;
    int synthetic;
} EXE_Section_Data;

typedef struct {
    char function_name[64];
    uint32_t hint_name_rva;
    uint32_t iat_rva;
} EXE_Import_Function;

typedef struct {
    char dll_name[64];
    EXE_Import_Function functions[EXE_WIN64_MAX_IMPORTS_PER_LIBRARY];
    uint32_t function_count;
    uint32_t descriptor_rva;
} EXE_Import_Library;

typedef struct {
    uint32_t function_rva_start;
    uint32_t function_rva_end;
    uint32_t unwind_info_rva;
    uint8_t unwind_info[64];
    uint32_t unwind_info_size;
} EXE_Unwind_Record;

typedef struct {
    uint32_t rva;
    uint16_t type;
} EXE_Base_Relocation;

typedef struct {
    EXE_PE_Headers headers;
    EXE_Section_Data sections[EXE_WIN64_MAX_SECTIONS];
    uint32_t section_count;
    uint32_t image_size;
    uint32_t code_size;
    uint32_t data_size;
    uint64_t image_base;
    uint32_t entry_point_rva;
    uint32_t header_size;

    EXE_Import_Library imports[EXE_WIN64_MAX_IMPORT_LIBRARIES];
    uint32_t import_library_count;
    uint32_t import_table_rva;
    uint32_t import_table_size;
    uint32_t iat_rva;
    uint32_t iat_size;

    EXE_Unwind_Record unwind_records[EXE_WIN64_MAX_UNWIND_RECORDS];
    uint32_t unwind_record_count;
    uint32_t exception_table_rva;
    uint32_t exception_table_size;

    EXE_Base_Relocation relocations[EXE_WIN64_MAX_RELOCATIONS];
    uint32_t relocation_count;
    uint32_t relocation_table_rva;
    uint32_t relocation_table_size;

    int deterministic_build;
    uint16_t subsystem;
    uint16_t dll_characteristics;
} EXE_Binary_Weaver_Context;

int EXE_Weaver_Initialize(EXE_Binary_Weaver_Context *ctx,
                          uint64_t image_base,
                          uint16_t subsystem);
int EXE_Weaver_AddSection(EXE_Binary_Weaver_Context *ctx,
                          const char *name,
                          const uint8_t *data,
                          uint32_t data_size,
                          uint32_t characteristics,
                          uint32_t alignment);
void EXE_Weaver_SetEntryPoint(EXE_Binary_Weaver_Context *ctx,
                              uint32_t entry_point_rva);
void EXE_Weaver_SetDllCharacteristics(EXE_Binary_Weaver_Context *ctx,
                                      uint16_t characteristics);
int EXE_Weaver_Finalize(EXE_Binary_Weaver_Context *ctx,
                        uint8_t **out_image,
                        uint32_t *out_size);
int EXE_Weaver_WriteToFile(EXE_Binary_Weaver_Context *ctx,
                           const char *output_path);
int EXE_Weaver_AddImport(EXE_Binary_Weaver_Context *ctx,
                         const char *dll_name,
                         const char *function_name,
                         uint32_t *out_iat_rva);
int EXE_Weaver_AddExceptionInfo(EXE_Binary_Weaver_Context *ctx,
                                const uint8_t *unwind_info,
                                uint32_t unwind_info_size,
                                uint32_t rva,
                                uint32_t size);
void EXE_Weaver_Cleanup(EXE_Binary_Weaver_Context *ctx);

void EXE_BuildDosHeader(EXE_DOS_Header *dos_header);
void EXE_BuildQuickLaunchDescriptor(Aethelium_Quick_Launch_Descriptor *descriptor,
                                    uint32_t stack_size,
                                    uint32_t entry_point_crc);
void EXE_BuildCoffHeader(EXE_COFF_Header *coff_header,
                         uint16_t number_of_sections,
                         uint16_t characteristics);
void EXE_BuildOptionalHeader(EXE_Optional_Header *opt_header,
                             uint32_t entry_point_rva,
                             uint32_t image_size,
                             uint32_t size_of_headers,
                             uint32_t size_of_code,
                             uint32_t size_of_initialized_data,
                             uint16_t subsystem,
                             uint16_t dll_characteristics);
void EXE_BuildSectionHeader(EXE_Section_Header *sect_header,
                            const char *name,
                            uint32_t virtual_size,
                            uint32_t virtual_address,
                            uint32_t size_of_raw_data,
                            uint32_t pointer_to_raw_data,
                            uint32_t characteristics);
uint32_t EXE_AlignUp(uint32_t value, uint32_t alignment);
int EXE_VerifyPeStructure(const EXE_Binary_Weaver_Context *ctx);
int EXE_VerifySectionAlignment(const EXE_Section_Header *sect);

#ifdef __cplusplus
}
#endif

#endif
