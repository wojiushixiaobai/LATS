/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Test the real marker recognizer and translation-time layout verifier
 * without enabling the experimental feature.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define TB_JMP_OFFSET_INVALID 0xffff
typedef struct TranslationBlock {
    struct { const void *ptr; size_t size; } tc;
    uint32_t ir2_num;
    uint16_t jmp_insn_offset[2], jmp_reset_offset[2];
    unsigned skip;
} TranslationBlock;
#define TB_EFLAGTB_SKIP(tb) ((tb)->skip)
#include "../../target/i386/latx/include/latx-sysflagtb-recovery.h"

static void test_marker_edge_recognizer(void)
{
    uint32_t code[32] = {0};
    TranslationBlock tb = {
        .tc = { code, sizeof(code) },
        .ir2_num = 32,
        .jmp_insn_offset = { 16, 48 },
        .jmp_reset_offset = { 24, 56 },
        .skip = 3,
    };
    uintptr_t base = (uintptr_t)code;
    code[3] = code[11] = LATX_SYSFLAGTB_MARKER_INSN;
    assert(latx_sysflagtb_recovery_edge(&tb, base + 24) == 0);
    assert(latx_sysflagtb_recovery_edge(&tb, base + 56) == 1);
    /* Patching or unlinking the two-word slot must not change its identity. */
    code[4] = 0x50000800;
    code[5] = 0x03400000;
    assert(latx_sysflagtb_recovery_edge(&tb, base + 24) == 0);
    code[4] = 0x1e000004;
    code[5] = 0x4c000080;
    assert(latx_sysflagtb_recovery_edge(&tb, base + 24) == 0);
    assert(latx_sysflagtb_recovery_edge(&tb, base + 20) == -1);
    assert(latx_sysflagtb_recovery_edge(&tb, base - 4) == -1);
    assert(latx_sysflagtb_recovery_edge(&tb, base + 26) == -1); /* misaligned */
    tb.skip = 1;
    assert(latx_sysflagtb_recovery_edge(&tb, base + 56) == -1);
    assert(latx_sysflagtb_recovery_edge_lim(&tb, base + 24, 128) == 0);
    assert(latx_sysflagtb_recovery_edge_lim(&tb, base + 24, 24) == -1);
    tb.skip = 3;
    code[3] = 0;
    assert(latx_sysflagtb_recovery_edge(&tb, base + 24) == -1);
    code[3] = LATX_SYSFLAGTB_MARKER_INSN;
    tb.jmp_reset_offset[0] = 28;
    assert(latx_sysflagtb_recovery_edge(&tb, base + 24) == -1);
    tb.jmp_reset_offset[0] = 24;
    tb.jmp_insn_offset[0] = 17;
    tb.jmp_reset_offset[0] = 25;
    assert(latx_sysflagtb_recovery_edge(&tb, base + 25) == -1);
    tb.jmp_insn_offset[0] = 16;
    tb.jmp_reset_offset[0] = 24;
    tb.ir2_num = 6;
    assert(latx_sysflagtb_recovery_edge(&tb, base + 24) == -1);
    tb.ir2_num = 32;
    tb.jmp_insn_offset[0] = TB_JMP_OFFSET_INVALID;
    assert(latx_sysflagtb_recovery_edge(&tb, base + 24) == -1);
}

static void test_layout_verifier(void)
{
    uint32_t code[32] = {0};
    TranslationBlock tb = {
        .tc = { code, sizeof(code) },
        .ir2_num = 0, /* not published yet during translation-time check */
        .jmp_insn_offset = { 16, 48 },
        .jmp_reset_offset = { 24, 56 },
        .skip = 3,
    };
    code[0] = LATX_SYSFLAGTB_ENTRY_CLEAR_INSN;
    code[3] = code[11] = LATX_SYSFLAGTB_MARKER_INSN;

    /* Valid deferred TB. */
    assert(latx_sysflagtb_verify_tb(&tb, sizeof(code)) == 0);
    /* No skip edge: only the entry clear is required. */
    tb.skip = 0;
    assert(latx_sysflagtb_verify_tb(&tb, sizeof(code)) == 0);
    tb.skip = 3;
    /* Missing entry clear. */
    code[0] = 0;
    assert(latx_sysflagtb_verify_tb(&tb, sizeof(code)) == -2);
    code[0] = LATX_SYSFLAGTB_ENTRY_CLEAR_INSN;
    /* Only one skip edge: the defer machinery always records both. */
    tb.skip = 1;
    assert(latx_sysflagtb_verify_tb(&tb, sizeof(code)) == -3);
    tb.skip = 3;
    /* Missing marker word at reported reset offset. */
    code[3] = 0;
    assert(latx_sysflagtb_verify_tb(&tb, sizeof(code)) == -5);
    code[3] = LATX_SYSFLAGTB_MARKER_INSN;
    /* Reset offset outside the assembled code. */
    tb.jmp_reset_offset[0] = 252;
    assert(latx_sysflagtb_verify_tb(&tb, sizeof(code)) != 0);
    tb.jmp_reset_offset[0] = 24;
    assert(latx_sysflagtb_verify_tb(&tb, sizeof(code)) == 0);
    /* Code size smaller than the marker window. */
    assert(latx_sysflagtb_verify_tb(&tb, 16) != 0);
}

int main(void)
{
    test_marker_edge_recognizer();
    test_layout_verifier();
    puts("sysflagtb experimental recovery edge recognition: PASS");
}
