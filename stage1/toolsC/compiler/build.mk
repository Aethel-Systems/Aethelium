# ============================================================================
# AethelOS Stage 1 C Compiler Build Module - toolsC/compiler/build.mk
# ============================================================================
# Builds the native C compiler (aethelc) that bootstraps the AE compiler
# This compiler translates AE source code to AETB intermediate format
# Note: Linking is handled in root Makefile to avoid duplicate target definitions
# ============================================================================

# Compiler source directory structure
COMPILER_C_SRC_DIR          := $(TOOLS_C_DIR)/compiler/src
COMPILER_C_MAIN_DIR         := $(COMPILER_C_SRC_DIR)/main
COMPILER_C_FRONTEND_DIR     := $(COMPILER_C_SRC_DIR)/frontend
COMPILER_C_LEXER_DIR        := $(COMPILER_C_FRONTEND_DIR)/lexer
COMPILER_C_PARSER_DIR       := $(COMPILER_C_FRONTEND_DIR)/parser
COMPILER_C_SYNTAX_DIR       := $(COMPILER_C_FRONTEND_DIR)/syntax
COMPILER_C_MIDDLEEND_DIR    := $(COMPILER_C_SRC_DIR)/middleend
COMPILER_C_CODEGEN_DIR      := $(COMPILER_C_MIDDLEEND_DIR)/codegen
COMPILER_C_LINKER_DIR       := $(COMPILER_C_SRC_DIR)/linker
COMPILER_C_FORMATS_DIR      := $(COMPILER_C_SRC_DIR)/formats

# ============================================================================
# C Compiler Source Files
# ============================================================================

# Main entry point
COMPILER_C_MAIN         := $(COMPILER_C_MAIN_DIR)/aethelc_main.c
COMPILER_C_AETHELC      := $(COMPILER_C_MAIN_DIR)/aethelc.c

# Frontend: Lexer, Parser and Preprocessor  
COMPILER_C_LEXER        := $(COMPILER_C_LEXER_DIR)/aec_lexer.c
COMPILER_C_UNIX_STRIKE  := $(COMPILER_C_LEXER_DIR)/unix_strike.c
COMPILER_C_STRING_TABLE := $(COMPILER_C_PARSER_DIR)/ast_string_table.c
COMPILER_C_STRING_TABLE_INTEGRATION := $(COMPILER_C_PARSER_DIR)/ast_string_table_integration.c
COMPILER_C_PARSER       := $(COMPILER_C_PARSER_DIR)/aec_parser.c
COMPILER_C_PREPROCESS   := $(COMPILER_C_FRONTEND_DIR)/preprocessor.c
COMPILER_C_SEMANTIC     := $(COMPILER_C_FRONTEND_DIR)/semantic_checker.c
COMPILER_C_SILICON_SEM  := $(COMPILER_C_FRONTEND_DIR)/silicon_semantics.c
COMPILER_C_IMPORT_RESOLVER := $(COMPILER_C_FRONTEND_DIR)/import_resolver.c
COMPILER_C_HARDWARE_LAYER := $(COMPILER_C_FRONTEND_DIR)/hardware_layer.c
COMPILER_C_HARDWARE_GATES := $(COMPILER_C_FRONTEND_DIR)/hardware_gates.c

# Middle-end: Code Generation
COMPILER_C_CODEGEN      := $(COMPILER_C_CODEGEN_DIR)/aec_codegen.c
COMPILER_C_BINARY_FMT   := $(COMPILER_C_CODEGEN_DIR)/binary_format.c
COMPILER_C_SILICON_CODEGEN := $(COMPILER_C_MIDDLEEND_DIR)/silicon_semantics_codegen.c
COMPILER_C_ZERO_COPY_VIEW := $(COMPILER_C_MIDDLEEND_DIR)/zero_copy_view.c
COMPILER_C_BUILTIN_PRINT := $(COMPILER_C_MIDDLEEND_DIR)/builtin_print.c
COMPILER_C_HARDWARE_LAYER := $(COMPILER_C_FRONTEND_DIR)/hardware_layer.c
COMPILER_C_HARDWARE_CODEGEN := $(COMPILER_C_MIDDLEEND_DIR)/hardware_codegen.c
COMPILER_C_X86_ENCODER := $(COMPILER_C_MIDDLEEND_DIR)/x86_encoder.c


