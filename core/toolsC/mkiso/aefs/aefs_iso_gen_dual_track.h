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
 * AethelOS ISO 生成器 - 双轨制头文件
 */

#ifndef AETHELOS_AEFS_ISO_GEN_DUAL_TRACK_H
#define AETHELOS_AEFS_ISO_GEN_DUAL_TRACK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 函数声明
 * ============================================================================ */

/**
 * 生成双轨制的混合 ISO 镜像
 * 
 * Track 1: Boot System (512MB) - 包含 Bootloader, Kernel, Installer GUI
 * Track 2: install.lz Payload (可达 400GB+) - 完整的系统镜像压缩包
 * 
 * 参数：
 *   bootloader_file: Bootloader 可执行文件路径
 *   kernel_file: 内核可执行文件路径
 *   installer_file: Installer GUI 应用文件路径（可选，为 NULL 则使用默认）
 *   source_data_path: 源系统数据路径（用于创建 install.lz）
 *   output_iso: 输出 ISO 文件路径
 *
 * 返回值：
 *   0: 成功创建 ISO
 *   -1: 失败
 *
 * 工作流程：
 * 1. 创建 Boot System 分区（512MB）
 *    - 嵌入 Bootloader
 *    - 嵌入 Kernel
 *    - 嵌入 Installer GUI
 * 2. 创建 install.lz Payload Track
 *    - 从源数据读取
 *    - 分割成 2MB 的段
 *    - 使用 LZ77 + Huffman 压缩
 *    - 计算 CRC32 和 XXH3 校验和
 *    - 直接写入 ISO
 * 3. 写入双轨制清单和元数据
 *
 * 性能特性：
 * - Boot System 可在 QEMU/KVM 中快速启动
 * - Installer 使用流式解压，内存占用最小
 * - 支持从 USB/光盘启动并部署到 400GB+ 的磁盘
 * - 使用 DMA 和顺序写入实现极快的部署速度（理论 2-5 分钟）
 */
int aefs_generate_dual_track_iso(const char *bootloader_file,
                                 const char *kernel_file,
                                 const char *installer_file,
                                 const char *source_data_path,
                                 const char *output_iso);

#ifdef __cplusplus
}
#endif

#endif // AETHELOS_AEFS_ISO_GEN_DUAL_TRACK_H
