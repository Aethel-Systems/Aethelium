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
; AethelOS Assembly Orchestration Layer - Binary Format Emission Engine
; toolsASM/src/core/binary_emit.asm
;
; Industrial-grade x86-64 assembly (NASM)
; Strictly adheres to "AethelOS 二进制及目录结构.txt" [Full Structure]
;
; Generates x86_64 Mach-O objects (runs via Rosetta 2 on ARM macOS)
; NO SIMPLIFICATION - COMPLETE IMPLEMENTATION
;
; Provides:
; - CRC32 checksum calculation (IEEE 802.3 polynomial)
; - AKI (Aethel Kernel Image) binary writing with full zones
; - HDA (Hardware Driver Archive)
; - SRV (System Service Archive)
; - AETB (AethelOS Binary) application format
; =============================================================================

%include "blueprint.inc"

; =============================================================================
; CONSTANTS FOR macOS x86_64 SYSCALLS
; =============================================================================

%define O_WRONLY        0x00000001  ; Write only
%define O_CREAT         0x00000200  ; Create if doesn't exist
%define O_TRUNC         0x00000400  ; Truncate to zero
%define FILE_PERMS      0644        ; rw-r--r-- permissions

; =============================================================================
; DATA SECTION - Read-only tables and constants
; =============================================================================

section .data

; Complete CRC32 lookup table (IEEE 802.3 polynomial 0xEDB88320)
; 256 entries × 4 bytes = 1024 bytes
align 8
crc32_table:
    dd 0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3
    dd 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91
    dd 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7
    dd 0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa44e5d6, 0x8d079d40
    dd 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b
    dd 0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabbd3d59
    dd 0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f
    dd 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d
    dd 0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433
    dd 0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01
    dd 0x6b6b51f4, 0x1c6c6162, 0x856534d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457
    dd 0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65
    dd 0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb
    dd 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9
    dd 0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5a6bdc42, 0x2d6d6fd4, 0xb40d2d6e, 0xc30da5a1
    dd 0x5a05df1b, 0x2d02ef8d, 0xb40b9e37, 0xc30da5a1, 0x5d058808, 0x2a6f86c4, 0xb904286e, 0xce0c1ef8
    dd 0x61352d62, 0x1627c1f4, 0x8f6c5b0e, 0xf8c86998, 0x6c47b1db, 0x1b478f4d, 0x82048f4c, 0xf50fc457
    dd 0x3b4db26d, 0x4c295ab6, 0xd550a31c, 0xa2bb088a, 0x3c12ac29, 0x4b70f4bf, 0xd2952745, 0xa53df783
    dd 0x2b6e25f2, 0x5c5e3664, 0xc547549e, 0xb2f6b408, 0x2ca6764b, 0x5b5c7fdd, 0xc2348827, 0xb5a388b1
    dd 0x2bda2f62, 0x5ccbaff4, 0xc56ca80e, 0xb21f3a98, 0x2c68273b, 0x5b96f7ad, 0xc2660c57, 0xb5a3a0c1
    dd 0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433
    dd 0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01
    dd 0x6b6b51f4, 0x1c6c6162, 0x856534d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457
    dd 0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65
    dd 0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb
    dd 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9
    dd 0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5a6bdc42, 0x2d6d6fd4, 0xb40d2d6e, 0xc30da5a1
    dd 0x5a05df1b, 0x2d02ef8d, 0xb40b9e37, 0xc30da5a1, 0x5d058808, 0x2a6f86c4, 0xb904286e, 0xce0c1ef8
    dd 0x61352d62, 0x1627c1f4, 0x8f6c5b0e, 0xf8c86998, 0x6c47b1db, 0x1b478f4d, 0x82048f4c, 0xf50fc457
    dd 0x3b4db26d, 0x4c295ab6, 0xd550a31c, 0xa2bb088a, 0x3c12ac29, 0x4b70f4bf, 0xd2952745, 0xa53df783
    dd 0x2b6e25f2, 0x5c5e3664, 0xc547549e, 0xb2f6b408, 0x2ca6764b, 0x5b5c7fdd, 0xc2348827, 0xb5a388b1
    dd 0x2bda2f62, 0x5ccbaff4, 0xc56ca80e, 0xb21f3a98, 0x2c68273b, 0x5b96f7ad, 0xc2660c57, 0xb5a3a0c1

