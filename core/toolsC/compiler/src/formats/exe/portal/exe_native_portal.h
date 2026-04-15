/*
 * =========================================================================
 * Aethelium EXE Native Portal System - Windows NT Direct Kernel Mapping
 * =========================================================================
 * 
 * File: exe_native_portal.h
 * Purpose: NT system call direct mapping without POSIX/CRT wrappers
 *          Portal Pool management for dynamic kernel API binding
 *          Zero-wrapper architecture for maximum efficiency
 * 
 * Design Principles:
 *   1. Direct NT API access (e.g., NtWriteFile, NtAllocateVirtualMemory)
 *   2. Zero POSIX semantics (no sleep(), no malloc(), no pthread_create())
 *   3. Portal Pool - runtime binding of kernel functions
 *   4. Strike Logic - reject invalid POSIX calls at compile time
 *   5. Callee-saved register usage (R12-R15 reserved for Aethelium)
 *   6. No COM IUnknown, no reference counting - direct vtable access
 * 
 * The Portal Pool manages function pointers retrieved via PEB scanning
 * at process startup, enabling fast kernel API dispatch from ActFlow code.
 * 
 * Register Preservation (Windows x64 ABI):
 *   - R12-R15: Callee-saved, reserved for Aethelium global state
 *   - R12: Global ActFlow Context base
 *   - R13: MirrorState base (constant metadata)
 *   - R14: TEB pointer (Thread Environment Block)
 *   - R15: SIMD state masks
 * 
 * =========================================================================
 */

#ifndef AETHELIUM_EXE_NATIVE_PORTAL_H
#define AETHELIUM_EXE_NATIVE_PORTAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
   NT Status Codes
   ========================================================================= */

#define NT_STATUS_SUCCESS                   0x00000000L
#define NT_STATUS_INVALID_PARAMETER         0xC000000DL
#define NT_STATUS_NOT_IMPLEMENTED           0xC0000002L
#define NT_STATUS_NO_MEMORY                 0xC0000017L
#define NT_STATUS_BUFFER_TOO_SMALL          0xC0000023L
#define NT_STATUS_END_OF_FILE               0xC0000011L
#define NT_STATUS_ABANDONED_WAIT_0          0x00000080L
#define NT_STATUS_TIMEOUT                   0x00000102L
#define NT_STATUS_PENDING                   0x00000103L
#define NT_STATUS_NTSTATUS_FROM_WIN32(x)    ((x) ? (0xC0070000L | (x)) : 0)

/* =========================================================================
   File Access and Creation Constants
   ========================================================================= */

/* File access rights */
#define FILE_READ_DATA                      0x00000001
#define FILE_WRITE_DATA                     0x00000002
#define FILE_APPEND_DATA                    0x00000004
#define FILE_READ_EA                        0x00000008
#define FILE_WRITE_EA                       0x00000010
#define FILE_EXECUTE                        0x00000020
#define FILE_DELETE_CHILD                   0x00000040
#define FILE_READ_ATTRIBUTES                0x00000080
#define FILE_WRITE_ATTRIBUTES               0x00000100

/* File creation disposition */
#define FILE_SUPERSEDE                      0x00000000
#define FILE_OPEN                           0x00000001
#define FILE_CREATE                         0x00000002
#define FILE_OPEN_IF                        0x00000003
#define FILE_OVERWRITE                      0x00000004
#define FILE_OVERWRITE_IF                   0x00000005

