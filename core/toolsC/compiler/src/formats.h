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
 * Format Modules Unified Interface
 * 
 * 包含所有二进制格式的生成模块
 * 提供统一的接口供编译器主程序调用
 */

#ifndef AETHELC_COMPILER_FORMATS_H
#define AETHELC_COMPILER_FORMATS_H

/* 公共格式定义 */
#include "format_common.h"

/* 各格式模块 */
#include "aki.h"
#include "hda.h"
#include "srv.h"
#include "ax.h"
#include "pe.h"
#include "pe_industrial.h"

/**
 * 编译器支持的输出格式枚举
 */
typedef enum {
    FORMAT_AKI,         /* Aethel Kernel Image */
    FORMAT_HDA,         /* Hardware Driver Archive */
    FORMAT_SRV,         /* System Service Archive */
    FORMAT_AETB,        /* Aethelium Binary (仅在IYA内部使用) */
    FORMAT_AX,          /* Aethel Index (B+Tree) */
    FORMAT_LET,         /* Logical Embryo Transfer */
    FORMAT_BIN,         /* Flat machine code binary */
    FORMAT_EFI,         /* UEFI PE32+ 嵌入式AETB (Embedded AETB bootloader) */
    FORMAT_PE           /* UEFI PE32+ 工业级应用 (Industrial Grade PE Application) */
} format_type_t;

/**
 * 概览：所有二进制格式详解
 * 
 * AKI (Aethel Kernel Image):
 *   - 微内核二进制镜像
 *   - 包含：vCore调度器 + SIP决策矩阵 + AethelA编译引擎
 *   - 三大Zone: ActFlow (执行流) / MirrorState (状态镜像) / ConstantTruth (不变真值)
 *   - 文件大小: 应为10MB+ (包含完整的内核代码、表格、元数据)
 * 
 * HDA (Hardware Driver Archive):
 *   - 硬件驱动二进制
 *   - 包含：硬件控制逻辑 + 契约元数据
 *   - 设备类型：PCI总线驱动、USB驱动、GPU驱动、存储驱动等
 *   - 可独立编译，多个HDA同时加载
 * 
 * SRV (System Service Archive):
 *   - 系统服务二进制
 *   - 包含：业务逻辑 + 运行状态 + 反射Schema
 *   - 示例：网络服务、渲染服务、文件系统服务等
 *   - 可独立编译，与用户应用并行运行
 * 
 * AETB (Aethelium Binary):
 *   - 应用逻辑二进制（仅在IYA内部使用）
 *   - 从AE源代码编译生成
 *   - 包含：x86-64机器码 + 编译期数据
 *   - 非独立格式，必须打包在IYA中才能执行
 * 
 * AX (Aethel Index):
 *   - B+树索引格式
 *   - 替代所有关系型数据库
 *   - 支持高效的范围查询和点查询
 *   - 用于文件索引、元数据索引等
 */

#endif
