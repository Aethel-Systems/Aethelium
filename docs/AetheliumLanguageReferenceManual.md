# Aethelium Language Reference Manual

**Version**: 1.0 (Bootstrap Core)  
**Target Audience**: System Architects, Kernel Developers, Bare-metal Programmers

---

## 1. Introduction & Design Philosophy (Paradigm Purge)

Aethelium is a systems-level language born for modern hardware. To eliminate the legacy baggage of the Unix era, we have executed a strict "Paradigm Purge":

1.  **Rejection of Underscores**: The use of `_` in identifiers is strictly forbidden (no snake_case). We recommend using `/` for logical layering.
2.  **Symbol Redefinition**: The `/` character has been reclaimed for the identifier namespace. Division returns to its mathematical origin: `÷`.
3.  **Hierarchical Access**: Member access and enum references use `\` (backslash) to distinguish them from namespace paths.
4.  **No Runtime**: There is no `main` function, only the `@entry` point. There is no `libc`, only direct hardware instructions.

---

## 2. Lexical Structure

### 2.1 Identifiers
Identifiers are used for variable names, function names, and type names.
*   **Allowed**: Letters, numbers, `/` (forward slash).
*   **Forbidden**: `_` (underscore), starting with a digit.
*   **Style**: Aethelium recommends **Path-Case** naming.

```aethelium
// Correct
var system/cpu/count: UInt64
var pci/bus/id: UInt8

// Incorrect
var system_cpu_count: UInt64  // Underscores forbidden
var 2nd/bus: UInt8            // Cannot start with a digit
```

### 2.2 Operator Changes
Because `/` is reserved for identifiers, mathematical operators have changed as follows:

| Operation | Symbol | Example | Description |
| :--- | :--- | :--- | :--- |
| Division | `÷` | `a = 100 ÷ 4` | Must use the UTF-8 character `0xC3 0xB7` |
| Member Access | `\` | `ctx\rip` | Accessing struct members or object properties |
| Enum Reference | `\` | `\Success` | Referencing enum values or states |
| Path Separator | `/` | `sys/io/port` | **Part of the identifier name itself** |

### 2.3 Literals
*   **Integers**: `123`, `0xFF`, `0b1010`.
*   **Strings**: `"Hello World"` (UTF-8).
*   **Booleans**: `true`, `false`.
*   **Null Value**: `nil`.

---

## 3. Type System

Aethelium employs a strong type system; all types must be explicitly declared.

### 3.1 Basic Types
*   `UInt8`, `UInt16`, `UInt32`, `UInt64` (Unsigned integers)
*   `Int8`, `Int16`, `Int32`, `Int64` (Signed integers)
*   `Float`, `Double` (Floating point)
*   `Bool` (Boolean)
*   `Void` (Empty type)

### 3.2 Hardware-Strong Types
To prevent address confusion during kernel development, Aethelium introduces address-specific types:
*   `PhysAddr`: Physical address (cannot be dereferenced directly; requires mapping).
*   `VirtAddr`: Virtual address.

### 3.3 Pointers and Views
*   **Pointers**: `ptr<T>`. Represents a memory address pointing to type `T`.
*   **Views (Zero-Copy View)**: `view<T>`.
    *   The `view<T>` is a core feature of Aethelium. It "interprets" a raw memory region as type `T` without performing a data copy.
    *   Commonly used for parsing network packet headers, ACPI tables, and file system metadata.

```aethelium
// Casting a raw byte pointer to an ACPI Header view
let raw/ptr: ptr<UInt8> = ...
let acpi/table = raw/ptr as view<ACPI/Header>

// Read directly without memcpy
if acpi/table\signature == 0x54445352 { ... }
```

### 3.4 Hardware-Bound Types
*   `reg<"name", T>`: Physical register binding. Maps a variable directly to a CPU register.
*   `port<T>`: I/O port abstraction.
*   `vector<T, N>`: SIMD vector types (e.g., `vector<UInt8, 32>` corresponds to a YMM register).

---

## 4. Declarations and Definitions

### 4.1 Variables
Use `var` (mutable) and `let` (immutable).

```aethelium
// Syntax: var Name: Type = Value
var counter: UInt64 = 0
let max/limit: UInt64 = 100

