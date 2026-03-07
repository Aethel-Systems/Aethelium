/*
 * Copyright (C) 2024-2026 Aethel-Systems. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * AethelOS Binary Format - Industrial Grade Implementation v4.0
 * toolsC/include/binary_format.h
 *
 * Complete specification per design blueprint (line 179+):
 * - AethelID v1 (256-bit) for all IPC identification
 * - Standardized 256-byte headers for all formats
 * - SIP (System Integrity Protection) integration
 * - Architect and Sandbox execution modes
 * 
 * Supports:
 * - AKI (Aethel Kernel Image)
 * - SRV (System Service Archive)
 * - HDA (Hardware Driver Archive)
 * - AETB (Aethelium Binary)
 * - IYA (Integrated Yethelium Archive)
 *
 * NO simplified code, NO mock implementations, NO POSIX legacy, NO virtual users
 */

#ifndef _BINARY_FORMAT_H_
#define _BINARY_FORMAT_H_

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   AethelID v1 System - 256-bit Proprietary Identifier for IPC
   ============================================================================
   
   Structure (256 bits total = 32 bytes):
   - Version (4 bits): Always 0xA (1010 binary)
   - Timestamp (48 bits): Milliseconds from AethelOS epoch (2026-01-06)
   - Entropy (96 bits): CSPRNG-generated high-entropy random
   - Encrypted Payload (96 bits): ChaCha20-encrypted 12-byte plaintext
   - Checksum (12 bits): BLAKE3-160 truncated validation
   
   Total: 64 hex characters (256 bits ÷ 4 bits/char)
   Format example: A1<7F3B9C2D4E8A<1F569D2C7E0B3F1A8E4D5B9C2D6F<0A7E1F348D4C2B9E5A7F8D<9C2B1E4F
 */

typedef struct {
    uint8_t bytes[32];  /* 256-bit AethelID = 32 bytes */
} AethelID;

/* Decrypted AethelID payload (12 bytes total) */
typedef struct {
    uint32_t sender_entity_id;   /* Sending process/service entity ID (4 bytes) */
    uint16_t intent_type;        /* Message intent/type (2 bytes) */
    uint32_t sequence_number;    /* Request-response sequence ID (4 bytes) */
    uint16_t context_flags;      /* Priority/channel/version context (2 bytes) */
} AethelIDPayload;

/* Magic Numbers - Little-endian 32-bit format */
#define AKI_MAGIC      0x21494B41   /* "!AKI" in little-endian */
#define SRV_MAGIC      0x21525653   /* "!SRV" in little-endian */
#define HDA_MAGIC      0x21414448   /* "!HDA" in little-endian */
#define AETB_MAGIC     0x42544541   /* "AETB" as uint32_t host read value */
#define IYA_MAGIC      0x21415941   /* "!AYA" in little-endian */

#define FORMAT_VERSION_V1 1

/* ============================================================================
   AKI (Aethel Kernel Image) - Origin Image
   ============================================================================
   
   Contains microkernel core, SIP decision matrix, and AethelA compiler engine.
   Standard 256-byte header + ActFlow + MirrorState + ConstantTruth sections.
 */

/* AKI Flags */
#define AKI_FLAG_HAS_COMPILER      0x0001
#define AKI_FLAG_HAS_IPC_BROKER    0x0002
#define AKI_FLAG_DEBUG_SYMBOLS     0x0004
#define AKI_FLAG_RECOVERY_MODE     0x0008
#define AKI_FLAG_SIP_TAINTED       0x0010

