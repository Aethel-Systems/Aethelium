/**
 * ============================================================================
 * AethelOS ADL ISO Builder - Command Line Tool
 * ============================================================================
 * 支持两种模式：
 * 1. 完整模式（默认）：从零开始生成完整ISO
 * 2. AEFS-only模式（--aefs-only）：追加AEFS到现有ISO
 */

#include "adl_iso_builder.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    const char *kernel_file = NULL;
    const char *efi_boot_file = NULL;
    const char *installer_file = NULL;
    const char *drivers_dir = NULL;
    const char *output_iso = NULL;
    int auto_size = 1;
    int aefs_only = 0;
    
    /* 解析命令行参数 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--kernel") == 0 && i + 1 < argc) {
            kernel_file = argv[++i];
        } else if (strcmp(argv[i], "--efi") == 0 && i + 1 < argc) {
            efi_boot_file = argv[++i];
        } else if (strcmp(argv[i], "--installer") == 0 && i + 1 < argc) {
            installer_file = argv[++i];
        } else if (strcmp(argv[i], "--drivers") == 0 && i + 1 < argc) {
            drivers_dir = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_iso = argv[++i];
        } else if (strcmp(argv[i], "--auto-size") == 0) {
            auto_size = 1;
        } else if (strcmp(argv[i], "--aefs-only") == 0) {
            aefs_only = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("AethelOS ADL ISO Builder\n");
            printf("\nUsage: %s [options]\n", argv[0]);
            printf("\nModes:\n");
            printf("  Full mode (default):  Generate complete UEFI-compliant ISO from scratch\n");
            printf("  AEFS-only mode:       Append AEFS content to existing ISO (with --aefs-only flag)\n");
            printf("\nOptions:\n");
            printf("  --kernel FILE        kernel.aki path\n");
            printf("  --efi FILE           BOOTX64.EFI path\n");
            printf("  --installer FILE     appInstaller.iya path (optional)\n");
            printf("  --drivers DIR        Drivers directory (optional)\n");
            printf("  --output FILE        Output ISO path\n");
            printf("  --auto-size          Auto-adjust ISO size (default)\n");
            printf("  --aefs-only          Append AEFS only (requires existing ISO with FAT32 ESP)\n");
            printf("  --help               Show this help\n");
            return 0;
        }
    }
    
    /* 验证必需参数 */
    if (!kernel_file || !efi_boot_file || !output_iso) {
        fprintf(stderr, "ERROR: Missing required parameters\n");
        fprintf(stderr, "Usage: %s --kernel FILE --efi FILE --output FILE\n", argv[0]);
        return 1;
    }
    
    int result;
    
    if (aefs_only) {
        /* AEFS-only 模式：追加到现有ISO */
        fprintf(stderr, "===============================================================================\n");
        fprintf(stderr, "ADL ISO Builder - AEFS-Only Mode\n");
        fprintf(stderr, "===============================================================================\n");
        fprintf(stderr, "Appending AEFS content to existing ISO...\n");
        fprintf(stderr, "  Kernel:     %s\n", kernel_file);
        fprintf(stderr, "  Installer:  %s\n", installer_file ? installer_file : "(null)");
        fprintf(stderr, "  Target ISO: %s\n", output_iso);
        fprintf(stderr, "\n");
        
        result = adl_iso_builder_append_aefs(
            output_iso,
            kernel_file,
            installer_file,
            drivers_dir
        );
    } else {
        /* 完整模式：从零生成ISO */
        result = adl_iso_builder_generate(
            kernel_file,
            efi_boot_file,
            installer_file,
            drivers_dir,
            output_iso,
            auto_size
        );
    }
    
    return result == 0 ? 0 : 1;
}