/* File open options */
#define FILE_DIRECTORY_FILE                 0x00000001
#define FILE_WRITE_THROUGH                  0x00000002
#define FILE_SEQUENTIAL_ONLY                0x00000004
#define FILE_NO_INTERMEDIATE_BUFFERING      0x00000008
#define FILE_SYNCHRONOUS_IO_ALERT           0x00000010
#define FILE_SYNCHRONOUS_IO_NONALERT        0x00000020
#define FILE_NON_DIRECTORY_FILE             0x00000040
#define FILE_CREATE_TREE_CONNECTION         0x00000080
#define FILE_COMPLETE_IF_OPLOCKED           0x00000100
#define FILE_NO_EA_KNOWLEDGE                0x00000200
#define FILE_OPEN_REPARSE_POINT             0x00000200
#define FILE_DELETE_ON_CLOSE                0x00001000
#define FILE_OPEN_BY_FILE_ID                0x00002000
#define FILE_OPEN_FOR_BACKUP_INTENT         0x00004000
#define FILE_NO_COMPRESSION                 0x00008000
#define FILE_RESERVE_OPFILTER               0x00100000
#define FILE_OPEN_OFFLINE_FILE              0x00200000
#define FILE_OPEN_FOR_FREE_SPACE_QUERY      0x00800000

/* Standard device handles */
#define STD_INPUT_HANDLE                    ((uint32_t)-11)
#define STD_OUTPUT_HANDLE                   ((uint32_t)-12)
#define STD_ERROR_HANDLE                    ((uint32_t)-13)

/* =========================================================================
   Memory Management Constants
   ========================================================================= */

/* Memory allocation types */
#define MEM_COMMIT                          0x00001000
#define MEM_RESERVE                         0x00002000
#define MEM_RESET                           0x00080000
#define MEM_RESET_UNDO                      0x01000000
#define MEM_DECOMMIT                        0x00004000
#define MEM_RELEASE                         0x00008000
#define MEM_LARGE_PAGES                     0x20000000
#define MEM_PHYSICAL                        0x00400000
#define MEM_TOP_DOWN                        0x00100000
#define MEM_WRITE_WATCH                     0x00200000

/* Memory protection flags */
#define PAGE_NOACCESS                       0x00000001
#define PAGE_READONLY                       0x00000002
#define PAGE_READWRITE                      0x00000004
#define PAGE_WRITECOPY                      0x00000008
#define PAGE_EXECUTE                        0x00000010
#define PAGE_EXECUTE_READ                   0x00000020
#define PAGE_EXECUTE_READWRITE              0x00000040
#define PAGE_EXECUTE_WRITECOPY              0x00000080
#define PAGE_GUARD                          0x00000100
#define PAGE_NOCACHE                        0x00000200
#define PAGE_WRITECOMBINE                   0x00000400
#define PAGE_ENCLAVE_THREAD_CONTROL         0x80000000

/* =========================================================================
   Process and Thread Constants
   ========================================================================= */

/* Process access rights */
#define PROCESS_TERMINATE                   0x00000001
#define PROCESS_CREATE_THREAD               0x00000002
#define PROCESS_SET_SESSIONID               0x00000004
#define PROCESS_VM_OPERATION                0x00000008
#define PROCESS_VM_READ                     0x00000010
#define PROCESS_VM_WRITE                    0x00000020
#define PROCESS_DUP_HANDLE                  0x00000040
#define PROCESS_CREATE_PROCESS              0x00000080
#define PROCESS_SET_QUOTA                   0x00000100
#define PROCESS_SET_INFORMATION             0x00000200
#define PROCESS_QUERY_INFORMATION           0x00000400
#define PROCESS_SUSPEND_RESUME              0x00000800
#define PROCESS_QUERY_LIMITED_INFORMATION   0x00001000

/* Thread access rights */
#define THREAD_TERMINATE                    0x00000001
#define THREAD_SUSPEND_RESUME               0x00000002
#define THREAD_GET_CONTEXT                  0x00000008
#define THREAD_SET_CONTEXT                  0x00000010
#define THREAD_QUERY_INFORMATION            0x00000040
#define THREAD_SET_INFORMATION              0x00000020
#define THREAD_SET_THREAD_TOKEN             0x00000080
#define THREAD_IMPERSONATE                  0x00000100
#define THREAD_DIRECT_IMPERSONATION         0x00000200
#define THREAD_SET_LIMITED_INFORMATION      0x00000400
#define THREAD_QUERY_LIMITED_INFORMATION    0x00000800

