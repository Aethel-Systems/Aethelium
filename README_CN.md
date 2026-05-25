# Aethelium

> **面向硬件与操作系统内核、无运行时、为现代底层系统编程与原生 Windows 应用而生的语言工具链。**
> A Hardware & Kernel-First, Runtime-Less Systems Language Toolchain for Bare-metal & Native Windows.

**Aethelium** 是一个独立的、自洽的系统编程语言工具链。它摒弃了传统的“编译器-汇编器-链接器”繁琐流程，不仅专注于为 **UEFI 环境和裸机 (Bare-metal) 开发**提供极致精简的构建方案，更全面支持在 **Windows 平台**下构建零运行时、零 C 依赖的高性能原生应用程序（Win64 EXE）。

---

## 🏗 核心架构

Aethelium 采用独特的 **"Weaver-Filler" (编排-填充)** 双引擎架构，实现了从高级语义到目标平台底层二进制（PE32+/Mach-O/ROM/BIN）的直接映射：

*   **Binary Weaver (二进制织机 - `toolsASM`)**:
    负责底层二进制物理布局的编排。在编译后期，织机根据目标环境规范（如引导扇区偏移、UEFI 段对齐、重定位表、异常区段等）在内存中精确编织目标二进制的结构骨架。
*   **Logic Filler (逻辑填充器 - `toolsC`)**:
    C 语言实现的编译器前端。负责 Aethelium 语法的语义分析、AST 构建、强类型检查，并将生成的机器码逻辑（ActFlow）、全局状态数据（MirrorState）与只读常量（ConstantTruth）精准填充至二进制织机预设的内存槽位中。

### 目录结构说明

```text
Aethelium/
├── Makefile                # 统一构建编排系统
├── README.md               # 项目技术英文文档
├── README_CN.md            # 项目技术中文文档 (本文件)
└── core/                   # 核心工具链源码
    ├── toolsC/             # [Frontend] 编译器前端与逻辑填充层
    │   ├── include/        #   - 核心头文件 (二进制格式规范、AEFS 协议)
    │   ├── aetb/           #   - AETB 目标格式生成引擎
    │   ├── compiler/       #   - 词法/语法分析与多平台代码生成主逻辑
    │   └── mkiso/aefs/     #   - 引导介质与文件系统支持
    └── toolsASM/           # [Backend] 二进制织机与汇编发射层
        ├── include/        #   - 汇编接口定义
        └── src/            #   - 核心指令发射与布局实现 (NASM)
```

---

## 🛠 构建工作流与依赖

## Windows用户推荐直接通过Release下载编译器的exe文件，以避免使用Posix模拟层构建源码

### 环境依赖

构建 Aethelium 工具链需要以下标准环境：

| 组件 | 要求 | 说明 |
| :--- | :--- | :--- |
| **Compiler** | Clang / GCC | 支持 C11 标准，推荐 Clang |
| **Assembler** | NASM 2.15+ | 用于处理后端二进制编排 |
| **Build System** | GNU Make 4.0+ | 自动化构建管理 |

*源码编译支持架构：Darwin (macOS) x86_64/arm64, 最终产物支持：macOS x86_64/arm64、Linux x86_64 和 Windows x86_64*

### 编译命令

在仓库根目录下执行：

1.  **环境检查**
    ```bash
    make check-platform
    ```

2.  **构建核心编译器**
    ```bash
    make all
    ```
    *注：此命令将自动编译 `toolsC` 与 `toolsASM` ，并链接为最终的 `aethelc` 可执行文件。*

3.  **产物验证**
    构建成功后，编译器二进制文件将位于：
    ```text
    build/output/aethelc
    ```

---

## ⚡ 核心技术特性

Aethelium 针对裸金属引导与现代 Windows 系统环境，设计了两种并行的编译与执行机制：

### 1. 裸金属与固件特性 (Bare-Metal & Firmware)
*   **无运行时 (Runtime-less)**: Aethelium 不依赖 libc 或任何复杂的运行时环境，生成的二进制文件除了硬件指令外没有任何冗余。
*   **直接 UEFI 发射**: 编译器内置 PE32+ 格式生成能力，无需 EDK II 等外部链接工具或庞大框架，一行命令即可直接编译生成标准的 `.efi` 应用程序或驱动。
*   **声明式硬件控制**: 通过 `@gate` 和 `@packed` 等语义，在高级语言层面实现对内存对齐、调用约定和寄存器行为的精确控制。
*   **物理 ROM 与模式切换**: 支持直接构建可烧录的物理 `.rom` 镜像。提供一键模式切换功能（Mode Jump），可在 16 位实模式、32 位保护模式与 64 位长模式之间无缝迁移，自动生成并加载内部 GDT/IDT。
*   **Apple Silicon 支持**: 针对苹果自研芯片，支持发射 MH_PRELOAD 格式的裸金属 Mach-O 固件，并自动封装为支持安全启动的 IM4P 容器。

