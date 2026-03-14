# Copyright (C) 2024-2026 Aethel-Systems. All rights reserved.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

# ============================================================================
# Aethelium Core Build System
# Migrated from AethelOS core bootstrap compiler chain
# ============================================================================

.PHONY: all build-dirs compilerCore check-platform clean distclean help status

ifeq ($(OS),Windows_NT)
export UNAME_S := Windows_NT
export UNAME_M := $(PROCESSOR_ARCHITECTURE)
else
export UNAME_S := $(shell uname -s 2>/dev/null)
export UNAME_M := $(shell uname -m 2>/dev/null)
endif
export ROOT := $(CURDIR)
export BUILD_DIR := $(ROOT)/build
export OUTPUT_DIR := $(BUILD_DIR)/output

# Core layout (refactored)
export CORE_DIR := $(ROOT)/core
export TOOLS_C_DIR := $(CORE_DIR)/toolsC
export TOOLS_C_INCLUDE := $(TOOLS_C_DIR)/include
export TOOLS_ASM_DIR := $(CORE_DIR)/toolsASM
export TOOLS_ASM_INCLUDE := $(TOOLS_ASM_DIR)/include
export TOOLS_ASM_SRC := $(TOOLS_ASM_DIR)/src

MINGW_X64_GCC := $(shell command -v x86_64-w64-mingw32-gcc 2>/dev/null)
os ?= native
ifeq ($(os),win)
TARGET_OS := windows
else
TARGET_OS := native
endif

# Toolchain common flags
export NATIVE_LDFLAGS :=
export NATIVE_LDLIBS :=

# Toolchain
ifeq ($(UNAME_S),Darwin)
ifeq ($(TARGET_OS),windows)
ifeq ($(MINGW_X64_GCC),)
$(error TARGET_OS=windows requires x86_64-w64-mingw32-gcc in PATH)
endif
export PLATFORM := windowsX64
export NATIVE_CC ?= x86_64-w64-mingw32-gcc
export NATIVE_CFLAGS := -Wall -Wextra -O2 -g -DWIN32_LEAN_AND_MEAN
export NATIVE_LDLIBS += -lkernel32
else
export PLATFORM := darwinX64
export NATIVE_CC ?= clang
export NATIVE_CFLAGS := -Wall -Wextra -O2 -g -arch x86_64
endif
else ifeq ($(UNAME_S),Linux)
export PLATFORM := linuxX64
export NATIVE_CC ?= clang
export NATIVE_CFLAGS := -Wall -Wextra -O2 -g
else ifeq ($(UNAME_S),Windows_NT)
export NATIVE_CC ?= clang
export NATIVE_CFLAGS := -Wall -Wextra -O2 -g -DWIN32_LEAN_AND_MEAN
export NATIVE_LDLIBS += -lkernel32
ifneq (,$(filter ARM64 AARCH64 arm64 aarch64,$(UNAME_M)))
export PLATFORM := windowsARM64
export NATIVE_CFLAGS += --target=x86_64-w64-mingw32
export NATIVE_LDFLAGS += --target=x86_64-w64-mingw32
else
export PLATFORM := windowsX64
endif
else ifneq (,$(findstring MINGW,$(UNAME_S)))
export NATIVE_CC ?= clang
export NATIVE_CFLAGS := -Wall -Wextra -O2 -g -DWIN32_LEAN_AND_MEAN
export NATIVE_LDLIBS += -lkernel32
ifneq (,$(filter arm64 aarch64,$(UNAME_M)))
export PLATFORM := windowsARM64
export NATIVE_CFLAGS += --target=x86_64-w64-mingw32
export NATIVE_LDFLAGS += --target=x86_64-w64-mingw32
else
export PLATFORM := windowsX64
endif
else ifneq (,$(findstring MSYS,$(UNAME_S)))
export NATIVE_CC ?= clang
export NATIVE_CFLAGS := -Wall -Wextra -O2 -g -DWIN32_LEAN_AND_MEAN
export NATIVE_LDLIBS += -lkernel32
ifneq (,$(filter arm64 aarch64,$(UNAME_M)))
export PLATFORM := windowsARM64
export NATIVE_CFLAGS += --target=x86_64-w64-mingw32
export NATIVE_LDFLAGS += --target=x86_64-w64-mingw32
else
export PLATFORM := windowsX64
endif
else ifneq (,$(findstring CYGWIN,$(UNAME_S)))
export NATIVE_CC ?= clang
export NATIVE_CFLAGS := -Wall -Wextra -O2 -g -DWIN32_LEAN_AND_MEAN
export NATIVE_LDLIBS += -lkernel32
ifneq (,$(filter arm64 aarch64,$(UNAME_M)))
export PLATFORM := windowsARM64
export NATIVE_CFLAGS += --target=x86_64-w64-mingw32
export NATIVE_LDFLAGS += --target=x86_64-w64-mingw32
else
export PLATFORM := windowsX64
endif
else
$(error Unsupported platform: $(UNAME_S))
endif