/* Thread creation flags */
#define THREAD_EXECUTE_IMMEDIATELY          0x00000000
#define THREAD_CREATE_SUSPENDED             0x00000001

/* =========================================================================
   Synchronization Constants
   ========================================================================= */

/* Object attribute flags */
#define OBJ_INHERIT                         0x00000001L
#define OBJ_PERMANENT                       0x00000010L
#define OBJ_EXCLUSIVE                       0x00000020L
#define OBJ_CASE_INSENSITIVE                0x00000040L
#define OBJ_OPENIF                          0x00000080L
#define OBJ_OPENLINK                        0x00000100L
#define OBJ_VALID_ATTRIBUTES                0x000001F2L

/* Event types */
#define EVENT_ALL_ACCESS                    0x001F0003L
#define EVENT_MODIFY_STATE                  0x00000002L
#define EVENT_QUERY_STATE                   0x00000001L

/* Semaphore */
#define SEMAPHORE_ALL_ACCESS                0x001F0003L
#define SEMAPHORE_MODIFY_STATE              0x00000002L
#define SEMAPHORE_QUERY_STATE               0x00000001L

/* Mutex */
#define MUTANT_ALL_ACCESS                   0x001F0001L
#define MUTANT_QUERY_STATE                  0x00000001L

/* Timer */
#define TIMER_ALL_ACCESS                    0x001F0003L
#define TIMER_MODIFY_STATE                  0x00000002L
#define TIMER_QUERY_STATE                   0x00000001L

/* Wait objects */
#define WAIT_OBJECT_0                       0x00000000L
#define WAIT_ABANDONED_0                    0x00000080L
#define WAIT_TIMEOUT                        0x00000102L
#define WAIT_FAILED                         ((uint32_t)-1L)

/* =========================================================================
   NT Data Structures
   ========================================================================= */

/* Unicode String (NT API standard) */
typedef struct {
    uint16_t length;                    /* String length in bytes (not including null) */
    uint16_t maximum_length;            /* Maximum length of buffer */
    uint16_t *buffer;                   /* UTF-16LE string buffer */
} NT_Unicode_String;

/* I/O Status Block - Required for all async I/O */
typedef struct {
    union {
        uint32_t status;                /* Completion status code (NT_STATUS_*) */
        void *pointer;
    } status_or_pointer;
    uint64_t information;               /* Bytes transferred or error code */
} NT_IO_Status_Block;

/* Object Attributes - Used when opening files/events/mutexes */
typedef struct {
    uint32_t length;                    /* Structure size */
    void *root_directory;               /* Base directory handle (NULL for absolute) */
    NT_Unicode_String *object_name;     /* Object name to open */
    uint32_t attributes;                /* OBJ_* flags */
    void *security_descriptor;          /* Security info (usually NULL) */
    void *security_quality_of_service;  /* QoS info (usually NULL) */
} NT_Object_Attributes;

/* =========================================================================
   Portal Pool Registry - Function Pointer Storage
   
   These slots store dynamically resolved kernel API addresses.
   Filled during process initialization via PEB/Export Table scanning.
   ========================================================================= */