# AEFS utility (for ISO generation)
COMPILER_C_AEFS         := $(TOOLS_C_DIR)/mkiso/aefs/aefs.c

# ADL disk layout implementation (for ISO generation)
COMPILER_C_ADL_DISK     := $(TOOLS_C_DIR)/mkiso/aefs/adl_disk.c

# Binary Format Modules (Pure AE compilation chain)
COMPILER_C_FORMAT_COMMON := $(COMPILER_C_FORMATS_DIR)/common/format_common.c
COMPILER_C_FORMAT_AKI   := $(COMPILER_C_FORMATS_DIR)/aki/aki_gen.c
COMPILER_C_FORMAT_HDA   := $(COMPILER_C_FORMATS_DIR)/hda/hda_gen.c
COMPILER_C_FORMAT_SRV   := $(COMPILER_C_FORMATS_DIR)/srv/srv_gen.c
COMPILER_C_FORMAT_AX    := $(COMPILER_C_FORMATS_DIR)/ax/ax_gen.c
COMPILER_C_FORMAT_PE    := $(COMPILER_C_FORMATS_DIR)/efi/pe_gen.c
COMPILER_C_FORMAT_PE_INDUSTRIAL := $(COMPILER_C_FORMATS_DIR)/efi/pe_industrial.c
COMPILER_C_FORMAT_LET   := $(COMPILER_C_FORMATS_DIR)/let/let_gen.c
COMPILER_C_FORMAT_LET_WEAVER := $(COMPILER_C_FORMATS_DIR)/let/let_weaver_bridge.c

# All compiler C sources
COMPILER_C_SRCS := $(COMPILER_C_MAIN) $(COMPILER_C_AETHELC) \
                   $(COMPILER_C_LEXER) $(COMPILER_C_UNIX_STRIKE) $(COMPILER_C_STRING_TABLE) $(COMPILER_C_STRING_TABLE_INTEGRATION) $(COMPILER_C_PREPROCESS) $(COMPILER_C_SEMANTIC) $(COMPILER_C_PARSER) $(COMPILER_C_SILICON_SEM) $(COMPILER_C_IMPORT_RESOLVER) \
                   $(COMPILER_C_HARDWARE_LAYER) $(COMPILER_C_HARDWARE_GATES) \
                   $(COMPILER_C_CODEGEN) $(COMPILER_C_BINARY_FMT) $(COMPILER_C_SILICON_CODEGEN) $(COMPILER_C_ZERO_COPY_VIEW) $(COMPILER_C_BUILTIN_PRINT) \
                   $(COMPILER_C_HARDWARE_CODEGEN) $(COMPILER_C_X86_ENCODER) \
                   $(COMPILER_C_AEFS) $(COMPILER_C_ADL_DISK) \
                   $(COMPILER_C_FORMAT_COMMON) $(COMPILER_C_FORMAT_AKI) $(COMPILER_C_FORMAT_HDA) \
                   $(COMPILER_C_FORMAT_SRV) $(COMPILER_C_FORMAT_AX) \
                   $(COMPILER_C_FORMAT_PE) $(COMPILER_C_FORMAT_PE_INDUSTRIAL) $(COMPILER_C_FORMAT_LET) $(COMPILER_C_FORMAT_LET_WEAVER)

# ============================================================================
# Compiled Compiler Object Files (Intermediate)
# ============================================================================

COMPILER_C_OBJS := $(patsubst $(ROOT)/%.c,$(OUTPUT_DIR)/%.o,$(COMPILER_C_SRCS))

# ============================================================================
# Compiler Build Directory Dependencies
# ============================================================================

COMPILER_BUILD_DIRS := $(sort $(dir $(COMPILER_C_OBJS)))

# ============================================================================
# Compiler Object File Compilation Rules (C → O)
# ============================================================================

# Specific rule for compiler C object files - must be before generic rules
$(OUTPUT_DIR)/toolsC/compiler/%.o: $(ROOT)/toolsC/compiler/%.c
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