ifneq (,$(filter windowsX64 windowsARM64,$(PLATFORM)))
NASM_FORMAT := win64
else ifeq ($(UNAME_S),Darwin)
NASM_FORMAT := macho64
else
NASM_FORMAT := elf64
endif
ifeq ($(NASM_FORMAT),win64)
NASM_FLAGS := -f $(NASM_FORMAT) -g -F cv8
else
NASM_FLAGS := -f $(NASM_FORMAT) -g -F dwarf
endif
NASM_INCLUDE := -I$(TOOLS_ASM_INCLUDE)/

# Output binary
ifeq ($(NASM_FORMAT),win64)
EXE_EXT := .exe
OBJ_EXT := .obj
else
EXE_EXT :=
OBJ_EXT := .o
endif
export COMPILER_CORE := $(OUTPUT_DIR)/aethelc$(EXE_EXT)

# toolsC/aetb
AETB_GEN_SRC := $(TOOLS_C_DIR)/aetb/src/aetb_gen.c
AETB_GEN_OBJ := $(patsubst $(ROOT)/%.c,$(OUTPUT_DIR)/%$(OBJ_EXT),$(AETB_GEN_SRC))

# toolsASM
TOOLS_ASM_MAIN := $(TOOLS_ASM_SRC)/main.asm
TOOLS_ASM_EMIT := $(TOOLS_ASM_SRC)/core/binary_emit.asm
TOOLS_ASM_WEAVER := $(TOOLS_ASM_SRC)/core/binary_weaver.asm
TOOLS_ASM_SYSCALL_MACOS := $(TOOLS_ASM_SRC)/core/syscall_macos.asm
TOOLS_ASM_OS_WINDOWS := $(TOOLS_ASM_SRC)/core/os_windows.asm
ifeq ($(NASM_FORMAT),win64)
TOOLS_ASM_OS_SRC := $(TOOLS_ASM_OS_WINDOWS)
else
TOOLS_ASM_OS_SRC := $(TOOLS_ASM_SYSCALL_MACOS)
endif
TOOLS_ASM_SRCS := $(TOOLS_ASM_MAIN) $(TOOLS_ASM_EMIT) $(TOOLS_ASM_WEAVER) $(TOOLS_ASM_OS_SRC)
TOOLS_ASM_OBJS := $(patsubst $(ROOT)/%.asm,$(OUTPUT_DIR)/%$(OBJ_EXT),$(TOOLS_ASM_SRCS))

