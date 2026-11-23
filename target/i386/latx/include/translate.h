/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _TRANSLATE_H_
#define _TRANSLATE_H_

#include "common.h"
#include "ir1.h"
#include "ir2.h"
#include "qemu-def.h"

bool latxs_translate_adcx(IR1_INST *pir1);
bool latxs_translate_adox(IR1_INST *pir1);
bool latxs_translate_aesdec(IR1_INST *pir1);
bool latxs_translate_aesdeclast(IR1_INST *pir1);
bool latxs_translate_aesenc(IR1_INST *pir1);
bool latxs_translate_aesenclast(IR1_INST *pir1);
bool latxs_translate_aesimc(IR1_INST *pir1);
bool latxs_translate_aeskeygenassist(IR1_INST *pir1);
bool latxs_translate_andn(IR1_INST *pir1);
bool latxs_translate_mpsadbw(IR1_INST *pir1);

bool latxs_translate_bextr(IR1_INST *pir1);
bool latxs_translate_bzhi(IR1_INST *pir1);
bool latxs_translate_blsi(IR1_INST *pir1);
bool latxs_translate_blsmsk(IR1_INST *pir1);
bool latxs_translate_blsr(IR1_INST *pir1);
bool latxs_translate_clflushopt(IR1_INST *pir1);
bool latxs_translate_cmppd(IR1_INST *pir1);
bool latxs_translate_cmpps(IR1_INST *pir1);
bool latxs_translate_cmpss(IR1_INST *pir1);
bool latxs_translate_lzcnt(IR1_INST *pir1);
bool latxs_translate_mulx(IR1_INST *pir1);
bool latxs_translate_pclmulqdq(IR1_INST * pir1);
bool latxs_translate_pdep(IR1_INST *pir1);
bool latxs_translate_pext(IR1_INST *pir1);
bool latxs_translate_phminposuw(IR1_INST *pir1);
bool latxs_translate_rorx(IR1_INST *pir1);
bool latxs_translate_salc(IR1_INST *pir1);

bool latxs_translate_sha1msg1(IR1_INST *pir1);
bool latxs_translate_sha1msg2(IR1_INST *pir1);
bool latxs_translate_sha1nexte(IR1_INST *pir1);
bool latxs_translate_sha1rnds4(IR1_INST *pir1);
bool latxs_translate_sha256msg1(IR1_INST *pir1);
bool latxs_translate_sha256msg2(IR1_INST *pir1);
bool latxs_translate_sha256rnds2(IR1_INST *pir1);

void latxs_tr_reset_fcsr_rm(void);

void la_ld_by_op_size(IR2_OPND rd, IR2_OPND rj, int imm_si12, int op_size, EXMode em);
void la_st_by_op_size(IR2_OPND rd, IR2_OPND rj, int imm_si12, int op_size);

IR2_OPND convert_gpr_opnd(IR1_OPND *, EXMode em);

#define NONE            0
#define IS_CONVERT      1
#define IS_XMM_HI       (1 << 2)
#define IS_DEST_MMX     (1 << 3)

#include "env.h"

/* main translation procees */
void latxs_tr_tcg_region_offset(uint64_t offset, int mtcc_id);
uint64_t get_region_offset(int mtcc_id);
void latxs_tr_sync_region_offset(int mtcc_id);
uint8_t latxs_cpu_read_code_via_qemu(CPUX86State *env, ADDRX pc);
void latxs_tr_disasm(TranslationBlock *);
const uint8_t *latxs_ir1_bytes(void);
IR1_INST *latxs_get_ir1_list(TranslationBlock *, ADDRX, int *);

void latxs_tr_data_reset(TranslationBlock *tb);
void latxs_tr_sys_init(TranslationBlock *, int max, void *code_highwater, void *host_pc);
void latxs_tr_init(TranslationBlock *tb);
int  latxs_tr_ir2_assemble(const void *code_buffer);
void latxs_tr_fini(void);

int latxs_tr_translate_tb(TranslationBlock *tb, int *search_size);
bool latxs_tr_ir2_generate(TranslationBlock *tb);
void latxs_tr_init_translate_ir1(TranslationBlock *tb, int index);
int latxs_tr_check_buffer_overflow(const void *, TRANSLATION_DATA *);

void latxs_tr_gen_tb_start(void);
void latxs_tr_gen_tb_end(void);
#if defined(CONFIG_SOFTMMU) && defined(CONFIG_LATX_TU)
void *latxs_external_tu_stub(TranslationBlock *tb, int n, void *target);
void latxs_external_tu_stub_unlink(TranslationBlock *tb, int n);
void latxs_external_tu_stub_reset(unsigned mtcc);
void latxs_external_tu_stub_summary(void);
#endif

void latxs_tr_tb_init(TRANSLATION_DATA *td, TranslationBlock *tb);
void latxs_tr_ra_init(TRANSLATION_DATA *td);
void latxs_load_intcheck_to_reg(IR2_OPND *intop);
void latxs_tr_ir2_array_init(TRANSLATION_DATA *td);
void latxs_tr_fpu_init(TRANSLATION_DATA *td, TranslationBlock *tb);

void latxs_label_dispose(const void *code_buffer);

/* translate functions */
void latxs_tr_save_registers_to_env(
        uint32_t gpr_to_save,
        uint8_t fpr_to_save, int save_top,
        uint32_t xmm_to_save,
        uint8_t vreg_to_save);

void latxs_tr_load_registers_from_env(
        uint32_t gpr_to_load,
        uint8_t fpr_to_load, int load_top,
        uint32_t xmm_to_load,
        uint8_t vreg_to_load);

void latxs_tr_save_gprs_to_env(uint32_t mask);
void latxs_tr_save_fprs_to_env(uint8_t mask, int save_top);
void latxs_tr_save_xmms_to_env(uint32_t mask);
void latxs_tr_save_vreg_to_env(uint8_t mask);

void latxs_tr_load_gprs_from_env(uint32_t mask);
void latxs_tr_load_fprs_from_env(uint8_t mask, int load_top);
void latxs_tr_load_xmms_from_env(uint32_t mask);
void latxs_tr_load_vreg_from_env(uint8_t mask);

/* helper related functions */

void latxs_convert_fpregs_64_to_x80(void);
void latxs_convert_fpregs_x80_to_64(void);
void latxs_tr_cvt_fp64_to_80(void);
void latxs_tr_cvt_fp80_to_64(void);

typedef struct helper_cfg_t {
    union {
        struct {
            int sv_allgpr:1;
            int sv_gpr:1;
            int sv_fpr:1;
            int sv_simd:1;
            int sv_eflags:1;
            int cvt_fp80:1;
        } cfg;
        int data;
    };
} helper_cfg_t;

extern helper_cfg_t all_helper_cfg;
extern helper_cfg_t zero_helper_cfg;
extern helper_cfg_t default_helper_cfg;
extern helper_cfg_t nofp_helper_cfg;
extern helper_cfg_t fplib_helper_cfg;
int cmp_helper_cfg(helper_cfg_t cfg1, helper_cfg_t cfg2);

IR2_INST *latxs_tr_gen_call_to_helper(ADDR func_addr, int helper_kind);

void latxs_tr_gen_call_to_helper_prologue_cfg(helper_cfg_t cfg);
void latxs_tr_gen_call_to_helper_epilogue_cfg(helper_cfg_t cfg);
void latxs_tr_gen_call_to_helper0_cfg(ADDR func, helper_cfg_t cfg, int helper_kind);
void latxs_tr_gen_call_to_helper1_cfg(ADDR func, helper_cfg_t cfg, int helper_kind);
void latxs_tr_gen_call_to_helper2_cfg(ADDR func,
        int arg, helper_cfg_t cfg, int helper_kind);
void latxs_tr_gen_call_to_helper3_cfg(ADDR func,
        int arg2, int arg3, helper_cfg_t cfg, int helper_kind);
void latxs_tr_gen_call_to_helper3_u64_cfg(ADDR func,
        uint64_t arg2, uint64_t arg3, helper_cfg_t cfg, int helper_kind);
void latxs_tr_gen_call_to_helper4_u64_cfg(ADDR func,
        uint64_t arg2, uint64_t arg3, uint64_t arg4, helper_cfg_t cfg, int helper_kind);
