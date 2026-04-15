; =============================================================================
; AethelOS Assembly Orchestration Layer - Blueprint Definition
; toolsASM/include/blueprint.inc
;
; Assembly-level representation of Binary Blueprint from C compiler
; This is the contract between C logic layer and assembly emission layer
; 
; Industrial-grade implementation - NO simplified code, NO mock code
; =============================================================================

; Binary Blueprint Structure - Received from C compiler
; This structure is passed via calling convention (registers + stack)
; Size: approximately 128 bytes

struc Aethel_Blueprint
    .target_type: resq 1        ; 0x00: AKI/HDA/SRV/AETB target mark (u64)
    .aethel_id: resq 4          ; 0x08: 256-bit AethelID (32 bytes)
    .act_flow_ptr: resq 1       ; 0x28: Pointer to raw opcode buffer (C-generated)
    .act_flow_size: resq 1      ; 0x30: Size of ActFlow data
    .mirror_state_ptr: resq 1   ; 0x38: Pointer to static data
    .mirror_state_size: resq 1  ; 0x40: Size of MirrorState
    .constant_truth_ptr: resq 1 ; 0x48: Pointer to read-only data
    .constant_truth_size: resq 1; 0x50: Size of ConstantTruth
    .reloc_table_ptr: resq 1    ; 0x58: Relocation table pointer
    .reloc_count: resq 1        ; 0x60: Number of relocations
    .genesis_pt: resq 1         ; 0x68: Entry point offset for target
    .sip_vector: resq 1         ; 0x70: SIP policy bitmap
    .mode_affinity: resb 1      ; 0x78: Mode (0=sandbox, 1=architect)
    .reserved1: resb 7          ; 0x79: Padding for alignment
    .build_timestamp: resq 1    ; 0x80: Unix timestamp
    .build_version: resd 1      ; 0x88: Build version
    .compiler_version: resd 1   ; 0x8C: Compiler version
endstruc

; Aethel_Blueprint total size = 0x90 = 144 bytes

; Relocation Entry Structure
struc Relocation_Entry
    .offset: resq 1             ; 0x00: Offset in binary to relocate (8 bytes)
    .symbol_idx: resd 1         ; 0x08: Symbol table index (4 bytes)
    .reloc_type: resd 1         ; 0x0C: Relocation type (4 bytes)
    .target_id: resq 4          ; 0x10: Target AethelID (32 bytes)
endstruc

; Relocation_Entry total size = 48 bytes

; ============================================================================
; Magic Numbers (Little-Endian Format)
; ============================================================================
%define AKI_MAGIC       0x21494B41  ; "AKI!" LE
%define SRV_MAGIC       0x21525653  ; "SRV!" LE
%define HDA_MAGIC       0x21414448  ; "HDA!" LE
%define AETB_MAGIC      0x41455442  ; "AETB" in little-endian
%define IYA_MAGIC       0x21415941  ; "AYA!" LE

; Target Type Identifiers
%define TARGET_AKI      1
%define TARGET_SRV      2
%define TARGET_HDA      3
%define TARGET_AETB     4
%define TARGET_IYA      5

