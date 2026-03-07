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
 * AethelOS x86-64 Machine Code Encoder Header
 * 直接二进制机器码编码库 - 硬件层专用
 */

#ifndef X86_ENCODER_H
#define X86_ENCODER_H

#include <stdint.h>

/* MOV指令编码 */
int hw_encode_mov_r64_imm32_signed(int reg_id, int32_t imm32, unsigned char *output, int output_size);
int hw_encode_mov_r64_imm64(int reg_id, uint64_t imm64, unsigned char *output, int output_size);
int hw_encode_mov_r64_r64(int src_reg, int dst_reg, unsigned char *output, int output_size);
int hw_encode_mov_r64_mem(int mem_reg, int dst_reg, unsigned char *output, int output_size);
int hw_encode_mov_mem_r64(int mem_reg, int src_reg, unsigned char *output, int output_size);

/* 控制寄存器MOV */
int hw_encode_mov_cr_r64(int cr_id, int src_reg, unsigned char *output, int output_size);
int hw_encode_mov_r64_cr(int dst_reg, int cr_id, unsigned char *output, int output_size);

/* 栈操作 */
int hw_encode_push_r64(int reg_id, unsigned char *output, int output_size);
int hw_encode_pop_r64(int reg_id, unsigned char *output, int output_size);

/* 程序控制 */
int hw_encode_ret(unsigned char *output, int output_size);
int hw_encode_nop(unsigned char *output, int output_size);

/* ISA操作 */
int hw_encode_syscall(unsigned char *output, int output_size);
int hw_encode_sysret(unsigned char *output, int output_size);
int hw_encode_cpuid(unsigned char *output, int output_size);
int hw_encode_iretq(unsigned char *output, int output_size);

/* 内存屏障 */
int hw_encode_mfence(unsigned char *output, int output_size);
int hw_encode_sfence(unsigned char *output, int output_size);
int hw_encode_lfence(unsigned char *output, int output_size);

/* 其他 */
int hw_encode_pause(unsigned char *output, int output_size);
int hw_encode_hlt(unsigned char *output, int output_size);
int hw_encode_wbinvd(unsigned char *output, int output_size);
int hw_encode_lgdt(int mem_reg, unsigned char *output, int output_size);
int hw_encode_invlpg(int mem_reg, unsigned char *output, int output_size);

#endif /* X86_ENCODER_H */
