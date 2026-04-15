# AethelOS Hardware Layer Industrial Manual (Machine Code Cross-Reference Edition)

**Version**: 2026-03-06  
**Scope**: **Covers only hardware layer capabilities currently implemented and compilable in this repository** (excludes unimplemented design drafts).  
**Primary References**:
- `examples/HardwareLayerComprehensive.ae`
- `toolsC/compiler/src/frontend/hardware_layer.c`
- `toolsC/compiler/src/frontend/hardware_gates.c`
- `toolsC/compiler/src/middleend/codegen/aec_codegen.c`

---

## 1. Documentation Objectives

This manual serves three primary scenarios:

1.  **Kernel/Bootloader Development**: Quickly confirm which machine code is emitted for a specific hardware layer statement.
2.  **Logic Debugging**: Compare actual byte streams against semantics when code "compiles but behaves unexpectedly."
3.  **Code Review**: Determine if a hardware layer implementation follows implemented and verified execution paths.

---

## 2. Rapid Verification Workflow (Recommended)

### 2.1 Compile Comprehensive Example
```bash
./build/output/aethelc examples/HardwareLayerComprehensive.ae -o /tmp/hwTest.bin --emit bin --bin-flat
```

### 2.2 Disassemble
```bash
ndisasm -b 64 /tmp/hwTest.bin
```

### 2.3 Keyword Check
```bash
ndisasm -b 64 /tmp/hwTest.bin | rg "syscall|in al|pause|cli|sti|hlt|mfence|mov rax,cr3|mov cr3,rax|rdmsr|wrmsr|lgdt|cpuid|retf"
```

---

## 3. Hardware Layer Syntax Overview (Currently Available)

1.  `hardware func ... { ... }`
2.  `hardware { ... }`
3.  `@gate(type: \interrupt|\syscall|\efi|\naked|\exception|\rom)`
4.  `reg<"...", T>`: Physical register binding
5.  `port<T>`: Port abstraction
6.  `@volatile view<T>`: MMIO views
7.  `CPU/Current\Control\CRx`: Control register R/W
8.  `CPU/Flags\...`: RFLAGS R/W
9.  `CPU/MSR\...`: Model Specific Register path access
10. `hardware\isa\op()`: ISA pass-through
11. `morph { ... goto ... }`: Morph (Context switch)
12. `hardware\state\snapshot(into: ...)`: Context snapshot
13. `vector/read` and `vector/write`: SIMD vector memory access
14. `@gate(type: \rom)`: ROM firmware entry (no automatic prologue/epilogue)

---

### 3.1 ROM Firmware Output (Flashable .rom)

- Use `@gate(type: \rom)` on the firmware entry function and mark it with `@entry`.
- Build with `aethelc firmware.ae -o firmware.rom --rom --side 16MB` (default size is 8MB).
- ROM images are padded to the requested size with `0xFF` for flash compatibility.
- Inline `asm { ... }` blocks are rejected for ROM builds; use hardware/primal layer instead.

## 4. ISA Pass-through & Machine Code Reference

The following table lists the current mappings implemented in `hw_generate_isa_opcode()`.

| Hardware Call | Machine Code | Description |
| :--- | :--- | :--- |
| `hardware\isa\syscall()` | `0F 05` | System call entry |
| `hardware\isa\cpuid()` | `0F A2` | CPU feature identification |
| `hardware\isa\sysret()` / `sysretq()` | `48 0F 07` | 64-bit Syscall return |
| `hardware\isa\iret()` / `iretq()` | `48 CF` | 64-bit Interrupt return |
| `hardware\isa\iretd()` | `CF` | 32-bit Interrupt return |
| `hardware\isa\int3()` | `CC` | Debug breakpoint |
| `hardware\isa\cli()` | `FA` | Clear interrupts |
| `hardware\isa\sti()` | `FB` | Set interrupts |
| `hardware\isa\pause()` | `F3 90` | Spin-wait friendly pause |
| `hardware\isa\hlt()` | `F4` | Halt until next interrupt |
| `hardware\isa\mfence()` | `0F AE F0` | Memory Fence |
| `hardware\isa\lfence()` | `0F AE E8` | Load Fence |
| `hardware\isa\sfence()` | `0F AE F8` | Store Fence |
| `hardware\isa\monitor()` | `0F 01 C8` | MONITOR |
| `hardware\isa\mwait()` | `0F 01 C9` | MWAIT |
| `hardware\isa\wbinvd()` | `0F 09` | Write-back and invalidate cache |
| `hardware\isa\invd()` | `0F 08` | Invalidate cache |
| `hardware\isa\nop()` | `90` | No Operation |

