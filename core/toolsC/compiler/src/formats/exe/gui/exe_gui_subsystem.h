/*
 * =========================================================================
 * Aethelium EXE GUI Subsystem - User32/D3D12 Direct Integration
 * =========================================================================
 * 
 * File: exe_gui_subsystem.h
 * Purpose: Complete implementation of GUI infrastructure
 *          User32.dll window management
 *          D3D12 rendering without COM wrapper overhead
 *          Message dispatch with ActFlow event integration
 *          Window procedure delegation and event routing
 * 
 * Implementation Notes:
 *   1. User32 functions are resolved from Portal Pool
 *   2. Window Procedure uses __stdcall ABI
 *   3. D3D12 accessed via virtual table offset-based binding
 *   4. Message queue integrated with IOCP
 *   5. No COM IUnknown, no reference counting
 *   6. Direct Win32 API calls, zero wrapper overhead
 * 
 * =========================================================================
 */

#ifndef AETHELIUM_EXE_GUI_SUBSYSTEM_H
#define AETHELIUM_EXE_GUI_SUBSYSTEM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
   Win32 Type Definitions (Minimal Required Types)
   ========================================================================= */

/* Window Handle */
typedef void *HWND;

/* Device Context Handle */
typedef void *HDC;

/* Instance Handle */
typedef void *HINSTANCE;

/* Menu Handle */
typedef void *HMENU;

/* Device Handle */
typedef void *HDEVICE;

/* Brush Handle */
typedef void *HBRUSH;

/* Icon Handle */
typedef void *HICON;

/* Cursor Handle */
typedef void *HCURSOR;

/* Integer types for Win32 parameter passing */
typedef uint32_t UINT;
typedef int32_t INT;
typedef uint64_t WPARAM;
typedef int64_t LPARAM;
typedef int64_t LRESULT;

/* Window message identifiers */
#define WM_CREATE                   0x0001
#define WM_DESTROY                  0x0002
#define WM_MOVE                     0x0003
#define WM_SIZE                     0x0005
#define WM_ACTIVATE                 0x0006
#define WM_SETFOCUS                 0x0007
#define WM_KILLFOCUS                0x0008
#define WM_PAINT                    0x000F
#define WM_CLOSE                    0x0010
#define WM_QUIT                     0x0012
#define WM_QUERYENDSESSION          0x0011
#define WM_ERASEBKGND               0x0014
#define WM_SHOWWINDOW               0x0018
#define WM_DRAWITEM                 0x002B
#define WM_KEYDOWN                  0x0100
#define WM_KEYUP                    0x0101
#define WM_CHAR                     0x0102
#define WM_LBUTTONDOWN              0x0201
#define WM_LBUTTONUP                0x0202
#define WM_RBUTTONDOWN              0x0204
#define WM_RBUTTONUP                0x0205
#define WM_MOUSEMOVE                0x0200
#define WM_TIMER                    0x0113
#define WM_SYSCOMMAND               0x0112

/* ShowWindow commands */
#define SW_HIDE                     0
#define SW_SHOWNORMAL               1
#define SW_SHOW                     5
#define SW_MAXIMIZE                 3
#define SW_MINIMIZE                 6

/* Window styles */
#define WS_OVERLAPPED               0x00000000
#define WS_POPUP                    0x80000000
#define WS_CHILD                    0x40000000
#define WS_MINIMIZE                 0x20000000
#define WS_VISIBLE                  0x10000000
#define WS_DISABLED                 0x08000000
#define WS_CLIPCHILDREN             0x02000000
#define WS_CLIPSIBLINGS             0x04000000
#define WS_CAPTION                  0x00C00000
#define WS_BORDER                   0x00800000
#define WS_DLGFRAME                 0x00400000
#define WS_VSCROLL                  0x00200000
#define WS_HSCROLL                  0x00100000
#define WS_SYSMENU                  0x00080000
#define WS_THICKFRAME               0x00040000
#define WS_GROUP                    0x00020000
#define WS_TABSTOP                  0x00010000
#define WS_MINIMIZEBOX              0x00020000
#define WS_MAXIMIZEBOX              0x00010000
#define WS_OVERLAPPEDWINDOW         (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)