typedef struct {
    /* ===== Memory Management ===== */
    void *nt_allocate_virtual_memory;       /* NtAllocateVirtualMemory */
    void *nt_allocate_virtual_memory_ex;    /* NtAllocateVirtualMemoryEx (ex version) */
    void *nt_free_virtual_memory;           /* NtFreeVirtualMemory */
    void *nt_protect_virtual_memory;        /* NtProtectVirtualMemory */
    void *nt_query_virtual_memory;          /* NtQueryVirtualMemory */
    void *nt_lock_virtual_memory;           /* NtLockVirtualMemory */
    void *nt_unlock_virtual_memory;         /* NtUnlockVirtualMemory */
    
    /* ===== Process Management ===== */
    void *nt_create_process_ex;             /* NtCreateProcessEx */
    void *nt_create_user_process;           /* NtCreateUserProcess */
    void *nt_open_process;                  /* NtOpenProcess */
    void *nt_terminate_process;             /* NtTerminateProcess */
    void *nt_query_information_process;     /* NtQueryInformationProcess */
    void *nt_set_information_process;       /* NtSetInformationProcess */
    void *nt_get_context_thread;            /* NtGetContextThread */
    void *nt_set_context_thread;            /* NtSetContextThread */
    
    /* ===== Thread Management ===== */
    void *nt_create_thread_ex;              /* NtCreateThreadEx */
    void *nt_open_thread;                   /* NtOpenThread */
    void *nt_terminate_thread;              /* NtTerminateThread */
    void *nt_suspend_thread;                /* NtSuspendThread */
    void *nt_resume_thread;                 /* NtResumeThread */
    void *nt_query_information_thread;      /* NtQueryInformationThread */
    void *nt_set_information_thread;        /* NtSetInformationThread */
    
    /* ===== File I/O ===== */
    void *nt_create_file;                   /* NtCreateFile */
    void *nt_open_file;                     /* NtOpenFile */
    void *nt_query_file;                    /* NtQueryInformationFile */
    void *nt_set_file_info;                 /* NtSetInformationFile */
    void *nt_read_file;                     /* NtReadFile */
    void *nt_write_file;                    /* NtWriteFile */
    void *nt_flush_buffers_file;            /* NtFlushBuffersFile */
    void *nt_close;                         /* NtClose */
    void *nt_fsctl_file;                    /* NtFsControlFile */
    void *nt_device_ioctl_file;             /* NtDeviceIoControlFile */
    
    /* ===== Console I/O ===== */
    void *nt_read_console;                  /* NtReadConsole */
    void *nt_write_console;                 /* NtWriteConsole */
    void *nt_get_std_handle;                /* NtGetStdHandle / GetStdHandle */
    void *nt_set_std_handle;                /* NtSetStdHandle / SetStdHandle */
    
    /* ===== Synchronization ===== */
    void *nt_create_event;                  /* NtCreateEvent */
    void *nt_open_event;                    /* NtOpenEvent */
    void *nt_set_event;                     /* NtSetEvent */
    void *nt_reset_event;                   /* NtResetEvent */
    void *nt_pulse_event;                   /* NtPulseEvent */
    void *nt_create_semaphore;              /* NtCreateSemaphore */
    void *nt_open_semaphore;                /* NtOpenSemaphore */
    void *nt_release_semaphore;             /* NtReleaseSemaphore */
    void *nt_create_mutex;                  /* NtCreateMutant */
    void *nt_open_mutex;                    /* NtOpenMutant */
    void *nt_release_mutex;                 /* NtReleaseMutant */
    void *nt_wait_for_single_object;        /* NtWaitForSingleObject */
    void *nt_wait_for_multiple_objects;     /* NtWaitForMultipleObjects */
    
    /* ===== Timer Operations ===== */
    void *nt_create_timer;                  /* NtCreateTimer */
    void *nt_open_timer;                    /* NtOpenTimer */
    void *nt_set_timer;                     /* NtSetTimer */
    void *nt_cancel_timer;                  /* NtCancelTimer */
    void *nt_query_timer;                   /* NtQueryTimer */
    
    /* ===== Time Operations ===== */
    void *nt_query_system_time;             /* NtQuerySystemTime */
    void *nt_query_performance_counter;     /* NtQueryPerformanceCounter */
    void *nt_delay_execution;               /* NtDelayExecution */
    
    /* ===== Registry Operations ===== */
    void *nt_open_key;                      /* NtOpenKey */
    void *nt_create_key;                    /* NtCreateKey */
    void *nt_query_value_key;               /* NtQueryValueKey */
    void *nt_set_value_key;                 /* NtSetValueKey */
    void *nt_enum_key;                      /* NtEnumerateKey */
    void *nt_enum_value_key;                /* NtEnumerateValueKey */
    void *nt_delete_key;                    /* NtDeleteKey */
    void *nt_delete_value_key;              /* NtDeleteValueKey */
    void *nt_close_key;                     /* NtClose (for key handles) */
    
    /* ===== I/O Completion Ports ===== */
    void *nt_create_io_completion_port;     /* NtCreateIoCompletionPort */
    void *nt_set_io_completion;             /* NtSetIoCompletion */
    void *nt_remove_io_completion;          /* NtRemoveIoCompletion */
    void *nt_remove_io_completion_ex;       /* NtRemoveIoCompletionEx */
    
    /* ===== Exception Handling ===== */
    void *nt_raise_exception;               /* NtRaiseException */
    void *nt_continue_ex;                   /* NtContinueEx */
    
    /* ===== Security ===== */
    void *nt_impersonate_thread;            /* NtImpersonateThread */
    void *nt_query_security_object;         /* NtQuerySecurityObject */
    
    /* ===== System Information ===== */
    void *nt_query_system_information;      /* NtQuerySystemInformation */
    void *nt_set_system_information;        /* NtSetSystemInformation */
    
    /* ===== Dynamic String Operations ===== */
    void *rtl_init_unicode_string;          /* RtlInitUnicodeString */
    void *rtl_unicode_string_to_ansi_string;/* RtlUnicodeStringToAnsiString */
    void *rtl_ansi_string_to_unicode_string;/* RtlAnsiStringToUnicodeString */
    void *rtl_free_ansi_string;             /* RtlFreeAnsiString */
    void *rtl_free_unicode_string;          /* RtlFreeUnicodeString */
    
} Portal_Pool;