void latxs_transfer_to_mmx_mode(void);
void latxs_transfer_to_fpu_mode(void);
void latxs_tr_gen_call_to_helper_pclmulqdq(ADDR func, int dest_xmm_num, int v_xmm_num, int src_xmm_num, int ctrl,
        int helper_kind);
void latxs_tr_gen_call_to_helper_pcmpxstrx(ADDR func, int dest_xmm_num, int src_xmm_num, int ctrl,
        int helper_kind);
void latxs_tr_gen_call_to_helper_aes(ADDR func, int dest_xmm_num,
        int src1_xmm_num, int src2_xmm_num, int helper_kind);
void latxs_tr_gen_call_to_helper_sha(ADDR func, int dest_xmm_num, int src1_xmm_num, int src2_xmm_num,
        int helper_kind);
void latxs_tr_gen_call_to_helper_sha256rnds2(ADDR func, int dest_xmm_num, int src1_xmm_num, int src2_xmm_num,
        int helper_kind);

void latxs_tr_gen_save_curr_eip(void);
void latxs_tr_gen_save_next_eip(void);
void latxs_tr_gen_load_from_curr_eip(IR2_OPND  *opnd0);
void latxs_tr_gen_store_to_curr_eip(IR2_OPND  *opnd0);
void latxs_tr_gen_add_st_curr_eip(IR2_OPND  *opnd0, int64_t offset, int64_t next_eip);
void latxs_tr_gen_add_curr_eip_to_ir2(IR2_OPND  *opnd0, int64_t offset, int64_t next_eip, bool use_sp);

void latxs_tr_save_temp_register(void);
void latxs_tr_restore_temp_register(void);
void latxs_tr_save_temp_register_mask(int mask);
void latxs_tr_restore_temp_register_mask(int mask);

/* eflags related functions */
void latxs_tr_save_eflags(void);
void latxs_tr_load_eflags(void);
void latxs_tr_gen_static_save_eflags(void);
void latxs_tr_gen_static_load_eflags(int simple);
void latx_cpu_load_eflags(IR2_OPND *eflags, IR2_OPND *update_mask);

/* FPU TOP related functions */
void latxs_tr_fpu_push(void);
void latxs_tr_fpu_pop(void);
void latxs_tr_fpu_inc(void);
void latxs_tr_fpu_dec(void);
void latxs_tr_gen_save_curr_top(void);
void latxs_tr_gen_save_curr_top_force(void);

void latxs_fpu_fix_before_exec_tb(CPUX86State *env, TranslationBlock *tb);
void latxs_fpu_fix_after_exec_tb(CPUX86State *env, TranslationBlock *tb);
void latxs_fpu_fix_cpu_loop_exit(void);

/* FPU FCSR */
void latxs_set_fpu_fcsr(IR2_OPND *new_fcsr);
IR2_OPND latxs_set_fpu_fcsr_rounding_field_by_x86(void);
void latxs_update_fcsr_by_cw(IR2_OPND cw);
void latxs_update_fcsr_rm(IR2_OPND control_word, IR2_OPND fcsr);

void latxs_save_fcsr_cs_helper_prologue(IR2_OPND *tmp);
void latxs_load_fcsr_cs_helper_epilogue(IR2_OPND *tmp);
void latxs_save_fcsr_scs_prologue(void);
void latxs_load_fcsr_scs_epilogue(void);
void latxs_load_dbt_fcsr(IR2_OPND *tmp);

/* opnd process */
void aot_load_host_addr_to_ir2(IR2_OPND *opnd2, uint64_t value, int addr_kind);
void aot_load_guest_addr_to_ir2(IR2_OPND *opnd2, uint64_t value, int addr_kind);
void latxs_load_imm64(IR2_OPND *, int64_t);
void latxs_load_imm32_to_ir2(IR2_OPND *, uint32_t, EXMode);
void latxs_load_imm64_to_ir2(IR2_OPND *, uint64_t);
void latxs_load_addrx_to_ir2(IR2_OPND *, ADDRX);
void latxs_load_eip_to_ir2(IR2_OPND *, ADDRX);
void latxs_load_addr_to_ir2(IR2_OPND *, ADDR, int);

void latxs_convert_mem_opnd_no_offset(IR2_OPND *opnd2, IR1_OPND *opnd1);

int can_do_hamt(CPUX86State *env);
void latxs_load_ir1_mem_addr_to_ir2(IR2_OPND *, IR1_OPND *, int as);
void latxs_convert_mem_opnd(IR2_OPND *, int *, IR1_OPND *, int as);
void latxs_convert_mem_opnd_opt(IR2_OPND *, int *, IR1_OPND *, int as);
void latxs_get_mem_opnd(IR2_OPND *, int *, IR1_OPND *, int as);
void latxs_convert_mem_opnd_with_bias(IR2_OPND *, int *, IR1_OPND *, int b, int as);
void latxs_convert_mem_opnd_with_bias_opt(IR2_OPND *, int *, IR1_OPND *, int b, int as);

void latxs_load_ir1_imm_to_ir2(IR2_OPND *, IR1_OPND *, EXMode);
void latxs_load_ir1_seg_to_ir2(IR2_OPND *, IR1_OPND *);
void latxs_load_ir1_mem_to_ir2(IR2_OPND *, IR1_OPND *, EXMode, int as);
void latxs_load_ir1_gpr_to_ir2(IR2_OPND *, IR1_OPND *, EXMode);
void latxs_load_ir1_mmx_to_ir2(IR2_OPND *, IR1_OPND *, EXMode);
void latxs_load_ir1_xmm_to_ir2(IR2_OPND *, IR1_OPND *, EXMode);
void latxs_load_ir1_cr_to_ir2(IR2_OPND *, IR1_OPND *);
void latxs_load_ir1_dr_to_ir2(IR2_OPND *, IR1_OPND *);
void latxs_load_ir1_to_ir2(IR2_OPND *, IR1_OPND *, EXMode);
IR2_OPND latxs_load_ireg_from_ir1(IR1_OPND *, EXMode);

void latxs_store_ir2_to_ir1_gpr(IR2_OPND *, IR1_OPND *);
void latxs_store_ir2_to_ir1_seg(IR2_OPND *, IR1_OPND *);
void latxs_store_ir2_to_ir1_mem(IR2_OPND *, IR1_OPND *, int as);
void latxs_store_ir2_to_ir1_cr(IR2_OPND *, IR1_OPND *);
void latxs_store_ir2_to_ir1_dr(IR2_OPND *, IR1_OPND *);
void latxs_store_ir2_to_ir1(IR2_OPND *, IR1_OPND *);

void latxs_load_eflags_cf_to_ir2(IR2_OPND *opnd2);

IR2_OPND latxs_load_freg_from_ir1_1(IR1_OPND *, bool cvt);
void latxs_load_freg_from_ir1_2(IR2_OPND *, IR1_OPND *, bool cvt);

void latxs_load_singles_from_ir1_pack(IR2_OPND *, IR2_OPND *, IR1_OPND *);
void latxs_store_singles_to_ir2_pack(IR2_OPND *, IR2_OPND *, IR2_OPND *);

void latxs_store_64_bit_freg_to_ir1_80_bit_mem(IR2_OPND *, IR2_OPND *, int);
void latxs_store_freg_to_ir1(IR2_OPND *, IR1_OPND *, bool cvt);

void latxs_load_freg128_from_ir1_mem(IR2_OPND *, IR1_OPND *);
void latxs_store_freg128_to_ir1_mem(IR2_OPND *, IR1_OPND *);
void latxs_store_freg128_to_ir1_mem_idx(IR2_OPND *, IR1_OPND *, int);
IR2_OPND latxs_load_freg128_from_ir1(IR1_OPND *);

void latxs_reverse_ir2_ireg_opnd_endian(IR2_OPND *, int);

/* Softmmu related */
typedef struct softmmu_slow_path_rcd {
    int tmp_need_save;
    int itmp_mask;
    int ftmp_mask;

    int is_load;
    IR2_OPCODE op;
    IR2_OPND gpr_ir2_opnd;
    IR2_OPND mem_ir2_opnd;
    int mmu_index;

    int fpu_top;
    ADDR retaddr;

    IR2_OPND label_slow_path;
    IR2_OPND label_exit;

} softmmu_sp_rcd_t;

uint64_t helper_softmmu_common(CPUX86State *env);
void gen_ldst_softmmu_helper(IR2_OPCODE op,
        IR2_OPND *opnd_gpr, IR2_OPND *mem_base, int mem_offset, int save_temp);