/* Extended window styles */
#define WS_EX_DLGMODALFRAME         0x00000001
#define WS_EX_NOPARENTNOTIFY        0x00000004
#define WS_EX_TOPMOST               0x00000008
#define WS_EX_ACCEPTFILES           0x00000010
#define WS_EX_TRANSPARENT           0x00000020
#define WS_EX_MDICHILD              0x00000040
#define WS_EX_TOOLWINDOW            0x00000080
#define WS_EX_WINDOWEDGE            0x00000100
#define WS_EX_CLIENTEDGE            0x00000200
#define WS_EX_CONTEXTHELP           0x00000400
#define WS_EX_RIGHT                 0x00001000
#define WS_EX_LEFT                  0x00000000

/* Class styles */
#define CS_VREDRAW                  0x0001
#define CS_HREDRAW                  0x0002
#define CS_DBLCLKS                  0x0008
#define CS_OWNDC                    0x0020
#define CS_CLASSDC                  0x0040
#define CS_PARENTDC                 0x0080
#define CS_KEYCVTWINDOW             0x0004
#define CS_NOCLOSE                  0x0200
#define CS_SAVEBITS                 0x0800
#define CS_BYTEALIGNCLIENT          0x0100
#define CS_BYTEALIGNWINDOW          0x0200
#define CS_GLOBALCLASS              0x4000

/* Color indices */
#define COLOR_WINDOW                5
#define COLOR_BACKGROUND            1
#define COLOR_SCROLLBAR             0
#define COLOR_BTNFACE               15
#define COLOR_BTNSHADOW             16

/* GetDC flags */
#define DCX_WINDOW                  0x00000001
#define DCX_CACHE                   0x00000002
#define DCX_NORESETATTRS            0x00000004
#define DCX_CLIPCHILDREN            0x00000008
#define DCX_CLIPSIBLINGS            0x00000010
#define DCX_PARENTCLIP              0x00000020
#define DCX_EXCLUDERGN              0x00000040
#define DCX_INTERSECTRGN            0x00000080
#define DCX_EXSAVE                  0x00000200
#define DCX_SAVEDVISrgn             0x00004000

/* =========================================================================
   Window Class Structure
   ========================================================================= */

typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct {
    UINT style;                         /* Class style */
    WNDPROC lpfnWndProc;                /* Window procedure address */
    int cbClsExtra;                     /* Class memory (-1 for dynamic) */
    int cbWndExtra;                     /* Window memory (-1 for dynamic) */
    HINSTANCE hInstance;                /* Instance handle */
    HICON hIcon;                        /* Icon handle */
    HCURSOR hCursor;                    /* Cursor handle */
    HBRUSH hbrBackground;               /* Background brush */
    const char *lpszMenuName;           /* Menu name */
    const char *lpszClassName;          /* Class name */
} WNDCLASS;

/* =========================================================================
   Message Structure
   ========================================================================= */

typedef struct {
    HWND hwnd;                          /* Window handle */
    UINT message;                       /* Message ID */
    WPARAM wParam;                      /* Word parameter */
    LPARAM lParam;                      /* Long parameter */
    uint32_t time;                      /* Message time */
    struct {
        int32_t x;
        int32_t y;
    } pt;                               /* Cursor position */
    uint32_t private;                   /* Private data */
} MSG;

/* =========================================================================
   Paint Structure
   ========================================================================= */

typedef struct {
    HDC hdc;                            /* Device context */
    int fErase;                         /* Background erased flag */
    struct {
        int32_t left;
        int32_t top;
        int32_t right;
        int32_t bottom;
    } rcPaint;                          /* Paint rectangle */
    int fRestore;                       /* Restore flag */
    int fIncUpdate;                     /* Incremental update */
    uint8_t rgbReserved[32];            /* Reserved */
} PAINTSTRUCT;

/* =========================================================================
   D3D12 Resource Types
   ========================================================================= */

/* D3D12 Device */
typedef void *ID3D12Device;

/* D3D12 Command Queue */
typedef void *ID3D12CommandQueue;

/* D3D12 Swap Chain */
typedef void *IDXGISwapChain;

/* D3D12 Command List */
typedef void *ID3D12GraphicsCommandList;

/* D3D12 Render Target View Descriptor */
typedef void *D3D12_CPU_DESCRIPTOR_HANDLE;

