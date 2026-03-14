/*
 * Copyright (C) 2024-2026 Aethel-Systems. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * ROM Firmware Image Generation
 * 完整ROM镜像生成模块（可直接刷入Flash）
 */

#ifndef AETHEL_ROM_H
#define AETHEL_ROM_H

#include <stdint.h>
#include <stddef.h>

int rom_generate_image(const char *output_file,
                       const uint8_t *code, size_t code_size,
                       const uint8_t *mirror_data, size_t mirror_size,
                       const uint8_t *constant_data, size_t constant_size,
                       uint64_t entry_offset,
                       uint16_t machine_bits,
                       uint16_t target_isa,
                       const uint8_t *aethel_id,
                       uint64_t rom_size,
                       uint8_t fill_byte);

#endif