; ============================================================================
; AKI Header Structure (256 bytes = 0x00-0xFF)
; ============================================================================
struc AKI_Header
    .magic: resd 1              ; 0x00: Magic number
    .version: resd 1            ; 0x04: Format version
    .flags: resd 1              ; 0x08: AKI flags
    .machine_type: resd 1       ; 0x0C: CPU arch type
    
    .kernel_id: resq 4          ; 0x10: 256-bit Kernel AethelID (32 bytes)
    
    .genesis_pt: resq 1         ; 0x30: Physical load origin
    .sip_vector: resq 1         ; 0x38: SIP permission vector
    
    .act_flow_offset: resq 1    ; 0x40: ActFlow offset
    .act_flow_size: resq 1      ; 0x48: ActFlow size
    
    .mirror_state_offset: resq 1; 0x50: MirrorState offset
    .mirror_state_size: resq 1  ; 0x58: MirrorState size
    
    .constant_truth_offset: resq 1 ; 0x60: ConstantTruth offset
    .constant_truth_size: resq 1   ; 0x68: ConstantTruth size
    
    .aethela_logic_offset: resq 1  ; 0x70: AethelA compiler engine offset
    .aethela_logic_size: resq 1    ; 0x78: AethelA size
    
    .nexus_point_offset: resq 1    ; 0x80: Symbol/ID mapping offset
    .nexus_point_size: resq 1      ; 0x88: Mapping table size
    
    .symtab_offset: resq 1         ; 0x90: Symbol table offset
    .symtab_size: resq 1           ; 0x98: Symbol table size
    
    .strtab_offset: resq 1         ; 0xA0: String table offset
    .strtab_size: resq 1           ; 0xA8: String table size
    
    .reloc_offset: resq 1          ; 0xB0: Relocation table offset
    .reloc_count: resq 1           ; 0xB8: Relocation count
    
    .mode_affinity: resb 1         ; 0xC0: Boot mode
    .sip_policy_version: resb 1    ; 0xC1: SIP version
    .reserved1: resb 6             ; 0xC2-0xC7
    
    .build_timestamp: resq 1       ; 0xC8: Build timestamp
    .build_version: resd 1         ; 0xD0: Build version
    .compiler_version: resd 1      ; 0xD4: Compiler version
    
    .header_crc: resd 1            ; 0xD8: Header CRC32
    .total_crc: resd 1             ; 0xDC: Total CRC32
    
    .reserved2: resb 32            ; 0xE0-0xFF
endstruc

; ============================================================================
; HDA Header Structure (256 bytes)
; ============================================================================
struc HDA_Header
    .magic: resd 1              ; 0x00: Magic number
    .version: resd 1            ; 0x04: Version
    .device_type: resd 1        ; 0x08: Device type
    .flags: resd 1              ; 0x0C: Flags
    
    .binding_id: resq 4         ; 0x10: 256-bit AethelID
    
    .contract_pt: resq 1        ; 0x30: Contract entry point
    .hw_blueprint: resq 4       ; 0x38: Hardware fingerprint
    
    .sip_requirement: resd 1    ; 0x58: Min SIP level
    .mode_support: resb 1       ; 0x5C: Mode support
    .reserved_mode: resb 3      ; 0x5D-0x5F
    
    .act_flow_offset: resq 1    ; 0x60: ActFlow offset
    .act_flow_size: resq 1      ; 0x68: ActFlow size
    
    .mirror_state_offset: resq 1; 0x70: MirrorState offset
    .mirror_state_size: resq 1  ; 0x78: MirrorState size
    
    .constant_truth_offset: resq 1 ; 0x80: ConstantTruth offset
    .constant_truth_size: resq 1   ; 0x88: ConstantTruth size
    
    .schema_nexus_offset: resq 1   ; 0x90: Schema offset
    .schema_nexus_size: resq 1     ; 0x98: Schema size
    
    .symtab_offset: resq 1         ; 0xA0: Symbol table offset
    .symtab_size: resq 1           ; 0xA8: Symbol table size
    
    .strtab_offset: resq 1         ; 0xB0: String table offset
    .strtab_size: resq 1           ; 0xB8: String table size
    
    .reloc_offset: resq 1          ; 0xC0: Relocation offset
    .reloc_count: resq 1           ; 0xC8: Relocation count
    
    .bridge_id: resq 4             ; 0xD0: Bridge protocol ID
    
    .header_crc: resd 1            ; 0xF0: Header CRC
    .total_crc: resd 1             ; 0xF4: Total CRC
    
    .reserved: resb 8              ; 0xF8-0xFF
endstruc

