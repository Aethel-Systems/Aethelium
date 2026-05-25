# Aethelium

> **A Hardware & Kernel-First, Runtime-Less Systems Language Toolchain for Bare-metal & Native Windows.**

**Aethelium** is an independent, self-contained systems programming language toolchain. By abandoning the cumbersome traditional "compiler-assembler-linker" workflow, it not only focuses on providing extremely streamlined build solutions for **UEFI environments and bare-metal development**, but also fully supports building zero-runtime, zero-C-dependency, high-performance native applications (Win64 EXE) on **Windows platforms**.

---

## 🏗 Core Architecture

Aethelium employs a unique **"Weaver-Filler"** dual-engine architecture, achieving direct mapping from high-level semantics to low-level target platform binaries (PE32+/Mach-O/ROM/BIN):

*   **Binary Weaver (`toolsASM`)**:
    Responsible for the orchestration of low-level binary physical layouts. In the late stages of compilation, the Weaver precisely weaves the skeleton of the target binary in memory according to target environment specifications (such as boot sector offsets, UEFI section alignment, relocation tables, exception sections, etc.).
*   **Logic Filler (`toolsC`)**:
    A compiler frontend implemented in C. It is responsible for semantic analysis, AST construction, and strong type checking of Aethelium syntax, and precisely fills the generated machine code logic (ActFlow), global state data (MirrorState), and read-only constants (ConstantTruth) into the memory slots pre-allocated by the Binary Weaver.

### Directory Structure

```text
Aethelium/
├── Makefile                # Unified build orchestration system
├── README.md               # Project technical documentation (English)
├── README_CN.md            # Project technical documentation (Chinese)
└── core/                   # Core toolchain source code
    ├── toolsC/             # [Frontend] Compiler frontend and logic filler layer
    │   ├── include/        #   - Core headers (Binary format specs, AEFS protocol)
    │   ├── aetb/           #   - AETB target format generation engine
    │   ├── compiler/       #   - Lexical/syntax analysis and multi-platform code gen logic
    │   └── mkiso/aefs/     #   - Boot media and file system support
    └── toolsASM/           # [Backend] Binary Weaver and assembly emission layer
        ├── include/        #   - Assembly interface definitions
        └── src/            #   - Core instruction emission and layout implementation (NASM)
```

---

## 🛠 Build Workflow and Dependencies

## Windows users are recommended to download the compiler's .exe file directly via Release to avoid using a Posix simulation layer to build the source code.

### Environmental Dependencies

Building the Aethelium toolchain requires the following standard environment:

| Component | Requirement | Note |
| :--- | :--- | :--- |
| **Compiler** | Clang / GCC | Supports C11 standard, Clang recommended |
| **Assembler** | NASM 2.15+ | Used for backend binary orchestration |
| **Build System** | GNU Make 4.0+ | Automated build management |

*Source compilation supports architecture: Darwin (macOS) x86_64/arm64; Final output supports: macOS x86_64/arm64, Linux x86_64, and Windows x86_64*

### Compilation Commands

Execute in the repository root directory:

1.  **Environment Check**
    ```bash
    make check-platform
    ```

2.  **Build Core Compiler**
    ```bash
    make all
    ```
    *Note: This command will automatically compile `toolsC` and `toolsASM`, and link them into the final `aethelc` executable.*

3.  **Output Verification**
    After a successful build, the compiler binary will be located at:
    ```text
    build/output/aethelc
    ```

---

## ⚡ Core Technical Features

Aethelium designs two parallel compilation and execution mechanisms tailored for bare-metal boot and modern Windows system environments:

### 1. Bare-Metal & Firmware Features
*   **Runtime-less**: Aethelium does not rely on libc or any complex runtime environments; the generated binaries contain nothing redundant beyond hardware instructions.
*   **Direct UEFI Emission**: The compiler has built-in PE32+ format generation capabilities, eliminating the need for external linking tools like EDK II or massive frameworks. Standard `.efi` applications or drivers can be compiled and generated with a single command.
*   **Declarative Hardware Control**: Through semantics such as `@gate` and `@packed`, precise control over memory alignment, calling conventions, and register behavior is achieved at the high-level language layer.
*   **Physical ROM & Mode Switching**: Supports direct construction of burnable physical `.rom` images. Provides one-click Mode Jump functionality, allowing seamless migration between 16-bit Real Mode, 32-bit Protected Mode, and 64-bit Long Mode, with automatic generation and loading of internal GDT/IDT.
*   **Apple Silicon Support**: For Apple's custom chips, it supports emitting bare-metal Mach-O firmware in MH_PRELOAD format, automatically encapsulated into IM4P containers that support Secure Boot.

