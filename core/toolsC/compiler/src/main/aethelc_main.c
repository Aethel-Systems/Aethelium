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
 * AethelOS Aethelium Compiler - Main Driver
 * aethelc: 编译器与链接器的主入口点
 *
 * 用法:
 * 编译: aethelc <input.ae> -o <output.aetb> [options]
 * 链接: aethelc --link -o <output> <obj1> <obj2> ... --entry <symbol>
 * ISO:  aethelc --iso -o <output.iso> --kernel <kernel> --efi <boot.efi> [--size <MB>]
 */
//错误说法：
/**
 * 从AETB格式中提取纯x86-64机器码
 * 
 * AETB是编译器后端的中间格式，有256字节的头部。
 * 此函数将code section（从0x100偏移开始）提取出来。
 * 
 * 用于：AKI/HDA/SRV等格式，这些格式需要纯机器码而非AETB格式。
 * 
 * 参数：
 *   aetb_data: AETB格式的二进制数据指针
 *   aetb_size: 数据大小
 *   code_out: 输出的机器码指针
 *   code_size_out: 输出的机器码大小
 * 
 * 返回：0 成功，-1 失败
 */
//正确说法：
/*
 AETB就是AETB，AETB是给iya目录（应用包）中的运行文件准备的，严禁aetb用于中间文件，AethelOS只有.let中间文件
*/

#include "aec_lexer.h"
#include "aec_parser.h"
#include "semantic_checker.h"
#include "../frontend/import_resolver.h"
#include <libgen.h>
#include "aec_codegen.h"
#include "let_gen.h"
#include "let_weaver_bridge.h"
#include "../include/aefs.h"
#include "../include/binary_format.h"
#include "../frontend/preprocessor.h"
#include "../frontend/lexer/unix_strike.h"
#include "../formats.h"
#include "../formats/common/format_common.h"
#include "../formats/efi/pe.h"
#include "../middleend/builtin_print.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <sys/stat.h>

#define MAX_INPUT_FILES 128

/* 编译目标格式 */
#define FORMAT_AETB 0
#define FORMAT_AKI  1
#define FORMAT_SRV  2
#define FORMAT_HDA  3
#define FORMAT_EFI  4        /* UEFI PE32+ 嵌入式 AETB Bootloader */
#define FORMAT_LET  5
#define FORMAT_BIN  6
#define FORMAT_PE   7        /* UEFI PE32+ 工业级应用（新增） */

// ============================================================================
// 二进制格式生成函数 - 直接从编译输出生成目标格式
// ============================================================================

// ============================================================================
// 从二进制数据中提取三个Zone的辅助函数
// ============================================================================

static int extract_zones_from_binary(const uint8_t *binary_data, size_t binary_size,
                                    const uint8_t **code_out, size_t *code_size_out,
                                    const uint8_t **mirror_out, size_t *mirror_size_out,
                                    const uint8_t **constant_out, size_t *constant_size_out) {
    if (!binary_data || binary_size < AETHEL_HEADER_SIZE) {
        fprintf(stderr, "Error: Invalid binary data size\n");
        return -1;
    }
    
    /* 从数据中读取header */
    AethelBinaryHeader *hdr = (AethelBinaryHeader *)binary_data;
    
    /* 提取ActFlow Zone */
    if (hdr->act_flow_size > 0 && hdr->act_flow_offset + hdr->act_flow_size <= binary_size) {
        *code_out = binary_data + hdr->act_flow_offset;
        *code_size_out = hdr->act_flow_size;
    } else {
        *code_out = NULL;
        *code_size_out = 0;
    }
    
    /* 提取MirrorState Zone */
    if (hdr->mirror_state_size > 0 && hdr->mirror_state_offset + hdr->mirror_state_size <= binary_size) {
        *mirror_out = binary_data + hdr->mirror_state_offset;
        *mirror_size_out = hdr->mirror_state_size;
    } else {
        *mirror_out = NULL;
        *mirror_size_out = 0;
    }
    
    /* 提取ConstantTruth Zone */
    if (hdr->constant_truth_size > 0 && hdr->constant_truth_offset + hdr->constant_truth_size <= binary_size) {
        *constant_out = binary_data + hdr->constant_truth_offset;
        *constant_size_out = hdr->constant_truth_size;
    } else {
        *constant_out = NULL;
        *constant_size_out = 0;
    }
    
    return 0;
}

/* PE 格式已迁移到模块化pe_gen.c中实现 */



/* 直接从缓冲区生成AKI格式 - 纯AE编译链（零C代码） */
static int generate_aki_image_from_buffer(const char *output_file, const uint8_t *code, size_t code_size) {
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "Error: Cannot create output file '%s'\n", output_file);
        return 1;
    }
    
    /* 检测输入是否为AETB格式 - 如果是则提取机器码 */
    const uint8_t *actflow_code = code;
    size_t actflow_size = code_size;
    int aetb_detected = 0;
    
    if (code_size >= 128) {
        uint32_t magic = *(uint32_t*)code;
        if (magic == AETB_MAGIC) {
            aetb_detected = 1;
            /* AETB格式：读取code_size从偏移0x14处 (小端序32位) */
            uint32_t aetb_code_size = *(uint32_t*)&code[0x14];
            
            fprintf(stderr, "[DEBUG] Detected AETB format; code_size_in_header=%u\n", aetb_code_size);
            
            /* 验证code_size不超过AETB文件大小 */
            if (0x80 + aetb_code_size <= code_size) {
                /* 提取实际的x86-64机器码（从偏移0x80 = 128字节之后） */
                actflow_code = &code[0x80];
                actflow_size = aetb_code_size;
                
                if (actflow_size == 0) {
                    /* 如果AETB内的code_size为0，使用整个剩余size */
                    actflow_size = code_size - 0x80;
                }
                fprintf(stderr, "[DEBUG] Extracted x86-64 code: %zu bytes\n", actflow_size);
            }
        }
    }
    
    if (!aetb_detected) {
        fprintf(stderr, "[DEBUG] No AETB detected; using code directly: %zu bytes\n", code_size);
    }
    
    /* 构建标准 256 字节 AKI 头部 */
    uint8_t header[256] = {0};
    
    /* 0x00-0x03: 魔数 */
    header[0] = 0x41;  /* A */
    header[1] = 0x4b;  /* K */
    header[2] = 0x49;  /* I */
    header[3] = 0x21;  /* ! */
    
    /* 0x04-0x07: 版本 */
    uint32_t version = 1;
    memcpy(&header[4], &version, 4);
    
    /* 0x40-0x47: ActFlow offset (紧跟头部) */
    uint64_t act_flow_offset = 256;
    memcpy(&header[0x40], &act_flow_offset, 8);
    
    /* 0x48-0x4F: ActFlow size (提取的机器码) */
    uint64_t act_flow_size = actflow_size;
    memcpy(&header[0x48], &act_flow_size, 8);
    
    /* 0x50-0x57: MirrorState offset (紧跟ActFlow) */
    uint64_t mirror_state_offset = 256 + actflow_size;
    memcpy(&header[0x50], &mirror_state_offset, 8);
    
    /* 0x58-0x5F: MirrorState size (暂为0，由.ae编译扩展) */
    uint64_t mirror_state_size = 0;
    memcpy(&header[0x58], &mirror_state_size, 8);
    
    /* 0x60-0x67: ConstantTruth offset (紧跟MirrorState) */
    uint64_t constant_truth_offset = 256 + actflow_size;
    memcpy(&header[0x60], &constant_truth_offset, 8);
    
    /* 0x68-0x6F: ConstantTruth size (暂为0，由.ae编译扩展) */
    uint64_t constant_truth_size = 0;
    memcpy(&header[0x68], &constant_truth_size, 8);
    
    /* 0xD8-0xDB: Header CRC */
    uint32_t header_crc = 0;
    for (int i = 0; i < 0xD8; i++) {
        header_crc ^= header[i];
    }
    memcpy(&header[0xD8], &header_crc, 4);
    
    /* 0xE0-0xE7: Build timestamp */
    uint64_t build_time = time(NULL);
    memcpy(&header[0xE0], &build_time, 8);
    
    /* 0xE8-0xEB: Build version */
    uint32_t build_version = 1;
    memcpy(&header[0xE8], &build_version, 4);
    
    /* 0xEC-0xEF: Compiler version */
    uint32_t compiler_version = 2;  /* Pure AE compilation chain */
    memcpy(&header[0xEC], &compiler_version, 4);
    
    /* 写入 256 字节头部 */
    if (fwrite(header, 1, 256, out) != 256) {
        fprintf(stderr, "Error: Failed to write AKI header\n");
        fclose(out);
        return 1;
    }
    
    /* 写入 ActFlow 段 (AE编译产生的x86-64机器码，零C代码) */
    if (actflow_code && actflow_size > 0) {
        if (fwrite(actflow_code, 1, actflow_size, out) != actflow_size) {
            fprintf(stderr, "Error: Failed to write ActFlow segment\n");
            fclose(out);
            return 1;
        }
    }
    
    /* MirrorState和ConstantTruth留待后续.ae编译扩展 */
    
    fclose(out);
    return 0;
}


