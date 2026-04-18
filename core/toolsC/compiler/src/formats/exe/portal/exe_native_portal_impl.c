/*
 * =========================================================================
 * Aethelium EXE Native Portal Implementation
 * =========================================================================
 */

#include "exe_native_portal.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

typedef struct {
    const char *symbol;
    size_t offset;
    int critical;
} Portal_Binding_Entry;

static Portal_Context g_portal_context;
static Aethelium_Global_State g_global_state;
static int g_portal_initialized = 0;

static const Portal_Binding_Entry g_portal_bindings[] = {
    { "NtAllocateVirtualMemory", offsetof(Portal_Pool, nt_allocate_virtual_memory), 1 },
    { "NtAllocateVirtualMemoryEx", offsetof(Portal_Pool, nt_allocate_virtual_memory_ex), 0 },
    { "NtFreeVirtualMemory", offsetof(Portal_Pool, nt_free_virtual_memory), 1 },
    { "NtProtectVirtualMemory", offsetof(Portal_Pool, nt_protect_virtual_memory), 0 },
    { "NtQueryVirtualMemory", offsetof(Portal_Pool, nt_query_virtual_memory), 0 },
    { "NtLockVirtualMemory", offsetof(Portal_Pool, nt_lock_virtual_memory), 0 },
    { "NtUnlockVirtualMemory", offsetof(Portal_Pool, nt_unlock_virtual_memory), 0 },
    { "NtCreateThreadEx", offsetof(Portal_Pool, nt_create_thread_ex), 1 },
    { "NtOpenThread", offsetof(Portal_Pool, nt_open_thread), 0 },
    { "NtSuspendThread", offsetof(Portal_Pool, nt_suspend_thread), 0 },
    { "NtResumeThread", offsetof(Portal_Pool, nt_resume_thread), 0 },
    { "NtCreateFile", offsetof(Portal_Pool, nt_create_file), 0 },
    { "NtOpenFile", offsetof(Portal_Pool, nt_open_file), 0 },
    { "NtReadFile", offsetof(Portal_Pool, nt_read_file), 0 },
    { "NtWriteFile", offsetof(Portal_Pool, nt_write_file), 0 },
    { "NtClose", offsetof(Portal_Pool, nt_close), 0 },
    { "NtCreateEvent", offsetof(Portal_Pool, nt_create_event), 0 },
    { "NtSetEvent", offsetof(Portal_Pool, nt_set_event), 0 },
    { "NtResetEvent", offsetof(Portal_Pool, nt_reset_event), 0 },
    { "NtWaitForSingleObject", offsetof(Portal_Pool, nt_wait_for_single_object), 1 },
    { "NtWaitForMultipleObjects", offsetof(Portal_Pool, nt_wait_for_multiple_objects), 0 },
    { "NtDelayExecution", offsetof(Portal_Pool, nt_delay_execution), 1 },
    { "NtRemoveIoCompletion", offsetof(Portal_Pool, nt_remove_io_completion), 0 },
    { "NtTerminateProcess", offsetof(Portal_Pool, nt_terminate_process), 1 },
    { "NtCreateProcessEx", offsetof(Portal_Pool, nt_create_process_ex), 0 },
    { "NtCreateUserProcess", offsetof(Portal_Pool, nt_create_user_process), 0 },
    { "NtOpenProcess", offsetof(Portal_Pool, nt_open_process), 0 },
    { "RtlInitUnicodeString", offsetof(Portal_Pool, rtl_init_unicode_string), 0 },
    { "RtlUnicodeStringToAnsiString", offsetof(Portal_Pool, rtl_unicode_string_to_ansi_string), 0 },
    { "RtlAnsiStringToUnicodeString", offsetof(Portal_Pool, rtl_ansi_string_to_unicode_string), 0 },
    { "RtlFreeAnsiString", offsetof(Portal_Pool, rtl_free_ansi_string), 0 },
    { "RtlFreeUnicodeString", offsetof(Portal_Pool, rtl_free_unicode_string), 0 }
};

#ifdef _WIN32
typedef struct {
    uint16_t e_magic;
    uint8_t reserved0[58];
    int32_t e_lfanew;
} PE_DOS_Header;

typedef struct {
    uint32_t virtual_address;
    uint32_t size;
} PE_Data_Directory;

typedef struct {
    uint8_t reserved0[112];
    PE_Data_Directory data_directories[16];
} PE_Optional_Header64_Min;

typedef struct {
    uint32_t signature;
    uint8_t file_header[20];
    PE_Optional_Header64_Min optional_header;
} PE_NT_Headers64_Min;

