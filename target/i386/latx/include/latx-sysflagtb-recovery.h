/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LATX_SYSFLAGTB_RECOVERY_H
#define LATX_SYSFLAGTB_RECOVERY_H

/*
 * Experimental building block for recovering a deferred TB exit.  It is only
 * active with sysflagtb=3 (defer + HAMT recovery), which is validation-only;
 * the production sysflagtb=1 + HAMT configuration stays forcibly disabled.
 *
 * Exit layout emitted by latxs_tr_gen_exit_tb_j_tb_link() for a deferred edge:
 *   pcaddi $ra, 3       # address of the materialisation after the slot
 *   <two-word link slot>
 *   <materialisation>   <- $ra target, marker == slot + 8
 *
 * Every TB starts with "addi.d $ra, $zero, 0" (latxs_tr_ir2_generate).  That
 * is the freshness protocol: $ra can only hold a marker between executing the
 * pcaddi above and entering the linked successor TB; any other TB entry, the
 * link-miss fallback and the pending-IRQ path all clear it before guest code
 * or any C observer can run.  A poisoned successor entry is the only place
 * where the clear does not execute, so the invalid-entry handler can rely on
 * $ra identifying the in-flight predecessor edge.
 *
 * Callers must first find the TB containing marker - 4 using the protected
 * code-region lookup; never treat an arbitrary native register as a TB
 * pointer.  The TB and its code must remain alive for the entire check and
 * subsequent recovery (true until an exclusive tb_flush, which cannot run
 * while the current vCPU is in the handler).  Marker and code_base must use
 * the same address alias.  The check deliberately does not inspect a live
 * link slot: it may change concurrently during unlink or relink.  The
 * instruction preceding the slot and the TB's skip bits are immutable after
 * publication.
 */
#define LATX_SYSFLAGTB_MARKER_INSN UINT32_C(0x18000061)

/* addi.d $ra, $zero, 0: first word of every ordinary TB while the recovery
 * protocol is enabled. */
#define LATX_SYSFLAGTB_ENTRY_CLEAR_INSN UINT32_C(0x02c00001)

static inline int latx_sysflagtb_recovery_edge_lim(const TranslationBlock *tb,
                                                   uintptr_t marker,
                                                   uint64_t code_bytes)
{
    uintptr_t base = (uintptr_t)tb->tc.ptr;
    if (!base || !code_bytes || marker < base || (marker & 3)) {
        return -1;
    }
    uint64_t marker_off = marker - base;
    for (int edge = 0; edge < 2; edge++) {
        unsigned slot = tb->jmp_insn_offset[edge];
        if (!(TB_EFLAGTB_SKIP(tb) & (1u << edge)) ||
            slot == TB_JMP_OFFSET_INVALID || slot < 4 || (slot & 3) ||
            tb->jmp_reset_offset[edge] != slot + 8 ||
            marker_off != slot + 8 || marker_off + 4 > code_bytes) {
            continue;
        }
        const uint32_t *tag = (const uint32_t *)(base + slot - 4);
        if (*tag == LATX_SYSFLAGTB_MARKER_INSN) {
            return edge;
        }
    }
    return -1;
}

static inline int latx_sysflagtb_recovery_edge(const TranslationBlock *tb,
                                               uintptr_t marker)
{
    return latx_sysflagtb_recovery_edge_lim(tb, marker,
                                            (uint64_t)tb->ir2_num * 4);
}

/*
 * Translation-time invariant check.  Runs once per TB after assembly, while
 * the code is still private to the translating vCPU.  @code_bytes is the
 * assembled code size (tb->ir2_num is not published yet at that point).
 *
 * Returns 0 when the TB is consistent with the runtime recovery protocol:
 *  - entry 0 is the $ra clear (poisoning replaces exactly this word),
 *  - a TB with any skip edge has both of them (the defer machinery always
 *    materialises both exits),
 *  - each skip edge's marker word is present and recognized with the same
 *    structural checks the HAMT handler uses.
 * A non-zero result is a compiler/protocol bug and must abort the build's
 * first translation instead of corrupting guest flags at run time.
 */
static inline int latx_sysflagtb_verify_tb(const TranslationBlock *tb,
                                           uint64_t code_bytes)
{
    uintptr_t base = (uintptr_t)tb->tc.ptr;
    unsigned skip = TB_EFLAGTB_SKIP(tb);

    if (!base || !code_bytes || code_bytes < 4) {
        return -1;
    }
    if (*(const uint32_t *)base != LATX_SYSFLAGTB_ENTRY_CLEAR_INSN) {
        return -2;
    }
    if (!skip) {
        return 0;
    }
    if (skip != 0x3u) {
        return -3;
    }
    for (int edge = 0; edge < 2; edge++) {
        if (tb->jmp_insn_offset[edge] == TB_JMP_OFFSET_INVALID ||
            (uint64_t)tb->jmp_insn_offset[edge] + 8 >= code_bytes) {
            return -4;
        }
        uintptr_t marker = base + tb->jmp_reset_offset[edge];
        if (latx_sysflagtb_recovery_edge_lim(tb, marker, code_bytes) != edge) {
            return -5;
        }
    }
    return 0;
}
#endif