# toolsC/compiler sources
COMPILER_C_SRC_DIR := $(TOOLS_C_DIR)/compiler/src
COMPILER_C_MAIN_DIR := $(COMPILER_C_SRC_DIR)/main
COMPILER_C_FRONTEND_DIR := $(COMPILER_C_SRC_DIR)/frontend
COMPILER_C_LEXER_DIR := $(COMPILER_C_FRONTEND_DIR)/lexer
COMPILER_C_PARSER_DIR := $(COMPILER_C_FRONTEND_DIR)/parser
COMPILER_C_SYNTAX_DIR := $(COMPILER_C_FRONTEND_DIR)/syntax
COMPILER_C_MIDDLEEND_DIR := $(COMPILER_C_SRC_DIR)/middleend
COMPILER_C_CODEGEN_DIR := $(COMPILER_C_MIDDLEEND_DIR)/codegen
COMPILER_C_LINKER_DIR := $(COMPILER_C_SRC_DIR)/linker
COMPILER_C_FORMATS_DIR := $(COMPILER_C_SRC_DIR)/formats

COMPILER_C_MAIN := $(COMPILER_C_MAIN_DIR)/aethelc_main.c
COMPILER_C_AETHELC := $(COMPILER_C_MAIN_DIR)/aethelc.c
COMPILER_C_LEXER := $(COMPILER_C_LEXER_DIR)/aec_lexer.c
COMPILER_C_UNIX_STRIKE := $(COMPILER_C_LEXER_DIR)/unix_strike.c
COMPILER_C_STRING_TABLE := $(COMPILER_C_PARSER_DIR)/ast_string_table.c
COMPILER_C_STRING_TABLE_INTEGRATION := $(COMPILER_C_PARSER_DIR)/ast_string_table_integration.c
COMPILER_C_PARSER := $(COMPILER_C_PARSER_DIR)/aec_parser.c
COMPILER_C_PREPROCESS := $(COMPILER_C_FRONTEND_DIR)/preprocessor.c
COMPILER_C_SEMANTIC := $(COMPILER_C_FRONTEND_DIR)/semantic_checker.c
COMPILER_C_SILICON_SEM := $(COMPILER_C_FRONTEND_DIR)/silicon_semantics.c
COMPILER_C_IMPORT_RESOLVER := $(COMPILER_C_FRONTEND_DIR)/import_resolver.c
COMPILER_C_HARDWARE_LAYER := $(COMPILER_C_FRONTEND_DIR)/hardware_layer.c
COMPILER_C_HARDWARE_GATES := $(COMPILER_C_FRONTEND_DIR)/hardware_gates.c
COMPILER_C_CODEGEN := $(COMPILER_C_CODEGEN_DIR)/aec_codegen.c
COMPILER_C_BINARY_FMT := $(COMPILER_C_CODEGEN_DIR)/binary_format.c
COMPILER_C_SILICON_CODEGEN := $(COMPILER_C_MIDDLEEND_DIR)/silicon_semantics_codegen.c
COMPILER_C_ZERO_COPY_VIEW := $(COMPILER_C_MIDDLEEND_DIR)/zero_copy_view.c
COMPILER_C_BUILTIN_PRINT := $(COMPILER_C_MIDDLEEND_DIR)/builtin_print.c
COMPILER_C_HARDWARE_CODEGEN := $(COMPILER_C_MIDDLEEND_DIR)/hardware_codegen.c
COMPILER_C_X86_ENCODER := $(COMPILER_C_MIDDLEEND_DIR)/x86_encoder.c
COMPILER_C_AEFS := $(TOOLS_C_DIR)/mkiso/aefs/aefs.c
COMPILER_C_ADL_DISK := $(TOOLS_C_DIR)/mkiso/aefs/adl_disk.c
COMPILER_C_FORMAT_COMMON := $(COMPILER_C_FORMATS_DIR)/common/format_common.c
COMPILER_C_FORMAT_AKI := $(COMPILER_C_FORMATS_DIR)/aki/aki_gen.c
COMPILER_C_FORMAT_HDA := $(COMPILER_C_FORMATS_DIR)/hda/hda_gen.c
COMPILER_C_FORMAT_SRV := $(COMPILER_C_FORMATS_DIR)/srv/srv_gen.c
COMPILER_C_FORMAT_AX := $(COMPILER_C_FORMATS_DIR)/ax/ax_gen.c
COMPILER_C_FORMAT_PE := $(COMPILER_C_FORMATS_DIR)/efi/pe_gen.c
COMPILER_C_FORMAT_PE_INDUSTRIAL := $(COMPILER_C_FORMATS_DIR)/efi/pe_industrial.c
COMPILER_C_FORMAT_LET := $(COMPILER_C_FORMATS_DIR)/let/let_gen.c
COMPILER_C_FORMAT_LET_WEAVER := $(COMPILER_C_FORMATS_DIR)/let/let_weaver_bridge.c
COMPILER_C_FORMAT_ROM := $(COMPILER_C_FORMATS_DIR)/rom/rom_gen.c

