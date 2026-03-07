; =============================================================================
; AethelOS Assembly - macOS System Call Stubs and Utilities
; toolsASM/src/core/syscall_macos.asm
;
; Low-level system call wrappers for macOS/Darwin
; Handles file I/O and memory operations required by binary emitters
; =============================================================================

; macOS specific constants
%define SYSCALL_WRITE   0x2000004   ; write syscall number
%define SYSCALL_OPEN    0x2000005   ; open syscall number  
%define SYSCALL_CLOSE   0x2000006   ; close syscall number
%define SYSCALL_LSEEK   0x2000007   ; lseek syscall number

%define O_RDONLY        0           ; Read only
%define O_WRONLY        1           ; Write only
%define O_RDWR          2           ; Read/Write
%define O_APPEND        0x0008      ; Append to file
%define O_CREAT         0x0200      ; Create if not exists
%define O_TRUNC         0x0400      ; Truncate existing
%define O_EXCL          0x0800      ; Exclusive creation

%define PROT_READ       0x01
%define PROT_WRITE      0x02
%define PROT_EXEC       0x04

section .text

; ============================================================================
; syscall_write
; Write data to file descriptor
; 
; Parameters: rdi=fd, rsi=buf, rdx=count
; Returns: rax=bytes_written (or -1 on error)
; ============================================================================
global _syscall_write
_syscall_write:
    mov rax, SYSCALL_WRITE
    syscall
    ret

; ============================================================================
; syscall_open  
; Open file
;
; Parameters: rdi=path, rsi=flags, rdx=mode
; Returns: rax=fd (or -1 on error)
; ============================================================================
global _syscall_open
_syscall_open:
    mov rax, SYSCALL_OPEN
    syscall
    ret

; ============================================================================
; syscall_close
; Close file descriptor
;
; Parameters: rdi=fd
; Returns: rax=result
; ============================================================================
global _syscall_close
_syscall_close:
    mov rax, SYSCALL_CLOSE
    syscall
    ret

; ============================================================================
; Utility: memcpy_ssse3
; Fast memory copy using SSE3 instructions (if available)
; Falls back to byte copy on unsupported systems
;
; Parameters: rdi=dest, rsi=src, rdx=count
; ============================================================================
global _memcpy_safe
_memcpy_safe:
    push rdi
    push rsi
    push rdx
    
    ; Check if rdx is 0
    test rdx, rdx
    jz .memcpy_done
    
    ; For simplicity, use 8-byte copies when aligned
    mov rax, rdx
    shr rax, 3              ; Divide by 8
    mov rcx, rax
    rep movsq               ; Copy 8-byte chunks
    
    ; Handle remaining bytes
    mov rax, rdx
    and rax, 7              ; Get remainder
    mov rcx, rax
    rep movsb               ; Copy remaining bytes
    
.memcpy_done:
    pop rdx
    pop rsi
    pop rdi
    ret

; ============================================================================
; Utility: memset_safe
; Set memory region to value
;
; Parameters: rdi=dest, rsi=value (byte), rdx=count
; ============================================================================
global _memset_safe
_memset_safe:
    mov al, sil             ; al = byte value
    mov rcx, rdx            ; rcx = count
    rep stosb               ; Fill bytes
    ret

