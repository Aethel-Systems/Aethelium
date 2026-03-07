# tools-c/aetb/build.mk

AETB_GEN_SRC := $(TOOLS_C_DIR)/aetb/src/aetb_gen.c
AETB_GEN_OBJ := $(patsubst $(ROOT)/%.c,$(OUTPUT_DIR)/%.o,$(AETB_GEN_SRC))

$(OUTPUT_DIR)/toolsC/aetb/%.o: $(ROOT)/toolsC/aetb/%.c
	@echo "[CC] Compiling AETB Generator: $<"
	@mkdir -p $(@D)
	$(NATIVE_CC) -c $(NATIVE_CFLAGS) \
		-I$(TOOLS_C_INCLUDE) \
		-I$(TOOLS_C_DIR)/aetb/src \
		$< -o $@