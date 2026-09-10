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
 * volatility.h - volatility classification of functions/operators
 *
 * A cross-cutting concept, not parser-specific: the client/parser uses it to
 * decide DEFAULT-expression folding, and the server reads it (stamped on the
 * residual's serialized REGU form) at INSERT execution to decide how often a
 * DEFAULT is evaluated (STABLE -> once per statement, VOLATILE -> once per row).
 */

#ifndef _VOLATILITY_H_
#define _VOLATILITY_H_

/*
 * Volatility governs *when* and *how often* a column DEFAULT expression is
 * evaluated and how aggressively it can be constant-folded -- it does NOT decide
 * whether an expression is admissible in a DEFAULT.  The zero value is a distinct
 * UNSET sentinel (NOT volatile): an un-annotated overload reaching DEFAULT
 * processing is treated as not constant-foldable, never silently as Volatile.
 * The values are ordered (IMMUTABLE < STABLE < VOLATILE) and persisted in two
 * regu-flag bits (REGU_VARIABLE_DEFAULT_VOLATILITY_*), so they must not change.
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
 * reduce IMMUTABLE subtrees).  Callers pair these with a
 * default_expr_type == DB_DEFAULT_NONE check to exclude legacy pseudo-column
 * defaults (SYSDATE, UUID(7), ...), which carry their own DB_DEFAULT_* enum.
 */
#define PT_VOLATILITY_IS_RESIDUAL(vol)          ((vol) >= PT_VOLATILITY_STABLE)
#define PT_VOLATILITY_IS_VOLATILE_RESIDUAL(vol) ((vol) == PT_VOLATILITY_VOLATILE)

#endif /* _VOLATILITY_H_ */