typedef struct {
    uint32_t characteristics;
    uint32_t time_date_stamp;
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t name;
    uint32_t base;
    uint32_t number_of_functions;
    uint32_t number_of_names;
    uint32_t address_of_functions;
    uint32_t address_of_names;
    uint32_t address_of_name_ordinals;
} PE_Export_Directory;

typedef struct Portal_List_Entry {
    struct Portal_List_Entry *flink;
    struct Portal_List_Entry *blink;
} Portal_List_Entry;

typedef struct {
    uint16_t length;
    uint16_t maximum_length;
    uint16_t *buffer;
} Portal_Unicode_String;

typedef struct {
    Portal_List_Entry in_load_order_links;
    Portal_List_Entry in_memory_order_links;
    Portal_List_Entry in_initialization_order_links;
    void *dll_base;
    void *entry_point;
    uint32_t size_of_image;
    Portal_Unicode_String full_dll_name;
    Portal_Unicode_String base_dll_name;
} Portal_Ldr_Data_Table_Entry;

static uint64_t portal_read_gs_qword(uint32_t offset) {
#if defined(_MSC_VER)
    return __readgsqword(offset);
#elif defined(__x86_64__)
    uint64_t value;
    __asm__ __volatile__("movq %%gs:(%1), %0" : "=r"(value) : "r"((unsigned long long)offset));
    return value;
#else
    (void)offset;
    return 0;
#endif
}

