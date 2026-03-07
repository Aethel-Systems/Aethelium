; Copyright (C) 2024-2026 Aethel-Systems. All rights reserved.
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program. If not, see <https://www.gnu.org/licenses/>.

; =============================================================================
; AethelOS Assembly - Windows OS Abstraction Layer
; toolsASM/src/core/os_windows.asm
;
; Windows forbids direct raw syscalls for user-space portability and stability.
; This module implements the POSIX-like wrappers used by the emitters via
; kernel32 imports (CreateFileA / WriteFile / CloseHandle).
;
; Build format: NASM -f win64 (x86_64 COFF)
; =============================================================================

bits 64
default rel

%define O_RDONLY                0x0000
%define O_WRONLY                0x0001
%define O_RDWR                  0x0002
%define O_CREAT                 0x0200
%define O_TRUNC                 0x0400
%define O_EXCL                  0x0800

%define GENERIC_READ            0x80000000
%define GENERIC_WRITE           0x40000000
%define FILE_SHARE_READ         0x00000001
%define FILE_SHARE_WRITE        0x00000002
%define OPEN_EXISTING           3
%define CREATE_NEW              1
%define CREATE_ALWAYS           2
%define OPEN_ALWAYS             4
%define TRUNCATE_EXISTING       5
%define FILE_ATTRIBUTE_NORMAL   0x00000080
%define INVALID_HANDLE_VALUE    -1

section .text

extern CreateFileA
extern WriteFile
extern CloseHandle

global _syscall_open
global _syscall_write
global _syscall_close
global _memcpy_safe
global _memset_safe

; ============================================================================
; syscall_open
; Parameters (SysV ABI): rdi=path, rsi=flags, rdx=mode
; Returns: rax=handle (or -1 on error)
; ============================================================================
_syscall_open:
    test rdi, rdi
    jz .open_error

    mov rax, rdi
    mov r10, rsi

    ; Map POSIX-like flags to Win32 desired access.
    mov edx, GENERIC_READ
    test r10, O_RDWR
    jnz .open_access_rw
    test r10, O_WRONLY
    jz .open_access_done
    mov edx, GENERIC_WRITE
    jmp .open_access_done
.open_access_rw:
    mov edx, GENERIC_READ | GENERIC_WRITE
.open_access_done:

    ; Map create/truncate flags to creation disposition.
    mov r11d, OPEN_EXISTING
    test r10, O_CREAT
    jz .open_no_creat
    test r10, O_EXCL
    jnz .open_disp_create_new
    test r10, O_TRUNC
    jnz .open_disp_create_always
    mov r11d, OPEN_ALWAYS
    jmp .open_disp_done
.open_no_creat:
    test r10, O_TRUNC
    jnz .open_disp_truncate_existing
    jmp .open_disp_done
.open_disp_create_new:
    mov r11d, CREATE_NEW
    jmp .open_disp_done
.open_disp_create_always:
    mov r11d, CREATE_ALWAYS
    jmp .open_disp_done
.open_disp_truncate_existing:
    mov r11d, TRUNCATE_EXISTING
.open_disp_done:

    ; Win64 call ABI bridge: CreateFileA(path, access, share, sa, disp, attr, tpl)
    sub rsp, 56
    mov rcx, rax
    mov r8d, FILE_SHARE_READ | FILE_SHARE_WRITE
    xor r9d, r9d
    mov qword [rsp + 32], 0
    mov dword [rsp + 40], r11d
    mov dword [rsp + 48], FILE_ATTRIBUTE_NORMAL
    call CreateFileA
    add rsp, 56

    cmp rax, INVALID_HANDLE_VALUE
    je .open_error
    ret

.open_error:
    mov rax, -1
    ret

; ============================================================================
; syscall_write
; Parameters (SysV ABI): rdi=handle, rsi=buf, rdx=count
; Returns: rax=bytes_written (or -1 on error)
; ============================================================================
_syscall_write:
    cmp rdi, INVALID_HANDLE_VALUE
    je .write_error
    test rdx, rdx
    jz .write_zero
    test rsi, rsi
    jz .write_error
    mov r11, rdx
    shr r11, 32
    jnz .write_error

    ; WriteFile(handle, buf, (DWORD)count, &written, NULL)
    sub rsp, 56
    mov dword [rsp + 48], 0

    mov rcx, rdi
    mov r11, rdx
    mov rdx, rsi
    mov r8d, r11d
    lea r9, [rsp + 48]
    mov qword [rsp + 32], 0
    call WriteFile
    test eax, eax
    jz .write_fail

    mov eax, dword [rsp + 48]
    add rsp, 56
    ret

.write_fail:
    add rsp, 56
.write_error:
    mov rax, -1
    ret

.write_zero:
    xor eax, eax
    ret

; ============================================================================
; syscall_close
; Parameters (SysV ABI): rdi=handle
; Returns: rax=0 on success, -1 on error
; ============================================================================
_syscall_close:
    cmp rdi, INVALID_HANDLE_VALUE
    je .close_error

    sub rsp, 40
    mov rcx, rdi
    call CloseHandle
    add rsp, 40

    test eax, eax
    jz .close_error
    xor eax, eax
    ret

.close_error:
    mov eax, -1
    ret

; ============================================================================
; memcpy_safe
; Parameters (SysV ABI): rdi=dest, rsi=src, rdx=count
; ============================================================================
_memcpy_safe:
    push rdi
    push rsi
    push rdx

    test rdx, rdx
    jz .memcpy_done

    mov rax, rdx
    shr rax, 3
    mov rcx, rax
    rep movsq

    mov rax, rdx
    and rax, 7
    mov rcx, rax
    rep movsb

.memcpy_done:
    pop rdx
    pop rsi
    pop rdi
    ret

; ============================================================================
; memset_safe
; Parameters (SysV ABI): rdi=dest, rsi=value(byte), rdx=count
; ============================================================================
_memset_safe:
    mov al, sil
    mov rcx, rdx
    rep stosb
    ret