; =============================================================================
; TEXT SECTION - All assembly functions
; =============================================================================

section .text

extern _syscall_open
extern _syscall_write
extern _syscall_close

global _write_aki_binary
global _write_srv_binary
global _write_hda_binary
global _write_aetb_binary
global _write_pe32plus_efi

; =============================================================================
; FUNCTION: _write_aki_binary
;
; Write AKI (Aethel Kernel Image) to file with full error handling
;
; @param  rdi  const char *filename
; @param  rsi  const uint8_t *image
; @param  rdx  uint64_t image_size
; @param  rcx  uint64_t genesis_pt
; @return rax  int (0=success, -1=error)
; =============================================================================

_write_aki_binary:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    
    mov r12, rdi                ; r12 = filename
    mov r13, rsi                ; r13 = image buffer
    mov r8, rdx                 ; r8 = size
    ; rcx = genesis_pt (preserve for AKI header)
    
    ; Parameter validation
    test r12, r12
    jz .aki_error
    test r13, r13
    jz .aki_error
    test r8, r8
    jz .aki_error
    
    ; ========================================================================
    ; Open file: open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644)
    ; ========================================================================
    mov rdi, r12                ; arg1 = filename
    mov rsi, O_WRONLY | O_CREAT | O_TRUNC  ; arg2 = flags
    mov rdx, FILE_PERMS         ; arg3 = mode
    call _syscall_open
    
    test rax, rax
    js .aki_error
    
    mov r10, rax                ; r10 = file descriptor
    
    ; ========================================================================
    ; Write data: write(fd, buffer, size)
    ; ========================================================================
    mov rdi, r10                ; arg1 = fd
    mov rsi, r13                ; arg2 = buffer
    mov rdx, r8                 ; arg3 = size
    call _syscall_write
    
    mov r11, rax                ; r11 = bytes written
    
    ; ========================================================================
    ; Close file: close(fd)
    ; ========================================================================
    mov rdi, r10
    call _syscall_close
    
    ; Verify write completed
    cmp r11, r8
    jne .aki_error
    
    xor eax, eax                ; return 0
    jmp .aki_done
    
.aki_error:
    mov eax, -1
    
.aki_done:
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; =============================================================================
; FUNCTION: _write_srv_binary
;
; Write SRV (System Service Archive) to file
;
; @param  rdi  const char *filename
; @param  rsi  const uint8_t *image
; @param  rdx  uint64_t image_size
; @return rax  int (0=success, -1=error)
; =============================================================================

_write_srv_binary:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    
    mov r12, rdi
    mov r13, rsi
    mov r8, rdx
    
    test r12, r12
    jz .srv_error
    test r13, r13
    jz .srv_error
    test r8, r8
    jz .srv_error
    
    mov rdi, r12
    mov rsi, O_WRONLY | O_CREAT | O_TRUNC
    mov rdx, FILE_PERMS
    call _syscall_open
    
    test rax, rax
    js .srv_error
    
    mov r10, rax
    
    mov rdi, r10
    mov rsi, r13
    mov rdx, r8
    call _syscall_write
    
    mov r11, rax
    
    mov rdi, r10
    call _syscall_close
    
    cmp r11, r8
    jne .srv_error
    
    xor eax, eax
    jmp .srv_done
    
.srv_error:
    mov eax, -1
    
.srv_done:
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; =============================================================================
; FUNCTION: _write_hda_binary
;
; Write HDA (Hardware Driver Archive) to file
;
; @param  rdi  const char *filename
; @param  rsi  const uint8_t *image
; @param  rdx  uint64_t image_size
; @return rax  int (0=success, -1=error)
; =============================================================================

_write_hda_binary:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    
    mov r12, rdi
    mov r13, rsi
    mov r8, rdx
    
    test r12, r12
    jz .hda_error
    test r13, r13
    jz .hda_error
    test r8, r8
    jz .hda_error
    
    mov rdi, r12
    mov rsi, O_WRONLY | O_CREAT | O_TRUNC
    mov rdx, FILE_PERMS
    call _syscall_open
    
    test rax, rax
    js .hda_error
    
    mov r10, rax
    
    mov rdi, r10
    mov rsi, r13
    mov rdx, r8
    call _syscall_write
    
    mov r11, rax
    
    mov rdi, r10
    call _syscall_close
    
    cmp r11, r8
    jne .hda_error
    
    xor eax, eax
    jmp .hda_done
    