/* 生成 AKI 格式镜像 - 纯AE编译链 */
static int generate_aki_image(const char *input_file, const char *output_file) {
    // 读取编译产生的二进制
    FILE *in = fopen(input_file, "rb");
    if (!in) {
        fprintf(stderr, "Error: Cannot read compiled binary '%s'\n", input_file);
        return 1;
    }
    
    // 获取大小
    fseek(in, 0, SEEK_END);
    size_t code_size = ftell(in);
    fseek(in, 0, SEEK_SET);
    
    // 读取二进制
    uint8_t *code = malloc(code_size);
    if (!code) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(in);
        return 1;
    }
    
    if (fread(code, 1, code_size, in) != code_size) {
        fprintf(stderr, "Error: Failed to read compiled binary\n");
        free(code);
        fclose(in);
        return 1;
    }
    fclose(in);
    
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "Error: Cannot create output file '%s'\n", output_file);
        free(code);
        return 1;
    }
    
    /* 检测输入是否为AETB格式 - 如果是则提取机器码 */
    const uint8_t *actflow_code = code;
    size_t actflow_size = code_size;
    
    if (code_size >= 128) {
        uint32_t magic = *(uint32_t*)code;
        if (magic == AETB_MAGIC) {
            /* AETB格式：读取code_size从偏移0x14处 (小端序32位) */
            uint32_t aetb_code_size = *(uint32_t*)&code[0x14];
            
            /* 验证code_size不超过AETB文件大小 */
            if (0x80 + aetb_code_size <= code_size) {
                /* 提取实际的x86-64机器码（从偏移0x80 = 128字节之后） */
                actflow_code = &code[0x80];
                actflow_size = aetb_code_size;
                
                if (actflow_size == 0) {
                    /* 如果AETB内的code_size为0，使用整个剩余size */
                    actflow_size = code_size - 0x80;
                }
            }
        }
    }
    
    /* 构建标准 256 字节 AKI 头部（按文档规范） */
    uint8_t header[256] = {0};
    
    /* 0x00-0x03: 魔数 */
    header[0] = 0x41;   /* A */
    header[1] = 0x4b;   /* K */
    header[2] = 0x49;   /* I */
    header[3] = 0x21;   /* ! */
    
    /* 0x04-0x07: 版本 */
    uint32_t version = 1;
    memcpy(&header[4], &version, 4);
    
    /* 0x40-0x47: ActFlow offset */
    uint64_t act_flow_offset = 256;
    memcpy(&header[0x40], &act_flow_offset, 8);
    
    /* 0x48-0x4F: ActFlow size */
    uint64_t act_flow_size = actflow_size;
    memcpy(&header[0x48], &act_flow_size, 8);
    
    /* 0x50-0x57: MirrorState offset */
    uint64_t mirror_state_offset = 256 + actflow_size;
    memcpy(&header[0x50], &mirror_state_offset, 8);
    
    /* 0x58-0x5F: MirrorState size (暂为0) */
    uint64_t mirror_state_size = 0;
    memcpy(&header[0x58], &mirror_state_size, 8);
    
    /* 0x60-0x67: ConstantTruth offset */
    uint64_t constant_truth_offset = 256 + actflow_size;
    memcpy(&header[0x60], &constant_truth_offset, 8);
    
    /* 0x68-0x6F: ConstantTruth size (暂为0) */
    uint64_t constant_truth_size = 0;
    memcpy(&header[0x68], &constant_truth_size, 8);
    
    /* 写入 256 字节头部 */
    if (fwrite(header, 1, 256, out) != 256) {
        fprintf(stderr, "Error: Failed to write AKI header\n");
        free(code);
        fclose(out);
        return 1;
    }
    
    /* 写入 ActFlow 段 (AE编译产生的x86-64机器码) */
    if (actflow_code && actflow_size > 0) {
        if (fwrite(actflow_code, 1, actflow_size, out) != actflow_size) {
            fprintf(stderr, "Error: Failed to write ActFlow segment\n");
            free(code);
            fclose(out);
            return 1;
        }
    }
    
    fclose(out);
    free(code);
    
    return 0;
}

/* 验证内核：禁止 C 导出 */
static int validate_no_foreign_c(const char *source_file, const char *target_mode) {
    if (strcmp(target_mode, "kernel") != 0) {
        return 0;  /* 仅在内核模式检查 */
    }
    
    FILE *f = fopen(source_file, "r");
    if (!f) return 0;
    
    char line[1024];
    int line_num = 0;
    int found_error = 0;
    
    while (fgets(line, sizeof(line), f)) {
        line_num++;
        
        /* 检查 @foreign("C") 模式 */
        if (strstr(line, "@foreign") && strstr(line, "\"C\"")) {
            fprintf(stderr, "ERROR [Line %d]: Kernel modules cannot export C interfaces!\n", line_num);
            fprintf(stderr, "  Found: @foreign(\"C\")\n");
            fprintf(stderr, "  Hint: Remove all C export interfaces from kernel code\n");
            found_error = 1;
        }
    }
    
    fclose(f);
    return found_error ? 1 : 0;
}


typedef struct {
    char *input_files[MAX_INPUT_FILES];
    int input_count;
    const char *output_file;
    const char *entry_point;
    const char *mode;         /* "compile" or "link" */
    int optimize_level;
    int verbose;
    int debug;                /* 新增：--debug 标志 */
    const char *output_format;  /* "aetb", "let", "aki", "hda", "srv", "efi", "bin" */
    const char *target_mode;    /* "application" or "kernel" */
    const char *emit_format;    /* --emit override */
    int machine_bits;           /* 16/32/64 */
    const char *isa;            /* x86|x86_64|aarch64 */
    int bin_flat;               /* --bin-flat */
    int bin_with_map;           /* --bin-with-map */
    const char *bin_entry;      /* --bin-entry <offset|symbol> */
    int has_bin_entry_offset;
    uint64_t bin_entry_offset;
    int verify_let_mode;        /* --verify-let-contract */
    const char *verify_let_file;
    int dump_reloc_mode;        /* --dump-reloc-dna */
    const char *dump_reloc_file;
    const char *dump_reloc_output;
    /* 新增：ISO生成选项 */
    int is_iso_mode;
    const char *kernel_file;
    const char *efi_boot_file;
    uint64_t iso_size_mb;
    /* 新增：编译标志（链接标志） */
    int freestanding;           /* --freestanding: 自由运行模式 */
    int no_stack_check;         /* --no-stack-check: 禁用堆栈检查 */
    int no_default_libs;        /* --no-default-libs: 不链接默认库 */
    int no_shared_libs;         /* --no-shared-libs: 不使用共享库 */
    int bundle_dependencies;    /* --bundle-dependencies: 绑定依赖 */
    int bundle_all_dependencies; /* --bundle-all-dependencies: 绑定所有依赖 */
    int static_only;            /* --static-only: 仅静态链接 */
    int static_complete;        /* --static-complete: 静态链接完整性 */
    int app_package;            /* --app-package: 应用包格式（IYA） */
    /* 新增：库包含支持 (for library source inlining) */
    char *include_libs[32];     /* 库名称列表（如 "std", "auraui"） */
    int include_lib_count;      /* 包含库的数量 */
} CompilerOptions;