void gen_ldst_c1_softmmu_helper(IR2_OPCODE op,
        IR2_OPND *opnd_fpr, IR2_OPND *mem_base, int mem_offset, int save_temp);
void tr_gen_softmmu_slow_path(void);
void tr_lookup_code_hva(
        IR2_OPND *add_opnd,
        IR2_OPND *mem,
        int mmu_index,
        IR2_OPND label_slow_path);
void tr_lookup_code_fast_itlb(
        IR2_OPND *hva,
        IR2_OPND *gva,
        int mmu,
        IR2_OPND label);
void tr_refill_code_fast_itlb(IR2_OPND *gva, IR2_OPND *gp12,
        IR2_OPND *tmp0, IR2_OPND *tmp1, IR2_OPND *tmp2, int mmu, int ps);
void tr_gen_atomic_lookup_qemu_tlb(IR2_OPCODE op, IR2_OPND *gpr_opnd, IR2_OPND *mem,
        IR2_OPND *rd, int mmu_index, bool is_load, IR2_OPND *label_atomic_helper);
void tr_gen_atomic_rmw_softmmu(IR2_OPCODE op, IR2_OPND *rd, IR2_OPND *value,
        IR2_OPND *mem, int mmu_index, IR2_OPND *slow);

/* LSFPU */
void latxs_tr_load_lstop_from_env(IR2_OPND *top);
void latxs_tr_save_lstop_to_env(IR2_OPND *top);
void latxs_tr_gen_top_mode_init(void);
void latxs_tr_fpu_enable_top_mode(void);
void latxs_tr_fpu_disable_top_mode(void);

/* convert address in system mode */
ADDR latxs_tr_cvt_cpu_addr_read(CPUX86State *, target_ulong, int *);
ADDR latxs_tr_cvt_cpu_addr_write(CPUX86State *, target_ulong, int *);
ADDR latxs_tr_cvt_cpu_addr_code(CPUX86State *, target_ulong, int *);

/* End of TB related functions */
void latxs_tr_generate_exit_tb(IR1_INST *branch, int succ_id);
void latxs_false_tr_generate_exit_tb(IR1_INST *branch, int succ_id);
#ifdef CONFIG_LATX_TU
struct HrasIndirectCallLabels;
void latxs_tr_gen_indirect_call_cold(struct HrasIndirectCallLabels labels);
bool latxs_hras_direct_checks_fit(uint32_t count, void *const *direct_calls,
                                  const int *label_pos);
void latxs_tr_gen_tu_cold_exits(uint32_t tb_num_in_tu,
        TranslationBlock **tb_list,
        const struct HrasIndirectCallLabels *indirect_calls,
        void *const *direct_calls);
#endif
void latxs_tr_gen_exit_tb_load_tb_addr(IR2_OPND *tbptr, ADDR tb_addr);
void latxs_tr_gen_exit_tb_j_tb_link(TranslationBlock *tb, int succ_id);
void latxs_tr_gen_exit_tb_j_context_switch(IR2_OPND *tbptr,
        int can_link, int succ_id);

void latxs_tr_gen_eob(void);
void latxs_tr_gen_sys_eob(IR1_INST *pir1);
void latxs_tr_gen_popf_no_link(IR1_INST *pir1);
void latxs_tr_gen_eob_if_tb_too_large(IR1_INST *pir1);

/* exception check */
int latxs_tr_gen_excp_check(IR1_INST *pir1);
int latxs_tr_gen_fp_common_excp_check(IR1_INST *pir1);
int latxs_tr_gen_sse_common_excp_check(IR1_INST *pir1);

void latxs_tr_gen_excp_illegal_op(IR1_INST *ir1, int end);
void latxs_tr_gen_excp_gp(IR1_INST *ir1, int end);
void latxs_tr_gen_excp_prex(IR1_INST *ir1, int end);
void latxs_tr_gen_excp_divz(IR1_INST *ir1, int end);
void latxs_tr_gen_excp_simd(IR1_INST *ir1);

void latxs_tr_gen_infinite_loop(void);

int latxs_tb_encode_search(TranslationBlock *, uint8_t *);

void latxs_tr_gen_simd_fpe_check(IR1_INST *pir1);
void latxs_tr_gen_simd_fpe_check_t(IR1_INST *pir1, IR2_OPND *dest_t);

/* eflags calculation */
void latxs_generate_eflag_calculation(IR2_OPND*,
        IR2_OPND*, IR2_OPND*,
        IR1_INST *, bool has_address);

void latxs_generate_imul_eflag_calculation(IR2_OPND*,
        IR2_OPND*, IR2_OPND*,
        IR1_INST *, bool has_address);

extern char pf_table[256];
/* sys attribute */
int latxs_get_sys_stack_addr_size(void);

/* sys developing */
int latxs_is_ir1_ok(IR1_INST *pir1);
void latxs_register_ir1(IR1_OPCODE opc);

void latxs_sys_misc_register_ir1(void);
void latxs_sys_io_register_ir1(void);
void latxs_sys_adcx_register_ir1(void);
void latxs_sys_bmi_register_ir1(void);
void latxs_sys_arith_register_ir1(void);
void latxs_sys_branch_register_ir1(void);
void latxs_sys_logic_register_ir1(void);
void latxs_sys_mov_register_ir1(void);
void latxs_sys_eflags_register_ir1(void);
void latxs_sys_setcc_register_ir1(void);
void latxs_sys_string_register_ir1(void);
void latxs_sys_fp_register_ir1(void);
void latxs_sys_fctrl_register_ir1(void);
void latxs_sys_farith_register_ir1(void);
void latxs_sys_fldst_register_ir1(void);
void latxs_sys_fcmovcc_register_ir1(void);
void latxs_sys_simd_register_ir1(void);
void latxs_sys_simd_cmp_register_ir1(void);
void latxs_sys_ssse3_register_ir1(void);
void latxs_sys_sse41_register_ir1(void);
void latxs_sys_sse42_register_ir1(void);

/* sys-misc */
bool latxs_translate_jmp_far(IR1_INST *pir1);
bool latxs_translate_cpuid(IR1_INST *pir1);

bool latxs_translate_call(IR1_INST *pir1);
bool latxs_translate_callin(IR1_INST *pir1);
bool latxs_translate_lcall(IR1_INST *pir1);
bool latxs_translate_ret(IR1_INST *pir1);
bool latxs_translate_iret(IR1_INST *pir1);
bool latxs_translate_retf(IR1_INST *pir1);

bool latxs_translate_jmp(IR1_INST *pir1);
bool latxs_translate_jmpin(IR1_INST *pir1);

bool latxs_translate_cli(IR1_INST *pir1);
bool latxs_translate_sti(IR1_INST *pir1);

bool latxs_translate_lidt(IR1_INST *pir1);
bool latxs_translate_sidt(IR1_INST *pir1);
bool latxs_translate_lgdt(IR1_INST *pir1);
bool latxs_translate_sgdt(IR1_INST *pir1);
bool latxs_translate_lldt(IR1_INST *pir1);
bool latxs_translate_sldt(IR1_INST *pir1);
bool latxs_translate_ltr(IR1_INST *pir1);
bool latxs_translate_str(IR1_INST *pir1);

bool latxs_translate_pop(IR1_INST *pir1);
bool latxs_translate_push(IR1_INST *pir1);
bool latxs_translate_popal(IR1_INST *pir1);
bool latxs_translate_popaw(IR1_INST *pir1);
bool latxs_translate_pushal(IR1_INST *pir1);
bool latxs_translate_pushaw(IR1_INST *pir1);

bool latxs_translate_xchg(IR1_INST *pir1);
bool latxs_translate_cmpxchg(IR1_INST *pir1);
bool latxs_translate_cmpxchg8b(IR1_INST *pir1);
bool latxs_translate_cmpxchg16b(IR1_INST *pir1);

bool latxs_translate_rsm(IR1_INST *pir1);
bool latxs_translate_invd(IR1_INST *pir1);
bool latxs_translate_wbinvd(IR1_INST *pir1);
bool latxs_translate_nop(IR1_INST *pir1);
bool latxs_translate_pause(IR1_INST *pir1);

bool latxs_translate_int(IR1_INST *pir1);
bool latxs_translate_int1(IR1_INST *pir1);
bool latxs_translate_int_3(IR1_INST *pir1);
bool latxs_translate_into(IR1_INST *pir1);

