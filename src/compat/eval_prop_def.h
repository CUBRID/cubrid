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
 * eval_prop_def.h - per-overload Evaluation Properties of functions/operators
 *
 * A Signature is a pure type contract (argument types -> return type); the
 * Evaluation Properties bundled alongside it describe how the matched overload
 * BEHAVES when evaluated: volatility (when/how often), SI requirement (what
 * synchronized statement state a client evaluation must have in sync), and
 * allow_default (whether it may appear in a column DEFAULT).  Volatility and
 * SI are stamped onto the expression node when overload resolution picks the
 * best match, so argument-pattern-dependent semantics (RAND () vs RAND (seed),
 * UNIX_TIMESTAMP () vs UNIX_TIMESTAMP (str)) fall out of matching with no
 * re-inspection of arguments.
 *
 * A shared vocabulary of the SQL expression layer, not parser-specific: the
 * client/parser classifies and folds DEFAULT expressions with it, and the
 * server reads the volatility carried in the DEFAULT slots of the INSERT XASL
 * to decide how often a DEFAULT is evaluated (STABLE -> once per statement,
 * VOLATILE -> once per row).  Pure type definitions plus tiny inline combiners.
 */

#ifndef _EVAL_PROP_DEF_H_
#define _EVAL_PROP_DEF_H_

/*
 * Volatility governs *when* and *how often* a column DEFAULT expression is
 * evaluated and how aggressively it can be constant-folded -- it does NOT decide
 * whether an expression is allowed in a DEFAULT.  The zero value is a distinct
 * UNSET sentinel (NOT volatile): an un-annotated overload reaching DEFAULT
 * processing is treated as not constant-foldable, never silently as Volatile.
 * The values are ordered (IMMUTABLE < STABLE < VOLATILE).  The Compact DEFAULT
 * Tree persists them per node, so they must not change.
 */
typedef enum
{
  PT_VOLATILITY_UNSET = 0,	/* not classified (zero value, sentinel) */
  PT_VOLATILITY_IMMUTABLE = 1,	/* same input always yields same output, forever */
  PT_VOLATILITY_STABLE = 2,	/* constant within a single statement */
  PT_VOLATILITY_VOLATILE = 3	/* may differ on every evaluation */
} PT_VOLATILITY;

/*
 * pt_volatility_max () - MAX-combine two volatilities for bottom-up propagation.
 *
 * A PT_VOLATILITY_UNSET operand "taints" the result to UNSET: an expression
 * containing an un-classified function/operator is conservatively treated as not
 * constant-foldable.  Otherwise the result is the more-volatile of the two
 * (IMMUTABLE < STABLE < VOLATILE).
 */
static inline PT_VOLATILITY
pt_volatility_max (PT_VOLATILITY a, PT_VOLATILITY b)
{
  if (a == PT_VOLATILITY_UNSET || b == PT_VOLATILITY_UNSET)
    {
      return PT_VOLATILITY_UNSET;
    }
  return (a > b) ? a : b;
}

/*
 * A "residual" column DEFAULT is an expression that survived constant folding:
 * its effective volatility is classified at or above STABLE (folding can only
 * reduce IMMUTABLE subtrees).
 */
#define PT_VOLATILITY_IS_RESIDUAL(vol)          ((vol) >= PT_VOLATILITY_STABLE)
#define PT_VOLATILITY_IS_VOLATILE_RESIDUAL(vol) ((vol) == PT_VOLATILITY_VOLATILE)

/*
 * SI requirement: a bitmask of the synchronized statement state an overload
 * reads, OR-combined over an expression tree.  Unlike PT_VOLATILITY the zero
 * value is a valid "no requirement" default, not an UNSET sentinel.  The
 * Compact DEFAULT Tree persists the bits per node, so they must not change.
 */
typedef enum
{
  PT_SI_NONE = 0,		/* reads no synchronized statement state (valid default) */
  PT_SI_SYS_DATETIME = 0x1	/* reads the statement clock (parser->sys_datetime/sys_epochtime) */
} PT_SI_REQUIREMENT;

/*
 * allow_default: the sole decider of whether a function/operator (as a code)
 * may appear in a column DEFAULT.  The zero value is an UNSET sentinel that is
 * REJECTED at DEFAULT admission, so an unclassified code fails safe.  UNSET is
 * "held back": not reviewed for a DEFAULT yet, listed in the completeness gate
 * and opened group by group.  NOT_ALLOWED is "reviewed and forbidden"; the
 * reason is a comment at the classification site.
 */
typedef enum
{
  PT_ALLOW_DEFAULT_UNSET = 0,	/* held back (zero value, sentinel) - rejected in DEFAULT */
  PT_DEFAULT_ALLOWED = 1,	/* may appear in a column DEFAULT */
  PT_DEFAULT_NOT_ALLOWED = 2	/* reviewed and forbidden */
} PT_ALLOW_DEFAULT;

/*
 * The per-overload properties bundle.  Zero-initialization ({}) yields the
 * sentinel/default of each axis: volatility UNSET, si NONE, allow_default UNSET.
 */
typedef struct pt_eval_prop PT_EVAL_PROP;
struct pt_eval_prop
{
  PT_VOLATILITY volatility;
  int si_requirement;		/* bitmask of PT_SI_REQUIREMENT bits */
  PT_ALLOW_DEFAULT allow_default;
};

#endif /* _EVAL_PROP_DEF_H_ */