COMPILER_C_SRCS := $(COMPILER_C_MAIN) $(COMPILER_C_AETHELC) \
	$(COMPILER_C_LEXER) $(COMPILER_C_UNIX_STRIKE) $(COMPILER_C_STRING_TABLE) $(COMPILER_C_STRING_TABLE_INTEGRATION) \
	$(COMPILER_C_PREPROCESS) $(COMPILER_C_SEMANTIC) $(COMPILER_C_PARSER) $(COMPILER_C_SILICON_SEM) $(COMPILER_C_IMPORT_RESOLVER) \
	$(COMPILER_C_HARDWARE_LAYER) $(COMPILER_C_HARDWARE_GATES) \
	$(COMPILER_C_CODEGEN) $(COMPILER_C_BINARY_FMT) $(COMPILER_C_SILICON_CODEGEN) $(COMPILER_C_ZERO_COPY_VIEW) $(COMPILER_C_BUILTIN_PRINT) \
	$(COMPILER_C_HARDWARE_CODEGEN) $(COMPILER_C_X86_ENCODER) \
	$(COMPILER_C_AEFS) $(COMPILER_C_ADL_DISK) \
	$(COMPILER_C_FORMAT_COMMON) $(COMPILER_C_FORMAT_AKI) $(COMPILER_C_FORMAT_HDA) \
	$(COMPILER_C_FORMAT_SRV) $(COMPILER_C_FORMAT_AX) \
	$(COMPILER_C_FORMAT_PE) $(COMPILER_C_FORMAT_PE_INDUSTRIAL) $(COMPILER_C_FORMAT_LET) $(COMPILER_C_FORMAT_LET_WEAVER) \
	$(COMPILER_C_FORMAT_ROM)

COMPILER_C_OBJS := $(patsubst $(ROOT)/%.c,$(OUTPUT_DIR)/%$(OBJ_EXT),$(COMPILER_C_SRCS))

all: compilerCore

build-dirs:
	@mkdir -p $(OUTPUT_DIR)

check-platform:
	@echo "OS: $(UNAME_S)"
	@echo "Build selector (os): $(os)"
	@echo "Platform: $(PLATFORM)"
	@echo "Native CC: $(NATIVE_CC)"
	@echo "Native CFLAGS: $(NATIVE_CFLAGS)"
	@echo "Native LDFLAGS: $(NATIVE_LDFLAGS)"
	@echo "Native LDLIBS: $(NATIVE_LDLIBS)"
	@echo "NASM format: $(NASM_FORMAT)"

compilerCore: $(COMPILER_CORE)
	@echo "================================================================================"
	@echo "✓ Core Compiler Complete: Bootstrap C compiler ready"
	@echo "  Binary: $(COMPILER_CORE)"
	@echo "================================================================================"