**Note**: `lgdt`, `lidt`, and `goto` are handled specifically within the codegen and do not strictly follow the table above (see Section 9).

---

## 5. Control & Flag Register Reference

### 5.1 Control Registers (CR0/2/3/4/8)

| Hardware Semantics | Machine Code Template | Notes |
| :--- | :--- | :--- |
| Read `CRx` to `RAX` | `48 0F 20 /r` | e.g., `CR3 -> RAX` is usually `0F 20 D8` |
| Write `RAX` to `CRx` | `48 0F 22 /r` | e.g., `MOV CR3,RAX` is `48 0F 22 D8` |

**Verification Samples**:
- `480F20D0` -> `mov rax,cr2`
- `480F22D8` -> `mov cr3,rax`
- `0F20D8` -> `mov rax,cr3` (Certain paths may omit the `48` REX prefix)

### 5.2 RFLAGS

| Hardware Semantics | Machine Code | Notes |
| :--- | :--- | :--- |
| Read all flags: `CPU/Flags\Value` | `9C 58` | `pushfq; pop rax` |
| Write all flags: `CPU/Flags\Value = x` | `50 9D` | `push rax; popfq` |
| `CPU/Flags\Interrupt/Enable=false` | `FA` | `cli` |
| `CPU/Flags\Interrupt/Enable=true` | `FB` | `sti` |

---

## 6. Port I/O Reference

`AST_HW_PORT_IO` is emitted directly by the codegen.

### 6.1 Read Port

| Width | Machine Code Path | Description |
| :--- | :--- | :--- |
| 8-bit | `EC imm8` or `E4 imm8` | `in al, port` |
| 16-bit | `66 ED imm8` | `in ax, port` |
| 32-bit | `ED imm8` | `in eax, port` |
| 64-bit | `48 ED imm8` | `in rax, port` |

### 6.2 Write Port

| Width | Machine Code Path | Description |
| :--- | :--- | :--- |
| 8-bit | `EE imm8` | `out port, al` |
| 16-bit | `66 EF imm8` | `out port, ax` |
| 32-bit | `EF imm8` | `out port, eax` |
| 64-bit | `48 EF imm8` | `out port, rax` |

**Verification Samples**:
- `E4 64` -> `in al,0x64`
- `E4 60` -> `in al,0x60`

---

## 7. MSR Reference

| Hardware Semantics | Machine Code | Notes |
| :--- | :--- | :--- |
| `rdmsr` | `0F 32` | `ECX` is MSR index, result in `EDX:EAX` |
| `wrmsr` | `0F 30` | `ECX` index, writes `EDX:EAX` |

**Special Paths (`CPU/MSR\...`)**:
The codegen automatically populates the `ECX` index and bit manipulation sequences for paths like `LSTAR`, `STAR`, and `EFER/Syscall/Enable`. Manual `rdmsr/wrmsr` is not required for these.

---

## 8. Vectorization (YMM) Reference

Current examples use:
- `vector/read<UInt8,32>`
- `vector/write<UInt8,32>`
- `reg<"ymm0"... "ymm3"...>`

**Emission Template**:

| Operation | Byte Prefix | Description |
| :--- | :--- | :--- |
| Read vector to YMM | `C5 FD 6F /r` | 256-bit load |
| Write YMM to Memory | `C5 FD 7F /r` | 256-bit store |

**Verification Samples**:
- `C5FD6F00`, `C5FD6F08`, `C5FD7F00`

---

## 9. Special Control Flow Reference

### 9.1 `goto target`

