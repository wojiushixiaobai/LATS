/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _LATX_INTB_SYS_H_
#define _LATX_INTB_SYS_H_

#if defined(CONFIG_SOFTMMU) && defined(CONFIG_LATX_HACK_MEM) && defined(NDEBUG)
#define LATX_SUPERVISOR_DISPATCH_BITS 14
extern TranslationBlock *latx_supervisor_dispatch[1 << LATX_SUPERVISOR_DISPATCH_BITS];
bool latx_supervisor_dispatch_enabled(void);
void latx_supervisor_dispatch_flush(void);
#endif

#define LATXS_INTB_LINK_ENABLE
#define LATXS_INTB_LINK_OPTION_CHECK





#ifdef LATXS_INTB_LINK_ENABLE

int gen_latxs_intb_lookup(void *code_ptr);
int gen_latxs_intb_lookup_compact(void *code_ptr);
int gen_latxs_intb_njc_return(void *code_ptr);
int gen_latxs_intb_resume_lookup(void *code_ptr);
void latxs_out_jmp_cache_hash(void *pc, void *tmp, void *hash);
bool latxs_tr_gen_compact_njc_slot(void *pc, void *entry, void *tmp,
        bool use_layout);
void gen_latxs_intb_lookup_njc_legacy(void *code_ptr,
        void *tmp, void *tmp0, void *tmp1,
        void *label_lookup_miss, void *slot_out);
void latxs_tr_gen_intb_sync_gpc(void);
void gen_latxs_intb_lookup_njc_compact(void *code_ptr,
        void *tmp, void *tmp0, void *tmp1,
        void *label_lookup_miss, void *slot_out, bool use_layout);
void gen_latxs_intb_lookup_njc_inline(bool compact, bool use_layout);
void gen_latxs_intb_lookup_njc_inline_target(bool compact, bool use_layout,
        bool call_target);
void gen_latxs_intb_lookup_njc_call(bool compact, void *miss_label,
        void *call_label, void *jump_target);
void gen_latxs_intb_fsht_call(int fsht_id, bool tucpfast,
        void *miss_label, void *call_label, void *jump_target);
void gen_latxs_intb_fsht_call_miss(int fsht_id, bool tucpfast,
        void *njc_miss, void *miss_label, void *call_label);
void gen_latxs_intb_fsht_call_njc(int fsht_id, void *njc_miss,
        void *call_label, void *jump_target);
void gen_latxs_intb_fsht_call_njc_save(int fsht_id, void *njc_miss,
        void *call_label, void *jump_target, void *scratch);
void gen_latxs_intb_fsht_call_miss_saved(int fsht_id, bool tucpfast,
        void *njc_miss, void *miss_label, void *call_label, const void *scratch);
int gen_latxs_intb_fsht_lookup(void *code_ptr, int fsht_id);
int gen_latxs_tu_cross_page_check(void *code_ptr, int n);
int gen_latxs_tu_cross_page_check_ptr(void *code_ptr);
int gen_latxs_intb_fsht_lookup_tucpfast(void *code_ptr, int fsht_id);
int gen_latxs_intb_fsht_call_lookup_tucpfast(void *code_ptr, int fsht_id);

#ifdef LATXS_INTB_LINK_OPTION_CHECK
#define intb_link_enable()  __intb_link_enable()
int __intb_link_enable(void);
#else
#define intb_link_enable()  (1)
#endif

#else /* no LATXS_INTB_LINK_ENABLE */

#define intb_link_enable()  (0)

#endif /* LATXS_INTB_LINK_ENABLE */



#endif
