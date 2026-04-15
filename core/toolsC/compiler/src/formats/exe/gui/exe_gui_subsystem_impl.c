/*
 * =========================================================================
 * Aethelium EXE GUI Subsystem Implementation
 * =========================================================================
 */

#include "exe_gui_subsystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[256];
    WNDPROC wnd_proc;
    HICON icon;
    HCURSOR cursor;
    HBRUSH background;
    uint32_t style;
} Registered_Class;

typedef struct {
    uint32_t magic;
    const Registered_Class *class_ref;
    void *user_data;
    uint32_t style;
} GUI_Window_Handle;

static GUI_Context g_gui_context;
static Registered_Class g_registered_classes[16];
static uint32_t g_registered_class_count = 0;
static int g_gui_initialized = 0;

static size_t gui_strlen(const char *str) {
    return str ? strlen(str) : 0U;
}

static int gui_strcmp(const char *lhs, const char *rhs) {
    if (!lhs || !rhs) {
        return -1;
    }
    return strcmp(lhs, rhs);
}

static Registered_Class *find_registered_class(const char *name) {
    uint32_t i;
    for (i = 0; i < g_registered_class_count; ++i) {
        if (gui_strcmp(g_registered_classes[i].name, name) == 0) {
            return &g_registered_classes[i];
        }
    }
    return NULL;
}

static GUI_Window_Handle *as_window_handle(HWND hwnd) {
    GUI_Window_Handle *handle = (GUI_Window_Handle *)hwnd;
    return (handle && handle->magic == 0xAE474849U) ? handle : NULL;
}

int GUI_Initialize(void) {
    memset(&g_gui_context, 0, sizeof(g_gui_context));
    memset(g_registered_classes, 0, sizeof(g_registered_classes));
    g_registered_class_count = 0;
    g_gui_initialized = 1;
    g_gui_context.initialized = 1;
    return 0;
}

void GUI_Shutdown(void) {
    g_gui_initialized = 0;
    memset(&g_gui_context, 0, sizeof(g_gui_context));
    memset(g_registered_classes, 0, sizeof(g_registered_classes));
    g_registered_class_count = 0;
}

uint16_t GUI_RegisterClass(const WNDCLASS *wnd_class) {
    Registered_Class *slot;
    if (!g_gui_initialized || !wnd_class || !wnd_class->lpszClassName || !wnd_class->lpfnWndProc) {
        return 0;
    }
    if (g_registered_class_count >= 16U) {
        return 0;
    }
    slot = &g_registered_classes[g_registered_class_count];
    strncpy(slot->name, wnd_class->lpszClassName, sizeof(slot->name) - 1U);
    slot->wnd_proc = wnd_class->lpfnWndProc;
    slot->icon = wnd_class->hIcon;
    slot->cursor = wnd_class->hCursor;
    slot->background = wnd_class->hbrBackground;
    slot->style = wnd_class->style;
    g_registered_class_count++;
    return (uint16_t)g_registered_class_count;
}

HWND GUI_CreateWindow(const char *class_name,
                      const char *window_name,
                      uint32_t style,
                      int32_t x, int32_t y,
                      uint32_t width, uint32_t height,
                      HWND parent, void *menu,
                      HINSTANCE instance, void *param) {
    GUI_Window_Handle *window;
    Registered_Class *registered_class;
    (void)window_name;
    (void)x;
    (void)y;
    (void)parent;
    (void)menu;
    (void)instance;

    if (!g_gui_initialized || !class_name) {
        return NULL;
    }
    registered_class = find_registered_class(class_name);
    if (!registered_class) {
        return NULL;
    }
    window = (GUI_Window_Handle *)calloc(1U, sizeof(*window));
    if (!window) {
        return NULL;
    }
    window->magic = 0xAE474849U;
    window->class_ref = registered_class;
    window->user_data = param;
    window->style = style;
    g_gui_context.main_window = (HWND)window;
    g_gui_context.window_width = width;
    g_gui_context.window_height = height;
    if (registered_class->wnd_proc) {
        registered_class->wnd_proc((HWND)window, WM_CREATE, 0, (LPARAM)param);
    }
    return (HWND)window;
}

int GUI_DestroyWindow(HWND hwnd) {
    GUI_Window_Handle *window = as_window_handle(hwnd);
    if (!window) {
        return 0;
    }
    if (window->class_ref && window->class_ref->wnd_proc) {
        window->class_ref->wnd_proc(hwnd, WM_DESTROY, 0, 0);
    }
    if (g_gui_context.main_window == hwnd) {
        g_gui_context.main_window = NULL;
    }
    free(window);
    return 1;
}