bool latxs_translate_cwd(IR1_INST *pir1);
bool latxs_translate_cdq(IR1_INST *pir1);
bool latxs_translate_cqo(IR1_INST *pir1);

bool latxs_translate_cwde(IR1_INST *pir1);
bool latxs_translate_cdqe(IR1_INST *pir1);
bool latxs_translate_arpl(IR1_INST *pir1);
bool latxs_translate_cbw(IR1_INST *pir1);

bool latxs_translate_lds(IR1_INST *pir1);
bool latxs_translate_les(IR1_INST *pir1);
bool latxs_translate_lfs(IR1_INST *pir1);
bool latxs_translate_lgs(IR1_INST *pir1);
bool latxs_translate_lss(IR1_INST *pir1);
bool latxs_translate_clts(IR1_INST *pir1);
bool latxs_translate_enter(IR1_INST *pir1);
bool latxs_translate_leave(IR1_INST *pir1);
bool latxs_translate_rdtsc(IR1_INST *pir1);
bool latxs_translate_rdtscp(IR1_INST *pir1);
bool latxs_translate_rdpmc(IR1_INST *pir1);
bool latxs_translate_rdrand(IR1_INST *pir1);
bool latxs_translate_avx(IR1_INST *pir1);
void latxs_gen_pclmulqdq(IR2_OPND dest, IR2_OPND left, IR2_OPND right,
                         uint8_t ctrl);
void latxs_gen_psadbw(IR2_OPND *dst, IR2_OPND *a, IR2_OPND *b);
void latxs_gen_avx_aes(IR1_OPCODE opcode, IR2_OPND dest, IR2_OPND src,
                       IR2_OPND key, uint8_t imm);
bool latxs_translate_popcnt(IR1_INST *pir1);
bool latxs_translate_crc32(IR1_INST *pir1);
bool latxs_translate_hlt(IR1_INST *pir1);
bool latxs_translate_rdmsr(IR1_INST *pir1);
bool latxs_translate_wrmsr(IR1_INST *pir1);
bool latxs_translate_invpcid(IR1_INST *pir1);
bool latxs_translate_invlpg(IR1_INST *pir1);
bool latxs_translate_invlpga(IR1_INST *pir1);
bool latxs_translate_lfence(IR1_INST *pir1);
bool latxs_translate_mfence(IR1_INST *pir1);
bool latxs_translate_sfence(IR1_INST *pir1);

bool latxs_translate_sysenter(IR1_INST *pir1);
bool latxs_translate_sysexit(IR1_INST *pir1);
bool latxs_translate_syscall(IR1_INST *pir1);
bool latxs_translate_sysret(IR1_INST *pir1);

bool latxs_translate_lmsw(IR1_INST *pir1);
bool latxs_translate_smsw(IR1_INST *pir1);
bool latxs_translate_verr(IR1_INST *pir1);
bool latxs_translate_verw(IR1_INST *pir1);

bool latxs_translate_lsl(IR1_INST *pir1);
bool latxs_translate_lar(IR1_INST *pir1);

bool latxs_translate_prefetchnta(IR1_INST *pir1);
bool latxs_translate_prefetcht0(IR1_INST *pir1);
bool latxs_translate_prefetcht1(IR1_INST *pir1);
bool latxs_translate_prefetcht2(IR1_INST *pir1);
bool latxs_translate_prefetchw(IR1_INST *pir1);
bool latxs_translate_prefetch(IR1_INST *pir1);
bool latxs_translate_tzcnt(IR1_INST *pir1);

bool latxs_translate_invalid(IR1_INST *pir1);
bool latxs_translate_ud0(IR1_INST *pir1);
bool latxs_translate_ud2(IR1_INST *pir1);

bool latxs_translate_swapgs(IR1_INST *pir1);

bool latxs_translate_clflush(IR1_INST *pir1);

/* sys-io */
void latxs_tr_gen_io_check(IR1_INST *, IR1_OPND *, int);
void latxs_tr_gen_io_bpt(IR1_INST *, IR1_OPND *, int);
bool latxs_translate_in(IR1_INST *pir1);
bool latxs_translate_out(IR1_INST *pir1);

void latxs_tr_gen_io_start(void); /* icount */
void latxs_tr_gen_io_end(void); /* icount */

/* sys-arith */
bool latxs_translate_add(IR1_INST *pir1);
bool latxs_false_translate_add(IR1_INST *pir1);
bool latxs_translate_adc(IR1_INST *pir1);
bool latxs_false_translate_adc(IR1_INST *pir1);
bool latxs_false_translate_cmp(IR1_INST *pir1);
bool latxs_translate_sub(IR1_INST *pir1);
bool latxs_translate_sbb(IR1_INST *pir1);
bool latxs_translate_inc(IR1_INST *pir1);
bool latxs_translate_dec(IR1_INST *pir1);
bool latxs_translate_neg(IR1_INST *pir1);
bool latxs_translate_cmp(IR1_INST *pir1);
bool latxs_translate_mul(IR1_INST *pir1);
bool latxs_translate_imul(IR1_INST *pir1);
bool latxs_translate_div(IR1_INST *pir1);
bool latxs_translate_idiv(IR1_INST *pir1);

void latxs_tr_gen_div_result_check(IR1_INST *, IR2_OPND, int s, int);

bool latxs_translate_aaa(IR1_INST *pir1);
bool latxs_translate_aad(IR1_INST *pir1);
bool latxs_translate_aam(IR1_INST *pir1);
bool latxs_translate_aas(IR1_INST *pir1);
bool latxs_translate_daa(IR1_INST *pir1);
bool latxs_translate_das(IR1_INST *pir1);

bool latxs_translate_xadd(IR1_INST *pir1);

/* sys-tr-branch */
bool latxs_translate_jz(IR1_INST *pir1);
bool latxs_translate_jnz(IR1_INST *pir1);
bool latxs_translate_js(IR1_INST *pir1);
bool latxs_translate_jns(IR1_INST *pir1);
bool latxs_translate_jb(IR1_INST *pir1);
bool latxs_translate_jae(IR1_INST *pir1);
bool latxs_translate_jo(IR1_INST *pir1);
bool latxs_translate_jno(IR1_INST *pir1);
bool latxs_translate_jbe(IR1_INST *pir1);
bool latxs_translate_ja(IR1_INST *pir1);
bool latxs_translate_false_ja(IR1_INST *pir1);
bool latxs_translate_jp(IR1_INST *pir1);
bool latxs_translate_jnp(IR1_INST *pir1);
bool latxs_translate_jl(IR1_INST *pir1);
bool latxs_translate_jge(IR1_INST *pir1);
bool latxs_translate_jle(IR1_INST *pir1);
bool latxs_translate_jg(IR1_INST *pir1);

bool latxs_translate_jcxz(IR1_INST *pir1);
bool latxs_translate_jecxz(IR1_INST *pir1);
bool latxs_translate_jrcxz(IR1_INST *pir1);

bool latxs_translate_loop(IR1_INST *pir1);
bool latxs_translate_loopz(IR1_INST *pir1);
bool latxs_translate_loopnz(IR1_INST *pir1);

/* sys-tr-logic */
bool latxs_translate_xor(IR1_INST *pir1);
bool latxs_translate_and(IR1_INST *pir1);
bool latxs_translate_test(IR1_INST *pir1);
bool latxs_translate_or(IR1_INST *pir1);
bool latxs_translate_not(IR1_INST *pir1);

bool latxs_translate_sarx(IR1_INST *pir1);
bool latxs_translate_shlx(IR1_INST *pir1);
bool latxs_translate_shrx(IR1_INST *pir1);

bool latxs_translate_shl(IR1_INST *pir1);
bool latxs_translate_shr(IR1_INST *pir1);
bool latxs_translate_sal(IR1_INST *pir1);
bool latxs_translate_sar(IR1_INST *pir1);

bool latxs_translate_rol(IR1_INST *pir1);
bool latxs_translate_ror(IR1_INST *pir1);
bool latxs_translate_rcl(IR1_INST *pir1);
bool latxs_translate_rcr(IR1_INST *pir1);

bool latxs_translate_shrd(IR1_INST *pir1);
bool latxs_translate_shld(IR1_INST *pir1);
bool latxs_translate_bswap(IR1_INST *pir1);