### 2. Windows Native Features
*   **Zero-CRT (Zero C Runtime Dependency)**: Breaks free from traditional C runtime library constraints. It does not reference any standard C initialization or memory management code, resulting in extremely fast application startup and no runtime library distribution burden.
*   **Direct NT Portal (Kernel System Call Mapping)**: Bypassing traditional Win32 API encapsulation, the Native Portal mechanism allows programs to directly bind and access native `ntdll.dll` system interfaces (e.g., `NtWriteFile`, `NtAllocateVirtualMemory`, `NtCreateThreadEx`) via the IAT (Import Address Table).
*   **Source-Weaved AELibrary**: Windows I/O operations (IOCP, Registry, Process/Thread) and graphical interfaces are inlined at compile-time using `.ae` source code provided by [AELibrary](https://github.com/Aethel-Systems/AELibrary). Combined with aggressive Dead Code Elimination (DCE), only the logic actually used is statically weaved into the final executable.
*   **Declarative Rendering Subsystem (Aura & Flux)**: AELibrary embeds a lightweight declarative interface system based on GDI. The window intercepts and takes over background drawing (`WM_CTLCOLORSTATIC`, `WM_PAINT`), allowing user interfaces to be rendered perfectly on DWM Mica (Liquid Glass) backgrounds without complex repaint logic.

---

## 📺 Running Showcase

Aethelium's multi-platform build capability can be freely emitted through the same compiler.

### 1. Bare-Metal/UEFI Example
```aethelium
@entry
@gate(type: \efi)
func efi/main(image/handle: ptr<Void>, sys/table: ptr<EFI/SystemTable>) : UInt64 {
    // Hierarchical namespace: sys/cpu/id, efi/con_out...
    print("Hello, AethelOS World!")

    hardware {
        loop {
            hardware\isa\pause() // Directly call underlying CPU instructions
        }
    }
    return 0 
}
```

### 2. Windows Native Application Example
```aethelium
//Aethelium
@entry
func start() : UInt64 {
    print("Launching native Windows GUI tools...")
    
    // 1. Open a webpage directly using the default Windows browser!
    // Invoking the magic of ShellExecuteA in shell32.dll, the last parameter 1 means show window normally
    win { shell32/ShellExecuteA(0, "open", "https://github.com/Aethel-Systems", 0, 0, 1) }
    
    // 2. Start Windows Notepad directly!
    win { shell32/ShellExecuteA(0, "open", "notepad.exe", 0, 0, 1) }
    
    print("Launch commands sent successfully.")
    return 0
}

/* Python, which requires importing about 2200 lines of external dependencies to achieve a simple "start browser and notepad" operation, plus loading a massive interpreter.
import subprocess
import webbrowser
webbrowser.open("https://github.com")
subprocess.Popen("notepad.exe")
*/
```

[UEFIDemo](https://github.com/Aethel-Systems/AetheliumDemo/UEFI/AERescue/assets/screenshot.png)

---

## 📂 User Manual

To get started quickly with different compilation targets, detailed technical documentation is included in the repository:

*   **Aethelium_Language_Reference.md**: Provides core explanations of Aethelium (.ae) syntax.
*   **Hardware_Layer_Manual_Machine_Code_Reference.md**: Provides a low-level guide for hardware passthrough and machine code generation.

---

## ⚠️ Supplementary Notes

*   **Target Output**: This toolchain supports Cross-Compilation. The generated executables can be **Aethelium Native** (.let / .aki / .srv / .hda), **Bare-metal BIN/ROM**, or **Windows x86_64 Native Applications**, depending on command-line arguments.
*   **Rejecting POSIX Historical Baggage**: Aethelium will never self-host through traditional POSIX libraries. Its long-term goal is to rewrite its compiler natively in Aethelium within future, pure system architectures untainted by UNIX/POSIX.

---

## 📄 License

This project is licensed under the **GNU General Public License v3.0 (GPLv3)**.

**Copyright (C) 2024-2026 Aethel-Systems. All rights reserved.**