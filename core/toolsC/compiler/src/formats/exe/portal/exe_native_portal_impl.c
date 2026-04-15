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
#include <windows.h>
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
    { "NtCreateThreadEx", offsetof(Portal_Pool, nt_create_thread_ex), 1 },
    { "NtCreateFile", offsetof(Portal_Pool, nt_create_file), 0 },
    { "NtReadFile", offsetof(Portal_Pool, nt_read_file), 0 },
    { "NtWriteFile", offsetof(Portal_Pool, nt_write_file), 0 },
    { "NtCreateEvent", offsetof(Portal_Pool, nt_create_event), 0 },
    { "NtWaitForSingleObject", offsetof(Portal_Pool, nt_wait_for_single_object), 1 },
    { "NtDelayExecution", offsetof(Portal_Pool, nt_delay_execution), 1 },
    { "NtRemoveIoCompletion", offsetof(Portal_Pool, nt_remove_io_completion), 0 },
    { "NtTerminateProcess", offsetof(Portal_Pool, nt_terminate_process), 1 },
    { "RtlInitUnicodeString", offsetof(Portal_Pool, rtl_init_unicode_string), 0 },
    { "RtlUnicodeStringToAnsiString", offsetof(Portal_Pool, rtl_unicode_string_to_ansi_string), 0 },
    { "RtlAnsiStringToUnicodeString", offsetof(Portal_Pool, rtl_ansi_string_to_unicode_string), 0 }
};

#ifdef _WIN32
static FARPROC resolve_symbol(const char *module_name, const char *symbol) {
    HMODULE module;
    module = GetModuleHandleA(module_name);
    if (!module) {
        module = LoadLibraryA(module_name);
    }
    if (!module) {
        return NULL;
    }
    return GetProcAddress(module, symbol);
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
    ctx->teb = (NT_TEB *)NtCurrentTeb();
    ctx->peb = ctx->teb ? (NT_PEB *)ctx->teb->peb : NULL;
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