/* sys-tr-mov */
bool latxs_translate_mov(IR1_INST *pir1);
bool latxs_translate_false_mov(IR1_INST *pir1);
bool latxs_translate_movzx(IR1_INST *pir1);
bool latxs_translate_movsx(IR1_INST *pir1);
bool latxs_translate_movsxd(IR1_INST *pir1);

bool latxs_translate_lea(IR1_INST *pir1);

bool latxs_translate_movd(IR1_INST *pir1);
bool latxs_translate_movq(IR1_INST *pir1);

bool latxs_translate_cmovo(IR1_INST *pir1);
bool latxs_translate_cmovno(IR1_INST *pir1);
bool latxs_translate_cmovb(IR1_INST *pir1);
bool latxs_translate_cmovae(IR1_INST *pir1);
bool latxs_translate_cmovz(IR1_INST *pir1);
bool latxs_translate_cmovnz(IR1_INST *pir1);
bool latxs_translate_cmovbe(IR1_INST *pir1);
bool latxs_translate_cmova(IR1_INST *pir1);
bool latxs_translate_cmovs(IR1_INST *pir1);
bool latxs_translate_cmovns(IR1_INST *pir1);
bool latxs_translate_cmovp(IR1_INST *pir1);
bool latxs_translate_cmovnp(IR1_INST *pir1);
bool latxs_translate_cmovl(IR1_INST *pir1);
bool latxs_translate_cmovge(IR1_INST *pir1);
bool latxs_translate_cmovle(IR1_INST *pir1);
bool latxs_translate_cmovg(IR1_INST *pir1);

bool latxs_translate_xlat(IR1_INST *pir1);

/* sys-eflags */
bool latxs_translate_cld(IR1_INST *pir1);
bool latxs_translate_std(IR1_INST *pir1);
bool latxs_translate_clc(IR1_INST *pir1);
bool latxs_translate_stc(IR1_INST *pir1);
bool latxs_translate_cmc(IR1_INST *pir1);
bool latxs_translate_clac(IR1_INST *pir1);
bool latxs_translate_stac(IR1_INST *pir1);
bool latxs_translate_popf(IR1_INST *pir1);
bool latxs_translate_pushf(IR1_INST *pir1);

bool latxs_translate_sahf(IR1_INST *pir1);
bool latxs_translate_lahf(IR1_INST *pir1);

/* sys-setcc */
bool latxs_translate_setcc(IR1_INST *pir1);

bool latxs_translate_bsf(IR1_INST *pir1);
bool latxs_translate_bsr(IR1_INST *pir1);
bool latxs_translate_btx(IR1_INST *pir1);

/* sys-string */
bool latxs_translate_ins(IR1_INST *pir1);
bool latxs_translate_outs(IR1_INST *pir1);
bool latxs_translate_movs(IR1_INST *pir1);
bool latxs_translate_scas(IR1_INST *pir1);
bool latxs_translate_cmps(IR1_INST *pir1);
bool latxs_translate_lods(IR1_INST *pir1);
bool latxs_translate_stos(IR1_INST *pir1);

/* sys-fp */
bool latxs_translate_wait(IR1_INST *pir1);
bool latxs_translate_fsetpm(IR1_INST *pir1);
bool latxs_translate_fxsave(IR1_INST *pir1);
bool latxs_translate_fxrstor(IR1_INST *pir1);
bool latxs_translate_fnstenv(IR1_INST *pir1);
bool latxs_translate_fldenv(IR1_INST *pir1);
bool latxs_translate_fnsave(IR1_INST *pir1);
bool latxs_translate_frstor(IR1_INST *pir1);
bool latxs_translate_xsave(IR1_INST *pir1);
bool latxs_translate_xrstor(IR1_INST *pir1);
bool latxs_translate_xsaveopt(IR1_INST *pir1);

bool latxs_translate_xgetbv(IR1_INST *pir1);
bool latxs_translate_xsetbv(IR1_INST *pir1);

bool latxs_translate_fninit(IR1_INST *pir1);
bool latxs_translate_ffree(IR1_INST *pir1);
bool latxs_translate_ffreep(IR1_INST *pir1);
bool latxs_translate_fincstp(IR1_INST *pir1);
bool latxs_translate_fdecstp(IR1_INST *pir1);

/* sys-fctrl */

bool latxs_translate_fnstcw(IR1_INST *pir1);
bool latxs_translate_fldcw(IR1_INST *pir1);
bool latxs_translate_fnstsw(IR1_INST *pir1);
bool latxs_translate_stmxcsr(IR1_INST *pir1);
bool latxs_translate_ldmxcsr(IR1_INST *pir1);

bool latxs_translate_fcomi(IR1_INST *pir1);
bool latxs_translate_fcomip(IR1_INST *pir1);
bool latxs_translate_fucomi(IR1_INST *pir1);
bool latxs_translate_fucomip(IR1_INST *pir1);

bool latxs_translate_fcom(IR1_INST *pir1);
bool latxs_translate_fcomp(IR1_INST *pir1);
bool latxs_translate_fcompp(IR1_INST *pir1);
bool latxs_translate_fucom(IR1_INST *pir1);
bool latxs_translate_fucomp(IR1_INST *pir1);
bool latxs_translate_fucompp(IR1_INST *pir1);

bool latxs_translate_ficom(IR1_INST *pir1);
bool latxs_translate_ficomp(IR1_INST *pir1);

bool latxs_translate_fnclex(IR1_INST *pir1);
bool latxs_translate_emms(IR1_INST *pir1);

/* sys-farith */
bool latxs_translate_fadd(IR1_INST *pir1);
bool latxs_translate_faddp(IR1_INST *pir1);
bool latxs_translate_fiadd(IR1_INST *pir1);
bool latxs_translate_fsub(IR1_INST *pir1);
bool latxs_translate_fisub(IR1_INST *pir1);
bool latxs_translate_fisubr(IR1_INST *pir1);
bool latxs_translate_fsubr(IR1_INST *pir1);
bool latxs_translate_fsubrp(IR1_INST *pir1);
bool latxs_translate_fsubp(IR1_INST *pir1);
bool latxs_translate_fmul(IR1_INST *pir1);
bool latxs_translate_fimul(IR1_INST *pir1);
bool latxs_translate_fmulp(IR1_INST *pir1);
bool latxs_translate_fdiv(IR1_INST *pir1);
bool latxs_translate_fdivr(IR1_INST *pir1);
bool latxs_translate_fidiv(IR1_INST *pir1);
bool latxs_translate_fidivr(IR1_INST *pir1);
bool latxs_translate_fdivrp(IR1_INST *pir1);
bool latxs_translate_fdivp(IR1_INST *pir1);
bool latxs_translate_fnop(IR1_INST *pir1);
bool latxs_translate_fsqrt(IR1_INST *pir1);
bool latxs_translate_fabs(IR1_INST *pir1);
bool latxs_translate_fchs(IR1_INST *pir1);
bool latxs_translate_fsin(IR1_INST *pir1);
bool latxs_translate_fcos(IR1_INST *pir1);
bool latxs_translate_fpatan(IR1_INST *pir1);
bool latxs_translate_fprem(IR1_INST *pir1);
bool latxs_translate_fprem1(IR1_INST *pir1);
bool latxs_translate_frndint(IR1_INST *pir1);
bool latxs_translate_fscale(IR1_INST *pir1);
bool latxs_translate_fxam(IR1_INST *pir1);
bool latxs_translate_f2xm1(IR1_INST *pir1);
bool latxs_translate_fxtract(IR1_INST *pir1);
bool latxs_translate_fyl2x(IR1_INST *pir1);
bool latxs_translate_fyl2xp1(IR1_INST *pir1);
bool latxs_translate_fsincos(IR1_INST *pir1);
bool latxs_translate_fxch(IR1_INST *pir1);
bool latxs_translate_ftst(IR1_INST *pir1);
bool latxs_translate_fptan(IR1_INST *pir1);
bool latxs_translate_fisttp(IR1_INST *pir1);