static int parse_u64(const char *text, uint64_t *out) {
    char *endp = NULL;
    unsigned long long v;
    if (!text || !out || text[0] == '\0') return -1;
    v = strtoull(text, &endp, 0);
    if (!endp || *endp != '\0') return -1;
    *out = (uint64_t)v;
    return 0;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "AethelOS Bootstrap Compiler (Stage 1) v2.0.0\n");
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  Compile: %s <input.ae> -o <output> [options]\n", prog);
    fprintf(stderr, "  Verify:  %s --verify-let-contract <input.let>\n", prog);
    fprintf(stderr, "  Dump:    %s --dump-reloc-dna <input.let> [-o <output.txt>]\n", prog);
    fprintf(stderr, "  ISO:     %s --iso -o <output.iso> --kernel <kernel> --efi <boot.efi> [--size <MB>]\n", prog);
    fprintf(stderr, "\nOutput Formats:\n");
    fprintf(stderr, "  --emit aetb|let|aki|srv|hda|efi|bin\n");
    fprintf(stderr, "\nCompiler Architecture:\n");
    fprintf(stderr, "  .ae -> AETB         Direct compile path for application runtime binaries\n");
    fprintf(stderr, "  .ae -> LET          Logical Embryo (the only intermediate format)\n");
    fprintf(stderr, "  .ae -> LET -> BIN   Contract-validated pure machine code path\n");
    fprintf(stderr, "  .let -> aki/srv/hda/aetb/bin  Assembly weaver path using Gene-Table metadata\n");
    fprintf(stderr, "  --verify-let-contract  Validate LET machine contract\n");
    fprintf(stderr, "  --dump-reloc-dna       Dump relocation DNA list from LET\n");
    fprintf(stderr, "\nTarget Modes:\n");
    fprintf(stderr, "  --target application   Application mode (default)\n");
    fprintf(stderr, "  --target kernel        Kernel mode (prohibits C exports)\n");
    fprintf(stderr, "\nMachine Contract:\n");
    fprintf(stderr, "  --machine-bits 16|32|64   (default 64)\n");
    fprintf(stderr, "  --isa x86|x86_64|aarch64  (default x86)\n");
    fprintf(stderr, "  --bin-entry <offset|symbol>\n");
    fprintf(stderr, "  --bin-flat\n");
    fprintf(stderr, "  --bin-with-map\n");
    fprintf(stderr, "\nISO Generation Options:\n");
    fprintf(stderr, "  --iso                Generate hybrid AEFS ISO image\n");
    fprintf(stderr, "  --kernel <file>      Kernel file to embed\n");
    fprintf(stderr, "  --efi <file>         EFI boot file\n");
    fprintf(stderr, "  --size <MB>          ISO size in MB (default: 512)\n");
    fprintf(stderr, "\nCompilation Flags:\n");
    fprintf(stderr, "  --freestanding       Freestanding mode (no libc dependencies)\n");
    fprintf(stderr, "  --no-stack-check     Disable stack checking\n");
    fprintf(stderr, "  --no-default-libs    Don't link default libraries\n");
    fprintf(stderr, "  --no-shared-libs     Don't use shared libraries\n");
    fprintf(stderr, "  --bundle-dependencies Bundle inline dependencies\n");
    fprintf(stderr, "  --bundle-all-dependencies Bundle all dependencies\n");
    fprintf(stderr, "  --static-only        Static linking only\n");
    fprintf(stderr, "  --static-complete    Complete static linking\n");
    fprintf(stderr, "  --app-package        Application package (IYA) format\n");
    fprintf(stderr, "\nLibrary Inclusion (Source Code Inlining):\n");
    fprintf(stderr, "  --include-lib <lib>  Include library source (std, auraui, aurakit, flux)\n");
    fprintf(stderr, "                       Multiple --include-lib flags can be used\n");
    fprintf(stderr, "                       Example: aethelc app.ae --include-lib std --include-lib auraui\n");
    fprintf(stderr, "\nDebug and Help Options:\n");
    fprintf(stderr, "  --debug              Enable debug output during compilation\n");
    fprintf(stderr, "  -h, --help           Display this help message\n");
    fprintf(stderr, "\nOther Options:\n");
    fprintf(stderr, "  -o <file>        Output file\n");
    fprintf(stderr, "  --mode <mode>    sandbox or architect (default: sandbox)\n");
    fprintf(stderr, "  -O <level>       Optimization level (0-3)\n");
    fprintf(stderr, "  --optimize <lvl> Same as -O (e.g. --optimize 2)\n");
    fprintf(stderr, "  -v               Verbose output\n");
    fprintf(stderr, "  --entry <sym>    Set entry point symbol (link mode only)\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  Compile to AETB:\n");
    fprintf(stderr, "    aethelc app.ae -o app.aetb --include-lib std --include-lib auraui\n");
    fprintf(stderr, "\n  Compile to BIN through LET contract:\n");
    fprintf(stderr, "    aethelc app.ae --emit bin --machine-bits 64 -o app.bin --bin-flat\n");
    fprintf(stderr, "\n  Verify LET contract:\n");
    fprintf(stderr, "    aethelc --verify-let-contract app.let\n");
    fprintf(stderr, "\n  Dump reloc DNA:\n");
    fprintf(stderr, "    aethelc --dump-reloc-dna app.let -o app.reloc.txt\n");
    fprintf(stderr, "\n  Compile to AKI (kernel):\n");
    fprintf(stderr, "    aethelc kernel.ae -o kernel.aki --emit aki --target kernel\n");
    fprintf(stderr, "\n  Compile to UEFI bootloader:\n");
    fprintf(stderr, "    aethelc bootloader.ae -o BOOTX64.EFI --emit efi\n");
    fprintf(stderr, "\n  Generate ISO image:\n");
    fprintf(stderr, "    aethelc --iso -o output.iso --kernel kernel.aki --efi BOOTX64.EFI --size 512\n");
    fprintf(stderr, "\n  Compile with debug output:\n");
    fprintf(stderr, "    aethelc app.ae -o app.aetb --debug -v\n");

}

