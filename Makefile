# ============================================================================
# Aethelium Stage 1 Build System
# Migrated from AethelOS Stage 1 compiler chain
# ============================================================================

.PHONY: all build-dirs compilerStage1 check-platform clean distclean help status

export UNAME_S := $(shell uname -s)
export ROOT := $(CURDIR)
export BUILD_DIR := $(ROOT)/build
export OUTPUT_DIR := $(BUILD_DIR)/output

# Stage1 layout (refactored)
export STAGE1_DIR := $(ROOT)/stage1
export TOOLS_C_DIR := $(STAGE1_DIR)/toolsC
export TOOLS_C_INCLUDE := $(TOOLS_C_DIR)/include
export TOOLS_ASM_DIR := $(STAGE1_DIR)/toolsASM
export TOOLS_ASM_INCLUDE := $(TOOLS_ASM_DIR)/include
export TOOLS_ASM_SRC := $(TOOLS_ASM_DIR)/src

# Toolchain
ifeq ($(UNAME_S),Darwin)
	export PLATFORM := darwin-x86_64
	export NATIVE_CC ?= clang
	export NATIVE_CFLAGS := -Wall -Wextra -O2 -g -arch x86_64
else ifeq ($(UNAME_S),Linux)
	export PLATFORM := linux-x86_64
	export NATIVE_CC ?= clang
	export NATIVE_CFLAGS := -Wall -Wextra -O2 -g
else
	$(error Unsupported platform: $(UNAME_S))
endif

ifeq ($(UNAME_S),Darwin)
	NASM_FORMAT := macho64
else
	NASM_FORMAT := elf64
endif
NASM_FLAGS := -f $(NASM_FORMAT) -g -F dwarf
NASM_INCLUDE := -I$(TOOLS_ASM_INCLUDE)/

# Output binary
export COMPILER_STAGE1 := $(OUTPUT_DIR)/aethelcStage1

# toolsC/aetb
AETB_GEN_SRC := $(TOOLS_C_DIR)/aetb/src/aetb_gen.c
AETB_GEN_OBJ := $(patsubst $(ROOT)/%.c,$(OUTPUT_DIR)/%.o,$(AETB_GEN_SRC))

# toolsASM
TOOLS_ASM_MAIN := $(TOOLS_ASM_SRC)/main.asm
TOOLS_ASM_EMIT := $(TOOLS_ASM_SRC)/core/binary_emit.asm
TOOLS_ASM_WEAVER := $(TOOLS_ASM_SRC)/core/binary_weaver.asm
TOOLS_ASM_SYSCALL_MACOS := $(TOOLS_ASM_SRC)/core/syscall_macos.asm
TOOLS_ASM_SRCS := $(TOOLS_ASM_MAIN) $(TOOLS_ASM_EMIT) $(TOOLS_ASM_WEAVER) $(TOOLS_ASM_SYSCALL_MACOS)
TOOLS_ASM_OBJS := $(patsubst $(ROOT)/%.asm,$(OUTPUT_DIR)/%.o,$(TOOLS_ASM_SRCS))

# toolsC/compiler sources (from AethelOS stage1)
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

COMPILER_C_SRCS := $(COMPILER_C_MAIN) $(COMPILER_C_AETHELC) \
	$(COMPILER_C_LEXER) $(COMPILER_C_UNIX_STRIKE) $(COMPILER_C_STRING_TABLE) $(COMPILER_C_STRING_TABLE_INTEGRATION) \
	$(COMPILER_C_PREPROCESS) $(COMPILER_C_SEMANTIC) $(COMPILER_C_PARSER) $(COMPILER_C_SILICON_SEM) $(COMPILER_C_IMPORT_RESOLVER) \
	$(COMPILER_C_HARDWARE_LAYER) $(COMPILER_C_HARDWARE_GATES) \
	$(COMPILER_C_CODEGEN) $(COMPILER_C_BINARY_FMT) $(COMPILER_C_SILICON_CODEGEN) $(COMPILER_C_ZERO_COPY_VIEW) $(COMPILER_C_BUILTIN_PRINT) \
	$(COMPILER_C_HARDWARE_CODEGEN) $(COMPILER_C_X86_ENCODER) \
	$(COMPILER_C_AEFS) $(COMPILER_C_ADL_DISK) \
	$(COMPILER_C_FORMAT_COMMON) $(COMPILER_C_FORMAT_AKI) $(COMPILER_C_FORMAT_HDA) \
	$(COMPILER_C_FORMAT_SRV) $(COMPILER_C_FORMAT_AX) \
	$(COMPILER_C_FORMAT_PE) $(COMPILER_C_FORMAT_PE_INDUSTRIAL) $(COMPILER_C_FORMAT_LET) $(COMPILER_C_FORMAT_LET_WEAVER)