/* sys-fldst */
bool latxs_translate_fld(IR1_INST *pir1);
bool latxs_translate_fldz(IR1_INST *pir1);
bool latxs_translate_fld1(IR1_INST *pir1);
bool latxs_translate_fldl2e(IR1_INST *pir1);
bool latxs_translate_fldl2t(IR1_INST *pir1);
bool latxs_translate_fldlg2(IR1_INST *pir1);
bool latxs_translate_fldln2(IR1_INST *pir1);
bool latxs_translate_fldpi(IR1_INST *pir1);
bool latxs_translate_fstp(IR1_INST *pir1);
bool latxs_translate_fst(IR1_INST *pir1);
bool latxs_translate_fild(IR1_INST *pir1);
bool latxs_translate_fist(IR1_INST *pir1);
bool latxs_translate_fistp(IR1_INST *pir1);
bool latxs_translate_fbld(IR1_INST *pir1);
bool latxs_translate_fbstp(IR1_INST *pir1);

/* sys-fcmovcc */
bool latxs_translate_fcmovcc(IR1_INST *pir1);

/* sys-simd */
bool latxs_translate_por(IR1_INST *pir1);
bool latxs_translate_pxor(IR1_INST *pir1);
bool latxs_translate_packuswb(IR1_INST *pir1);
bool latxs_translate_packssdw(IR1_INST *pir1);
bool latxs_translate_packsswb(IR1_INST *pir1);
bool latxs_translate_paddb(IR1_INST *pir1);
bool latxs_translate_paddw(IR1_INST *pir1);
bool latxs_translate_paddd(IR1_INST *pir1);
bool latxs_translate_paddusb(IR1_INST *pir1);
bool latxs_translate_paddusw(IR1_INST *pir1);
bool latxs_translate_paddsw(IR1_INST *pir1);
bool latxs_translate_paddsb(IR1_INST *pir1);
bool latxs_translate_psubusb(IR1_INST *pir1);
bool latxs_translate_psubusw(IR1_INST *pir1);
bool latxs_translate_psubsw(IR1_INST *pir1);
bool latxs_translate_psubsb(IR1_INST *pir1);
bool latxs_translate_pand(IR1_INST *pir1);
bool latxs_translate_pandn(IR1_INST *pir1);
bool latxs_translate_pcmpeqb(IR1_INST *pir1);
bool latxs_translate_pcmpeqw(IR1_INST *pir1);
bool latxs_translate_pcmpeqd(IR1_INST *pir1);
bool latxs_translate_pcmpgtb(IR1_INST *pir1);
bool latxs_translate_pcmpgtw(IR1_INST *pir1);
bool latxs_translate_pcmpgtd(IR1_INST *pir1);
bool latxs_translate_pmullw(IR1_INST *pir1);
bool latxs_translate_psllw(IR1_INST *pir1);
bool latxs_translate_pslld(IR1_INST *pir1);
bool latxs_translate_psllq(IR1_INST *pir1);
bool latxs_translate_psrlw(IR1_INST *pir1);
bool latxs_translate_psrld(IR1_INST *pir1);
bool latxs_translate_psrlq(IR1_INST *pir1);
bool latxs_translate_psraw(IR1_INST *pir1);
bool latxs_translate_psrad(IR1_INST *pir1);
bool latxs_translate_psubb(IR1_INST *pir1);
bool latxs_translate_psubw(IR1_INST *pir1);
bool latxs_translate_psubd(IR1_INST *pir1);
bool latxs_translate_punpckhbw(IR1_INST *pir1);
bool latxs_translate_punpckhwd(IR1_INST *pir1);
bool latxs_translate_punpckhdq(IR1_INST *pir1);
bool latxs_translate_punpcklbw(IR1_INST *pir1);
bool latxs_translate_punpcklwd(IR1_INST *pir1);
bool latxs_translate_punpckldq(IR1_INST *pir1);
bool latxs_translate_punpckhqdq(IR1_INST *pir1);
bool latxs_translate_punpcklqdq(IR1_INST *pir1);
bool latxs_translate_addsd(IR1_INST *pir1);
bool latxs_translate_addss(IR1_INST *pir1);
bool latxs_translate_andps(IR1_INST *pir1);
bool latxs_translate_cmpeqpd(IR1_INST *pir1);
bool latxs_translate_cmpltpd(IR1_INST *pir1);
bool latxs_translate_cmplepd(IR1_INST *pir1);
bool latxs_translate_cmpunordpd(IR1_INST *pir1);
bool latxs_translate_cmpneqpd(IR1_INST *pir1);
bool latxs_translate_cmpnltpd(IR1_INST *pir1);
bool latxs_translate_cmpnlepd(IR1_INST *pir1);
bool latxs_translate_cmpordpd(IR1_INST *pir1);
bool latxs_translate_cmpeqps(IR1_INST *pir1);
bool latxs_translate_cmpltps(IR1_INST *pir1);
bool latxs_translate_cmpleps(IR1_INST *pir1);
bool latxs_translate_cmpunordps(IR1_INST *pir1);
bool latxs_translate_cmpneqps(IR1_INST *pir1);
bool latxs_translate_cmpnltps(IR1_INST *pir1);
bool latxs_translate_cmpnleps(IR1_INST *pir1);
bool latxs_translate_cmpordps(IR1_INST *pir1);
bool latxs_translate_cmpeqsd(IR1_INST *pir1);
bool latxs_translate_cmpltsd(IR1_INST *pir1);
bool latxs_translate_cmplesd(IR1_INST *pir1);
bool latxs_translate_cmpunordsd(IR1_INST *pir1);
bool latxs_translate_cmpneqsd(IR1_INST *pir1);
bool latxs_translate_cmpnltsd(IR1_INST *pir1);
bool latxs_translate_cmpnlesd(IR1_INST *pir1);
bool latxs_translate_cmpordsd(IR1_INST *pir1);
bool latxs_translate_cmpeqss(IR1_INST *pir1);
bool latxs_translate_cmpltss(IR1_INST *pir1);
bool latxs_translate_cmpless(IR1_INST *pir1);
bool latxs_translate_cmpunordss(IR1_INST *pir1);
bool latxs_translate_cmpneqss(IR1_INST *pir1);
bool latxs_translate_cmpnltss(IR1_INST *pir1);
bool latxs_translate_cmpnless(IR1_INST *pir1);
bool latxs_translate_cmpordss(IR1_INST *pir1);
bool latxs_translate_comisd(IR1_INST *pir1);
bool latxs_translate_comiss(IR1_INST *pir1);
bool latxs_translate_movmskps(IR1_INST *pir1);
bool latxs_translate_movmskpd(IR1_INST *pir1);
bool latxs_translate_cvtdq2pd(IR1_INST *pir1);
bool latxs_translate_cvtdq2ps(IR1_INST *pir1);
bool latxs_translate_cvtpd2ps(IR1_INST *pir1);
bool latxs_translate_cvtsd2ss(IR1_INST *pir1);
bool latxs_translate_cvtsi2sd(IR1_INST *pir1);
bool latxs_translate_cvtss2sd(IR1_INST *pir1);
bool latxs_translate_cvttss2si(IR1_INST *pir1);

bool latxs_translate_cvtpi2ps(IR1_INST *pir1);
bool latxs_translate_cvtps2pi(IR1_INST *pir1);
bool latxs_translate_cvtsi2ss(IR1_INST *pir1);
bool latxs_translate_cvtss2si(IR1_INST *pir1);
bool latxs_translate_cvttps2pi(IR1_INST *pir1);
bool latxs_translate_cvtpi2pd(IR1_INST *pir1);
bool latxs_translate_cvtpd2pi(IR1_INST *pir1);
bool latxs_translate_cvtsd2si(IR1_INST *pir1);
bool latxs_translate_cvttpd2pi(IR1_INST *pir1);

bool latxs_translate_cvtpd2dq(IR1_INST *pir1);
bool latxs_translate_cvtps2dq(IR1_INST *pir1);
bool latxs_translate_cvttps2dq(IR1_INST *pir1);
bool latxs_translate_cvttpd2dq(IR1_INST *pir1);