1.  **Standard path**: `FF E0` (`jmp rax`)
2.  **Far-transfer path**: Triggered if `reg<"cs"> = selector` was set:
    - `B9 xx xx xx xx` (Load selector)
    - `51` (`push rcx`)
    - `50` (`push rax`, Target RIP)
    - `CB` (`retf`)

**Verification**: Look for `CB` (`retf`).

### 9.2 `hardware\isa\lgdt(ptr)`

Handled specifically by codegen as:
- `0F 01 10` (Format: `lgdt [rax]`)

---

## 10. `hardware\state\snapshot` Semantics

Used for thread context saving. The current implementation covers:
1. Writing the GPR set into `ThreadContext` field offsets.
2. Saving `rsp`.
3. Saving `rip` (using a `call` + `pop` sequence to capture current location).
4. Saving `cr3` into the `page/table` field.

---

## 11. `@gate` Type and Return Instruction Mapping

| Gate Type | Current Return Semantics |
| :--- | :--- |
| `\interrupt` | `iretq` path |
| `\exception` | `iretq` path |
| `\syscall` | `sysretq` path (when no explicit return is present) |
| `\efi` | Standard function epilogue (`leave; ret`) |
| `\naked` | No prologue/epilogue generated |

---

## 12. Source-to-Machine Code Examples

Snippets from `examples/HardwareLayerComprehensive.ae`:

### 12.1 Syscall Preparation
- **Syntax**: Bind `rax/rdi/rsi/rdx`, then `hardware\isa\syscall()`.
- **Bytes**: `0F 05` (`syscall`).

### 12.2 PS/2 Polling
- **Syntax**: `status = PS2/STATUS/PORT\read()`, then `hardware\isa\pause()`.
- **Bytes**: `E4 64` (`in al,0x64`), `F3 90` (`pause`).

### 12.3 Reading Page Fault Address
- **Syntax**: `fault/addr = CPU/Current\Control\CR2`.
- **Bytes**: `48 0F 20 D0` (`mov rax,cr2`).

### 12.4 Interrupt Critical Section
- **Syntax**: Save `CPU/Flags\Value`, set `Enable = false`, `mfence()`.
- **Bytes**: `9C 58` (`pushf; pop rax`), `FA` (`cli`), `0F AE F0` (`mfence`).

---

## 13. Struct Access & Offset Rules

1. Codegen calculates field offsets based on struct layout.
2. `ptr<T>`/`view<T>` fields are accessed directly via offsets.
3. Supports `field[i]` and nested indexing.
4. **Engineering Practice**: Always use precise field names.

---

## 14. Recommended Engineering Practices

1. Place all architectural side-effect statements within `hardware {}` blocks.
2. Force the use of `@gate(type: \interrupt|\exception)` for interrupt entries.
3. Encapsulate `CRx/Flags/MSR` operations into independent `hardware func` blocks.
4. Use the unified Context Switch pattern: `snapshot` -> `morph` -> `goto`.
5. Use the unified SIMD copy pattern: Vector blocks followed by a tail-byte loop.

---

## 15. Common Failures & Diagnostics

### 15.1 `Failed to emit function machine code`
1. Isolate the target function into a minimal file.
2. Remove non-essential logic, leaving only the `hardware` path.
3. Verify register names against the supported set.
4. Ensure struct field names match definitions exactly.

### 15.2 Code compiles but behavior is incorrect
1. Run `ndisasm -b 64` to inspect the actual bytes.
2. Cross-reference the bytes with this manual's tables.
3. If it is a gate function, verify the `@gate` type.
4. For `goto` or segment switches, check if the `retf` path was correctly triggered.

---

## 16. Implementation Boundaries

1. This manual describes only existing, implemented paths. It does not speculate on future features.
2. The most stable syntax subset is that used in `HardwareLayerComprehensive.ae`.
3. While some ISA names have frontend definitions, complex variants might still be constrained by current codegen limitations.

---

## 17. Appendix: Hardware Layer Cheat Sheet