/* =========================================================================
   Context Information Structures
   ========================================================================= */

/* Thread Environment Block (TEB) - Access via FS:[0x30] (x64) */
typedef struct {
    uint8_t reserved0[0x30];
    void *peb;                          /* Pointer to Process Environment Block */
    uint64_t client_id[2];              /* Thread and Process IDs */
    void *tls_storage;                  /* Thread Local Storage */
    uint8_t reserved1[0xB0];
} NT_TEB;

/* Process Environment Block (PEB) - Accessed via TEB->PEB */
typedef struct {
    uint8_t inherited_address_space;    /* Non-zero if inherited */
    uint8_t read_image_file_exec_options;
    uint8_t being_debugged;
    uint8_t bit_field;
    uint8_t reserved0[4];
    void *mutant;                       /* Process-wide mutex */
    void *image_base;                   /* Base address of executable image */
    void *ldr;                          /* Loader data */
    void *process_parameters;
    void *sub_system_data;
    void *heap;                         /* Process heap handle */
    uint8_t reserved1[0x100];
} NT_PEB;

/* Loader Data (for manual DLL loading) */
typedef struct {
    uint32_t length;
    uint32_t initialized;
    void *ss_handle;
    void *load_order_module_list;       /* LIST_ENTRY */
    void *memory_order_module_list;     /* LIST_ENTRY */
    void *initialization_order_module_list; /* LIST_ENTRY */
} NT_PEB_LDR_DATA;

/* =========================================================================
   Portal Container - Aggregates Portal Pool and context
   ========================================================================= */

typedef struct {
    Portal_Pool pool;                   /* All kernel function pointers */
    
    /* Runtime context */
    NT_TEB *teb;                        /* Thread Environment Block */
    NT_PEB *peb;                        /* Process Environment Block */
    
    /* Initialization state */
    int initialized;                    /* Non-zero if portal is ready */
    
    /* Thread-local state */
    uint64_t last_error;                /* Last NT status code */
    
} Portal_Context;

/* =========================================================================
   Aethelium Global State - Register R12-R15 Reservation
   ========================================================================= */

typedef struct {
    /* R12: Global ActFlow context base */
    Portal_Context *portal_context;
    
    /* R13: Mirror state base (constant metadata) */
    void *mirror_state_base;
    
    /* R14: Current Thread Environment Block */
    NT_TEB *current_teb;
    
    /* R15: SIMD operation state masks */
    uint64_t simd_state_mask;
    uint64_t mxcsr_state;
    
    /* Additional global state */
    void *allocation_context;           /* Memory allocator context */
    void *thread_pool_context;          /* Thread pool manager */
    
} Aethelium_Global_State;

