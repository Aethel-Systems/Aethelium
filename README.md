# Aethelium

Aethelium 当前聚焦于 **AethelOS Stage 1 引导编译器链** 的独立化与可维护重构。

本仓库已完成以下工作：
- 从 `AethelOS` 完整迁移 `stage1` 相关源码。
- 将 Stage 1 统一重构到 `stage1/` 目录下。
- 在仓库根目录提供可独立构建的 `Makefile`，复用 AethelOS 的 Stage 1 逻辑（`compilerStage1`）。

## 目录结构

```text
Aethelium/
├── Makefile
├── README.md
└── stage1/
    ├── toolsC/
    │   ├── include/
    │   ├── aetb/
    │   ├── compiler/
    │   └── mkiso/aefs/
    └── toolsASM/
        ├── include/
        └── src/
```

## Stage 1 包含内容

- `toolsC/compiler`: C 实现的引导编译器（aethelc stage1）。
- `toolsC/aetb`: AETB 生成相关模块。
- `toolsC/include`: 公共头文件（如 `aefs.h`、`binary_format.h`）。
- `toolsC/mkiso/aefs`: Stage 1 需要的 AEFS/ADL 支持实现。
- `toolsASM`: 汇编发射层与系统调用桥接。

## 构建

### 依赖

- `clang`
- `nasm`
- `make`

> macOS 使用 `macho64` NASM 目标，Linux 使用 `elf64`。

### 命令

```bash
make check-platform
make compilerStage1
```

默认输出：

```text
build/output/aethelcStage1
```

## 其他目标

```bash
make all            # 等同 compilerStage1
make status         # 查看 stage1 编译器产物状态
make clean          # 清理 build/
make distclean      # 同 clean
```

## 说明

- 本仓库当前仅承载 Stage 1 相关内容，不包含 AethelOS 的完整内核/安装盘/ISO 全链路构建。
- 如需扩展 Stage 2 或完整系统构建，可在此结构基础上继续模块化引入。