// Physical register binding (only valid within hardware blocks)
var rax: reg<"rax", UInt64>
rax = 1  // Generates: mov rax, 1
```

### 4.2 Structs
Supports `@packed` and `@aligned(n)` decorators.

```aethelium
@packed
struct EFI/SystemTable {
    header: EFI/TableHeader
    firmware/vendor: ptr<UInt16>
    firmware/revision: UInt32
    // ...
}
```

### 4.3 Functions
Functions are defined using the `func` keyword. Return types are specified with `:`.

```aethelium
// Syntax: func Name(Param: Type): ReturnType
func kernel/init(boot/info: ptr<BootInfo>): Void {
    // ...
}
```

### 4.4 Decorators
*   `@entry`: Marks the program entry point (replaces `main`).
*   `@gate(type: ...)`: Defines specialized hardware gate functions (interrupts, exceptions, syscalls).
    *   `@gate(type: \interrupt)`: Automatically generates `iretq` returns.
    *   `@gate(type: \syscall)`: Automatically handles `sysretq`.
    *   `@gate(type: \efi)`: Follows the Microsoft x64 ABI (for UEFI).
    *   `@gate(type: \naked)`: Generates no prologue or epilogue.
    *   `@gate(type: \rom)`: ROM firmware entry, no automatic prologue/epilogue.

---

## 5. Control Flow

### 5.1 Conditionals and Loops
Aethelium discards C-style `for(;;)` in favor of modern iterator/infinite loop styles.

```aethelium
if x > 10 {
    // ...
} else {
    // ...
}

// Infinite loop (replaces while(true))
loop {
    if status\ready { break }
}

// Conditional loop
while x < 100 {
    // ...
}
```

### 5.2 Pattern Matching (Match)
Used to replace `switch`; supports enums and numerical values.

```aethelium
match port\status {
    case \Ready => { handle/ready() }
    case \Error => { handle/error() }
    default => { wait() }
}
```

### 5.3 Morph
**A unique feature of the Hardware Layer**. The `morph` block represents the absolute termination of the current control flow and a complete context switch.

```aethelium
hardware func context/switch(next: ptr<Thread>) {
    // ... Save current state ...
    
    // Execute Morph: Load new stack, switch page tables, jump to IP
    morph {
        reg<"rsp"> = next\rsp
        reg<"cr3"> = next\cr3
        goto next\rip
    }
    // Code here will never execute
}
```

---

## 6. System Level Blocks

Aethelium separates code into different abstraction layers, isolated through specific block syntax.

### 6.1 `hardware { ... }`
Hardware layer code block. In this block, you can:
*   Bind physical registers (`reg<...>`).
*   Execute ISA pass-through instructions (`hardware\isa\...`).
*   Manipulate I/O ports.
*   Access control registers (`CPU/Current\Control\CR3`, etc.).

```aethelium
hardware {
    var cr0: UInt64 = CPU/Current\Control\CR0
    hardware\isa\cli()
    // ...
}
```

### 6.2 `silicon { ... }`
Silicon-semantics block. Used for micro-architectural tuning and configuration.
*   Pipeline control.
*   Cache management.
*   Prefetch hints.

```aethelium
silicon {
    // Configure MSR (Model Specific Register)
    using CPU/Current {
        MSR/EFER\Syscall/Enable = true
    }
    
    // Cache line flush
    silicon\cache\flush(addr)
}
```

### 6.3 `metal { ... }`
Bare-metal logic block. Allows direct memory pointer manipulation and unsafe type conversions (such as `as view<T>`).

```aethelium
metal {
    let ptr = 0xB8000 as ptr<UInt16> // VGA Video Memory
    *ptr = 0x0F41  // Print white 'A'
}
```

---

## 7. Modularity & Imports

Aethelium currently uses compilation rules for automatic imports.

*   `aethelc [Source 1] [Source 2] -o [Output]`

**Note**: Import paths also follow the `/` separator rules.

---

## 8. Appendix: Operator Cheat Sheet

| Operator | Description | C Equivalent | Notes |
| :--- | :--- | :--- | :--- |
| `÷` | Division | `/` | **Must use `÷`; `/` is an identifier character** |
| `\` | Member Access | `.` or `->` | Used for struct members and enum values |
| `as` | Type Casting | `(Type)` | e.g., `val as UInt8` |
| `^` | Bitwise XOR | `^` | |
| `&` | Bitwise AND | `&` | |
| `|` | Bitwise OR | `|` | |
| `<<` | Left Shift | `<<` | |
| `>>` | Right Shift | `>>` | |

---

*For specific machine code generation rules corresponding to each instruction, please refer to `docs/HardwareLayerManualMachineCodeComparisonVersion.md`.*
