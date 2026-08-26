/*
 *
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * px_scan_instnum.hpp - ROWNUM (inst_num) support for parallel scan.
 *
 * Implementations live in px_scan_instnum.cpp, split there by build mode: eligibility runs
 * client-side (px_scan_checker.cpp links into cs/sa only), numbering runs server-side.
 * get_instnum_upper_limit_rhs stays inline here because both halves call it.
 */

#ifndef _PX_SCAN_INSTNUM_HPP_
#define _PX_SCAN_INSTNUM_HPP_

#include <atomic>
#include <vector>

#include "regu_var.hpp"
#include "xasl.h"
#include "xasl_predicate.hpp"

#if defined (SERVER_MODE) || defined (SA_MODE)
#include "query_list.h"
#include "thread_entry.hpp"
#endif /* SERVER_MODE || SA_MODE */

namespace parallel_scan
{
  /* Which mechanism assigns ROWNUM. The two are mutually exclusive: RENUMBER requires
   * instnum_pred == NULL, ATOMIC_DRAW requires one. */
  enum class instnum_mode
  {
    NONE,
    RENUMBER,			/* ROWNUM is only projected -> main numbers the rows at merge */
    ATOMIC_DRAW,		/* WHERE ROWNUM <= N -> workers draw from a shared counter */
  };

  /* rhs of a single-term "inst_num() <= ?" (or "< ?") instnum_pred, else nullptr. */
  inline REGU_VARIABLE *
  get_instnum_upper_limit_rhs (XASL_NODE *x, bool *is_less_than)
  {
    if (x == nullptr || x->instnum_pred == nullptr || x->instnum_val == nullptr)
      {
	return nullptr;
      }
    if (x->instnum_flag & XASL_INSTNUM_FLAG_SCAN_CONTINUE)
      {
	return nullptr;		/* pred cannot stop the scan (e.g. OR-term) -> not an upper-limit form */
      }
    PRED_EXPR *pr = x->instnum_pred;
    if (pr->type != T_EVAL_TERM || pr->pe.m_eval_term.et_type != T_COMP_EVAL_TERM)
      {
	return nullptr;
      }
    COMP_EVAL_TERM *comp = &pr->pe.m_eval_term.et.et_comp;
    if (comp->lhs == nullptr || comp->rhs == nullptr)
      {
	return nullptr;
      }
    if (comp->lhs->type != TYPE_CONSTANT || comp->lhs->value.dbvalptr != x->instnum_val)
      {
	return nullptr;
      }
    if (comp->rhs->type != TYPE_POS_VALUE && comp->rhs->type != TYPE_DBVAL)
      {
	return nullptr;
      }
    if (comp->rel_op != R_LE && comp->rel_op != R_LT)
      {
	return nullptr;
      }
    if (is_less_than != nullptr)
      {
	*is_less_than = (comp->rel_op == R_LT);
      }
    return comp->rhs;
  }

  /* Global numbering for ATOMIC_DRAW: every qualifying row takes the next number from one counter,
   * so the quota is exact no matter how the workers interleave. */
  class atomic_instnum
  {
    public:
      bool is_less_than = false;
      bool limit_resolved = false;
      INT64 limit = 0;
      REGU_VARIABLE *limit_rhs = nullptr;
      std::atomic<INT64> counter {0};

      /* A scan block reset rebuilds the handler while the output list keeps the rows already emitted;
       * resume from there instead of granting the quota again per block. */
      inline void seed (INT64 already_emitted) noexcept
      {
	counter.store (already_emitted);
      }

      /* raw is already inclusive: resolve_instnum_limit folds the R_LT decrement and the
       * fractional-bound correction into it before calling here. */
      inline void resolve_limit (INT64 raw) noexcept
      {
	limit = (raw <= 0) ? 0 : raw;
	limit_resolved = true;
      }

      inline INT64 next () noexcept
      {
	return counter.fetch_add (1, std::memory_order_relaxed) + 1;
      }

      inline bool exceeded (INT64 drawn) const noexcept
      {
	return drawn > limit;
      }
  };

  /* Eligibility, decided while the XASL is built.  wf119: the compiler now
   * lives inside the server, so these are visible in all modes. */
  bool is_renumberable_instnum (XASL_NODE *x);
  bool is_atomic_instnum_eligible (XASL_NODE *x);

  /* Numbering, run while the scan executes. Guarded because these reach into the server query
   * engine (qfile_*, fetch_peek_dbval), which the client library does not link. */
#if defined (SERVER_MODE) || defined (SA_MODE)
  /* Which mechanism this XASL needs; fills the out-params. */
  instnum_mode detect_instnum_mode (XASL_NODE *x, std::vector<int> &rownum_col_indices, atomic_instnum &draw);

  /* Resolves the "<= ?" bound into draw.limit. On failure the error is already raised via er_set. */
  int resolve_instnum_limit (THREAD_ENTRY *thread_p, atomic_instnum &draw, VAL_DESCR *vd);

  /* Assigns global ROWNUM in-place across the worker lists, before merge and before any ORDER BY
   * sort. Continues from start_at: a scan block reset rebuilds the handler while dest keeps its rows. */
  int renumber_instnum_lists (THREAD_ENTRY *thread_p, std::vector<QFILE_LIST_ID *> &lists,
			      const std::vector<int> &col_indices, INT64 start_at);
#endif /* SERVER_MODE || SA_MODE */
}

#endif /*_PX_SCAN_INSTNUM_HPP_ */