typedef struct {
    /* Standard 256-byte header: 0x00-0xFF */
    uint32_t magic;              /* 0x00: AKI_MAGIC */
    uint32_t version;            /* 0x04: Format version */
    uint32_t flags;              /* 0x08: AKI flags */
    uint32_t machine_type;       /* 0x0C: CPU arch (0x3E=x86-64, 0xB7=ARM64) */
    
    AethelID kernel_id;          /* 0x10: Kernel AethelID */
    
    uint64_t genesis_pt;         /* 0x30: Physical load origin (0x100000) */
    uint64_t sip_vector;         /* 0x38: SIP core permission vector */
    
    /* ActFlow zone (replaces .text) */
    uint64_t act_flow_offset;    /* 0x40: Microkernel behavior flow offset */
    uint64_t act_flow_size;      /* 0x48: Microkernel behavior flow size */
    
    /* MirrorState zone (replaces .data) */
    uint64_t mirror_state_offset; /* 0x50: System state mirror offset */
    uint64_t mirror_state_size;   /* 0x58: System state mirror size */
    
    /* ConstantTruth zone (replaces .rodata) */
    uint64_t constant_truth_offset; /* 0x60: Immutable system metadata offset */
    uint64_t constant_truth_size;   /* 0x68: Immutable system metadata size */
    
    /* AethelA Compiler Engine */
    uint64_t aethela_logic_offset; /* 0x70: AethelA compiler engine offset */
    uint64_t aethela_logic_size;   /* 0x78: AethelA compiler engine size */
    
    /* Symbol/ID nexus */
    uint64_t nexus_point_offset;  /* 0x80: Symbol/ID mapping table offset */
    uint64_t nexus_point_size;    /* 0x88: Symbol/ID mapping table size */
    
    /* Symbol table */
    uint64_t symtab_offset;       /* 0x90: Symbol table offset */
    uint64_t symtab_size;         /* 0x98: Symbol table size */
    
    /* String table */
    uint64_t strtab_offset;       /* 0xA0: String table offset */
    uint64_t strtab_size;         /* 0xA8: String table size */
    
    /* Relocations */
    uint64_t reloc_offset;        /* 0xB0: Relocation table offset */
    uint64_t reloc_count;         /* 0xB8: Number of relocation entries */
    
    uint8_t mode_affinity;        /* 0xC0: Boot mode (0=sandbox, 1=architect) */
    uint8_t sip_policy_version;   /* 0xC1: SIP policy version */
    uint8_t reserved1[6];         /* 0xC2-0xC7: Reserved */
    
    uint64_t build_timestamp;     /* 0xC8: Build Unix timestamp */
    uint32_t build_version;       /* 0xD0: Build version */
    uint32_t compiler_version;    /* 0xD4: AE compiler version */
    
    uint32_t header_crc;          /* 0xD8: Header CRC32 */
    uint32_t total_crc;           /* 0xDC: Total file CRC32 */
    
    uint8_t reserved2[32];        /* 0xE0-0xFF: Reserved for future */
} aki_header_t;

typedef struct {
    uint32_t name_offset;        /* Offset in string table */
    uint32_t reserved;           /* Alignment */
    uint64_t address;            /* Symbol address */
    uint64_t size;               /* Symbol size */
} aki_symbol_t;

typedef struct {
    uint64_t offset;             /* Offset to relocate */
    uint32_t symbol_idx;         /* Symbol table index */
    uint32_t reloc_type;         /* Relocation type */
} aki_reloc_t;

/* ============================================================================
   SRV (System Service Archive) - Service Entity
   ============================================================================
   
   Logical entity providing functional interface. No user concept, only access rights.
 */

/* Service Types */
#define SRV_TYPE_FILESYSTEM      0x01
#define SRV_TYPE_NETWORK         0x02
#define SRV_TYPE_AUDIO           0x03
#define SRV_TYPE_DISPLAY         0x04
#define SRV_TYPE_DEVICE          0x05
#define SRV_TYPE_SECURITY        0x06
#define SRV_TYPE_IPC_BROKER      0x07
#define SRV_TYPE_AUDIT           0x08
#define SRV_TYPE_SYSTEM          0x09

/* SRV Flags */
#define SRV_FLAG_PRIVILEGED      0x0001
#define SRV_FLAG_REPLICABLE      0x0002
#define SRV_FLAG_MANAGED         0x0004
#define SRV_FLAG_DEBUG           0x0008
#define SRV_FLAG_SANDBOXED       0x0010