static int portal_ascii_casecmp(const char *a, const char *b) {
    unsigned char ca;
    unsigned char cb;
    if (!a || !b) {
        return (a == b) ? 0 : 1;
    }
    while (*a && *b) {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int portal_unicode_equals_ascii(const uint16_t *wide, uint16_t wide_len_bytes, const char *ascii) {
    size_t wide_len;
    size_t i;
    if (!wide || !ascii) return 0;
    wide_len = (size_t)(wide_len_bytes / 2U);
    for (i = 0; i < wide_len && ascii[i] != '\0'; ++i) {
        unsigned char wa = (unsigned char)(wide[i] & 0xFFU);
        unsigned char aa = (unsigned char)ascii[i];
        if (wa >= 'A' && wa <= 'Z') wa = (unsigned char)(wa + ('a' - 'A'));
        if (aa >= 'A' && aa <= 'Z') aa = (unsigned char)(aa + ('a' - 'A'));
        if (wa != aa) return 0;
    }
    return i == wide_len && ascii[i] == '\0';
}

static void *portal_find_loaded_module(const char *module_name) {
    NT_PEB *peb;
    NT_PEB_LDR_DATA *ldr;
    Portal_List_Entry *head;
    Portal_List_Entry *entry;

    if (!module_name) return NULL;
    peb = (NT_PEB *)(uintptr_t)portal_read_gs_qword(0x60U);
    if (!peb || !peb->ldr) return NULL;
    ldr = (NT_PEB_LDR_DATA *)peb->ldr;
    head = (Portal_List_Entry *)ldr->memory_order_module_list;
    if (!head) return NULL;

    for (entry = head->flink; entry && entry != head; entry = entry->flink) {
        Portal_Ldr_Data_Table_Entry *module =
            (Portal_Ldr_Data_Table_Entry *)((uint8_t *)entry - offsetof(Portal_Ldr_Data_Table_Entry, in_memory_order_links));
        if (module->dll_base &&
            portal_unicode_equals_ascii(module->base_dll_name.buffer,
                                        module->base_dll_name.length,
                                        module_name)) {
            return module->dll_base;
        }
    }
    return NULL;
}

static void *portal_resolve_export(void *module_base, const char *symbol) {
    PE_DOS_Header *dos;
    PE_NT_Headers64_Min *nt;
    PE_Export_Directory *exports;
    uint32_t *names;
    uint16_t *ordinals;
    uint32_t *functions;
    uint32_t i;

    if (!module_base || !symbol) return NULL;
    dos = (PE_DOS_Header *)module_base;
    if (dos->e_magic != 0x5A4DU) return NULL;
    nt = (PE_NT_Headers64_Min *)((uint8_t *)module_base + dos->e_lfanew);
    if (nt->signature != 0x00004550U) return NULL;
    if (nt->optional_header.data_directories[0].virtual_address == 0U) return NULL;

    exports = (PE_Export_Directory *)((uint8_t *)module_base + nt->optional_header.data_directories[0].virtual_address);
    names = (uint32_t *)((uint8_t *)module_base + exports->address_of_names);
    ordinals = (uint16_t *)((uint8_t *)module_base + exports->address_of_name_ordinals);
    functions = (uint32_t *)((uint8_t *)module_base + exports->address_of_functions);

    for (i = 0; i < exports->number_of_names; ++i) {
        const char *export_name = (const char *)((uint8_t *)module_base + names[i]);
        if (portal_ascii_casecmp(export_name, symbol) == 0) {
            uint32_t function_rva = functions[ordinals[i]];
            return (uint8_t *)module_base + function_rva;
        }
    }
    return NULL;
}

static void *resolve_symbol(const char *module_name, const char *symbol) {
    void *module;
    module = portal_find_loaded_module(module_name);
    if (!module) {
        return NULL;
    }
    return portal_resolve_export(module, symbol);
}
#else
static void *resolve_symbol(const char *module_name, const char *symbol) {
    (void)module_name;
    (void)symbol;
    return NULL;
}
#endif

uint32_t Portal_GetSlotOffsetByName(const char *function_name) {
    size_t i;
    if (!function_name) {
        return 0U;
    }
    for (i = 0; i < sizeof(g_portal_bindings) / sizeof(g_portal_bindings[0]); ++i) {
        if (strcmp(g_portal_bindings[i].symbol, function_name) == 0) {
            return (uint32_t)g_portal_bindings[i].offset;
        }
    }
    return 0U;
}

static int portal_bind_exports(Portal_Pool *pool) {
    size_t i;
    if (!pool) {
        return -1;
    }
    memset(pool, 0, sizeof(*pool));
    for (i = 0; i < sizeof(g_portal_bindings) / sizeof(g_portal_bindings[0]); ++i) {
        const Portal_Binding_Entry *entry = &g_portal_bindings[i];
        const char *module = entry->symbol[0] == 'R' ? "ntdll.dll" : "ntdll.dll";
        void **slot = (void **)(((uint8_t *)pool) + entry->offset);
        *slot = (void *)resolve_symbol(module, entry->symbol);
    }
    return 0;
}

static int portal_check_critical_bindings(const Portal_Pool *pool) {
    size_t i;
    if (!pool) {
        return -1;
    }
    for (i = 0; i < sizeof(g_portal_bindings) / sizeof(g_portal_bindings[0]); ++i) {
        const Portal_Binding_Entry *entry = &g_portal_bindings[i];
        if (entry->critical) {
            void *const *slot = (void *const *)(((const uint8_t *)pool) + entry->offset);
            if (!*slot) {
                return -1;
            }
        }
    }
    return 0;
}

int Portal_Initialize(Portal_Context *ctx) {
    if (!ctx) {
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
#ifdef _WIN32
    ctx->teb = (NT_TEB *)(uintptr_t)portal_read_gs_qword(0x30U);
    ctx->peb = (NT_PEB *)(uintptr_t)portal_read_gs_qword(0x60U);
#endif
    if (portal_bind_exports(&ctx->pool) != 0) {
        return -1;
    }
    if (portal_check_critical_bindings(&ctx->pool) != 0) {
        return -1;
    }
    ctx->initialized = 1;
    return 0;
}

Portal_Pool *Portal_GetPool(Portal_Context *ctx) {
    return (ctx && ctx->initialized) ? &ctx->pool : NULL;
}

Portal_Context *Portal_GetContext(void) {
    return g_portal_initialized ? &g_portal_context : NULL;
}

int Portal_SetGlobalState(Aethelium_Global_State *state) {
    if (!state) {
        return -1;
    }
    memcpy(&g_global_state, state, sizeof(g_global_state));
    g_global_state.portal_context = &g_portal_context;
    g_global_state.current_teb = g_portal_context.teb;
    return 0;
}

Aethelium_Global_State *Portal_GetGlobalState(void) {
    return &g_global_state;
}

int Portal_Bootstrap(void) {
    if (g_portal_initialized) {
        return 0;
    }
    if (Portal_Initialize(&g_portal_context) != 0) {
        return -1;
    }
    memset(&g_global_state, 0, sizeof(g_global_state));
    g_global_state.portal_context = &g_portal_context;
    g_global_state.current_teb = g_portal_context.teb;
    g_global_state.mxcsr_state = 0x1F80U;
    g_portal_initialized = 1;
    return 0;
}

int Portal_VerifyBindings(void) {
    return g_portal_initialized ? portal_check_critical_bindings(&g_portal_context.pool) : -1;
}

void Portal_DisplayInfo(void) {
    size_t i;
    fprintf(stderr, "Portal initialized: %s\n", g_portal_initialized ? "yes" : "no");
    if (!g_portal_initialized) {
        return;
    }
    for (i = 0; i < sizeof(g_portal_bindings) / sizeof(g_portal_bindings[0]); ++i) {
        void *const *slot = (void *const *)(((const uint8_t *)&g_portal_context.pool) + g_portal_bindings[i].offset);
        fprintf(stderr, "  %s => %p\n", g_portal_bindings[i].symbol, *slot);
    }
}