/* =========================================================================
   Portal Initialization and Management
   ========================================================================= */

/* Initialize Portal Pool by scanning NTDLL exports and performing DLL binding */
int Portal_Initialize(Portal_Context *ctx);
int Portal_Bootstrap(void);
int Portal_VerifyBindings(void);
void Portal_DisplayInfo(void);

/* Retrieve Portal Pool for NT API function access */
Portal_Pool *Portal_GetPool(Portal_Context *ctx);

/* Get current portal context (usually stored in R12 during execution) */
Portal_Context *Portal_GetContext(void);

/* Set global Aethelium state (usually during bootstrapping) */
int Portal_SetGlobalState(Aethelium_Global_State *state);

/* Get global Aethelium state */
Aethelium_Global_State *Portal_GetGlobalState(void);

/* Resolve a portal slot offset by exported NT symbol name. */
uint32_t Portal_GetSlotOffsetByName(const char *function_name);

/* =========================================================================
   Kernel Function Dispatch Macros
   
   These macros enable direct kernel API calls with proper ABI compliance.
   All functions follow Windows x64 calling convention:
     - First 4 params: RCX, RDX, R8, R9
     - Additional params: Stack (RSP + 0x20, 0x28, etc)
     - Return: RAX
     - Caller-saved: RAX, RCX, RDX, R8-R11, XMM0-XMM5
     - Callee-saved: RBX, RBP, RDI, RSI, RSP, R12-R15
   ========================================================================= */

/* Call NT function with 1 parameter */
#define NT_CALL_1(pool, func_ptr, p1) \
    ((typeof(func_ptr))(pool)->func_ptr)((uint64_t)(p1))

/* Call NT function with 2 parameters */
#define NT_CALL_2(pool, func_ptr, p1, p2) \
    ((typeof(func_ptr))(pool)->func_ptr)((uint64_t)(p1), (uint64_t)(p2))

/* Call NT function with 3 parameters */
#define NT_CALL_3(pool, func_ptr, p1, p2, p3) \
    ((typeof(func_ptr))(pool)->func_ptr)((uint64_t)(p1), (uint64_t)(p2), (uint64_t)(p3))

/* =========================================================================
   Strike Logic - Compile-time rejection of invalid POSIX calls
   
   These functions are intentionally UNDEFINED to cause linker errors
   when user code tries to call them. This ensures POSIX wrappers
   are never accidentally linked into Aethelium executables.
   ========================================================================= */

/* FORBIDDEN: Standard C library functions that depend on CRT */
typedef void (*STRIKE_sleep_forbidden)(unsigned);
typedef void *(*STRIKE_malloc_forbidden)(size_t);
typedef void *(*STRIKE_calloc_forbidden)(size_t, size_t);
typedef void *(*STRIKE_realloc_forbidden)(void *, size_t);
typedef void (*STRIKE_free_forbidden)(void *);
typedef int (*STRIKE_pthread_create_forbidden)(void *, const void *, 
                                                void *(*)(void *), void *);

/* These are declared but NOT defined, causing linker errors in the emitted
   Windows-native program when the strike layer is enabled by that build. */
#ifdef AETHELIUM_EXE_ENABLE_STRIKE_DECLS
extern STRIKE_sleep_forbidden sleep;                /* Use NtDelayExecution */
extern STRIKE_malloc_forbidden malloc;              /* Use NtAllocateVirtualMemory */
extern STRIKE_calloc_forbidden calloc;              /* Use NtAllocateVirtualMemory */
extern STRIKE_realloc_forbidden realloc;            /* Use NtAllocateVirtualMemory */
extern STRIKE_free_forbidden free;                  /* Use NtFreeVirtualMemory */
extern STRIKE_pthread_create_forbidden pthread_create; /* Use NtCreateThreadEx */
#endif

#ifdef __cplusplus
}
#endif

#endif /* AETHELIUM_EXE_NATIVE_PORTAL_H */