static int parse_args(int argc, char **argv, CompilerOptions *opts) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    // 初始化选项
    opts->input_count = 0;
    opts->output_file = "a.out";
    opts->entry_point = "main";
    opts->mode = "sandbox";
    opts->optimize_level = 2;
    opts->verbose = 0;
    opts->debug = 0;           /* 新增：初始化debug标志 */
    opts->output_format = "aetb";
    opts->target_mode = "application";
    opts->emit_format = NULL;
    opts->machine_bits = 64;
    opts->isa = "x86";
    opts->bin_flat = 0;
    opts->bin_with_map = 0;
    opts->bin_entry = NULL;
    opts->has_bin_entry_offset = 0;
    opts->bin_entry_offset = 0;
    opts->verify_let_mode = 0;
    opts->verify_let_file = NULL;
    opts->dump_reloc_mode = 0;
    opts->dump_reloc_file = NULL;
    opts->dump_reloc_output = NULL;
    /* ISO模式初始化 */
    opts->is_iso_mode = 0;
    opts->kernel_file = NULL;
    opts->efi_boot_file = NULL;
    opts->iso_size_mb = 512;
    /* 编译标志初始化 */
    opts->freestanding = 0;
    opts->no_stack_check = 0;
    opts->no_default_libs = 0;
    opts->no_shared_libs = 0;
    opts->bundle_dependencies = 0;
    opts->bundle_all_dependencies = 0;
    opts->static_only = 0;
    opts->static_complete = 0;
    opts->app_package = 0;
    /* 库包含初始化 */
    opts->include_lib_count = 0;
    memset(opts->include_libs, 0, sizeof(opts->include_libs));
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 1;
        } else if (strcmp(argv[i], "--debug") == 0) {
            opts->debug = 1;
            opts->verbose = 1;
        } else if (strcmp(argv[i], "--iso") == 0) {
            opts->is_iso_mode = 1;
        } else if (strcmp(argv[i], "--verify-let-contract") == 0) {
            if (i + 1 < argc) {
                opts->verify_let_mode = 1;
                opts->verify_let_file = argv[++i];
            } else {
                fprintf(stderr, "Error: --verify-let-contract requires a .let file\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--dump-reloc-dna") == 0) {
            if (i + 1 < argc) {
                opts->dump_reloc_mode = 1;
                opts->dump_reloc_file = argv[++i];
            } else {
                fprintf(stderr, "Error: --dump-reloc-dna requires a .let file\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) opts->output_file = argv[++i];
        } else if (strcmp(argv[i], "--kernel") == 0) {
            if (i + 1 < argc) opts->kernel_file = argv[++i];
        } else if (strcmp(argv[i], "--efi") == 0) {
            if (i + 1 < argc) opts->efi_boot_file = argv[++i];
        } else if (strcmp(argv[i], "--size") == 0) {
            if (i + 1 < argc) opts->iso_size_mb = atoll(argv[++i]);
        } else if (strcmp(argv[i], "--entry") == 0) {
            if (i + 1 < argc) opts->entry_point = argv[++i];
        } else if (strcmp(argv[i], "--mode") == 0) {
            if (i + 1 < argc) opts->mode = argv[++i];
        } else if (strcmp(argv[i], "--emit") == 0 || strcmp(argv[i], "--format") == 0) {
            if (i + 1 < argc) {
                const char *fmt = argv[++i];
                if (strcmp(fmt, "aetb") == 0 || strcmp(fmt, "let") == 0 || strcmp(fmt, "aki") == 0 ||
                    strcmp(fmt, "efi") == 0 || strcmp(fmt, "uefi_app") == 0 || strcmp(fmt, "pe") == 0 ||
                    strcmp(fmt, "hda") == 0 || strcmp(fmt, "srv") == 0 || strcmp(fmt, "bin") == 0) {
                    opts->output_format = fmt;
                    opts->emit_format = fmt;
                } else {
                    fprintf(stderr, "Error: unsupported emit format '%s'\n", fmt);
                    return 1;
                }
            }
        } else if (strcmp(argv[i], "--machine-bits") == 0) {
            if (i + 1 < argc) {
                opts->machine_bits = atoi(argv[++i]);
                if (!(opts->machine_bits == 16 || opts->machine_bits == 32 || opts->machine_bits == 64)) {
                    fprintf(stderr, "Error: --machine-bits must be 16/32/64\n");
                    return 1;
                }
            }
        } else if (strcmp(argv[i], "--isa") == 0) {
            if (i + 1 < argc) {
                opts->isa = argv[++i];
                if (strcmp(opts->isa, "x86") != 0 &&
                    strcmp(opts->isa, "x86_64") != 0 &&
                    strcmp(opts->isa, "aarch64") != 0) {
                    fprintf(stderr, "Error: --isa must be x86|x86_64|aarch64\n");
                    return 1;
                }
            }
        } else if (strcmp(argv[i], "--bin-entry") == 0) {
            if (i + 1 < argc) {
                opts->bin_entry = argv[++i];
                if (parse_u64(opts->bin_entry, &opts->bin_entry_offset) == 0) {
                    opts->has_bin_entry_offset = 1;
                } else {
                    opts->has_bin_entry_offset = 0;
                }
            }
        } else if (strcmp(argv[i], "--bin-flat") == 0) {
            opts->bin_flat = 1;
        } else if (strcmp(argv[i], "--bin-with-map") == 0) {
            opts->bin_with_map = 1;
        } else if (strcmp(argv[i], "--target") == 0) {
            if (i + 1 < argc) {
                const char *target = argv[++i];
                if (strcmp(target, "application") == 0 || strcmp(target, "kernel") == 0) {
                    opts->target_mode = target;
                } else {
                    fprintf(stderr, "Error: Invalid target '%s'. Valid: application, kernel\n", target);
                    return 1;
                }
            }
        } else if (strcmp(argv[i], "--freestanding") == 0) {
            opts->freestanding = 1;
        } else if (strcmp(argv[i], "--no-stack-check") == 0) {
            opts->no_stack_check = 1;
        } else if (strcmp(argv[i], "--no-default-libs") == 0) {
            opts->no_default_libs = 1;
        } else if (strcmp(argv[i], "--no-shared-libs") == 0) {
            opts->no_shared_libs = 1;
        } else if (strcmp(argv[i], "--bundle-dependencies") == 0) {
            opts->bundle_dependencies = 1;
        } else if (strcmp(argv[i], "--bundle-all-dependencies") == 0) {
            opts->bundle_all_dependencies = 1;
        } else if (strcmp(argv[i], "--static-only") == 0) {
            opts->static_only = 1;
        } else if (strcmp(argv[i], "--static-complete") == 0) {
            opts->static_complete = 1;
        } else if (strcmp(argv[i], "--app-package") == 0) {
            opts->app_package = 1;
        } else if (strcmp(argv[i], "--include-lib") == 0) {
            if (i + 1 < argc && opts->include_lib_count < 32) {
                opts->include_libs[opts->include_lib_count++] = argv[++i];
            } else if (opts->include_lib_count >= 32) {
                fprintf(stderr, "Error: Too many libraries to include (max 32)\n");
                return 1;
            }
        } else if (strncmp(argv[i], "-O", 2) == 0) {
            // 处理 -O2
            if (strlen(argv[i]) > 2) {
                opts->optimize_level = atoi(&argv[i][2]);
            } else if (i + 1 < argc) {
                opts->optimize_level = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--optimize") == 0) {
            // 处理 --optimize O2
            if (i + 1 < argc) {
                char *val = argv[++i];
                if (val[0] == 'O') {
                    opts->optimize_level = atoi(val + 1);
                } else {
                    opts->optimize_level = atoi(val);
                }
            }
        } else if (strcmp(argv[i], "-v") == 0) {
            opts->verbose = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        } else {
            // 输入文件
            if (opts->input_count < MAX_INPUT_FILES) {
                opts->input_files[opts->input_count++] = argv[i];
            } else {
                fprintf(stderr, "Error: Too many input files\n");
                return 1;
            }
        }
    }

    if (opts->verify_let_mode || opts->dump_reloc_mode) {
        if (opts->dump_reloc_mode && opts->output_file && strcmp(opts->output_file, "a.out") != 0) {
            opts->dump_reloc_output = opts->output_file;
        }
        return 0;
    }

    if (opts->bin_entry && !opts->has_bin_entry_offset) {
        fprintf(stderr,
                "Error: --bin-entry symbol mode is not available in Stage1; use numeric offset\n");
        return 1;
    }

    if (strcmp(opts->isa, "aarch64") == 0 && opts->machine_bits != 64) {
        fprintf(stderr, "Error: aarch64 requires --machine-bits 64\n");
        return 1;
    }
    if (strcmp(opts->isa, "x86_64") == 0 && opts->machine_bits != 64) {
        fprintf(stderr, "Error: x86_64 requires --machine-bits 64\n");
        return 1;
    }
    
    /* 验证必需的参数 */
    if (opts->is_iso_mode) {
        if (!opts->kernel_file || !opts->efi_boot_file) {
            fprintf(stderr, "Error: ISO mode requires --kernel and --efi options\n");
            return 1;
        }
        /* ISO模式不需要输入文件 */
    } else if (opts->input_count == 0) {
        fprintf(stderr, "Error: No input files specified\n");
        return 1;
    }

    if (opts->bundle_all_dependencies) {
        opts->bundle_dependencies = 1;
    }
    if (opts->static_complete) {
        opts->static_only = 1;
    }
    
    return 0;
}

static char* read_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        fprintf(stderr, "Error: Out of memory\n");
        return NULL;
    }
    
    if (fread(content, 1, size, f) != (size_t)size) {
        free(content);
        fclose(f);
        fprintf(stderr, "Error: Failed to read file '%s'\n", filename);
        return NULL;
    }
    
    content[size] = '\0';
    fclose(f);
    
    return content;
}

