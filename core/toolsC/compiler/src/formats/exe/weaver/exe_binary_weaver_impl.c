/*
 * =========================================================================
 * Aethelium EXE Binary Weaver - PE32+ industrial implementation
 * =========================================================================
 */

#include "exe_binary_weaver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALIGN_UP(value, alignment) (((value) + ((alignment) - 1U)) & ~((alignment) - 1U))

typedef struct {
    uint32_t import_directory_rva;
    uint32_t import_directory_size;
    uint32_t iat_rva;
    uint32_t iat_size;
} Import_Blob_Info;

typedef struct {
    uint32_t pdata_rva;
    uint32_t pdata_size;
} Exception_Blob_Info;

typedef struct {
    uint32_t reloc_rva;
    uint32_t reloc_size;
} Reloc_Blob_Info;

static int validate_section_name(const char *name) {
    size_t len = 0U;
    if (!name || name[0] == '\0') {
        return 0;
    }
    len = strlen(name);
    return len <= 8U;
}

static uint32_t crc32_simple(const uint8_t *data, uint32_t size) {
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t i;
    if (!data || size == 0U) {
        return 0U;
    }
    for (i = 0; i < size; ++i) {
        uint32_t j;
        crc ^= (uint32_t)data[i];
        for (j = 0; j < 8U; ++j) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

static void copy_section_name(uint8_t *dest, const char *src) {
    memset(dest, 0, 8U);
    if (src) {
        size_t len = strlen(src);
        if (len > 8U) {
            len = 8U;
        }
        memcpy(dest, src, len);
    }
}

static void write_u16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void write_u32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16U) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static EXE_Section_Data *find_section(EXE_Binary_Weaver_Context *ctx, const char *name) {
    uint32_t i;
    if (!ctx || !name) {
        return NULL;
    }
    for (i = 0; i < ctx->section_count; ++i) {
        if (strncmp(ctx->sections[i].name, name, sizeof(ctx->sections[i].name)) == 0) {
            return &ctx->sections[i];
        }
    }
    return NULL;
}

static int ensure_section_capacity(EXE_Binary_Weaver_Context *ctx) {
    if (!ctx || ctx->section_count >= EXE_WIN64_MAX_SECTIONS) {
        return -1;
    }
    return 0;
}

static int set_section_payload(EXE_Section_Data *section,
                               const uint8_t *payload,
                               uint32_t payload_size,
                               uint32_t characteristics,
                               int synthetic) {
    uint8_t *raw_data = NULL;
    if (!section) {
        return -1;
    }
    if (payload_size > 0U) {
        raw_data = (uint8_t *)malloc(payload_size);
        if (!raw_data) {
            return -1;
        }
        memcpy(raw_data, payload, payload_size);
    }
    free(section->raw_data);
    section->raw_data = raw_data;
    section->raw_size = payload_size;
    section->virtual_size = payload_size;
    section->characteristics = characteristics;
    section->alignment = EXE_WIN64_SECTION_ALIGNMENT;
    section->synthetic = synthetic;
    return 0;
}

static int ensure_synthetic_section(EXE_Binary_Weaver_Context *ctx,
                                    const char *name,
                                    const uint8_t *payload,
                                    uint32_t payload_size,
                                    uint32_t characteristics) {
    EXE_Section_Data *section = find_section(ctx, name);
    if (!section) {
        if (ensure_section_capacity(ctx) != 0) {
            return -1;
        }
        section = &ctx->sections[ctx->section_count++];
        memset(section, 0, sizeof(*section));
        strncpy(section->name, name, sizeof(section->name) - 1U);
    }
    return set_section_payload(section, payload, payload_size, characteristics, 1);
}

void EXE_BuildDosHeader(EXE_DOS_Header *dos_header) {
    if (!dos_header) {
        return;
    }
    memset(dos_header, 0, sizeof(*dos_header));
    dos_header->e_magic = 0x5A4DU;
    dos_header->e_cparhdr = 0x0004U;
    dos_header->e_lfanew = 0x40U;
}

void EXE_BuildQuickLaunchDescriptor(Aethelium_Quick_Launch_Descriptor *descriptor,
                                    uint32_t stack_size,
                                    uint32_t entry_point_crc) {
    if (!descriptor) {
        return;
    }
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->signature = 0x514C5441U;
    descriptor->stack_size = stack_size;
    descriptor->entry_point_crc = entry_point_crc;
    descriptor->metadata_version = 0x20260420U;
    memcpy(descriptor->section_alias_text, ".text\\actflow", 14U);
    memcpy(descriptor->section_alias_rdata, ".rdata\\mirror", 14U);
    memcpy(descriptor->section_alias_data, ".data\\state", 11U);
    memcpy(descriptor->section_alias_pdata, ".pdata\\unwind", 14U);
}

void EXE_BuildCoffHeader(EXE_COFF_Header *coff_header,
                         uint16_t number_of_sections,
                         uint16_t characteristics) {
    if (!coff_header) {
        return;
    }
    memset(coff_header, 0, sizeof(*coff_header));
    coff_header->machine = EXE_WIN64_MACHINE_X64;
    coff_header->number_of_sections = number_of_sections;
    coff_header->size_of_optional_header = EXE_WIN64_OPT_HEADER_SIZE;
    coff_header->characteristics = characteristics;
}

void EXE_BuildOptionalHeader(EXE_Optional_Header *opt_header,
                             uint32_t entry_point_rva,
                             uint32_t image_size,
                             uint32_t size_of_headers,
                             uint32_t size_of_code,
                             uint32_t size_of_initialized_data,
                             uint16_t subsystem,
                             uint16_t dll_characteristics) {
    if (!opt_header) {
        return;
    }
    memset(opt_header, 0, sizeof(*opt_header));
    opt_header->magic = EXE_WIN64_MAGIC_PE32PLUS;
    opt_header->major_linker_version = 1;
    opt_header->minor_linker_version = 0;
    opt_header->size_of_code = size_of_code;
    opt_header->size_of_initialized_data = size_of_initialized_data;
    opt_header->address_of_entry_point = entry_point_rva;
    opt_header->base_of_code = EXE_WIN64_SECTION_ALIGNMENT;
    opt_header->image_base = EXE_WIN64_IMAGE_BASE_DEFAULT;
    opt_header->section_alignment = EXE_WIN64_SECTION_ALIGNMENT;
    opt_header->file_alignment = EXE_WIN64_FILE_ALIGNMENT;
    opt_header->major_operating_system_version = 6U;
    opt_header->major_subsystem_version = 6U;
    opt_header->size_of_image = image_size;
    opt_header->size_of_headers = size_of_headers;
    opt_header->subsystem = subsystem;
    opt_header->dll_characteristics = dll_characteristics;
    opt_header->size_of_stack_reserve = EXE_WIN64_STACK_RESERVE;
    opt_header->size_of_stack_commit = EXE_WIN64_STACK_COMMIT;
    opt_header->size_of_heap_reserve = EXE_WIN64_HEAP_RESERVE;
    opt_header->size_of_heap_commit = EXE_WIN64_HEAP_COMMIT;
    opt_header->number_of_rva_and_sizes = EXE_WIN64_DATA_DIRECTORIES_COUNT;
}

void EXE_BuildSectionHeader(EXE_Section_Header *sect_header,
                            const char *name,
                            uint32_t virtual_size,
                            uint32_t virtual_address,
                            uint32_t size_of_raw_data,
                            uint32_t pointer_to_raw_data,
                            uint32_t characteristics) {
    if (!sect_header) {
        return;
    }
    memset(sect_header, 0, sizeof(*sect_header));
    copy_section_name(sect_header->name, name);
    sect_header->virtual_size = virtual_size;
    sect_header->virtual_address = virtual_address;
    sect_header->size_of_raw_data = size_of_raw_data;
    sect_header->pointer_to_raw_data = pointer_to_raw_data;
    sect_header->characteristics = characteristics;
}

uint32_t EXE_AlignUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0U) {
        return value;
    }
    return ALIGN_UP(value, alignment);
}

