#ifndef AETHEL_LET_WEAVER_BRIDGE_H
#define AETHEL_LET_WEAVER_BRIDGE_H

enum {
    LET_WEAVE_TARGET_AKI = 1,
    LET_WEAVE_TARGET_SRV = 2,
    LET_WEAVE_TARGET_HDA = 3,
    LET_WEAVE_TARGET_AETB = 4,
    LET_WEAVE_TARGET_BIN = 5
};

typedef struct {
    int bin_flat;
    int bin_with_map;
    int has_bin_entry_offset;
    unsigned long long bin_entry_offset;
} LetWeaveOptions;

int let_weave_to_target(const char *let_file,
                        const char *output_file,
                        int target_format,
                        int verbose,
                        const LetWeaveOptions *options);

int let_verify_contract(const char *let_file, int verbose);
int let_dump_reloc_dna(const char *let_file, const char *output_file);

#endif