$(COMPILER_CORE): $(COMPILER_C_OBJS) $(AETB_GEN_OBJ) $(TOOLS_ASM_OBJS) | build-dirs
	@echo "================================================================================"
	@echo "Linking Core Compiler (native C bootstrap compiler + assembly layer)"
	@echo "  C Objects: $(words $(COMPILER_C_OBJS)) files"
	@echo "  Assembly Objects: $(words $(TOOLS_ASM_OBJS)) files"
	@echo "  Output: $(COMPILER_CORE)"
	@echo "================================================================================"
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(NATIVE_LDFLAGS) -o $@ $(COMPILER_C_OBJS) $(AETB_GEN_OBJ) $(TOOLS_ASM_OBJS) $(NATIVE_LDLIBS)
	@echo "✓ Core Compiler linked successfully: $@"

$(OUTPUT_DIR)/core/toolsC/compiler/%$(OBJ_EXT): $(ROOT)/core/toolsC/compiler/%.c
	@mkdir -p $(dir $@)
	@echo "  [CC] Compiling: $(notdir $<)"
	$(NATIVE_CC) -c $(NATIVE_CFLAGS) \
		-I$(TOOLS_C_INCLUDE) \
		-I$(TOOLS_ASM_INCLUDE) \
		-I$(COMPILER_C_FRONTEND_DIR) \
		-I$(COMPILER_C_LEXER_DIR) \
		-I$(COMPILER_C_PARSER_DIR) \
		-I$(COMPILER_C_SYNTAX_DIR) \
		-I$(COMPILER_C_MIDDLEEND_DIR) \
		-I$(COMPILER_C_CODEGEN_DIR) \
		-I$(COMPILER_C_LINKER_DIR) \
		-I$(COMPILER_C_MAIN_DIR) \
		-I$(COMPILER_C_FORMATS_DIR)/common \
		-I$(COMPILER_C_FORMATS_DIR)/aki \
		-I$(COMPILER_C_FORMATS_DIR)/hda \
		-I$(COMPILER_C_FORMATS_DIR)/srv \
		-I$(COMPILER_C_FORMATS_DIR)/ax \
		-I$(COMPILER_C_FORMATS_DIR)/efi \
		-I$(COMPILER_C_FORMATS_DIR)/let \
		-I$(COMPILER_C_FORMATS_DIR)/rom \
		-I$(TOOLS_C_DIR)/aetb/src \
		$< -o $@

$(OUTPUT_DIR)/core/toolsC/aetb/%$(OBJ_EXT): $(ROOT)/core/toolsC/aetb/%.c
	@echo "[CC] Compiling AETB Generator: $<"
	@mkdir -p $(@D)
	$(NATIVE_CC) -c $(NATIVE_CFLAGS) \
		-I$(TOOLS_C_INCLUDE) \
		-I$(TOOLS_C_DIR)/aetb/src \
		$< -o $@

$(OUTPUT_DIR)/core/toolsC/mkiso/%$(OBJ_EXT): $(ROOT)/core/toolsC/mkiso/%.c
	@mkdir -p $(dir $@)
	@echo "  [CC] Compiling mkiso source: $(notdir $<)"
	$(NATIVE_CC) -c $(NATIVE_CFLAGS) -I$(TOOLS_C_INCLUDE) $< -o $@

$(OUTPUT_DIR)/core/toolsASM/%$(OBJ_EXT): $(ROOT)/core/toolsASM/%.asm
	@mkdir -p $(dir $@)
	@echo "  [NASM] Assembling: $(notdir $<)"
	nasm $(NASM_FLAGS) $(NASM_INCLUDE) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

# Backward-compatible aliases

# New aliases
compilerCore: $(COMPILER_CORE)

status:
	@test -f "$(COMPILER_CORE)" && echo "✓ Core Compiler" || echo "✗ Core Compiler (missing)"

help:
	@echo "make all            - Build core compiler"
	@echo "make all os=win     - Cross-build Windows x64 on macOS (mingw-w64 required)"
	@echo "make compilerCore   - Build core compiler"
	@echo "make check-platform - Show platform/toolchain info"
	@echo "make clean          - Remove build outputs"