int EXE_Weaver_Initialize(EXE_Binary_Weaver_Context *ctx,
                          uint64_t image_base,
                          uint16_t subsystem) {
    if (!ctx) {
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->image_base = image_base;
    ctx->subsystem = subsystem;
    ctx->deterministic_build = 1;
    ctx->dll_characteristics = EXE_WIN64_DLLCHAR_AETHELIUM_DEFAULT;
    return 0;
}

int EXE_Weaver_AddSection(EXE_Binary_Weaver_Context *ctx,
                          const char *name,
                          const uint8_t *data,
                          uint32_t data_size,
                          uint32_t characteristics,
                          uint32_t alignment) {
    EXE_Section_Data *section;
    if (!ctx || !name || !validate_section_name(name)) {
        return -1;
    }
    if (ensure_section_capacity(ctx) != 0) {
        return -1;
    }
    section = &ctx->sections[ctx->section_count++];
    memset(section, 0, sizeof(*section));
    strncpy(section->name, name, sizeof(section->name) - 1U);
    section->alignment = alignment ? alignment : EXE_WIN64_SECTION_ALIGNMENT;
    section->characteristics = characteristics;
    if (data_size > 0U) {
        section->raw_data = (uint8_t *)malloc(data_size);
        if (!section->raw_data) {
            return -1;
        }
        if (data) {
            memcpy(section->raw_data, data, data_size);
        } else {
            memset(section->raw_data, 0, data_size);
        }
    }
    section->raw_size = data_size;
    section->virtual_size = data_size;
    if ((characteristics & EXE_WIN64_SECT_TEXT) == EXE_WIN64_SECT_TEXT) {
        ctx->code_size += ALIGN_UP(data_size, EXE_WIN64_FILE_ALIGNMENT);
    } else {
        ctx->data_size += ALIGN_UP(data_size, EXE_WIN64_FILE_ALIGNMENT);
    }
    return 0;
}

void EXE_Weaver_SetEntryPoint(EXE_Binary_Weaver_Context *ctx,
                              uint32_t entry_point_rva) {
    if (ctx) {
        ctx->entry_point_rva = entry_point_rva;
    }
}

void EXE_Weaver_SetDllCharacteristics(EXE_Binary_Weaver_Context *ctx,
                                      uint16_t characteristics) {
    if (ctx) {
        ctx->dll_characteristics = characteristics;
    }
}

static EXE_Import_Library *get_or_create_import_library(EXE_Binary_Weaver_Context *ctx,
                                                        const char *dll_name) {
    uint32_t i;
    if (!ctx || !dll_name) {
        return NULL;
    }
    for (i = 0; i < ctx->import_library_count; ++i) {
        if (strncmp(ctx->imports[i].dll_name, dll_name, sizeof(ctx->imports[i].dll_name)) == 0) {
            return &ctx->imports[i];
        }
    }
    if (ctx->import_library_count >= EXE_WIN64_MAX_IMPORT_LIBRARIES) {
        return NULL;
    }
    memset(&ctx->imports[ctx->import_library_count], 0, sizeof(ctx->imports[ctx->import_library_count]));
    strncpy(ctx->imports[ctx->import_library_count].dll_name,
            dll_name,
            sizeof(ctx->imports[ctx->import_library_count].dll_name) - 1U);
    return &ctx->imports[ctx->import_library_count++];
}

int EXE_Weaver_AddImport(EXE_Binary_Weaver_Context *ctx,
                         const char *dll_name,
                         const char *function_name,
                         uint32_t *out_iat_rva) {
    EXE_Import_Library *library;
    EXE_Import_Function *function;
    uint32_t i;
    if (!ctx || !dll_name || !function_name) {
        return -1;
    }
    library = get_or_create_import_library(ctx, dll_name);
    if (!library) {
        return -1;
    }
    for (i = 0; i < library->function_count; ++i) {
        if (strncmp(library->functions[i].function_name, function_name, sizeof(library->functions[i].function_name)) == 0) {
            if (out_iat_rva) {
                *out_iat_rva = library->functions[i].iat_rva;
            }
            return 0;
        }
    }
    if (library->function_count >= EXE_WIN64_MAX_IMPORTS_PER_LIBRARY) {
        return -1;
    }
    function = &library->functions[library->function_count++];
    memset(function, 0, sizeof(*function));
    strncpy(function->function_name, function_name, sizeof(function->function_name) - 1U);
    if (out_iat_rva) {
        *out_iat_rva = 0U;
    }
    return 0;
}

int EXE_Weaver_AddExceptionInfo(EXE_Binary_Weaver_Context *ctx,
                                const uint8_t *unwind_info,
                                uint32_t unwind_info_size,
                                uint32_t rva,
                                uint32_t size) {
    EXE_Unwind_Record *record;
    if (!ctx || !unwind_info || unwind_info_size == 0U || unwind_info_size > sizeof(ctx->unwind_records[0].unwind_info)) {
        return -1;
    }
    if (ctx->unwind_record_count >= EXE_WIN64_MAX_UNWIND_RECORDS) {
        return -1;
    }
    record = &ctx->unwind_records[ctx->unwind_record_count++];
    memset(record, 0, sizeof(*record));
    record->function_rva_start = rva;
    record->function_rva_end = rva + size;
    record->unwind_info_size = unwind_info_size;
    memcpy(record->unwind_info, unwind_info, unwind_info_size);
    return 0;
}

static int build_import_blob(EXE_Binary_Weaver_Context *ctx, Import_Blob_Info *info) {
    uint32_t total_size = 0U;
    uint32_t i;
    uint32_t cursor;
    uint8_t *blob;
    if (!ctx || !info) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    if (ctx->import_library_count == 0U) {
        return 0;
    }

    total_size += (ctx->import_library_count + 1U) * 20U;
    total_size += (ctx->import_library_count + 1U) * 8U;
    for (i = 0; i < ctx->import_library_count; ++i) {
        uint32_t j;
        total_size += (ctx->imports[i].function_count + 1U) * 8U * 2U;
        total_size += (uint32_t)strlen(ctx->imports[i].dll_name) + 1U;
        for (j = 0; j < ctx->imports[i].function_count; ++j) {
            total_size += 2U + (uint32_t)strlen(ctx->imports[i].functions[j].function_name) + 1U;
        }
    }

    blob = (uint8_t *)calloc(1U, total_size);
    if (!blob) {
        return -1;
    }

    cursor = (ctx->import_library_count + 1U) * 20U;
    info->iat_rva = 0U;
    for (i = 0; i < ctx->import_library_count; ++i) {
        uint32_t j;
        uint32_t ilt_offset = cursor;
        uint32_t iat_offset;
        cursor += (ctx->imports[i].function_count + 1U) * 8U;
        iat_offset = cursor;
        cursor += (ctx->imports[i].function_count + 1U) * 8U;
        for (j = 0; j < ctx->imports[i].function_count; ++j) {
            uint32_t hint_name_offset = cursor;
            write_u16(blob + hint_name_offset, 0U);
            memcpy(blob + hint_name_offset + 2U,
                   ctx->imports[i].functions[j].function_name,
                   strlen(ctx->imports[i].functions[j].function_name) + 1U);
            ctx->imports[i].functions[j].hint_name_rva = hint_name_offset;
            write_u32(blob + i * 20U + 0U, ilt_offset);
            write_u32(blob + i * 20U + 12U, (uint32_t)(cursor + 2U + strlen(ctx->imports[i].functions[j].function_name) + 1U));
            write_u32(blob + i * 20U + 16U, iat_offset);
            write_u32(blob + ilt_offset + j * 8U, hint_name_offset);
            write_u32(blob + iat_offset + j * 8U, hint_name_offset);
            ctx->imports[i].functions[j].iat_rva = iat_offset + j * 8U;
            cursor += 2U + (uint32_t)strlen(ctx->imports[i].functions[j].function_name) + 1U;
        }
        memcpy(blob + cursor, ctx->imports[i].dll_name, strlen(ctx->imports[i].dll_name) + 1U);
        write_u32(blob + i * 20U + 12U, cursor);
        cursor += (uint32_t)strlen(ctx->imports[i].dll_name) + 1U;
        if (info->iat_rva == 0U) {
            info->iat_rva = iat_offset;
        }
        info->iat_size += (ctx->imports[i].function_count + 1U) * 8U;
    }

    if (ensure_synthetic_section(ctx, ".idata", blob, total_size, EXE_WIN64_SECT_IDATA) != 0) {
        free(blob);
        return -1;
    }
    free(blob);
    info->import_directory_rva = 0U;
    info->import_directory_size = (ctx->import_library_count + 1U) * 20U;
    return 0;
}

static int build_exception_blob(EXE_Binary_Weaver_Context *ctx, Exception_Blob_Info *info) {
    uint32_t i;
    uint32_t record_count;
    uint32_t total_size;
    uint8_t *blob;
    if (!ctx || !info) {
        return -1;
    }
    memset(info, 0, sizeof(*info));

    if (ctx->unwind_record_count == 0U) {
        uint8_t default_unwind[4] = { 0x01U, 0x00U, 0x00U, 0x00U };
        EXE_Section_Data *text = find_section(ctx, ".text");
        if (!text) {
            return -1;
        }
        if (EXE_Weaver_AddExceptionInfo(ctx,
                                        default_unwind,
                                        sizeof(default_unwind),
                                        EXE_WIN64_SECTION_ALIGNMENT,
                                        text->raw_size ? text->raw_size : text->virtual_size) != 0) {
            return -1;
        }
    }

    record_count = ctx->unwind_record_count;
    total_size = record_count * 12U;
    for (i = 0; i < record_count; ++i) {
        total_size += ALIGN_UP(ctx->unwind_records[i].unwind_info_size, 4U);
    }
    blob = (uint8_t *)calloc(1U, total_size);
    if (!blob) {
        return -1;
    }
    {
        uint32_t cursor = record_count * 12U;
        for (i = 0; i < record_count; ++i) {
            write_u32(blob + i * 12U + 0U, ctx->unwind_records[i].function_rva_start);
            write_u32(blob + i * 12U + 4U, ctx->unwind_records[i].function_rva_end);
            ctx->unwind_records[i].unwind_info_rva = cursor;
            write_u32(blob + i * 12U + 8U, cursor);
            memcpy(blob + cursor,
                   ctx->unwind_records[i].unwind_info,
                   ctx->unwind_records[i].unwind_info_size);
            cursor += ALIGN_UP(ctx->unwind_records[i].unwind_info_size, 4U);
        }
    }
    if (ensure_synthetic_section(ctx, ".pdata", blob, total_size, EXE_WIN64_SECT_PDATA) != 0) {
        free(blob);
        return -1;
    }
    free(blob);
    info->pdata_size = record_count * 12U;
    return 0;
}

static int build_reloc_blob(EXE_Binary_Weaver_Context *ctx, Reloc_Blob_Info *info) {
    uint8_t blob[12];
    if (!ctx || !info) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    memset(blob, 0, sizeof(blob));
    write_u32(blob + 0U, EXE_WIN64_SECTION_ALIGNMENT);
    write_u32(blob + 4U, sizeof(blob));
    write_u16(blob + 8U, 0x0000U);
    write_u16(blob + 10U, 0x0000U);
    if (ensure_synthetic_section(ctx, ".reloc", blob, sizeof(blob), EXE_WIN64_SECT_RELOC) != 0) {
        return -1;
    }
    info->reloc_size = sizeof(blob);
    return 0;
}

static void assign_layout(EXE_Binary_Weaver_Context *ctx) {
    uint32_t i;
    uint32_t file_offset;
    uint32_t rva;
    ctx->header_size = ALIGN_UP((uint32_t)(EXE_WIN64_DOS_HEADER_SIZE +
                                           EXE_WIN64_SIGNATURE_SIZE +
                                           EXE_WIN64_COFF_HEADER_SIZE +
                                           EXE_WIN64_OPT_HEADER_SIZE +
                                           ctx->section_count * EXE_WIN64_SECTION_HEADER_SIZE),
                                EXE_WIN64_FILE_ALIGNMENT);
    file_offset = ctx->header_size;
    rva = EXE_WIN64_SECTION_ALIGNMENT;
    ctx->code_size = 0U;
    ctx->data_size = 0U;
    for (i = 0; i < ctx->section_count; ++i) {
        EXE_Section_Data *section = &ctx->sections[i];
        section->virtual_address = rva;
        section->pointer_to_raw_data = file_offset;
        if ((section->characteristics & 0x20U) != 0U) {
            ctx->code_size += ALIGN_UP(section->raw_size, EXE_WIN64_FILE_ALIGNMENT);
        } else {
            ctx->data_size += ALIGN_UP(section->raw_size, EXE_WIN64_FILE_ALIGNMENT);
        }
        file_offset += ALIGN_UP(section->raw_size, EXE_WIN64_FILE_ALIGNMENT);
        rva += ALIGN_UP(section->virtual_size ? section->virtual_size : section->raw_size,
                        EXE_WIN64_SECTION_ALIGNMENT);
    }
    ctx->image_size = rva;
}

static void patch_directory_rvas(EXE_Binary_Weaver_Context *ctx,
                                 Import_Blob_Info *import_info,
                                 Exception_Blob_Info *exception_info,
                                 Reloc_Blob_Info *reloc_info) {
    EXE_Section_Data *idata = find_section(ctx, ".idata");
    EXE_Section_Data *pdata = find_section(ctx, ".pdata");
    EXE_Section_Data *reloc = find_section(ctx, ".reloc");
    uint32_t i;

    if (idata) {
        ctx->import_table_rva = idata->virtual_address;
        ctx->import_table_size = import_info->import_directory_size;
        ctx->iat_rva = idata->virtual_address + import_info->iat_rva;
        ctx->iat_size = import_info->iat_size;
        for (i = 0; i < ctx->import_library_count; ++i) {
            uint32_t j;
            for (j = 0; j < ctx->imports[i].function_count; ++j) {
                ctx->imports[i].functions[j].iat_rva += idata->virtual_address;
            }
        }
    } else {
        ctx->import_table_rva = 0U;
        ctx->import_table_size = 0U;
        ctx->iat_rva = 0U;
        ctx->iat_size = 0U;
    }
    if (pdata) {
        ctx->exception_table_rva = pdata->virtual_address;
        ctx->exception_table_size = exception_info->pdata_size;
    }
    if (reloc) {
        ctx->relocation_table_rva = reloc->virtual_address;
        ctx->relocation_table_size = reloc_info->reloc_size;
    }
}

static void patch_idata_payload(EXE_Binary_Weaver_Context *ctx) {
    EXE_Section_Data *idata = find_section(ctx, ".idata");
    uint32_t i;
    if (!ctx || !idata || !idata->raw_data) {
        return;
    }
    for (i = 0; i < ctx->import_library_count; ++i) {
        uint32_t j;
        uint32_t descriptor_offset = i * 20U;
        uint32_t ilt_offset = 0U;
        uint32_t name_offset = 0U;
        uint32_t iat_offset = 0U;
        memcpy(&ilt_offset, idata->raw_data + descriptor_offset + 0U, sizeof(uint32_t));
        memcpy(&name_offset, idata->raw_data + descriptor_offset + 12U, sizeof(uint32_t));
        memcpy(&iat_offset, idata->raw_data + descriptor_offset + 16U, sizeof(uint32_t));
        write_u32(idata->raw_data + descriptor_offset + 0U, idata->virtual_address + ilt_offset);
        write_u32(idata->raw_data + descriptor_offset + 12U, idata->virtual_address + name_offset);
        write_u32(idata->raw_data + descriptor_offset + 16U, idata->virtual_address + iat_offset);
        for (j = 0; j < ctx->imports[i].function_count; ++j) {
            uint32_t thunk_offset = ilt_offset + j * 8U;
            uint32_t hint_rva = idata->virtual_address + ctx->imports[i].functions[j].hint_name_rva;
            write_u32(idata->raw_data + thunk_offset, hint_rva);
            write_u32(idata->raw_data + iat_offset + j * 8U, hint_rva);
        }
    }
}

static void patch_pdata_payload(EXE_Binary_Weaver_Context *ctx) {
    EXE_Section_Data *pdata = find_section(ctx, ".pdata");
    EXE_Section_Data *text = find_section(ctx, ".text");
    uint32_t i;
    if (!ctx || !pdata || !pdata->raw_data) {
        return;
    }
    for (i = 0; i < ctx->unwind_record_count; ++i) {
        if (text && ctx->unwind_records[i].function_rva_start == EXE_WIN64_SECTION_ALIGNMENT) {
            ctx->unwind_records[i].function_rva_start = text->virtual_address;
            ctx->unwind_records[i].function_rva_end = text->virtual_address +
                (text->raw_size ? text->raw_size : text->virtual_size);
            write_u32(pdata->raw_data + i * 12U + 0U, ctx->unwind_records[i].function_rva_start);
            write_u32(pdata->raw_data + i * 12U + 4U, ctx->unwind_records[i].function_rva_end);
        }
        write_u32(pdata->raw_data + i * 12U + 8U, pdata->virtual_address + ctx->unwind_records[i].unwind_info_rva);
    }
}

int EXE_VerifyPeStructure(const EXE_Binary_Weaver_Context *ctx) {
    if (!ctx) {
        return 0;
    }
    if (ctx->section_count == 0U || ctx->section_count > EXE_WIN64_MAX_SECTIONS) {
        return 0;
    }
    if (ctx->headers.dos_header.e_magic != 0x5A4DU) {
        return 0;
    }
    if (ctx->headers.coff_header.machine != EXE_WIN64_MACHINE_X64) {
        return 0;
    }
    if (ctx->headers.optional_header.magic != EXE_WIN64_MAGIC_PE32PLUS) {
        return 0;
    }
    if ((uint64_t)ctx->headers.optional_header.size_of_image > EXE_WIN64_IMAGE_MAX_SIZE) {
        return 0;
    }
    return 1;
}

int EXE_VerifySectionAlignment(const EXE_Section_Header *sect) {
    if (!sect) {
        return 0;
    }
    if ((sect->virtual_address % EXE_WIN64_SECTION_ALIGNMENT) != 0U) {
        return 0;
    }
    if (sect->pointer_to_raw_data != 0U &&
        (sect->pointer_to_raw_data % EXE_WIN64_FILE_ALIGNMENT) != 0U) {
        return 0;
    }
    return 1;
}

int EXE_Weaver_Finalize(EXE_Binary_Weaver_Context *ctx,
                        uint8_t **out_image,
                        uint32_t *out_size) {
    Import_Blob_Info import_info;
    Exception_Blob_Info exception_info;
    Reloc_Blob_Info reloc_info;
    uint32_t i;
    uint32_t total_size;
    uint8_t *image;

    if (!ctx || !out_image || !out_size) {
        return -1;
    }

    if (build_import_blob(ctx, &import_info) != 0 ||
        build_exception_blob(ctx, &exception_info) != 0 ||
        build_reloc_blob(ctx, &reloc_info) != 0) {
        return -1;
    }

    assign_layout(ctx);
    patch_directory_rvas(ctx, &import_info, &exception_info, &reloc_info);
    patch_idata_payload(ctx);
    patch_pdata_payload(ctx);

    total_size = ctx->header_size;
    for (i = 0; i < ctx->section_count; ++i) {
        total_size += ALIGN_UP(ctx->sections[i].raw_size, EXE_WIN64_FILE_ALIGNMENT);
    }

    memset(&ctx->headers, 0, sizeof(ctx->headers));
    EXE_BuildDosHeader(&ctx->headers.dos_header);
    EXE_BuildQuickLaunchDescriptor(&ctx->headers.quick_launch,
                                   (uint32_t)EXE_WIN64_STACK_RESERVE,
                                   crc32_simple(find_section(ctx, ".text") ? find_section(ctx, ".text")->raw_data : NULL,
                                                find_section(ctx, ".text") ? find_section(ctx, ".text")->raw_size : 0U));
    ctx->headers.pe_signature = 0x00004550U;
    EXE_BuildCoffHeader(&ctx->headers.coff_header,
                        (uint16_t)ctx->section_count,
                        ctx->subsystem == EXE_WIN64_SUBSYSTEM_WINDOWS_GUI ? EXE_WIN64_CHAR_GUI_DEFAULT : EXE_WIN64_CHAR_CONSOLE_DEFAULT);
    EXE_BuildOptionalHeader(&ctx->headers.optional_header,
                            ctx->entry_point_rva + EXE_WIN64_SECTION_ALIGNMENT,
                            ctx->image_size,
                            ctx->header_size,
                            ctx->code_size,
                            ctx->data_size,
                            ctx->subsystem,
                            ctx->dll_characteristics);

    ctx->headers.optional_header.image_base = ctx->image_base;
    ctx->headers.optional_header.data_directories[EXE_WIN64_DATADIR_IMPORT].virtual_address = ctx->import_table_rva;
    ctx->headers.optional_header.data_directories[EXE_WIN64_DATADIR_IMPORT].size = ctx->import_table_size;
    ctx->headers.optional_header.data_directories[EXE_WIN64_DATADIR_EXCEPTION].virtual_address = ctx->exception_table_rva;
    ctx->headers.optional_header.data_directories[EXE_WIN64_DATADIR_EXCEPTION].size = ctx->exception_table_size;
    ctx->headers.optional_header.data_directories[EXE_WIN64_DATADIR_BASERELOC].virtual_address = ctx->relocation_table_rva;
    ctx->headers.optional_header.data_directories[EXE_WIN64_DATADIR_BASERELOC].size = ctx->relocation_table_size;
    ctx->headers.optional_header.data_directories[EXE_WIN64_DATADIR_IAT].virtual_address = ctx->iat_rva;
    ctx->headers.optional_header.data_directories[EXE_WIN64_DATADIR_IAT].size = ctx->iat_size;

    for (i = 0; i < ctx->section_count; ++i) {
        EXE_BuildSectionHeader(&ctx->headers.section_headers[i],
                               ctx->sections[i].name,
                               ctx->sections[i].virtual_size,
                               ctx->sections[i].virtual_address,
                               ALIGN_UP(ctx->sections[i].raw_size, EXE_WIN64_FILE_ALIGNMENT),
                               ctx->sections[i].pointer_to_raw_data,
                               ctx->sections[i].characteristics);
    }

    image = (uint8_t *)calloc(1U, total_size);
    if (!image) {
        return -1;
    }

    memcpy(image, &ctx->headers.dos_header, sizeof(ctx->headers.dos_header));
    memcpy(image + sizeof(ctx->headers.dos_header),
           &ctx->headers.quick_launch,
           sizeof(ctx->headers.quick_launch));
    memcpy(image + ctx->headers.dos_header.e_lfanew, &ctx->headers.pe_signature, sizeof(ctx->headers.pe_signature));
    memcpy(image + ctx->headers.dos_header.e_lfanew + 4U,
           &ctx->headers.coff_header,
           sizeof(ctx->headers.coff_header));
    memcpy(image + ctx->headers.dos_header.e_lfanew + 4U + sizeof(ctx->headers.coff_header),
           &ctx->headers.optional_header,
           sizeof(ctx->headers.optional_header));
    memcpy(image + ctx->headers.dos_header.e_lfanew + 4U + sizeof(ctx->headers.coff_header) + sizeof(ctx->headers.optional_header),
           &ctx->headers.section_headers[0],
           ctx->section_count * sizeof(EXE_Section_Header));

    for (i = 0; i < ctx->section_count; ++i) {
        if (ctx->sections[i].raw_size > 0U && ctx->sections[i].raw_data) {
            memcpy(image + ctx->sections[i].pointer_to_raw_data,
                   ctx->sections[i].raw_data,
                   ctx->sections[i].raw_size);
        }
    }

    *out_image = image;
    *out_size = total_size;
    return EXE_VerifyPeStructure(ctx) ? 0 : -1;
}

int EXE_Weaver_WriteToFile(EXE_Binary_Weaver_Context *ctx,
                           const char *output_path) {
    FILE *file;
    uint8_t *image = NULL;
    uint32_t image_size = 0U;
    size_t written;
    if (!ctx || !output_path) {
        return -1;
    }
    if (EXE_Weaver_Finalize(ctx, &image, &image_size) != 0) {
        return -1;
    }
    file = fopen(output_path, "wb");
    if (!file) {
        free(image);
        return -1;
    }
    written = fwrite(image, 1U, image_size, file);
    fclose(file);
    free(image);
    return written == image_size ? 0 : -1;
}

void EXE_Weaver_Cleanup(EXE_Binary_Weaver_Context *ctx) {
    uint32_t i;
    if (!ctx) {
        return;
    }
    for (i = 0; i < ctx->section_count; ++i) {
        free(ctx->sections[i].raw_data);
        ctx->sections[i].raw_data = NULL;
    }
    ctx->section_count = 0U;
}