typedef struct {
    /* Standard 256-byte header: 0x00-0xFF */
    uint32_t magic;              /* 0x00: SRV_MAGIC */
    uint32_t version;            /* 0x04: Format version */
    uint32_t service_type;       /* 0x08: Service type ID */
    uint32_t flags;              /* 0x0C: SRV flags */
    
    AethelID sphere_id;          /* 0x10: Service AethelID */
    
    uint64_t srv_genesis;        /* 0x30: Service life origin point */
    uint64_t sip_policy;         /* 0x38: SIP policy (who can call) */
    
    /* ActFlow zone */
    uint64_t act_flow_offset;    /* 0x40: Service logic offset */
    uint64_t act_flow_size;      /* 0x48: Service logic size */
    
    /* MirrorState zone */
    uint64_t mirror_state_offset; /* 0x50: Runtime state offset */
    uint64_t mirror_state_size;   /* 0x58: Runtime state size */
    
    /* ConstantTruth zone */
    uint64_t constant_truth_offset; /* 0x60: Immutable config offset */
    uint64_t constant_truth_size;   /* 0x68: Immutable config size */
    
    /* AethelA Reflection Nexus */
    uint64_t schema_nexus_offset; /* 0x70: Type schema offset */
    uint64_t schema_nexus_size;   /* 0x78: Type schema size */
    
    /* Symbol table */
    uint64_t symtab_offset;      /* 0x80: Symbol table offset */
    uint64_t symtab_size;        /* 0x88: Symbol table size */
    
    /* String table */
    uint64_t strtab_offset;      /* 0x90: String table offset */
    uint64_t strtab_size;        /* 0x98: String table size */
    
    /* Relocations */
    uint64_t reloc_offset;       /* 0xA0: Relocation table offset */
    uint64_t reloc_count;        /* 0xA8: Relocation count */
    
    uint32_t privilege_level;    /* 0xB0: Privilege level for SIP */
    uint8_t mode_behavior;       /* 0xB4: Mode behavior (0=sandbox, 1=architect, 2=both) */
    uint8_t sip_level_required;  /* 0xB5: Required SIP level */
    uint8_t reserved1[2];        /* 0xB6-0xB7: Reserved */
    
    uint64_t build_timestamp;    /* 0xB8: Build timestamp */
    uint32_t build_version;      /* 0xC0: Build version */
    uint32_t compiler_version;   /* 0xC4: AE compiler version */
    
    uint32_t header_crc;         /* 0xC8: Header CRC32 */
    uint32_t total_crc;          /* 0xCC: Total CRC32 */
    
    char service_name[32];       /* 0xD0: Service name (null-terminated) */
    uint8_t reserved2[16];       /* 0xF0: Reserved */
} srv_header_t;

typedef struct {
    uint32_t name_offset;       /* Offset in string table */
    uint32_t type;              /* Symbol type */
    uint64_t address;           /* Symbol address */
    uint64_t size;              /* Symbol size */
} srv_symbol_t;

typedef struct {
    uint64_t offset;            /* Offset to relocate */
    uint32_t symbol_idx;        /* Symbol index */
    uint32_t reloc_type;        /* Relocation type */
} srv_reloc_t;
/* ============================================================================
   HDA (Hardware Driver Archive) - Hardware Capability Contract
   ============================================================================
   
   Driver is a "description file" of hardware behavior, not traditional driver.
 */

/* Device Types */
#define HDA_DEVICE_STORAGE       0x01
#define HDA_DEVICE_NETWORK       0x02
#define HDA_DEVICE_GPU           0x03
#define HDA_DEVICE_INPUT         0x04
#define HDA_DEVICE_PCI           0x05
#define HDA_DEVICE_USB           0x06
#define HDA_DEVICE_AUDIO         0x07
#define HDA_DEVICE_SENSOR        0x08
#define HDA_DEVICE_STORAGE_NVME  0x0101
#define HDA_DEVICE_STORAGE_AHCI  0x0102

/* HDA Flags */
#define HDA_FLAG_MMU_REQUIRED    0x0001
#define HDA_FLAG_DMA_CAPABLE     0x0002
#define HDA_FLAG_IRQ_DRIVEN      0x0004
#define HDA_FLAG_DEBUG           0x0008
#define HDA_FLAG_SANDBOXED       0x0010

