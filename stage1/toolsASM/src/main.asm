; =============================================================================
; AethelOS Assembly Orchestration - Main Entry Point
; toolsASM/src/main.asm
;
; Dispatcher and integration point for binary emission functions
; This file coordinates between C compiler and assembly emission layer
; =============================================================================

; Import binary emission functions
extern _write_aki_binary
extern _write_srv_binary
extern _write_hda_binary
extern _write_aetb_binary
extern _write_pe32plus_efi

; C runtime (if needed for future extensions)
extern _malloc
extern _free

section .data

; Dispatch table for format emitters
; Maps target type to emitter function
emit_dispatch:
    dq 0                        ; Index 0: invalid
    dq _write_aki_binary        ; Index 1: TARGET_AKI
    dq _write_srv_binary        ; Index 2: TARGET_SRV
    dq _write_hda_binary        ; Index 3: TARGET_HDA
    dq _write_aetb_binary       ; Index 4: TARGET_AETB
    dq 0                        ; Index 5: TARGET_IYA (not used)
    dq _write_pe32plus_efi      ; Index 6: TARGET_PE32PLUS_EFI

section .text

global _emit_binary

; ============================================================================
; FUNCTION: _emit_binary
;
; Main dispatcher for binary emission
; Receives a Blueprint and routes to appropriate emitter based on target type
;
; Parameters:
;   rdi = Aethel_Blueprint* (from C compiler)
;   rsi = const char* output_filename
;   rdx = target_type (override from blueprint if needed)
;
; Returns:
;   rax = 0 on success, -1 on error
; ============================================================================
_emit_binary:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    
    push rbx
    push r12
    
    ; Validate blueprint pointer
    test rdi, rdi
    jz .emit_error_null_blueprint
    
    ; Load target type from blueprint
    ; Assuming first field at offset 0 is target_type
    mov r12d, [rdi]             ; Load target type into r12d
    
    ; Validate target type (1-4 are valid)
    cmp r12d, 1
    jl .emit_error_invalid_type
    cmp r12d, 4
    jg .emit_error_invalid_type
    
    ; Look up emitter function in dispatch table
    lea rax, [rel emit_dispatch]
    mov rax, [rax + r12*8]      ; Get function pointer
    
    test rax, rax
    jz .emit_error_invalid_type
    
    ; Call appropriate emitter
    ; rdi = Blueprint (already in rdi)
    ; rsi = filename (already in rsi)
    ; rdx = flags (caller's rdx)
    call rax
    
    ; rax now contains result
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret
    
.emit_error_null_blueprint:
    mov eax, -1
    pop rbx
    pop r12
    mov rsp, rbp
    pop rbp
    ret
    
.emit_error_invalid_type:
    mov eax, -1
    pop rbx
    pop r12
    mov rsp, rbp
    pop rbp
    ret