static char *trim_inplace(char *s) {
    char *end;
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static int append_asm_line(char ***lines, size_t *count, size_t *cap, const char *line) {
    char **grown;
    char *dup;
    if (!lines || !count || !cap || !line) return -1;
    if (*count >= *cap) {
        size_t new_cap = (*cap == 0) ? 64 : (*cap * 2);
        grown = (char **)realloc(*lines, new_cap * sizeof(char *));
        if (!grown) return -1;
        *lines = grown;
        *cap = new_cap;
    }
    dup = strdup(line);
    if (!dup) return -1;
    (*lines)[(*count)++] = dup;
    return 0;
}

static void free_asm_lines(char **lines, size_t count) {
    size_t i;
    if (!lines) return;
    for (i = 0; i < count; i++) free(lines[i]);
    free(lines);
}

static int is_word_boundary_char(char c) {
    return !(isalnum((unsigned char)c) || c == '_');
}

static int extract_asm_strings_from_source(const char *src, char ***out_lines, size_t *out_count) {
    const char *p;
    char **lines = NULL;
    size_t count = 0, cap = 0;
    if (!src || !out_lines || !out_count) return -1;
    *out_lines = NULL;
    *out_count = 0;
    p = src;
    while (*p) {
        if ((p == src || is_word_boundary_char(*(p - 1))) &&
            p[0] == 'a' && p[1] == 's' && p[2] == 'm' &&
            is_word_boundary_char(p[3])) {
            const char *q = p + 3;
            int depth = 0;
            while (*q && isspace((unsigned char)*q)) q++;
            if (*q != '{') {
                p++;
                continue;
            }
            depth = 1;
            q++;
            while (*q && depth > 0) {
                if (*q == '/' && q[1] == '/') {
                    q += 2;
                    while (*q && *q != '\n') q++;
                    continue;
                }
                if (*q == '/' && q[1] == '*') {
                    q += 2;
                    while (*q && !(q[0] == '*' && q[1] == '/')) q++;
                    if (*q) q += 2;
                    continue;
                }
                if (*q == '"') {
                    char buf[8192];
                    size_t bi = 0;
                    q++;
                    while (*q && *q != '"') {
                        if (*q == '\\' && q[1] != '\0') {
                            if (q[1] == 'n') {
                                if (bi + 1 < sizeof(buf)) buf[bi++] = '\n';
                                q += 2;
                                continue;
                            }
                            if (bi + 1 < sizeof(buf)) buf[bi++] = q[1];
                            q += 2;
                            continue;
                        }
                        if (bi + 1 < sizeof(buf)) buf[bi++] = *q;
                        q++;
                    }
                    buf[bi] = '\0';
                    if (bi > 0) {
                        char *trimmed;
                        char tmp[8192];
                        snprintf(tmp, sizeof(tmp), "%s", buf);
                        trimmed = trim_inplace(tmp);
                        if (*trimmed != '\0') {
                            if (append_asm_line(&lines, &count, &cap, trimmed) != 0) {
                                free_asm_lines(lines, count);
                                return -1;
                            }
                        }
                    }
                    if (*q == '"') q++;
                    continue;
                }
                if (*q == '{') depth++;
                else if (*q == '}') depth--;
                q++;
            }
            p = q;
            continue;
        }
        p++;
    }
    *out_lines = lines;
    *out_count = count;
    return 0;
}

static void replace_slash_with_underscore(char *s) {
    char *p;
    if (!s) return;
    p = s;
    while (*p) {
        if (*p == '/') {
            char prev = (p == s) ? ' ' : *(p - 1);
            if (p == s ||
                prev == ' ' || prev == '\t' || prev == ',' ||
                prev == '[' || prev == '+' || prev == '-' ||
                prev == '(' || prev == ':') {
                memmove(p, p + 1, strlen(p));
                continue;
            }
            *p = '_';
        }
        p++;
    }
}

static int asm_line_needs_64(const char *line) {
    static const char *needles[] = {
        " rsp", " rbp", " rsi", " rdi", " rip", " r8", " r9", " r10", " r11", " r12", " r13", " r14", " r15"
    };
    size_t i;
    if (!line) return 0;
    for (i = 0; i < sizeof(needles) / sizeof(needles[0]); i++) {
        if (strstr(line, needles[i]) != NULL) return 1;
    }
    if (strstr(line, "aethel2_long_mode_entry:") != NULL) return 1;
    return 0;
}

static int normalize_asm_line(const char *in, char *out, size_t out_sz) {
    char buf[8192];
    char *t;
    char symbuf[8192];
    size_t si = 0, i = 0;
    if (!in || !out || out_sz == 0) return -1;
    snprintf(buf, sizeof(buf), "%s", in);
    t = trim_inplace(buf);
    if (*t == '\0') {
        out[0] = '\0';
        return 0;
    }
    replace_slash_with_underscore(t);
    if (strncmp(t, ".org", 4) == 0 && isspace((unsigned char)t[4])) {
        snprintf(out, out_sz, "org%s", t + 4);
        return 0;
    }
    if (strncmp(t, ".byte", 5) == 0 && isspace((unsigned char)t[5])) {
        snprintf(out, out_sz, "db%s", t + 5);
        return 0;
    }
    if (strncmp(t, ".word", 5) == 0 && isspace((unsigned char)t[5])) {
        snprintf(out, out_sz, "dw%s", t + 5);
        return 0;
    }
    if (strncmp(t, ".long", 5) == 0 && isspace((unsigned char)t[5])) {
        snprintf(out, out_sz, "dd%s", t + 5);
        return 0;
    }
    if (strncmp(t, ".align", 6) == 0 && isspace((unsigned char)t[6])) {
        char *arg = trim_inplace(t + 6);
        snprintf(out, out_sz, "align %s, db 0", arg);
        return 0;
    }
    while (t[i] != '\0' && si + 1 < sizeof(symbuf)) {
        if (t[i] == '.' &&
            (i == 0 || t[i - 1] == ' ' || t[i - 1] == '\t' || t[i - 1] == ',' || t[i - 1] == ':') &&
            (isalnum((unsigned char)t[i + 1]) || t[i + 1] == '_')) {
            i++;
            continue;
        }
        symbuf[si++] = t[i++];
    }
    symbuf[si] = '\0';
    snprintf(out, out_sz, "%s", symbuf);
    return 0;
}

static long file_size_or_neg(const char *path) {
    struct stat st;
    if (!path) return -1;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

static int compile_inline_asm_to_bin(const char *input_file,
                                     const char *output_file,
                                     int machine_bits,
                                     int verbose) {
    char *source = NULL;
    char **lines = NULL;
    size_t line_count = 0;
    char asm_tmp[] = "/tmp/aethelc-asm-XXXXXX";
    int asm_fd = -1;
    FILE *asm_fp = NULL;
    int rc = -1;
    int status = 0;
    int switched64 = 0;
    size_t i;
#ifndef _WIN32
    pid_t pid;
#endif

    if (!input_file || !output_file) return 0;
    source = read_file(input_file);
    if (!source) return -1;
    if (extract_asm_strings_from_source(source, &lines, &line_count) != 0) {
        free(source);
        return -1;
    }
    free(source);
    if (line_count == 0) {
        free_asm_lines(lines, line_count);
        return 0;
    }

    asm_fd = mkstemp(asm_tmp);
    if (asm_fd < 0) {
        free_asm_lines(lines, line_count);
        return -1;
    }
    asm_fp = fdopen(asm_fd, "w");
    if (!asm_fp) {
        close(asm_fd);
        unlink(asm_tmp);
        free_asm_lines(lines, line_count);
        return -1;
    }

    fprintf(asm_fp, "; auto-generated from %s\n", input_file);
    if (machine_bits == 16) fprintf(asm_fp, "bits 16\n");
    else if (machine_bits == 32) fprintf(asm_fp, "bits 32\n");
    else fprintf(asm_fp, "bits 64\n");

    for (i = 0; i < line_count; i++) {
        char norm[8192];
        if (normalize_asm_line(lines[i], norm, sizeof(norm)) != 0) {
            fclose(asm_fp);
            unlink(asm_tmp);
            free_asm_lines(lines, line_count);
            return -1;
        }
        if (norm[0] == '\0') continue;
        if (!switched64 && machine_bits != 64 && asm_line_needs_64(norm)) {
            fprintf(asm_fp, "bits 64\n");
            switched64 = 1;
        }
        fprintf(asm_fp, "%s\n", norm);
    }
    fclose(asm_fp);

    if (verbose) {
        fprintf(stderr, "[ASM-BIN] direct NASM pipeline: %s -> %s\n", input_file, output_file);
    }

#ifdef _WIN32
    {
        char cmd[8192];
        int n = snprintf(cmd, sizeof(cmd), "nasm -f bin -o \"%s\" \"%s\"", output_file, asm_tmp);
        if (n < 0 || (size_t)n >= sizeof(cmd)) {
            unlink(asm_tmp);
            free_asm_lines(lines, line_count);
            return -1;
        }
        status = system(cmd);
        if (status == 0 && file_size_or_neg(output_file) >= 0) {
            rc = 1;
        } else {
            unlink(output_file);
            rc = -1;
        }
    }
#else
    pid = fork();
    if (pid == 0) {
        execlp("nasm", "nasm", "-f", "bin", "-o", output_file, asm_tmp, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) {
        unlink(asm_tmp);
        free_asm_lines(lines, line_count);
        return -1;
    }
    if (waitpid(pid, &status, 0) < 0) {
        unlink(asm_tmp);
        free_asm_lines(lines, line_count);
        return -1;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && file_size_or_neg(output_file) >= 0) {
        rc = 1;
    } else {
        unlink(output_file);
        rc = -1;
    }
#endif

    unlink(asm_tmp);
    free_asm_lines(lines, line_count);
    return rc;
}

static int merge_semantic_result(SemanticResult *dst, const SemanticResult *src) {
    size_t i;
    if (!dst || !src) return -1;

    dst->error_count += src->error_count;
    dst->warning_count += src->warning_count;
    dst->has_metal_block |= src->has_metal_block;
    dst->requires_architect_mode |= src->requires_architect_mode;
    dst->has_rimport |= src->has_rimport;
    dst->trap_hint_count += src->trap_hint_count;
    dst->reloc_dna_count += src->reloc_dna_count;
    dst->ipc_contract_count += src->ipc_contract_count;
    if (src->identity_contract_min_sip > dst->identity_contract_min_sip) {
        dst->identity_contract_min_sip = src->identity_contract_min_sip;
    }

    for (i = 0; i < src->import_count; i++) {
        if (dst->import_count >= 128) {
            fprintf(stderr, "Error: Semantic import table overflow while merging\n");
            return -1;
        }
        dst->imports[dst->import_count++] = src->imports[i];
    }

    return 0;
}

static int merge_program_ast(ASTNode *dst, ASTNode *src) {
    ASTNode **merged_decls;
    int new_count;
    int i;

    if (!dst || !src) return -1;
    if (dst->type != AST_PROGRAM || src->type != AST_PROGRAM) return -1;
    if (src->data.program.decl_count == 0) return 0;

    new_count = dst->data.program.decl_count + src->data.program.decl_count;
    merged_decls = realloc(dst->data.program.declarations, sizeof(ASTNode*) * (size_t)new_count);
    if (!merged_decls) {
        return -1;
    }

    dst->data.program.declarations = merged_decls;
    for (i = 0; i < src->data.program.decl_count; i++) {
        dst->data.program.declarations[dst->data.program.decl_count + i] =
            src->data.program.declarations[i];
    }
    dst->data.program.decl_count = new_count;
    return 0;
}

static uint16_t map_isa_to_contract(const char *isa, int machine_bits) {
    if (!isa) return (machine_bits == 64) ? LET_ISA_X86_64 : LET_ISA_X86;
    if (strcmp(isa, "x86") == 0) {
        return (machine_bits == 64) ? LET_ISA_X86_64 : LET_ISA_X86;
    }
    if (strcmp(isa, "aarch64") == 0) return LET_ISA_AARCH64;
    if (strcmp(isa, "x86_64") == 0 && machine_bits == 64) return LET_ISA_X86_64;
    if (strcmp(isa, "x86_64") == 0) return LET_ISA_X86;
    return LET_ISA_UNKNOWN;
}

static uint8_t default_reloc_width_for_bits(int machine_bits) {
    if (machine_bits <= 16) return 16;
    if (machine_bits <= 32) return 32;
    return 64;
}

static int run_compiler(CompilerOptions *opts) {
    const char *effective_mode = opts->mode;
    const char *effective_target_mode = opts->target_mode;
    LetEmitOptions let_emit_opts;
    LetWeaveOptions let_weave_opts;
    int input_is_let_only;
    // 编译模式：支持多个输入文件用于内核构建
    if (opts->input_count == 0) {
        fprintf(stderr, "Error: No input files specified.\n");
        return 1;
    }
    input_is_let_only = (opts->input_count == 1 &&
                         strstr(opts->input_files[0], ".let") != NULL);

    if (!input_is_let_only &&
        strcmp(opts->isa, "aarch64") == 0) {
        fprintf(stderr,
                "Error: Stage1 codegen currently supports only x86/x86_64 for AE source input\n");
        return 1;
    }

    if (opts->debug) {
        printf("[DEBUG] Compiler Debug Mode Enabled\n");
        printf("[DEBUG] Input files: %d\n", opts->input_count);
        for (int i = 0; i < opts->input_count; i++) {
            printf("[DEBUG]   [%d] %s\n", i + 1, opts->input_files[i]);
        }
        printf("[DEBUG] Output file: %s\n", opts->output_file);
        printf("[DEBUG] Target mode: %s\n", opts->target_mode);
        printf("[DEBUG] Optimization level: O%d\n", opts->optimize_level);
        printf("[DEBUG] Debug flags enabled\n");
    }

    if (opts->verbose) {
        printf("[INFO] 编译中: %d 个文件 -> %s\n", opts->input_count, opts->output_file);
        printf("[INFO] 目标模式: %s\n", opts->target_mode);
        printf("[INFO] 优化级别: O%d\n", opts->optimize_level);
        if (opts->freestanding) printf("[INFO] 模式: 裸机 (无 libc)\n");
        if (opts->no_default_libs) printf("[INFO] 标志: 不使用默认库\n");
        if (opts->no_shared_libs) printf("[INFO] 标志: 不使用共享库\n");
        if (opts->bundle_dependencies) printf("[INFO] 标志: 打包依赖\n");
        if (opts->bundle_all_dependencies) printf("[INFO] 标志: 打包所有依赖\n");
        if (opts->app_package) printf("[INFO] 标志: 应用程序包 (IYA) 格式\n");
        printf("[INFO] 机器契约: isa=%s bits=%d\n", opts->isa, opts->machine_bits);
        if (opts->debug) printf("[INFO] 模式: 调试信息已启用\n");
    }
    
    /* 检测目标格式 */
    const char *output_file = opts->output_file;
    int target_format = FORMAT_AETB;  /* 默认 */
    if (opts->emit_format) {
        if (strcmp(opts->emit_format, "aki") == 0) target_format = FORMAT_AKI;
        else if (strcmp(opts->emit_format, "let") == 0) target_format = FORMAT_LET;
        else if (strcmp(opts->emit_format, "srv") == 0) target_format = FORMAT_SRV;
        else if (strcmp(opts->emit_format, "hda") == 0) target_format = FORMAT_HDA;
        else if (strcmp(opts->emit_format, "efi") == 0 || strcmp(opts->emit_format, "uefi_app") == 0) target_format = FORMAT_EFI;
        else if (strcmp(opts->emit_format, "pe") == 0) target_format = FORMAT_PE;
        else if (strcmp(opts->emit_format, "bin") == 0) target_format = FORMAT_BIN;
        else target_format = FORMAT_AETB;
    } else if (strstr(output_file, ".aki") != NULL) {
        target_format = FORMAT_AKI;
    } else if (strstr(output_file, ".let") != NULL) {
        target_format = FORMAT_LET;
    } else if (strstr(output_file, ".srv") != NULL) {
        target_format = FORMAT_SRV;
    } else if (strstr(output_file, ".hda") != NULL) {
        target_format = FORMAT_HDA;
    } else if (strstr(output_file, ".EFI") != NULL || strstr(output_file, ".efi") != NULL) {
        target_format = FORMAT_EFI;
    } else if (strstr(output_file, ".bin") != NULL) {
        target_format = FORMAT_BIN;
    }
    
    if (opts->verbose) {
        const char *fmt_name = "AETB";
        switch (target_format) {
            case FORMAT_LET: fmt_name = "LET"; break;
            case FORMAT_AKI: fmt_name = "AKI"; break;
            case FORMAT_SRV: fmt_name = "SRV"; break;
            case FORMAT_HDA: fmt_name = "HDA"; break;
            case FORMAT_EFI: fmt_name = "PE32+ EFI (Embedded AETB)"; break;
            case FORMAT_PE: fmt_name = "PE32+ (Industrial Grade UEFI)"; break;
            case FORMAT_BIN: fmt_name = "BIN"; break;
        }
        printf("[INFO] 目标格式: %s\n", fmt_name);
    }

    if ((target_format == FORMAT_EFI ||
         target_format == FORMAT_PE ||
         target_format == FORMAT_AKI ||
         target_format == FORMAT_HDA ||
         target_format == FORMAT_SRV ||
         target_format == FORMAT_BIN) &&
        strcmp(opts->mode, "architect") != 0) {
        effective_mode = "architect";
        if (opts->verbose) {
            printf("[INFO] 系统二进制目标自动切换语义模式: sandbox -> architect\n");
        }
    }
    if ((target_format == FORMAT_EFI ||
         target_format == FORMAT_PE ||
         target_format == FORMAT_AKI ||
         target_format == FORMAT_HDA ||
         target_format == FORMAT_SRV ||
         target_format == FORMAT_BIN) &&
        strcmp(opts->target_mode, "kernel") != 0) {
        effective_target_mode = "kernel";
        if (opts->verbose) {
            printf("[INFO] 系统二进制目标自动切换 target: application -> kernel\n");
        }
    }

    if (target_format == FORMAT_BIN && opts->input_count == 1) {
        int asm_direct = compile_inline_asm_to_bin(opts->input_files[0],
                                                   output_file,
                                                   opts->machine_bits,
                                                   opts->verbose);
        if (asm_direct == 1) {
            if (opts->verbose) {
                printf("✓ 编译成功\n");
                printf("  输出: %s\n", opts->output_file);
            }
            return 0;
        }
        if (asm_direct < 0) {
            fprintf(stderr, "Error: direct asm->bin pipeline failed for '%s'\n", opts->input_files[0]);
            return 1;
        }
    }

    memset(&let_emit_opts, 0, sizeof(let_emit_opts));
    let_emit_opts.target_isa = map_isa_to_contract(opts->isa, opts->machine_bits);
    let_emit_opts.machine_bits = (uint16_t)opts->machine_bits;
    let_emit_opts.endianness = LET_ENDIAN_LITTLE;
    let_emit_opts.abi_kind = opts->freestanding ? LET_ABI_FREESTANDING : LET_ABI_SYSV;
    let_emit_opts.code_model = LET_CODE_MODEL_FLAT;
    let_emit_opts.reloc_width = default_reloc_width_for_bits(opts->machine_bits);
    let_emit_opts.entry_encoding = LET_ENTRY_ENCODING_OFFSET;
    let_emit_opts.bin_flags = LET_BIN_FLAG_EXPORTABLE | LET_BIN_FLAG_RELOC_COMPLETE_REQUIRED;
    if (opts->bin_flat) {
        let_emit_opts.bin_flags |= LET_BIN_FLAG_FLAT_DEFAULT;
    }
    let_emit_opts.reloc_encoding_version = 1u;
    let_emit_opts.instruction_profile = 1u;
    let_emit_opts.syscall_profile = opts->freestanding ? 1u : 2u;
    let_emit_opts.sandbox_patch_required = (strcmp(effective_mode, "architect") == 0) ? 0u : 1u;

    /* LET weaving path: .let -> AKI/HDA/SRV/AETB via assembly weaver */
    memset(&let_weave_opts, 0, sizeof(let_weave_opts));
    let_weave_opts.bin_flat = opts->bin_flat;
    let_weave_opts.bin_with_map = opts->bin_with_map;
    let_weave_opts.has_bin_entry_offset = opts->has_bin_entry_offset;
    let_weave_opts.bin_entry_offset = (unsigned long long)opts->bin_entry_offset;

    if (opts->input_count == 1 &&
        strstr(opts->input_files[0], ".let") != NULL &&
        target_format != FORMAT_LET &&
        target_format != FORMAT_EFI) {
        int weave_target = 0;
        switch (target_format) {
            case FORMAT_AKI: weave_target = LET_WEAVE_TARGET_AKI; break;
            case FORMAT_SRV: weave_target = LET_WEAVE_TARGET_SRV; break;
            case FORMAT_HDA: weave_target = LET_WEAVE_TARGET_HDA; break;
            case FORMAT_AETB: weave_target = LET_WEAVE_TARGET_AETB; break;
            case FORMAT_BIN: weave_target = LET_WEAVE_TARGET_BIN; break;
            default: weave_target = 0; break;
        }
        if (weave_target == 0) {
            fprintf(stderr, "Error: Unsupported weave target for LET input\n");
            return 1;
        }
        if (opts->verbose) {
            printf("[INFO] LET weaving: %s -> %s\n", opts->input_files[0], opts->output_file);
        }
        if (let_weave_to_target(opts->input_files[0], opts->output_file, weave_target, opts->verbose, &let_weave_opts) != 0) {
            fprintf(stderr, "Error: LET weaving failed\n");
            return 1;
        }
        if (opts->verbose) {
            printf("✓ 编译成功\n");
            printf("  输出: %s\n", opts->output_file);
        }
        return 0;
    }
    
    /* 创建临时内存文件用于生成代码 */
    FILE *temp_output = tmpfile();
    if (!temp_output) {
        fprintf(stderr, "Error: Failed to create temporary file\n");
        return 1;
    }
    
    CodeGenerator *gen = codegen_create(temp_output);
    if (!gen) {
        fprintf(stderr, "Error: Failed to create code generator\n");
        fclose(temp_output);
        return 1;
    }
    
    /* 设置编译目标格式（用于print()机器码生成）*/
    gen->target_format = target_format;
    gen->use_uefi = (target_format == FORMAT_EFI || target_format == FORMAT_PE);
    gen->use_syscall = (target_format == FORMAT_AETB || target_format == FORMAT_AKI || 
                        target_format == FORMAT_SRV || target_format == FORMAT_HDA);
    
    if (codegen_set_target(gen, opts->isa, opts->machine_bits) != 0) {
        fprintf(stderr, "Error: unsupported Stage1 target combination: isa=%s bits=%d\n", opts->isa, opts->machine_bits);
        codegen_destroy(gen);
        fclose(temp_output);
        return 1;
    }
    
    ASTNode *merged_ast = NULL;
    SemanticResult semantic_agg;
    int frontend_failed = 0;
    semantic_result_init(&semantic_agg);
    
    /* 处理所有输入文件（含递归导入解析） */
    for (int file_idx = 0; file_idx < opts->input_count; file_idx++) {
        if (opts->verbose) {
            printf("[INFO] Processing file %d/%d: %s\n", file_idx + 1, opts->input_count, opts->input_files[file_idx]);
        }
        
        /* 新增：验证禁止 C 导出（仅内核模式） */
        if (validate_no_foreign_c(opts->input_files[file_idx], effective_target_mode) != 0) {
            codegen_destroy(gen);
            return 1;
        }
        
        char *source = read_file(opts->input_files[file_idx]);
        if (!source) {
            codegen_destroy(gen);
            return 1;
        }
        
        /* [TODO-03] 预处理阶段：检查禁忌头文件 */
        if (preprocessor_scan_for_forbidden_includes(source) > 0) {
            fprintf(stderr, "编译已被罢工，退出码: %d\n", COMPILER_STRIKE_CODE);
            free(source);
            codegen_destroy(gen);
            return COMPILER_STRIKE_CODE;
        }
        
        // 1. 词法分析
        Lexer *lexer = lexer_create(source);
        Token *tokens = lexer_tokenize(lexer);
        const char *lex_err = lexer_get_error(lexer);
        if (lex_err && lex_err[0] != '\0') {
            fprintf(stderr, "[FATAL] Lexer error in file '%s': %s\n",
                    opts->input_files[file_idx], lex_err);
            frontend_failed = 1;
        }
        
        // 2. 语法分析
        Parser *parser = parser_create(tokens, opts->debug);

        ASTNode *file_ast = parser_parse_program(parser);

        const char *parse_err = parser_get_error(parser);
        int parse_err_count = parser_get_error_count(parser);
        int parse_panic = parser_is_panic(parser);
        if ((parse_err && parse_err[0] != '\0') || parse_err_count > 0 || parse_panic) {
            fprintf(stderr,
                    "[FATAL] Parser failed in file '%s' (errors=%d, panic=%d): %s\n",
                    opts->input_files[file_idx],
                    parse_err_count,
                    parse_panic,
                    (parse_err && parse_err[0] != '\0') ? parse_err : "(no parser message)");
            frontend_failed = 1;
        }

        if (!frontend_failed && file_ast) {
            char sem_error[512] = {0};
            SemanticResult file_sem;
            int sem_ret;
            sem_ret = semantic_analyze_program(file_ast,
                                               opts->input_files[file_idx],
                                               effective_mode,
                                               &file_sem,
                                               sem_error,
                                               sizeof(sem_error));
            if (sem_ret != 0) {
                fprintf(stderr,
                        "[FATAL] Semantic check failed in '%s': %s\n",
                        opts->input_files[file_idx],
                        sem_error[0] ? sem_error : "semantic violation");
                frontend_failed = 1;
            } else if (merge_semantic_result(&semantic_agg, &file_sem) != 0) {
                fprintf(stderr,
                        "[FATAL] Failed to merge semantic result for '%s'\n",
                        opts->input_files[file_idx]);
                frontend_failed = 1;
            }
        }
        
        /* 合并AST：将文件的声明添加到合并AST中 */
        if (file_ast) {
            if (merged_ast == NULL) {
                merged_ast = file_ast;
            } else if (merge_program_ast(merged_ast, file_ast) != 0) {
                fprintf(stderr,
                        "[FATAL] Failed to merge AST for '%s'\n",
                        opts->input_files[file_idx]);
                frontend_failed = 1;
            }
        }
        
        parser_destroy(parser);
        lexer_destroy(lexer);
        free(source);

        /* 递归解析导入：仅收集，解析和编译留给上层构建系统通过输入列表提供
         * （工业级：保留显式输入控制，避免隐式文件注入） */
        ImportList imports = {0};
        import_list_collect(file_ast, opts->input_files[file_idx], &imports);

        /* 仅告警提示缺失输入，由调用层更新 mk 依赖；不进行隐式编译以保持可控性 */
        for (size_t ii = 0; ii < imports.count; ii++) {
            char resolved[512];
            char from_buf[512];
            char root_buf[512];
            char *from_dir = NULL;
            char *root_dir = NULL;

            strncpy(from_buf, imports.items[ii].from_file, sizeof(from_buf) - 1);
            from_buf[sizeof(from_buf) - 1] = '\0';
            if (from_buf[0]) {
                from_dir = dirname(from_buf);
            }

            if (opts->input_count > 0) {
                strncpy(root_buf, opts->input_files[0], sizeof(root_buf) - 1);
                root_buf[sizeof(root_buf) - 1] = '\0';
                root_dir = dirname(root_buf);
            }

            if (import_resolve_path(imports.items[ii].module,
                                    from_dir,
                                    root_dir,
                                    resolved,
                                    sizeof(resolved)) == 0) {
                int already_listed = 0;
                for (int k = 0; k < opts->input_count; k++) {
                    if (strcmp(opts->input_files[k], resolved) == 0) {
                        already_listed = 1;
                        break;
                    }
                }
                if (!already_listed) {
                    fprintf(stderr,
                            "[WARN] Import '%s' resolved to '%s' (from %s) is not in build graph; add to input list for full compilation.\n",
                            imports.items[ii].module, resolved, imports.items[ii].from_file);
                }
            } else {
                fprintf(stderr,
                        "[WARN] Import '%s' (from %s) could not be resolved automatically; ensure mk translates naming per AethelOS rules.\n",
                        imports.items[ii].module,
                        imports.items[ii].from_file);
            }
        }

        if (frontend_failed) {
            break;
        }
    }

    if (frontend_failed) {
        fprintf(stderr, "[FATAL] Frontend compilation failed. Output file '%s' will NOT be generated.\n",
                output_file);
        codegen_destroy(gen);
        fclose(temp_output);
        return 1;
    }
    
    /* 使用合并的AST进行代码生成 */
    int result = 0;
    if (!merged_ast) {
        fprintf(stderr, "[FATAL] No valid AST generated from input sources. Aborting.\n");
        codegen_destroy(gen);
        fclose(temp_output);
        return 1;
    }
    result = codegen_generate(gen, merged_ast);
    
    if (result != 0) {
        const char *cg_error = codegen_get_error(gen);
        if (cg_error && cg_error[0] != '\0') {
            fprintf(stderr, "[FATAL] Code generation failed: %s\n", cg_error);
        } else {
            fprintf(stderr, "[FATAL] Code generation failed with error code %d\n", result);
        }
        codegen_destroy(gen);
        fclose(temp_output);
        return result;
    }

    codegen_destroy(gen);
    
    /* 从临时文件读取编译的二进制数据 */
    fseek(temp_output, 0, SEEK_END);
    size_t binary_size = ftell(temp_output);
    fseek(temp_output, 0, SEEK_SET);
    
    uint8_t *binary_data = malloc(binary_size);
    if (!binary_data) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(temp_output);
        return 1;
    }
    
    if (fread(binary_data, 1, binary_size, temp_output) != binary_size) {
        fprintf(stderr, "Error: Failed to read compiled binary\n");
        free(binary_data);
        fclose(temp_output);
        return 1;
    }
    
    fclose(temp_output);
    
    /* 根据目标格式直接生成最终文件 */
    int format_result = 0;
    
    /* 从完整二进制数据中提取三个Zone */
    const uint8_t *code = NULL, *mirror_data = NULL, *constant_data = NULL;
    size_t code_size = 0, mirror_size = 0, constant_size = 0;
    uint32_t entry_offset = 0;  /* PE格式所需的EFI入口点偏移 */
    
    if (extract_zones_from_binary(binary_data, binary_size, 
                                 &code, &code_size,
                                 &mirror_data, &mirror_size,
                                 &constant_data, &constant_size) != 0) {
        fprintf(stderr, "Error: Failed to extract zones from binary data\n");
        free(binary_data);
        return 1;
    }
    
    /* 对于PE格式，计算entry_point_offset */
    if (target_format == FORMAT_PE) {
        /* 从binary_data header中提取entry point offset */
        if (binary_size >= sizeof(AethelBinaryHeader)) {
            AethelBinaryHeader *hdr = (AethelBinaryHeader *)binary_data;
            
            /* 对于bootloader，entry point通常从act_flow_offset的第一条指令开始 */
            /* 但如果提供了显式的entry offset，使用它 */
            if (opts->has_bin_entry_offset) {
                entry_offset = (uint32_t)opts->bin_entry_offset;
            } else {
                /* 默认：假设entry point在code section的开始（偏移0）
                 * 这是标准UEFI EFI_IMAGE_ENTRY_POINT的位置 */
                entry_offset = 0;
            }
            
            if (opts->verbose) {
                fprintf(stderr, "[PE-IND] Entry point offset will be: 0x%x\n", entry_offset);
            }
        }
    }
    
    if (target_format == FORMAT_AKI) {
        if (opts->verbose) {
            printf("[INFO] 使用模块化后端生成 AKI 内核镜像: %s\n", output_file);
        }
        format_result = aki_generate_image(output_file, code, code_size, 
                                          mirror_data, mirror_size, 
                                          constant_data, constant_size);
    }
    else if (target_format == FORMAT_LET) {
        if (opts->verbose) {
            printf("[INFO] 生成 LET 逻辑胚胎: %s\n", output_file);
        }
        format_result = let_generate_image(output_file,
                                          code, code_size,
                                          mirror_data, mirror_size,
                                          constant_data, constant_size,
                                          &semantic_agg,
                                          &let_emit_opts);
    }
    else if (target_format == FORMAT_BIN) {
        char let_tmp[] = "/tmp/aethelc-let-XXXXXX";
        int let_fd;
        if (opts->verbose) {
            printf("[INFO] 生成 BIN: AE -> LET -> BIN\n");
        }
        let_fd = mkstemp(let_tmp);
        if (let_fd < 0) {
            fprintf(stderr, "Error: failed to create temporary LET file\n");
            free(binary_data);
            return 1;
        }
        close(let_fd);

        format_result = let_generate_image(let_tmp,
                                          code, code_size,
                                          mirror_data, mirror_size,
                                          constant_data, constant_size,
                                          &semantic_agg,
                                          &let_emit_opts);
        if (format_result == 0) {
            format_result = let_weave_to_target(let_tmp,
                                                output_file,
                                                LET_WEAVE_TARGET_BIN,
                                                opts->verbose,
                                                &let_weave_opts);
        }
        unlink(let_tmp);
    }
    else if (target_format == FORMAT_SRV) {
        if (opts->verbose) {
            printf("[INFO] 使用模块化后端生成 SRV 系统服务: %s\n", output_file);
        }
        format_result = srv_generate_image(output_file, code, code_size,
                                          mirror_data, mirror_size,
                                          constant_data, constant_size);
    }
    else if (target_format == FORMAT_HDA) {
        if (opts->verbose) {
            printf("[INFO] 使用模块化后端生成 HDA 硬件驱动: %s\n", output_file);
        }
        format_result = hda_generate_image(output_file, code, code_size,
                                          mirror_data, mirror_size,
                                          constant_data, constant_size);
    }
    else if (target_format == FORMAT_EFI) {
        if (opts->verbose) {
            printf("[INFO] 使用模块化后端生成 PE32+ EFI UEFI 加载器: %s\n", output_file);
        }
        
        /* PE32+ 必须包含完整的AETB逻辑结构，就像AKI一样 
           传入binary_data（完整AETB）而不是单个zone */
        format_result = pe32plus_generate_efi(output_file, binary_data, binary_size);
    }
    else if (target_format == FORMAT_PE) {
        if (opts->verbose) {
            printf("[INFO] 使用工业级后端生成 PE32+ UEFI 应用: %s\n", output_file);
        }
        
        /* 工业级PE生成器从三个独立的Zone生成标准UEFI应用
         * 不包含AETB结构，而是使用标准的.text/.data/.rodata sections
         */
        
        PE_Industrial_Input pe_input = {
            .output_filename = output_file,
            .code_section = code,
            .code_size = code_size,
            .data_section = mirror_data,
            .data_size = mirror_size,
            .rodata_section = constant_data,
            .rodata_size = constant_size,
            .entry_point_offset = entry_offset,
            .image_base = PE_IND_IMAGE_BASE,
            .stack_reserve = 0x10000,
            .stack_commit = 0x1000,
            .heap_reserve = 0x100000,
            .heap_commit = 0x1000
        };
        
        if (opts->verbose) {
            printf("[INFO] Entry point offset: 0x%x (code size: 0x%zx)\n", entry_offset, code_size);
        }
        
        format_result = pe_industrial_generate_efi(&pe_input);
    }
    else {
        /* 格式为AETB - 直接复制到最终文件 */
        if (opts->verbose) {
            printf("[INFO] 直接生成 AETB 格式: %s\n", output_file);
        }
        
        FILE *out = fopen(output_file, "wb");
        if (!out) {
            fprintf(stderr, "Error: Cannot create output file '%s'\n", output_file);
            free(binary_data);
            return 1;
        }
        
        if (fwrite(binary_data, 1, binary_size, out) != binary_size) {
            fprintf(stderr, "Error: Failed to write output file\n");
            fclose(out);
            free(binary_data);
            return 1;
        }
        
        fclose(out);
        format_result = 0;
    }
    
    free(binary_data);
    
    if (format_result != 0) {
        unlink(output_file);
        fprintf(stderr, "Error: Failed to generate output format\n");
        return 1;
    }
    
    if (opts->verbose) {
        printf("✓ 编译成功\n");
        printf("  输出: %s\n", opts->output_file);
    }
    
    return 0;
}

int main(int argc, char **argv) {
    CompilerOptions opts;
    
    if (parse_args(argc, argv, &opts) != 0) {
        return 1;
    }

    if (opts.verify_let_mode) {
        return let_verify_contract(opts.verify_let_file, opts.verbose) == 0 ? 0 : 1;
    }

    if (opts.dump_reloc_mode) {
        return let_dump_reloc_dna(opts.dump_reloc_file, opts.dump_reloc_output) == 0 ? 0 : 1;
    }
    
    if (opts.is_iso_mode) {
        if (opts.verbose) {
            printf("[INFO] ISO 生成模式\n");
            printf("[INFO] 内核: %s\n", opts.kernel_file);
            printf("[INFO] EFI 引导: %s\n", opts.efi_boot_file);
            printf("[INFO] 输出: %s\n", opts.output_file);
            printf("[INFO] 大小: %llu MB\n", opts.iso_size_mb);
        }
        
        return aefs_generate_iso_image(opts.kernel_file, opts.efi_boot_file,
                                      opts.output_file, opts.iso_size_mb);
    }
    
    return run_compiler(&opts);
}