int GUI_ShowWindow(HWND hwnd, int32_t cmd_show) {
    (void)cmd_show;
    return as_window_handle(hwnd) ? 1 : 0;
}

void GUI_UpdateWindow(HWND hwnd) {
    GUI_Window_Handle *window = as_window_handle(hwnd);
    if (window && window->class_ref && window->class_ref->wnd_proc) {
        window->class_ref->wnd_proc(hwnd, WM_PAINT, 0, 0);
    }
}

int GUI_GetMessage(MSG *msg, HWND hwnd, UINT min_filter, UINT max_filter) {
    MSG *queued;
    if (!msg) {
        return 0;
    }
    memset(msg, 0, sizeof(*msg));
    while (g_gui_context.queue_head != g_gui_context.queue_tail) {
        queued = &g_gui_context.message_queue[g_gui_context.queue_head];
        g_gui_context.queue_head = (g_gui_context.queue_head + 1U) % 256U;
        if (hwnd && queued->hwnd != hwnd) {
            continue;
        }
        if (min_filter && queued->message < min_filter) {
            continue;
        }
        if (max_filter && queued->message > max_filter) {
            continue;
        }
        memcpy(msg, queued, sizeof(*msg));
        return msg->message == WM_QUIT ? 0 : 1;
    }
    return 0;
}

void GUI_PostMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    uint32_t next;
    MSG *slot;
    if (!as_window_handle(hwnd)) {
        return;
    }
    next = (g_gui_context.queue_tail + 1U) % 256U;
    if (next == g_gui_context.queue_head) {
        return;
    }
    slot = &g_gui_context.message_queue[g_gui_context.queue_tail];
    memset(slot, 0, sizeof(*slot));
    slot->hwnd = hwnd;
    slot->message = msg;
    slot->wParam = wparam;
    slot->lParam = lparam;
    g_gui_context.queue_tail = next;
}

void GUI_DispatchMessage(const MSG *msg) {
    GUI_Window_Handle *window;
    if (!msg) {
        return;
    }
    window = as_window_handle(msg->hwnd);
    if (window && window->class_ref && window->class_ref->wnd_proc) {
        window->class_ref->wnd_proc(msg->hwnd, msg->message, msg->wParam, msg->lParam);
    }
}

HDC GUI_GetDC(HWND hwnd) {
    return as_window_handle(hwnd) ? hwnd : NULL;
}

int GUI_ReleaseDC(HWND hwnd, HDC hdc) {
    return (as_window_handle(hwnd) && hdc == hwnd) ? 1 : 0;
}

int GUI_InitializeD3D12(HWND hwnd, D3D12_Context *context) {
    if (!as_window_handle(hwnd) || !context) {
        return -1;
    }
    memset(context, 0, sizeof(*context));
    context->width = g_gui_context.window_width;
    context->height = g_gui_context.window_height;
    context->format = 87U;
    return 0;
}

void GUI_BeginFrame(D3D12_Context *context) {
    (void)context;
}

void GUI_EndFrame(D3D12_Context *context) {
    (void)context;
}

void GUI_ClearRenderTarget(D3D12_Context *context, uint32_t color) {
    (void)context;
    (void)color;
}

void GUI_DrawPrimitives(D3D12_Context *context,
                        const void *vertex_buffer, uint32_t vertex_count,
                        const void *index_buffer, uint32_t index_count) {
    (void)context;
    (void)vertex_buffer;
    (void)vertex_count;
    (void)index_buffer;
    (void)index_count;
}

void GUI_CleanupD3D12(D3D12_Context *context) {
    if (context) {
        memset(context, 0, sizeof(*context));
    }
}

HMENU GUI_CreatePopupMenu(void) {
    return (HMENU)calloc(1U, sizeof(uint64_t));
}

int GUI_AddMenuItem(HMENU menu, uint32_t id, const char *text) {
    (void)id;
    return (menu && text && gui_strlen(text) > 0U) ? 0 : -1;
}

int GUI_TrackPopupMenu(HMENU menu, uint32_t flags, int x, int y, HWND hwnd) {
    (void)flags;
    (void)x;
    (void)y;
    return (menu && as_window_handle(hwnd)) ? 0 : -1;
}

GUI_Context *GUI_GetContext(void) {
    return g_gui_initialized ? &g_gui_context : NULL;
}

void GUI_DumpState(void) {
    fprintf(stderr, "GUI initialized=%d main_window=%p queue=(%u,%u)\n",
            g_gui_initialized,
            g_gui_context.main_window,
            g_gui_context.queue_head,
            g_gui_context.queue_tail);
}
