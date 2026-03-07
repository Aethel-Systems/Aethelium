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