bool latxs_translate_divpd(IR1_INST *pir1);
bool latxs_translate_divps(IR1_INST *pir1);
bool latxs_translate_divsd(IR1_INST *pir1);
bool latxs_translate_divss(IR1_INST *pir1);
bool latxs_translate_maxpd(IR1_INST *pir1);
bool latxs_translate_maxps(IR1_INST *pir1);
bool latxs_translate_maxsd(IR1_INST *pir1);
bool latxs_translate_maxss(IR1_INST *pir1);
bool latxs_translate_minpd(IR1_INST *pir1);
bool latxs_translate_minps(IR1_INST *pir1);
bool latxs_translate_minsd(IR1_INST *pir1);
bool latxs_translate_minss(IR1_INST *pir1);
bool latxs_translate_movnti(IR1_INST *pir1);
bool latxs_translate_movntdq(IR1_INST *pir1);
bool latxs_translate_movntpd(IR1_INST *pir1);
bool latxs_translate_movntps(IR1_INST *pir1);
bool latxs_translate_movntq(IR1_INST *pir1);
bool latxs_translate_mulpd(IR1_INST *pir1);
bool latxs_translate_mulps(IR1_INST *pir1);
bool latxs_translate_mulsd(IR1_INST *pir1);
bool latxs_translate_mulss(IR1_INST *pir1);
bool latxs_translate_orpd(IR1_INST *pir1);
bool latxs_translate_orps(IR1_INST *pir1);
bool latxs_translate_paddq(IR1_INST *pir1);
bool latxs_translate_pavgb(IR1_INST *pir1);
bool latxs_translate_pavgw(IR1_INST *pir1);
bool latxs_translate_pextrw(IR1_INST *pir1);
bool latxs_translate_pinsrw(IR1_INST *pir1);
bool latxs_translate_pmaxsw(IR1_INST *pir1);
bool latxs_translate_pmaxub(IR1_INST *pir1);
bool latxs_translate_pminsw(IR1_INST *pir1);
bool latxs_translate_pminub(IR1_INST *pir1);
bool latxs_translate_pmovmskb(IR1_INST *pir1);
bool latxs_translate_maskmovdqu(IR1_INST *pir1);
bool latxs_translate_maskmovq(IR1_INST *pir1);
bool latxs_translate_psadbw(IR1_INST *pir1);
bool latxs_translate_pmulhw(IR1_INST *pir1);
bool latxs_translate_pmulhuw(IR1_INST *pir1);
bool latxs_translate_pmuludq(IR1_INST *pir1);
bool latxs_translate_pmaddwd(IR1_INST *pir1);
bool latxs_translate_pshufd(IR1_INST *pir1);
bool latxs_translate_pshufw(IR1_INST *pir1);
bool latxs_translate_pshuflw(IR1_INST *pir1);
bool latxs_translate_pshufhw(IR1_INST *pir1);
bool latxs_translate_pslldq(IR1_INST *pir1);
bool latxs_translate_psrldq(IR1_INST *pir1);
bool latxs_translate_psubq(IR1_INST *pir1);
bool latxs_translate_ucomisd(IR1_INST *pir1);
bool latxs_translate_ucomiss(IR1_INST *pir1);
bool latxs_translate_lddqu(IR1_INST *pir1);
bool latxs_translate_addsubpd(IR1_INST *pir1);
bool latxs_translate_addsubps(IR1_INST *pir1);
/* sse */
bool latxs_translate_movupd(IR1_INST *pir1);
bool latxs_translate_movdqa(IR1_INST *pir1);
bool latxs_translate_movdqu(IR1_INST *pir1);
bool latxs_translate_movups(IR1_INST *pir1);
bool latxs_translate_movapd(IR1_INST *pir1);
bool latxs_translate_movaps(IR1_INST *pir1);
bool latxs_translate_movbe(IR1_INST *pir1);
bool latxs_translate_movlhps(IR1_INST *pir1);
bool latxs_translate_movhlps(IR1_INST *pir1);
bool latxs_translate_movsd(IR1_INST *pir1);
bool latxs_translate_movss(IR1_INST *pir1);
bool latxs_translate_movhpd(IR1_INST *pir1);
bool latxs_translate_movhps(IR1_INST *pir1);
bool latxs_translate_movlpd(IR1_INST *pir1);
bool latxs_translate_movlps(IR1_INST *pir1);
bool latxs_translate_movq2dq(IR1_INST *pir1);
bool latxs_translate_movdq2q(IR1_INST *pir1);
bool latxs_translate_addps(IR1_INST *pir1);
bool latxs_translate_addpd(IR1_INST *pir1);
bool latxs_translate_andpd(IR1_INST *pir1);
bool latxs_translate_andnpd(IR1_INST *pir1);
bool latxs_translate_andnps(IR1_INST *pir1);
bool latxs_translate_unpcklpd(IR1_INST *pir1);
bool latxs_translate_unpckhpd(IR1_INST *pir1);
bool latxs_translate_unpcklps(IR1_INST *pir1);
bool latxs_translate_unpckhps(IR1_INST *pir1);
bool latxs_translate_shufps(IR1_INST *pir1);
bool latxs_translate_shufpd(IR1_INST *pir1);
bool latxs_translate_xorps(IR1_INST *pir1);
bool latxs_translate_xorpd(IR1_INST *pir1);
bool latxs_translate_subss(IR1_INST *pir1);
bool latxs_translate_subsd(IR1_INST *pir1);
bool latxs_translate_subps(IR1_INST *pir1);
bool latxs_translate_subpd(IR1_INST *pir1);
bool latxs_translate_rcpps(IR1_INST *pir1);
bool latxs_translate_rcpss(IR1_INST *pir1);
bool latxs_translate_sqrtpd(IR1_INST *pir1);
bool latxs_translate_sqrtps(IR1_INST *pir1);
bool latxs_translate_sqrtsd(IR1_INST *pir1);
bool latxs_translate_sqrtss(IR1_INST *pir1);
bool latxs_translate_rsqrtps(IR1_INST *pir1);
bool latxs_translate_rsqrtss(IR1_INST *pir1);
bool latxs_translate_cvtps2pd(IR1_INST *pir1);
bool latxs_translate_cvttsd2si(IR1_INST *pir1);

/* softfpu x87 */
bool latxs_translate_fld_softfpu(IR1_INST *pir1);
bool latxs_translate_fild_softfpu(IR1_INST *pir1);
bool latxs_translate_fstp_softfpu(IR1_INST *pir1);
bool latxs_translate_fst_softfpu(IR1_INST *pir1);
bool latxs_translate_fist_softfpu(IR1_INST *pir1);
bool latxs_translate_fistp_softfpu(IR1_INST *pir1);
bool latxs_translate_fisttp_softfpu(IR1_INST *pir1);
bool latxs_translate_fbld_softfpu(IR1_INST *pir1);
bool latxs_translate_fbstp_softfpu(IR1_INST *pir1);

bool latxs_translate_fcmovb_softfpu(IR1_INST *pir1);
bool latxs_translate_fcmove_softfpu(IR1_INST *pir1);
bool latxs_translate_fcmovbe_softfpu(IR1_INST *pir1);
bool latxs_translate_fcmovu_softfpu(IR1_INST *pir1);
bool latxs_translate_fcmovnb_softfpu(IR1_INST *pir1);
bool latxs_translate_fcmovne_softfpu(IR1_INST *pir1);
bool latxs_translate_fcmovnbe_softfpu(IR1_INST *pir1);
bool latxs_translate_fcmovnu_softfpu(IR1_INST *pir1);

bool latxs_translate_fadd_softfpu(IR1_INST *pir1);
bool latxs_translate_faddp_softfpu(IR1_INST *pir1);
bool latxs_translate_fiadd_softfpu(IR1_INST *pir1);
bool latxs_translate_fmul_softfpu(IR1_INST *pir1);
bool latxs_translate_fmulp_softfpu(IR1_INST *pir1);
bool latxs_translate_fimul_softfpu(IR1_INST *pir1);

bool latxs_translate_fsub_softfpu(IR1_INST *pir1);
bool latxs_translate_fsubp_softfpu(IR1_INST *pir1);
bool latxs_translate_fisub_softfpu(IR1_INST *pir1);
bool latxs_translate_fsubr_softfpu(IR1_INST *pir1);
bool latxs_translate_fsubrp_softfpu(IR1_INST *pir1);
bool latxs_translate_fisubr_softfpu(IR1_INST *pir1);

bool latxs_translate_fsin_softfpu(IR1_INST *pir1);
bool latxs_translate_fcos_softfpu(IR1_INST *pir1);
bool latxs_translate_fpatan_softfpu(IR1_INST *pir1);
bool latxs_translate_fsincos_softfpu(IR1_INST *pir1);
bool latxs_translate_fptan_softfpu(IR1_INST *pir1);