### 2. Windows 原生特性 (Windows Native Application)
*   **零 C 运行时依赖 (Zero-CRT)**: 摆脱传统的 C 运行库限制，不引用任何标准的 C 初始化或内存管理代码，生成的程序启动极快、无运行库分发负担。
*   **Direct NT Portal (内核系统调用直接映射)**: 绕过传统的 Win32 API 封装层，通过 Native Portal 机制，直接在程序内部通过 IAT（导入地址表）绑定并访问 `ntdll.dll` 原生系统接口（例如 `NtWriteFile`, `NtAllocateVirtualMemory`, `NtCreateThreadEx`）。
*   **源码编织的 AELibrary**: Windows 的 I/O 操作（完成端口 IOCP、注册表、进程/线程）与图形界面均采用 [AELibrary](https://github.com/Aethel-Systems/AELibrary) 提供的 `.ae` 源代码进行编译期内联。配合激进的死代码消除（DCE），仅将使用的逻辑静态编织入最终可执行文件中。
*   **声明式渲染子系统 (Aura & Flux)**: AELibrary 内嵌基于 GDI 的轻量化声明式界面系统。窗口拦截并接管背景绘制（`WM_CTLCOLORSTATIC`, `WM_PAINT`），使得用户界面无需复杂的重绘逻辑即可完美呈现在 DWM Mica（Liquid Glass）毛玻璃质感背景之上。

---

## 📺 运行展示

Aethelium 的多平台构建能力可通过同一套编译器自由发射。

### 1. 裸金属/UEFI 示例
```aethelium
@entry
@gate(type: \efi)
func efi/main(image/handle: ptr<Void>, sys/table: ptr<EFI/SystemTable>) : UInt64 {
    // 层级命名空间: sys/cpu/id, efi/con_out...
    print("Hello, AethelOS World!")

    hardware {
        loop {
            hardware\isa\pause() // 直接调用底层 CPU 指令
        }
    }
    return 0 
}
```

### 2. Windows 原生应用示例
```aethelium
//Aethelium
@entry
func start() : UInt64 {
    print("Launching native Windows GUI tools...")
    
    // 1. 直接用 Windows 默认浏览器打开一个网页！
    // 呼叫 shell32.dll 里的 ShellExecuteA 魔法，最后一个参数 1 代表正常显示窗口
    win { shell32/ShellExecuteA(0, "open", "https://github.com/Aethel-Systems", 0, 0, 1) }
    
    // 2. 直接启动系统记事本！
    win { shell32/ShellExecuteA(0, "open", "notepad.exe", 0, 0, 1) }
    
    print("Launch commands sent successfully.")
    return 0
}

/* python，需要引入大约2200行的外部依赖实现简单的“启动浏览器和记事本"操作，还需要加载庞大的解释器
import subprocess
import webbrowser
webbrowser.open("https://github.com")
subprocess.Popen("notepad.exe")
*/
```

[UEFIDemo](https://github.com/Aethel-Systems/AetheliumDemo/UEFI/AERescue/assets/screenshot.png)

---

## 📂 使用手册

为了快速上手不同的编译目标，仓库中附带了详细的技术文档：

*   **Aethelium语言参考手册.md**: 提供 Aethelium（.ae）语法的核心说明。
*   **硬件层手册_机器码对照版.md**: 提供硬件直通与机器码生成的底层指南。

---

## ⚠️ 补充说明

*   **目标产物**: 本工具链支持交叉编译（Cross-Compilation）。生成的可执行文件根据命令行参数可以为 **Aethelium Native**（.let / .aki / .srv / .hda）、**裸机 BIN/ROM** 或者是 **Windows 平台 x86_64 原生应用**。
*   **拒绝 POSIX 历史包袱**: Aethelium 永远不会通过传统的 POSIX 库进行自我托管，其长远目标是在未来的、无 UNIX/POSIX 污染的纯净系统架构中，用 Aethelium 原生重写其编译器自身。

---

## 📄 许可证 (License)

本项目采用 **GNU General Public License v3.0 (GPLv3)** 开源协议。

**Copyright (C) 2024-2026 Aethel-Systems. All rights reserved.**