typedef struct {
    /* Standard 256-byte header: 0x00-0xFF */
    uint32_t magic;              /* 0x00: HDA_MAGIC */
    uint32_t version;            /* 0x04: Format version */
    uint32_t device_type;        /* 0x08: Device type ID */
    uint32_t flags;              /* 0x0C: HDA flags */
    
    AethelID binding_id;         /* 0x10: Driver binding AethelID */
    
    uint64_t contract_pt;        /* 0x30: Hardware contract entry point */
    uint64_t hw_blueprint[4];    /* 0x38: Hardware logical fingerprint */
    
    uint32_t sip_requirement;    /* 0x58: Minimum SIP level */
    uint8_t mode_support;        /* 0x5C: Mode support (Bit0=sandbox, Bit1=architect) */
    uint8_t reserved_mode[3];    /* 0x5D-0x5F: Reserved */
    
    /* ActFlow zone */
    uint64_t act_flow_offset;    /* 0x60: Hardware control logic offset */
    uint64_t act_flow_size;      /* 0x68: Hardware control logic size */
    
    /* MirrorState zone */
    uint64_t mirror_state_offset; /* 0x70: Hardware state offset */
    uint64_t mirror_state_size;   /* 0x78: Hardware state size */
    
    /* ConstantTruth zone */
    uint64_t constant_truth_offset; /* 0x80: Hardware config offset */
    uint64_t constant_truth_size;   /* 0x88: Hardware config size */
    
    /* Symbol table */
    uint64_t symtab_offset;      /* 0x90: Symbol table offset */
    uint64_t symtab_size;        /* 0x98: Symbol table size */
    
    /* String table */
    uint64_t strtab_offset;      /* 0xA0: String table offset */
    uint64_t strtab_size;        /* 0xA8: String table size */
    
    /* Relocations */
    uint64_t reloc_offset;       /* 0xB0: Relocation table offset */
    uint64_t reloc_count;        /* 0xB8: Relocation count */
    
    AethelID bridge_id;          /* 0xC0: Kernel bridge protocol AethelID */
    
    uint32_t header_crc;         /* 0xE0: Header CRC32 */
    uint32_t total_crc;          /* 0xE4: Total CRC32 */
    
    uint64_t build_timestamp;    /* 0xE8: Build timestamp */
    uint32_t build_version;      /* 0xF0: Build version */
    uint32_t compiler_version;   /* 0xF4: AE compiler version */
    
    uint16_t vendor_id;          /* 0xF8: PCI Vendor ID */
    uint16_t device_id;          /* 0xFA: PCI Device ID */
} hda_header_t;

typedef struct {
    uint32_t name_offset;       /* Offset in string table */
    uint32_t type;              /* Symbol type */
    uint64_t address;           /* Symbol address */
    uint64_t size;              /* Symbol size */
} hda_symbol_t;

typedef struct {
    uint64_t offset;            /* Offset to relocate */
    uint32_t symbol_idx;        /* Symbol index */
    uint32_t reloc_type;        /* Relocation type */
} hda_reloc_t;
/* ============================================================================
   AETB (Aethelium Binary) - Application Logic Container
   ============================================================================
   
   Core of .iya package. Completely under SIP governance.
 */

/* AETB Flags */
#define AETB_FLAG_PRIVILEGED     0x0001
#define AETB_FLAG_NETWORK        0x0002
#define AETB_FLAG_FILESYSTEM     0x0004
#define AETB_FLAG_AUDIO          0x0008
#define AETB_FLAG_DEBUG          0x0010