bool latxs_translate_fscale_softfpu(IR1_INST *pir1);
bool latxs_translate_f2xm1_softfpu(IR1_INST *pir1);
bool latxs_translate_fyl2x_softfpu(IR1_INST *pir1);
bool latxs_translate_fyl2xp1_softfpu(IR1_INST *pir1);

bool latxs_translate_fdiv_softfpu(IR1_INST *pir1);
bool latxs_translate_fdivp_softfpu(IR1_INST *pir1);
bool latxs_translate_fidiv_softfpu(IR1_INST *pir1);
bool latxs_translate_fdivr_softfpu(IR1_INST *pir1);
bool latxs_translate_fdivrp_softfpu(IR1_INST *pir1);
bool latxs_translate_fidivr_softfpu(IR1_INST *pir1);

bool latxs_translate_fldz_softfpu(IR1_INST *pir1);
bool latxs_translate_fld1_softfpu(IR1_INST *pir1);
bool latxs_translate_fldl2e_softfpu(IR1_INST *pir1);
bool latxs_translate_fldl2t_softfpu(IR1_INST *pir1);
bool latxs_translate_fldlg2_softfpu(IR1_INST *pir1);
bool latxs_translate_fldln2_softfpu(IR1_INST *pir1);
bool latxs_translate_fldpi_softfpu(IR1_INST *pir1);
bool latxs_translate_fsqrt_softfpu(IR1_INST *pir1);
bool latxs_translate_fabs_softfpu(IR1_INST *pir1);
bool latxs_translate_fchs_softfpu(IR1_INST *pir1);
bool latxs_translate_fprem_softfpu(IR1_INST *pir1);
bool latxs_translate_fprem1_softfpu(IR1_INST *pir1);
bool latxs_translate_frndint_softfpu(IR1_INST *pir1);
bool latxs_translate_fxtract_softfpu(IR1_INST *pir1);
bool latxs_translate_fxch_softfpu(IR1_INST *pir1);

bool latxs_translate_fcomi_softfpu(IR1_INST *pir1);
bool latxs_translate_fcomip_softfpu(IR1_INST *pir1);
bool latxs_translate_fucomi_softfpu(IR1_INST *pir1);
bool latxs_translate_fucomip_softfpu(IR1_INST *pir1);

bool latxs_translate_fcom_softfpu(IR1_INST *pir1);
bool latxs_translate_fcomp_softfpu(IR1_INST *pir1);
bool latxs_translate_fcompp_softfpu(IR1_INST *pir1);
bool latxs_translate_fucom_softfpu(IR1_INST *pir1);
bool latxs_translate_fucomp_softfpu(IR1_INST *pir1);
bool latxs_translate_fucompp_softfpu(IR1_INST *pir1);

bool latxs_translate_ficom_softfpu(IR1_INST *pir1);
bool latxs_translate_ficomp_softfpu(IR1_INST *pir1);

bool latxs_translate_fxam_softfpu(IR1_INST *pir1);
bool latxs_translate_ftst_softfpu(IR1_INST *pir1);

bool latxs_translate_fincstp_softfpu(IR1_INST *pir1);
bool latxs_translate_fdecstp_softfpu(IR1_INST *pir1);

bool latxs_translate_wait_softfpu(IR1_INST *pir1);
bool latxs_translate_fninit_softfpu(IR1_INST *pir1);

bool latxs_translate_fnstcw_softfpu(IR1_INST *pir1);
bool latxs_translate_fldcw_softfpu(IR1_INST *pir1);
bool latxs_translate_fnstsw_softfpu(IR1_INST *pir1);

bool latxs_translate_fnstenv_softfpu(IR1_INST *pir1);
bool latxs_translate_fldenv_softfpu(IR1_INST *pir1);
bool latxs_translate_fnsave_softfpu(IR1_INST *pir1);
bool latxs_translate_frstor_softfpu(IR1_INST *pir1);

bool latxs_translate_fnclex_softfpu(IR1_INST *pir1);

bool latxs_translate_ffree_softfpu(IR1_INST *pir1);
bool latxs_translate_ffreep_softfpu(IR1_INST *pir1);
bool latxs_translate_fnop_softfpu(IR1_INST *pir1);

bool latxs_translate_fxsave_softfpu(IR1_INST *pir1);
bool latxs_translate_fxrstor_softfpu(IR1_INST *pir1);
bool latxs_translate_lock_add(IR1_INST *pir1);
bool latxs_translate_lock_sub(IR1_INST *pir1);
bool latxs_translate_lock_adc(IR1_INST *pir1);
bool latxs_translate_lock_sbb(IR1_INST *pir1);
bool latxs_translate_lock_inc(IR1_INST *pir1);
bool latxs_translate_lock_dec(IR1_INST *pir1);
bool latxs_translate_lock_xadd(IR1_INST *pir1);
bool latxs_translate_lock_and(IR1_INST *pir1);
bool latxs_translate_lock_or(IR1_INST *pir1);
bool latxs_translate_lock_xor(IR1_INST *pir1);
bool latxs_translate_lock_not(IR1_INST *pir1);
bool latxs_translate_lock_neg(IR1_INST *pir1);
bool latxs_translate_lock_btx(IR1_INST *pir1);
bool latxs_translate_lock_cmpxchg(IR1_INST *pir1);
bool latxs_translate_lock_cmpxchg8b(IR1_INST *pir1);
bool latxs_translate_lock_cmpxchg16b(IR1_INST *pir1);
bool latxs_translate_lock_xchg(IR1_INST *pir1);

bool latxs_translate_mov_byhand64(IR1_INST *pir1);
bool latxs_translate_movzx_byhand64(IR1_INST *pir1);
bool latxs_translate_movsx_byhand64(IR1_INST *pir1);
bool latxs_translate_movsxd_byhand64(IR1_INST *pir1);
bool latxs_translate_xor_byhand64(IR1_INST *pir1);
bool latxs_translate_cmp_byhand64(IR1_INST *pir1);
bool latxs_translate_test_byhand64(IR1_INST *pir1);
bool latxs_translate_add_byhand64(IR1_INST *pir1);
bool latxs_translate_sub_byhand64(IR1_INST *pir1);
bool latxs_translate_and_byhand64(IR1_INST *pir1);
bool latxs_translate_or_byhand64(IR1_INST *pir1);
bool latxs_translate_adc_byhand64(IR1_INST *pir1);
bool latxs_translate_sbb_byhand64(IR1_INST *pir1);
bool latxs_translate_not_byhand64(IR1_INST *pir1);
bool latxs_translate_neg_byhand64(IR1_INST *pir1);
bool latxs_translate_shl_byhand64(IR1_INST *pir1);
bool latxs_translate_shr_byhand64(IR1_INST *pir1);
bool latxs_translate_sar_byhand64(IR1_INST *pir1);

/* optimization */
int scs_enabled(void);
void latxs_tr_gen_call_to_helper_prologue_inline(helper_cfg_t cfg);
void latxs_tr_gen_call_to_helper_epilogue_inline(helper_cfg_t cfg);
int gen_latxs_scs_prologue_cfg(void *, helper_cfg_t);
int gen_latxs_scs_prologue_cfg_nofp(void *, helper_cfg_t);
int gen_latxs_scs_epilogue_cfg(void *, helper_cfg_t);
int gen_latxs_scs_epilogue_cfg_nofp(void *, helper_cfg_t);
void latxs_tr_gen_static_save_registers_to_env(
        uint32_t, uint8_t, uint32_t, uint8_t);
void latxs_tr_gen_static_load_registers_from_env(
        uint32_t, uint8_t, uint32_t, uint8_t);

int intb_njc_enabled(void);

void latxs_enter_mmx(IR1_INST *pir1);
#ifdef CONFIG_LATX_OPT_NOFP
#define LATXS_SCS_MMX_FRAME_SIZE 16
#else
#define LATXS_SCS_MMX_FRAME_SIZE 0
#endif
#ifdef CONFIG_LATX_OPT_NOFP
void latxs_gen_nofp_sync_fprs_to_env(void);
void latxs_gen_nofp_sync_fprs_to_env_static(void);
void latxs_gen_nofp_restore_fprs_static(void);
void latxs_gen_nofp_sync_fprs_from_env(void);
#endif
void do_func(ADDRX addr, int func);

void latxs_update_stack_pointer(int64_t val, int size);

bool si12_overflow(long si12);
bool inst_may_cross_page(ADDRX pc);
#endif
