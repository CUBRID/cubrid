/*
 * Copyright 2008 Search Solution Corporation
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
 * bind_variant.h - plan variants of one cached query, chosen by bind-value fingerprint
 *
 * A query whose estimates move with its bind values can keep more than one plan. Every plan
 * (variant) is an ordinary XASL cache entry of its own; what ties them to the query is a small
 * directory kept on the query's base cache entry on the server, shared by every client:
 *
 *   - the fingerprint each variant was planned under: the rows every histogram-priced
 *     host-variable predicate was expected to scan, in tree order;
 *   - how many plans the query has and how many compiles produced them.
 *
 * An execution with new values runs the variant whose fingerprint is within the band of its
 * own. A client looks in the directory it last received first and asks the server only when
 * nothing there suits; the server then answers with a variant another client added since, or
 * reserves a new one to compile. A compile that produces a plan some variant already has does
 * not add a variant; its fingerprint becomes another way to reach that variant.
 *
 * The query learns until it has plan_cache_bind_variants distinct plans or BIND_VARIANT_MAX_COMPILES
 * compiles, whichever comes first. Then the directory is final: with one plan the values are not
 * looked at again, with more an execution runs the variant nearest to its values and never
 * compiles. No variant is ever replaced, so a plan another client is running never changes under
 * it.
 *
 * Everything is dropped with the base entry (DDL, eviction, cache drop, restart), and the next
 * execution starts over. Variant entries carry the base entry's creation time in their key, so
 * a new generation never reuses an old generation's plans.
 */

#ifndef _BIND_VARIANT_H_
#define _BIND_VARIANT_H_

#ident "$Id$"

#include <float.h>

#include "cache_time.h"
#include "sha1.h"

/* predicates fingerprinted per statement, in tree order; a statement with more keeps the first
 * ones: the order is the same on every execution, so the comparison stays consistent */
#define BIND_WATCH_MAX_TERMS 32

/* the most compiles a query learns from, however many plans it is allowed: a compile that keeps
 * producing a plan the query already has (a range bound drifting outside every band) would
 * otherwise never stop. Every compile adds one fingerprint, so this also bounds the directory. */
#define BIND_VARIANT_MAX_COMPILES 32

/* the rows each priced predicate is expected to scan under one set of bind values */
typedef struct bind_fingerprint BIND_FINGERPRINT;
struct bind_fingerprint
{
  int terms;			/* priced predicates; 0 = nothing priceable */
  double rows[BIND_WATCH_MAX_TERMS];
};

/* one way to reach a variant: the fingerprint it was planned under, or one that compiled to
 * the same plan */
typedef struct bind_variant_record BIND_VARIANT_RECORD;
struct bind_variant_record
{
  int variant;
  BIND_FINGERPRINT fp;
};

typedef enum
{
  BIND_VARIANT_OP_CHECK = 1,	/* nothing in the client's directory suits this fingerprint */
  BIND_VARIANT_OP_REGISTER = 2	/* the reserved variant was compiled to this plan */
} BIND_VARIANT_OP;

typedef enum
{
  BIND_VARIANT_NOT_FOUND = 0,	/* no base entry of that generation: start over */
  BIND_VARIANT_MATCH,		/* CHECK: run variant `variant` (another client added it) */
  BIND_VARIANT_COMPILE,		/* CHECK: nothing suits; compile into the reserved `variant` */
  BIND_VARIANT_FROZEN,		/* CHECK: the query has learned all it may; choose from the directory */
  BIND_VARIANT_NEW,		/* REGISTER: the plan is new, `variant` is now part of the query */
  BIND_VARIANT_SAME_PLAN	/* REGISTER: an existing variant has that plan; run `variant` */
} BIND_VARIANT_RESULT;

/* a client that found the query done learning while a variant was still being compiled asks
 * again this many times at most before it settles on what it has (the compiling client may
 * have died) */
#define BIND_VARIANT_MAX_POLLS 4