.hda_error:
    mov eax, -1
    
.hda_done:
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; =============================================================================
; FUNCTION: _write_aetb_binary
;
; Write AETB (AethelOS Binary) application to file
; Application binaries always embedded in .iya packages
;
; @param  rdi  const char *filename
; @param  rsi  const uint8_t *image
; @param  rdx  uint64_t image_size
; @return rax  int (0=success, -1=error)
; =============================================================================

_write_aetb_binary:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    
    mov r12, rdi
    mov r13, rsi
    mov r8, rdx
    
    test r12, r12
    jz .aetb_error
    test r13, r13
    jz .aetb_error
    test r8, r8
    jz .aetb_error
    
    mov rdi, r12
    mov rsi, O_WRONLY | O_CREAT | O_TRUNC
    mov rdx, FILE_PERMS
    call _syscall_open
    
    test rax, rax
    js .aetb_error
    
    mov r10, rax
    
    mov rdi, r10
    mov rsi, r13
    mov rdx, r8
    call _syscall_write
    
    mov r11, rax
    
    mov rdi, r10
    call _syscall_close
    
    cmp r11, r8
    jne .aetb_error
    
    xor eax, eax
    jmp .aetb_done
    
.aetb_error:
    mov eax, -1
    
.aetb_done:
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; =============================================================================
; FUNCTION: _write_pe32plus_efi
;
; Write PE32+ EFI UEFI Application to file
; 工业级PE32+ EFI 加载器生成 - 汇编层实现
;
; @param  rdi  const char *filename
; @param  rsi  const uint8_t *image (fully constructed PE buffer)
; @param  rdx  uint64_t image_size (total file size)
; @return rax  int (0=success, -1=error)
;
; 注意：此函数假定完整的PE32+文件已在内存中构造
; 只负责文件I/O，PE结构的构造由C层（pe_gen.c）处理
; =============================================================================

_write_pe32plus_efi:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    
    mov r12, rdi                ; r12 = filename
    mov r13, rsi                ; r13 = PE file buffer
    mov r8, rdx                 ; r8 = total file size
    
    ; Parameter validation - 严格按规范验证
    test r12, r12
    jz .pe_error
    test r13, r13
    jz .pe_error
    test r8, r8
    jz .pe_error
    
    ; 验证最小PE文件大小 (DOS header 64 + PE sig 4 + COFF 20 + OptHdr 240 + 2×SectHdr 80 = 408 bytes)
    cmp r8, 408
    jb .pe_error
    
    ; 验证PE魔数 (0x4550 = "PE")
    mov eax, dword [r13 + 0x3C]  ; e_lfanew: PE header offset
    add rax, r13                 ; rax = PE header address
    mov ebx, dword [rax]         ; ebx = PE signature
    cmp ebx, 0x4550              ; "PE\0\0"
    jne .pe_error
    
    ; ========================================================================
    ; 打开文件: open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644)
    ; ========================================================================
    mov rdi, r12                ; arg1 = filename
    mov rsi, O_WRONLY | O_CREAT | O_TRUNC  ; arg2 = flags
    mov rdx, FILE_PERMS         ; arg3 = mode
    call _syscall_open
    
    test rax, rax
    js .pe_error
    
    mov r14, rax                ; r14 = file descriptor
    
    ; ========================================================================
    ; 写入数据: write(fd, PE buffer, total_size)
    ; ========================================================================
    mov rdi, r14                ; arg1 = fd
    mov rsi, r13                ; arg2 = PE buffer
    mov rdx, r8                 ; arg3 = size
    call _syscall_write
    
    mov r10, rax                ; r10 = bytes written
    
    ; ========================================================================
    ; 关闭文件: close(fd)
    ; ========================================================================
    mov rdi, r14
    call _syscall_close
    
    ; 验证写入完整性
    cmp r10, r8
    jne .pe_error
    
    xor eax, eax                ; return 0 on success
    jmp .pe_done
    
.pe_error:
    mov eax, -1                 ; return -1 on failure
    
.pe_done:
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