/* D3D12 Render Target Context */
typedef struct {
    uint32_t width;                     /* Window width */
    uint32_t height;                    /* Window height */
    uint32_t format;                    /* DXGI format */
    
    /* D3D12 resources - raw pointers, no COM reference counting */
    void *device;                       /* ID3D12Device */
    void *command_queue;                /* ID3D12CommandQueue */
    void *swap_chain;                   /* IDXGISwapChain */
    void *command_list;                 /* ID3D12GraphicsCommandList */
    
    /* Render targets */
    void *render_targets[2];            /* Double buffering */
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[2];
    
    /* Descriptor heap */
    void *descriptor_heap;              /* ID3D12DescriptorHeap */
    uint32_t rtv_descriptor_size;
    
    /* Synchronization */
    void *fence;                        /* ID3D12Fence */
    uint64_t fence_value;
    void *fence_event;                  /* Windows event for sync */
    
} D3D12_Context;

/* =========================================================================
   GUI Context - Central Management Structure
   ========================================================================= */

typedef struct {
    /* Main window */
    HWND main_window;
    uint32_t window_width;
    uint32_t window_height;
    
    /* Message queue */
    MSG message_queue[256];
    uint32_t queue_head;
    uint32_t queue_tail;
    
    /* Graphics context */
    D3D12_Context d3d12_context;
    
    /* Input state */
    uint32_t keyboard_state[256];       /* Key down states */
    struct {
        int32_t x;
        int32_t y;
    } mouse_position;
    uint32_t mouse_buttons;
    
    /* Window registry */
    HWND registered_windows[64];
    uint32_t window_count;
    
    /* Initialization state */
    int initialized;
    
} GUI_Context;

/* =========================================================================
   GUI Initialization and Management
   ========================================================================= */

/* Initialize GUI subsystem */
int GUI_Initialize(void);

/* Shutdown GUI subsystem */
void GUI_Shutdown(void);

/* Register window class */
uint16_t GUI_RegisterClass(const WNDCLASS *wnd_class);

/* Create window instance */
HWND GUI_CreateWindow(const char *class_name,
                      const char *window_name,
                      uint32_t style,
                      int32_t x, int32_t y,
                      uint32_t width, uint32_t height,
                      HWND parent, void *menu,
                      HINSTANCE instance, void *param);

/* Destroy window */
int GUI_DestroyWindow(HWND hwnd);

/* Show window */
int GUI_ShowWindow(HWND hwnd, int32_t cmd_show);

/* Update window */
void GUI_UpdateWindow(HWND hwnd);

/* =========================================================================
   Message Management
   ========================================================================= */

/* Get next message from queue */
int GUI_GetMessage(MSG *msg, HWND hwnd, UINT min_filter, UINT max_filter);

/* Post message to queue */
void GUI_PostMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

/* Dispatch message to window procedure */
void GUI_DispatchMessage(const MSG *msg);

/* =========================================================================
   Device Context Operations
   ========================================================================= */

/* Get device context for window */
HDC GUI_GetDC(HWND hwnd);

/* Release device context */
int GUI_ReleaseDC(HWND hwnd, HDC hdc);

/* =========================================================================
   D3D12 Graphics Operations
   ========================================================================= */

/* Initialize D3D12 for rendering */
int GUI_InitializeD3D12(HWND hwnd, D3D12_Context *context);

/* Begin frame rendering */
void GUI_BeginFrame(D3D12_Context *context);

/* End frame and present */
void GUI_EndFrame(D3D12_Context *context);

/* Clear render target */
void GUI_ClearRenderTarget(D3D12_Context *context, uint32_t color);

/* Draw primitives (triangles, lines, etc) */
void GUI_DrawPrimitives(D3D12_Context *context,
                        const void *vertex_buffer, uint32_t vertex_count,
                        const void *index_buffer, uint32_t index_count);

/* Cleanup D3D12 resources */
void GUI_CleanupD3D12(D3D12_Context *context);

/* =========================================================================
   Menu and Dialog Support (Minimal)
   ========================================================================= */

/* Create popup menu */
HMENU GUI_CreatePopupMenu(void);

/* Add menu item */
int GUI_AddMenuItem(HMENU menu, uint32_t id, const char *text);

/* Show context menu */
int GUI_TrackPopupMenu(HMENU menu, uint32_t flags, int x, int y, HWND hwnd);

/* =========================================================================
   Utility Functions
   ========================================================================= */

/* Get GUI context (for advanced usage) */
GUI_Context *GUI_GetContext(void);

/* Dump GUI state (for debugging) */
void GUI_DumpState(void);

#ifdef __cplusplus
}
#endif

#endif /* AETHELIUM_EXE_GUI_SUBSYSTEM_H */