typedef enum
{
  BIND_VARIANT_STATE_WATCH = 0,	/* still learning, or a variant is still being compiled */
  BIND_VARIANT_STATE_DONE,	/* done learning, one plan: the values are not looked at again */
  BIND_VARIANT_STATE_SELECT	/* done learning, several plans: pick the nearest, never compile */
} BIND_VARIANT_STATE;

typedef struct bind_variant_request BIND_VARIANT_REQUEST;
struct bind_variant_request
{
  SHA1Hash sha1;		/* the base entry */
  CACHE_TIME time_stored;	/* ... and its generation */
  int op;			/* BIND_VARIANT_OP */
  int limit;			/* distinct plans the query may have */
  int variant;			/* REGISTER: the reserved variant */
  double band;			/* CHECK: a variant within this factor suits */
  double floor;			/* predicates below this many rows on both sides are not compared */
  UINT64 plan_sig;		/* REGISTER: signature of the compiled plan */
  BIND_FINGERPRINT fp;
};

typedef struct bind_variant_reply BIND_VARIANT_REPLY;
struct bind_variant_reply
{
  int result;			/* BIND_VARIANT_RESULT */
  int variant;
  int state;			/* BIND_VARIANT_STATE after this request */
  int compiles;			/* compiles reserved so far */
  int plans;			/* distinct plans */
  int pending;			/* variants reserved but not registered yet */
  int n_records;
  BIND_VARIANT_RECORD records[BIND_VARIANT_MAX_COMPILES];
};

#define OR_BIND_VARIANT_REQUEST_SIZE \
  (OR_SHA1_SIZE + OR_INT_SIZE * 6 + MAX_ALIGNMENT + OR_INT64_SIZE + OR_DOUBLE_SIZE * (2 + BIND_WATCH_MAX_TERMS))
#define OR_BIND_VARIANT_REPLY_SIZE \
  (OR_INT_SIZE * (7 + 2 * BIND_VARIANT_MAX_COMPILES) + MAX_ALIGNMENT \
   + OR_DOUBLE_SIZE * BIND_VARIANT_MAX_COMPILES * BIND_WATCH_MAX_TERMS)

/*
 * bind_fingerprint_distance () - how far apart two fingerprints are: the largest factor by
 *   which one predicate's expected scan differs, either way
 * return    : 1.0 when nothing comparable moved; DBL_MAX when the predicate sets differ
 * floor (in): a predicate below this many rows on both sides moves no plan and is skipped
 */
static inline double
bind_fingerprint_distance (const BIND_FINGERPRINT * a, const BIND_FINGERPRINT * b, double floor)
{
  double worst = 1.0;
  int i;

  if (a->terms != b->terms || a->terms <= 0)
    {
      return DBL_MAX;
    }
  for (i = 0; i < a->terms; i++)
    {
      double x = a->rows[i], y = b->rows[i], fold;

      if (x < floor && y < floor)
	{
	  continue;
	}
      if (x <= 0.0 || y <= 0.0)
	{
	  return DBL_MAX;
	}
      fold = (x >= y) ? x / y : y / x;
      if (fold > worst)
	{
	  worst = fold;
	}
    }
  return worst;
}

/*
 * bind_variant_nearest () - the record whose fingerprint is nearest to fp
 * return        : index into records, -1 when there are none
 * out_dist (out): its distance
 */
static inline int
bind_variant_nearest (const BIND_VARIANT_RECORD * records, int n_records, const BIND_FINGERPRINT * fp,
		      double floor, double *out_dist)
{
  int best = -1, i;
  double best_dist = DBL_MAX;

  for (i = 0; i < n_records; i++)
    {
      double d = bind_fingerprint_distance (&records[i].fp, fp, floor);

      if (best < 0 || d < best_dist)
	{
	  best = i;
	  best_dist = d;
	}
    }
  if (out_dist != NULL)
    {
      *out_dist = best_dist;
    }
  return best;
}

#endif /* _BIND_VARIANT_H_ */