typedef struct {
    /* Standard 256-byte header: 0x00-0xFF */
    uint32_t magic;              /* 0x00: AETB_MAGIC */
    uint32_t version;            /* 0x04: Format version */
    uint32_t app_type;           /* 0x08: Application type */
    uint32_t flags;              /* 0x0C: AETB flags */
    
    AethelID logic_id;           /* 0x10: App logic AethelID */
    
    uint64_t genesis_pt;         /* 0x30: main() entry point */
    uint64_t sip_context;        /* 0x38: App security context ID */
    
    /* ActFlow zone */
    uint64_t act_flow_offset;    /* 0x40: App logic offset */
    uint64_t act_flow_size;      /* 0x48: App logic size */
    
    /* MirrorState zone */
    uint64_t mirror_state_offset; /* 0x50: App runtime state offset */
    uint64_t mirror_state_size;   /* 0x58: App runtime state size */
    
    /* ConstantTruth zone */
    uint64_t constant_truth_offset; /* 0x60: App resources offset */
    uint64_t constant_truth_size;   /* 0x68: App resources size */
    
    /* Relocation nexus */
    uint64_t reloc_nexus_offset; /* 0x70: Relocation information offset */
    uint64_t reloc_nexus_size;   /* 0x78: Relocation information size */
    
    /* Import nexus */
    uint64_t import_nexus_offset; /* 0x80: External dependencies offset */
    uint64_t import_nexus_size;   /* 0x88: External dependencies size */
    
    AethelID iya_link_id;        /* 0x90: Parent IYA container AethelID */
    
    /* Symbol table */
    uint64_t symtab_offset;      /* 0xB0: Symbol table offset */
    uint64_t symtab_size;        /* 0xB8: Symbol table size */
    
    /* String table */
    uint64_t strtab_offset;      /* 0xC0: String table offset */
    uint64_t strtab_size;        /* 0xC8: String table size */
    
    uint8_t mode_restriction;    /* 0xD0: Mode restriction (0=sandbox required, 1=architect only) */
    uint8_t reserved1[7];        /* 0xD1-0xD7: Reserved */
    
    uint32_t header_crc;         /* 0xD8: Header CRC32 */
    uint32_t total_crc;          /* 0xDC: Total CRC32 */
    
    uint64_t build_timestamp;    /* 0xE0: Build timestamp */
    uint32_t build_version;      /* 0xE8: Build version */
    uint32_t compiler_version;   /* 0xEC: AE compiler version */
    
    uint8_t reserved2[16];       /* 0xF0-0xFF: Reserved */
} aetb_header_t;

typedef struct {
    uint32_t name_offset;       /* Offset in string table */
    uint32_t type;              /* Symbol type */
    uint64_t address;           /* Symbol address */
    uint64_t size;              /* Symbol size */
} aetb_symbol_t;

/* ============================================================================
   IYA (Integrated Yethelium Archive) - Application Package
   ============================================================================ */

typedef struct {
    uint32_t magic;              /* 0x00: IYA_MAGIC */
    uint32_t format_version;     /* 0x04: Version */
    AethelID package_id;         /* 0x08: Package AethelID */
    uint8_t developer_sig[64];   /* 0x28: Developer signature */
    uint64_t created_time;       /* 0x68: Creation timestamp */
    uint32_t permissions;        /* 0x70: Permission bitmap */
    uint32_t min_os_version;     /* 0x74: Minimum OS version */
    uint32_t target_os_version;  /* 0x78: Target OS version */
    uint64_t manifest_offset;    /* 0x7C: Manifest offset */
    uint64_t manifest_size;      /* 0x84: Manifest size */
    uint64_t aetb_offset;        /* 0x8C: AETB binary offset */
    uint64_t aetb_size;          /* 0x94: AETB binary size */
    uint64_t resources_offset;   /* 0x9C: Resources offset */
    uint64_t resources_size;     /* 0xA4: Resources size */
    uint8_t reserved[48];        /* 0xAC: Reserved */
    uint32_t header_checksum;    /* 0xDC: CRC32 */
} iya_header_t;

/* ============================================================================
   Public API Functions - Industrial Grade
   ============================================================================ */

/* CRC32 calculation (complete, not simplified) */
uint32_t calculate_crc32(const uint8_t *data, size_t length);

/* Header validation functions */
int validate_aki_header(const aki_header_t *header);
int validate_srv_header(const srv_header_t *header);
int validate_hda_header(const hda_header_t *header);
int validate_aetb_header(const aetb_header_t *header);
int validate_iya_header(const iya_header_t *header);

