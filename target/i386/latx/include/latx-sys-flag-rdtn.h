/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _LATX_SYS_FLAG_RDTN_H_
#define _LATX_SYS_FLAG_RDTN_H_

#include "ir1.h"
#include "flag-reduction.h"

#define LATXS_FLAGRDTN_ENABLE
#define LATXS_FLAGRDTN_OPTION_CHECK
#define LATXS_FLAGRDTN_CPL_FILTER_



#ifdef LATXS_FLAGRDTN_ENABLE
#define latxs_flag_reduction(tb) do {   \
      __latxs_flag_reduction(tb);       \
} while (0)
void __latxs_flag_reduction(TranslationBlock *tb);
#define LATXS_FLAG_REDUCTION_CHECKIF(pir1) \
if (ir1_need_calculate_any_flag(pir1))

#else /* no LATXS_FLAGRDTN_ENABLE */

#define latxs_flag_reduction(tb) tb_flag(tb)
#define LATXS_FLAG_REDUCTION_CHECKIF(pir1)
#endif /* LATXS_FLAGRDTN_ENABLE */

#ifdef LATXS_FLAGRDTN_OPTION_CHECK
#define latxs_flag_check(opt, tb) do {  \
    if (!(opt)) {                       \
        tb_flag(tb);                    \
        return;                         \
    }                                   \
} while (0)
#else /* no LATXS_FLAGRDTN_OPTION_CHECK */
#define latxs_flag_check(opt)
#endif /* LATXS_FLAGRDTN_OPTION_CHECK */

#ifdef LATXS_FLAGRDTN_CPL_FILTER_
#define latxs_flag_cpl_filter(tb) do {  \
    if ((tb->flags & 0x3) != 3)  {      \
        tb_flag(tb);                    \
        return;                         \
    }                                   \
} while (0)
#else /* no LATXS_FLAGRDTN_CPL_FILTER_ */
#define latxs_flag_cpl_filter(tb)
#endif /* LATXS_FLAGRDTN_CPL_FILTER_ */

const IR1_EFLAG_USEDEF *latxs_ir1_opcode_to_eflag_usedef(IR1_INST *ir1);
IR1_EFLAG_USEDEF latxs_ir1_get_tu_eflag_usedef(IR1_INST *ir1);

/* option sysflagtb: TB-mode per-TB incoming-flag usedef answer. */
void __latxs_flag_reduction_tb_mode_parse_tb_use(TranslationBlock *tb);

#endif
