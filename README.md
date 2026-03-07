# Aethelium

> **面向硬件、无运行时、为现代底层系统编程而生的语言工具链。**
> A Hardware-First, Runtime-Less Systems Language Toolchain.

**Aethelium** 是一个独立的、自洽的系统编程语言工具链。它摒弃了传统的“编译器-汇编器-链接器”繁琐流程，专注于为 **UEFI 环境** 和 **裸机 (Bare-metal)** 开发提供极致精简的构建方案。

本仓库包含 Aethelium 的**自举核心 (Bootstrap Core)**，能够在宿主机（macOS/Linux）上直接生成目标架构的原生机器码或 UEFI PE 可执行文件，无需依赖标准对象文件（.obj/.o）或外部链接器。

---

## 🏗 核心架构

Aethelium 采用独特的 **"Weaver-Filler" (编排-填充)** 双引擎架构，实现了从高级语义到硅片逻辑的直接映射：

*   **Binary Weaver (二进制织机 - `toolsASM`)**:
    负责底层二进制布局的物理编排。
*   **Logic Filler (逻辑填充器 - `toolsC`)**:
    C 语言实现的编译器前端。负责 Aethelium 语法的语义分析、AST 构建，并将生成的机器码逻辑精准填充至织机预设的内存槽位中。

### 目录结构说明

```text
Aethelium/
├── Makefile                # 统一构建编排系统
├── README.md               # 项目技术文档
└── core/                   # 核心工具链源码
    ├── toolsC/             # [Frontend] 编译器前端与逻辑填充层
    │   ├── include/        #   - 核心头文件 (二进制格式规范、AEFS 协议)
    │   ├── aetb/           #   - AETB 中间格式生成引擎
    │   ├── compiler/       #   - 词法/语法分析与代码生成主逻辑
    │   └── mkiso/aefs/     #   - 引导介质与文件系统支持
    └── toolsASM/           # [Backend] 二进制织机与汇编发射层
        ├── include/        #   - 汇编接口定义
        └── src/            #   - 核心指令发射与布局实现 (NASM)
```

---

## 🛠 构建工作流

### 环境依赖

构建 Aethelium 工具链需要以下标准环境：

| 组件 | 要求 | 说明 |
| :--- | :--- | :--- |
| **Compiler** | Clang / GCC | 支持 C11 标准，推荐 Clang |
| **Assembler** | NASM 2.15+ | 用于处理后端二进制编排 |
| **Build System** | GNU Make 4.0+ | 自动化构建管理 |

*支持架构：Darwin (macOS) x86_64/arm64, Linux x86_64*

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

### 其他操作

```bash
make clean          # 清理构建产生的中间文件
make distclean      # 重置仓库至初始状态
make status         # 查看当前构建产物信息
```

---

## ⚡ 技术特性

*   **无运行时 (Runtime-less)**: Aethelium 不依赖 libc 或任何复杂的运行时环境，生成的二进制文件除了硬件指令外没有任何冗余。
*   **直接 UEFI 发射**: 编译器内置 PE32+ 格式生成能力，一行命令即可生成 `.efi` 应用，无需 EDK II 等庞大框架。
*   **声明式硬件控制**: 通过 `@gate` 和 `@packed` 等语义，在高级语言层面实现对内存对齐、调用约定和寄存器行为的精确控制。

---

## ⚠️ 说明

*   **目标产物**: 本工具链生成的二进制文件通常为 **Aethelium Native** 、**x86机器码** 格式或 **UEFI PE** 格式，无法直接在 Windows 或 Linux 宿主机上运行（除非在虚拟机或裸机环境中）。
*   **交叉编译**: 本仓库提供的工具链本质上是运行在宿主机上的交叉编译器 (Cross-Compiler)。

---