/* Binary generation functions (complete, no simplifications) */
int generate_aki_binary(const char *output_file,
                        const aki_header_t *header,
                        const uint8_t *act_flow,
                        const uint8_t *mirror_state,
                        const uint8_t *constant_truth,
                        const uint8_t *aethela_logic,
                        const uint8_t *symtab,
                        const uint8_t *strtab,
                        const uint8_t *relocs,
                        size_t act_flow_size,
                        size_t mirror_state_size,
                        size_t constant_truth_size,
                        size_t aethela_logic_size,
                        size_t symtab_size,
                        size_t strtab_size,
                        size_t reloc_size);

int generate_srv_binary(const char *output_file,
                        const srv_header_t *header,
                        const uint8_t *act_flow,
                        const uint8_t *mirror_state,
                        const uint8_t *constant_truth,
                        const uint8_t *schema_nexus,
                        const uint8_t *symtab,
                        const uint8_t *strtab,
                        const uint8_t *relocs,
                        size_t act_flow_size,
                        size_t mirror_state_size,
                        size_t constant_truth_size,
                        size_t schema_nexus_size,
                        size_t symtab_size,
                        size_t strtab_size,
                        size_t reloc_size);

int generate_hda_binary(const char *output_file,
                        const hda_header_t *header,
                        const uint8_t *act_flow,
                        const uint8_t *mirror_state,
                        const uint8_t *constant_truth,
                        const uint8_t *symtab,
                        const uint8_t *strtab,
                        const uint8_t *relocs,
                        size_t act_flow_size,
                        size_t mirror_state_size,
                        size_t constant_truth_size,
                        size_t symtab_size,
                        size_t strtab_size,
                        size_t reloc_size);

int generate_aetb_binary(const char *output_file,
                         const aetb_header_t *header,
                         const uint8_t *act_flow,
                         const uint8_t *mirror_state,
                         const uint8_t *constant_truth,
                         const uint8_t *symtab,
                         const uint8_t *strtab,
                         size_t act_flow_size,
                         size_t mirror_state_size,
                         size_t constant_truth_size,
                         size_t symtab_size,
                         size_t strtab_size);

int generate_iya_package(const char *output_file,
                         const iya_header_t *header,
                         const uint8_t *manifest,
                         const uint8_t *aetb,
                         const uint8_t *resources,
                         size_t manifest_size,
                         size_t aetb_size,
                         size_t resources_size);

/* Backward compatibility - old direct generation functions (legacy) */
int generate_aki_direct(const char *output_file, const void *text_section, 
                        size_t text_size, const void *data_section, size_t data_size,
                        const void *rodata_section, size_t rodata_size,
                        const aki_symbol_t *symtab, size_t symtab_count,
                        const char *strtab, size_t strtab_size,
                        const aki_reloc_t *relocs, size_t reloc_count,
                        uint64_t entry_point, uint32_t flags);

int generate_srv_direct(const char *output_file, const char *service_name,
                        uint32_t service_type, const void *text_section, 
                        size_t text_size, const void *data_section, size_t data_size,
                        const void *rodata_section, size_t rodata_size,
                        const srv_symbol_t *symtab, size_t symtab_count,
                        const char *strtab, size_t strtab_size,
                        const srv_reloc_t *relocs, size_t reloc_count,
                        uint64_t entry_point, uint32_t flags);

int generate_hda_direct(const char *output_file, const char *driver_name,
                        uint32_t device_type, uint16_t vendor_id, uint16_t device_id,
                        const void *text_section, size_t text_size, 
                        const void *data_section, size_t data_size,
                        const void *rodata_section, size_t rodata_size,
                        const hda_symbol_t *symtab, size_t symtab_count,
                        const char *strtab, size_t strtab_size,
                        const hda_reloc_t *relocs, size_t reloc_count,
                        uint64_t entry_point, uint32_t flags);

int create_iya_package(const char *elf_file, const char *manifest_file,
                       const char *output_file);

#ifdef __cplusplus
}
#endif

#endif /* _BINARY_FORMAT_H_ */