COMPILER_C_OBJS := $(patsubst $(ROOT)/%.c,$(OUTPUT_DIR)/%.o,$(COMPILER_C_SRCS))

all: compilerStage1

build-dirs:
	@mkdir -p $(OUTPUT_DIR)

check-platform:
	@echo "OS: $(UNAME_S)"
	@echo "Platform: $(PLATFORM)"
	@echo "Native CC: $(NATIVE_CC)"
	@echo "NASM format: $(NASM_FORMAT)"

compilerStage1: $(COMPILER_STAGE1)
	@echo "================================================================================"
	@echo "✓ Stage 1 Complete: Bootstrap C compiler ready"
	@echo "  Binary: $(COMPILER_STAGE1)"
	@echo "================================================================================"

$(COMPILER_STAGE1): $(COMPILER_C_OBJS) $(AETB_GEN_OBJ) $(TOOLS_ASM_OBJS) | build-dirs
	@echo "================================================================================"
	@echo "Linking Stage 1 Compiler (native C bootstrap compiler + assembly layer)"
	@echo "  C Objects: $(words $(COMPILER_C_OBJS)) files"
	@echo "  Assembly Objects: $(words $(TOOLS_ASM_OBJS)) files"
	@echo "  Output: $(COMPILER_STAGE1)"
	@echo "================================================================================"
	$(NATIVE_CC) $(NATIVE_CFLAGS) -o $@ $(COMPILER_C_OBJS) $(AETB_GEN_OBJ) $(TOOLS_ASM_OBJS)
	@echo "✓ Stage 1 Compiler linked successfully: $@"

$(OUTPUT_DIR)/stage1/toolsC/compiler/%.o: $(ROOT)/stage1/toolsC/compiler/%.c
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
		-I$(TOOLS_C_DIR)/aetb/src \
		$< -o $@

$(OUTPUT_DIR)/stage1/toolsC/aetb/%.o: $(ROOT)/stage1/toolsC/aetb/%.c
	@echo "[CC] Compiling AETB Generator: $<"
	@mkdir -p $(@D)
	$(NATIVE_CC) -c $(NATIVE_CFLAGS) \
		-I$(TOOLS_C_INCLUDE) \
		-I$(TOOLS_C_DIR)/aetb/src \
		$< -o $@

$(OUTPUT_DIR)/stage1/toolsC/mkiso/%.o: $(ROOT)/stage1/toolsC/mkiso/%.c
	@mkdir -p $(dir $@)
	@echo "  [CC] Compiling mkiso source: $(notdir $<)"
	$(NATIVE_CC) -c $(NATIVE_CFLAGS) -I$(TOOLS_C_INCLUDE) $< -o $@

$(OUTPUT_DIR)/stage1/toolsASM/%.o: $(ROOT)/stage1/toolsASM/%.asm
	@mkdir -p $(dir $@)
	@echo "  [NASM] Assembling: $(notdir $<)"
	nasm $(NASM_FLAGS) $(NASM_INCLUDE) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

# Keep alias with AethelOS naming habit
bootstrap-compiler-stage1: compilerStage1
compiler-stage1: compilerStage1

distclean: clean

status:
	@test -f "$(COMPILER_STAGE1)" && echo "✓ Stage 1 Compiler" || echo "✗ Stage 1 Compiler (missing)"

help:
	@echo "make compilerStage1   - Build stage1 bootstrap compiler"
	@echo "make check-platform   - Show platform/toolchain info"
	@echo "make clean            - Remove build outputs"