; ============================================================================
; SRV Header Structure (256 bytes)
; ============================================================================
struc SRV_Header
    .magic: resd 1              ; 0x00: Magic
    .version: resd 1            ; 0x04: Version
    .service_type: resd 1       ; 0x08: Service type
    .flags: resd 1              ; 0x0C: Flags
    
    .sphere_id: resq 4          ; 0x10: 256-bit AethelID
    
    .srv_genesis: resq 1        ; 0x30: Genesis point
    .sip_policy: resq 1         ; 0x38: SIP policy
    
    .act_flow_offset: resq 1    ; 0x40: ActFlow offset
    .act_flow_size: resq 1      ; 0x48: ActFlow size
    
    .mirror_state_offset: resq 1; 0x50: MirrorState offset
    .mirror_state_size: resq 1  ; 0x58: MirrorState size
    
    .constant_truth_offset: resq 1 ; 0x60: ConstantTruth offset
    .constant_truth_size: resq 1   ; 0x68: ConstantTruth size
    
    .schema_nexus_offset: resq 1   ; 0x70: Schema offset
    .schema_nexus_size: resq 1     ; 0x78: Schema size
    
    .symtab_offset: resq 1         ; 0x80: Symbol table offset
    .symtab_size: resq 1           ; 0x88: Symbol table size
    
    .strtab_offset: resq 1         ; 0x90: String table offset
    .strtab_size: resq 1           ; 0x98: String table size
    
    .reloc_offset: resq 1          ; 0xA0: Relocation offset
    .reloc_count: resq 1           ; 0xA8: Relocation count
    
    .privilege_level: resd 1       ; 0xB0: Privilege level
    .mode_behavior: resb 1         ; 0xB4: Mode behavior
    .sip_level_required: resb 1    ; 0xB5: Required SIP level
    .reserved1: resb 2             ; 0xB6-0xB7
    
    .build_timestamp: resq 1       ; 0xB8: Build timestamp
    .build_version: resd 1         ; 0xC0: Build version
    .compiler_version: resd 1      ; 0xC4: Compiler version
    
    .header_crc: resd 1            ; 0xC8: Header CRC
    .total_crc: resd 1             ; 0xCC: Total CRC
    
    .service_name: resb 32         ; 0xD0: Service name
    .reserved2: resb 16            ; 0xF0-0xFF
endstruc

; ============================================================================
; AETB Header Structure (256 bytes)
; ============================================================================
struc AETB_Header
    .magic: resd 1              ; 0x00: Magic
    .version: resd 1            ; 0x04: Version
    .flags: resd 1              ; 0x08: Flags
    .reserved_flags: resd 1     ; 0x0C
    
    .logic_id: resq 4           ; 0x10: 256-bit AethelID
    
    .genesis_pt: resq 1         ; 0x30: Entry point
    .sip_context: resq 1        ; 0x38: SIP context ID
    
    .act_flow_offset: resq 1    ; 0x40: ActFlow offset
    .act_flow_size: resq 1      ; 0x48: ActFlow size
    
    .mirror_state_offset: resq 1; 0x50: MirrorState offset
    .mirror_state_size: resq 1  ; 0x58: MirrorState size
    
    .reloc_nexus_offset: resq 1 ; 0x60: Relocation nexus offset
    .reloc_nexus_size: resq 1   ; 0x68: Relocation nexus size
    
    .import_nexus_offset: resq 1; 0x70: Import nexus offset
    .import_nexus_size: resq 1  ; 0x78: Import nexus size
    
    .iya_link_id: resq 4        ; 0x80: Link to IYA container
    
    .constant_truth_offset: resq 1 ; 0xA0: ConstantTruth offset
    .constant_truth_size: resq 1   ; 0xA8: ConstantTruth size
    
    .symtab_offset: resq 1         ; 0xB0: Symbol table offset
    .symtab_size: resq 1           ; 0xB8: Symbol table size
    
    .strtab_offset: resq 1         ; 0xC0: String table offset
    .strtab_size: resq 1           ; 0xC8: String table size
    
    .mode_restriction: resb 1      ; 0xD0: Mode restriction
    .reserved_mode: resb 7         ; 0xD1-0xD7
    
    .header_crc: resd 1            ; 0xD8: Header CRC
    .total_crc: resd 1             ; 0xDC: Total CRC
    
    .reserved: resb 32             ; 0xE0-0xFF
endstruc

; ============================================================================
; Alignment and Convenience Macros
; ============================================================================

; Physical Address Alignment for ActFlow (16-byte boundary)
%define ALIGN_ACTFLOW   16

; Calculate next aligned offset (value, alignment)
%macro ALIGN_UP 2
    ((%1 + %2 - 1) / %2) * %2
%endmacro

; CRC32 Polynomial (ETHERNET standard)
%define CRC32_POLY      0xEDB88320