| Statement | Target Behavior |
| :--- | :--- |
| `hardware\isa\syscall()` | Enter kernel syscall entry |
| `CPU/Flags\Interrupt/Enable = false` | Disable interrupts (`cli`) |
| `CPU/Flags\Interrupt/Enable = true` | Enable interrupts (`sti`) |
| `CPU/Current\Control\CR3 = x` | Switch page table |
| `CPU/MSR\LSTAR = x` | Set syscall entry point |
| `hardware\isa\lgdt(ptr)` | Load GDT descriptor |
| `hardware\state\snapshot(into: ctx)` | Save current register context |
| `morph { ... goto rip }` | Non-linear control transfer |
| `src\vector/read<UInt8,32>(off)` | 256-bit Vector Read |
| `dst\vector/write<UInt8,32>(ymm,off)` | 256-bit Vector Write |

---
## 18. New CPU Mode One-Liner Transitions (2026-03-08)

### 18.1 `hardware\isa\modejump32(entry[, loadBase])`

Scope:
- Valid in 16-bit machine mode (`--machine-bits 16`).
- Default `loadBase` is `0x7C00` if omitted.

Auto-emitted sequence:
1. `cli`
2. Build and load internal `GDTR/GDT` (`lgdt [disp16]`)
3. `CR0.PE = 1`
4. Far jump to internal 32-bit entry (`66 EA ptr16:32`)
5. Reload `DS/ES/FS/GS/SS` to selector `0x10`
6. Set `ESP`
7. Jump to `entry` (32-bit linear target)

Key bytes:
- `0F 01 16` (`lgdt [disp16]`)
- `66 0F 20 C0`, `66 0F 22 C0` (`mov eax,cr0` / `mov cr0,eax`)
- `66 EA ... 08 00` (far jump to 32-bit code segment)

### 18.2 `hardware\isa\modejump64(entry[, loadBase])`

Scope:
- Valid in 32-bit machine mode (`--machine-bits 32`).
- Default `loadBase` is `0x8000` if omitted.

Auto-emitted sequence:
1. `cli`
2. Build and load internal `GDTR/GDT` (`lgdt [disp32]`)
3. Enable `CR4.PAE`
4. Enable `EFER.LME` via `rdmsr/wrmsr` (`MSR 0xC0000080`)
5. Build internal page tables and load `CR3`
6. Enable paging (`CR0.PG|PE`)
7. Far jump to internal 64-bit entry
8. Load 64-bit `RAX=entry`, `jmp rax`

Key bytes:
- `0F 01 15` (`lgdt [disp32]`)
- `0F 20 E0`, `0F 22 E0` (`mov eax,cr4` / `mov cr4,eax`)
- `0F 32`, `0F 30` (`rdmsr` / `wrmsr`)
- `0F 22 D8` (`mov cr3,eax`)
- `EA ... 08 00` (far jump)
- `48 B8 <imm64> FF E0` (`mov rax,imm64; jmp rax`)

### 18.3 BIOS-oriented Usage

- Stage1 real mode -> protected mode:
  - `hardware { hardware\isa\modejump32(0x8000) }`
- Stage2 protected mode -> long mode:
  - `hardware { hardware\isa\modejump64(0x1000000) }`

---
*This document represents the current implementation state of the repository. Maintain auditability by ensuring new hardware capabilities are added to this machine code reference before merging.*

## 19. New Port-Width ISA Calls (2026-03-09)

Added parameterized hardware ISA calls for explicit I/O widths:

| Hardware Call | Machine Code (Representative) | Notes |
| :--- | :--- | :--- |
| `hardware\isa\inport16(port)` | `66 E5 ib` / `66 ED` | Reads AX from port (zero-extended to accumulator). |
| `hardware\isa\outport16(port, v)` | `66 E7 ib` / `66 EF` | Writes AX low 16-bit to port. |
| `hardware\isa\inport32(port)` | `E5 ib` / `ED` | Reads EAX from port. |
| `hardware\isa\outport32(port, v)` | `E7 ib` / `EF` | Writes EAX low 32-bit to port. |

Implementation path:
- `mc_emit_hw_isa_param_call(...)` in codegen now recognizes `inport16/outport16/inport32/outport32`.
- For 16-bit machine mode, operand-size prefixes are emitted where needed to preserve explicit width semantics.
