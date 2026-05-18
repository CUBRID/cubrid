# CUBRID Optimizer Costing Review Against PostgreSQL

## Purpose

This document reviews the optimizer costing changes made while investigating JOB 22c style join-order regressions.  The focus is not only what changed, but whether each change follows a PostgreSQL-like cost-based optimizer principle: prefer statistics-driven selectivity and repeated-lookup costing over post-hoc guards or query-shape penalties.

The main CUBRID files reviewed are:

- `src/optimizer/query_planner.c`
- `src/optimizer/query_planner_constants.h`
- `src/optimizer/histogram/histogram_cl.cpp`

The main PostgreSQL reference points are:

- PostgreSQL `src/backend/optimizer/path/costsize.c`, especially `cost_memoize_rescan()`
- PostgreSQL `src/backend/utils/adt/selfuncs.c`, especially `eqjoinsel_inner()`

## Source References

### CUBRID

- `src/optimizer/query_planner.c`
  - `qo_nljoin_cost()`: applies `inner_cost_cardinality` to NL inner CPU/I/O cost.
  - `qo_is_memoize_favorable_plan()`: enables memoize-like costing for unique-family inner index lookups.
  - `qo_estimate_memoize_outer_distinct_keys()`: estimates distinct outer lookup keys from join terms.
  - `qo_get_non_unique_residual_lookup_costs()`: adds non-unique lookup residual CPU and heap I/O costs.
  - `qo_apply_unique_join_cardinality()`: adjusts equality join selectivity when exactly one side is unique.
- `src/optimizer/query_planner_constants.h`
  - `QO_NLJOIN_MEMOIZE_HIT_COST_RATIO`
  - `QO_SSCAN_FILTER_CPU_FACTOR`
- `src/optimizer/histogram/histogram_cl.cpp`
  - `histogram_get_eqjoin_selectivity()`
  - `histogram_eqjoin_exact_mcv_mass_t()`

### PostgreSQL

- `src/backend/optimizer/path/costsize.c`
  - `cost_memoize_rescan()` estimates Memoize rescan cost from expected calls, distinct parameter values, cache entries, hit ratio, and eviction ratio.
  - Source: <https://doxygen.postgresql.org/costsize_8c_source.html>
- `src/backend/utils/adt/selfuncs.c`
  - `eqjoinsel_inner()` directly matches both sides' MCV lists and combines exact MCV match mass with unmatched/non-MCV estimates.
  - Source: <https://github.com/postgres/postgres/blob/master/src/backend/utils/adt/selfuncs.c>

## Background

For JOB 22c, the problematic CUBRID plan shape joined a large fan-out table too early and repeatedly performed index lookups whose cost was underestimated or misclassified.  The goal of the optimizer changes was to make the CBO compare join orders using more faithful costs:

- Unique/PK lookups in nested loop joins can be cheap when the same parameter key repeats.
- Non-unique index lookups can be expensive when each lookup reads several rows and evaluates residual predicates.
- Join selectivity for skewed equality joins should come from histogram/MCV estimators, not from a later hot-key guard.

## Summary Of CUBRID Changes

### Removed Post-Hoc Guard And Penalty Logic

The following CUBRID functions and constants were removed from the active costing path:

- `qo_apply_mcv_hotkey_join_guard()`
- `qo_get_delayed_sarg_lookup_penalty()`
- `qo_get_skew_uncertainty_lookup_penalty()`
- `qo_info_is_small_filtered_side()`
- `QO_MCV_GUARD_*`
- `QO_DELAYED_SARG_*`
- `QO_READ_BEFORE_FILTER_*`
- `QO_BRIDGE_FANOUT_*`
- `QO_SKEW_UNCERTAINTY_*`

Review note: these were query-shape or risk guards layered after the normal selectivity/costing flow.  They could move the plan away from a bad join order, but the reason was difficult to audit and not tightly tied to the statistics used by the optimizer.

PostgreSQL alignment: PostgreSQL generally models skew and repeated work inside selectivity estimators and path cost functions.  For example, MCV skew is handled in `eqjoinsel_inner()` as part of equality join selectivity, and repeated parameterized scans are modeled in `cost_memoize_rescan()`.

### Unique Join Cardinality Adjustment

CUBRID function:

- `qo_apply_unique_join_cardinality()`

The adjustment applies when an equality join has exactly one unique side.  It uses the filtered cardinality ratio of the unique side to estimate the join cardinality of the non-unique side:

```text
unique_ratio = filtered_unique_side_cardinality / unique_index_cardinality
join_card    = non_unique_side_cardinality * unique_ratio
term_sel     = join_card / base_cardinality
```

The adjusted selectivity is applied as an upper bound:

```text
term_sel = min(original_term_sel, adjusted_unique_term_sel)
```

This keeps one-unique joins from increasing estimated cardinality beyond the non-unique side, while still preserving a more selective existing estimate when the generic join selectivity is already lower.

PostgreSQL alignment: this follows the same CBO principle as PostgreSQL's equality join selectivity logic: unique/NDV information should constrain join cardinality directly.  PostgreSQL's `eqjoinsel_inner()` falls back to NDV-based estimates when MCV lists are unavailable, using the smaller-side NDV perspective to bound equality join selectivity.

Difference from PostgreSQL: this is implemented as a CUBRID-specific adjustment around existing join term selectivity rather than as a complete rewrite of the equality selectivity estimator.  The ceiling keeps the change conservative and easy to revert.

### Nested Loop Inner Lookup Cost Uses Effective Lookup Cardinality

CUBRID function:

- `qo_nljoin_cost()`

Previously, nested loop inner CPU and index I/O cost used the guessed outer/result cardinality directly.  The current path introduces `inner_cost_cardinality`:

```text
inner_cost_cardinality = guessed_result_cardinality
qo_is_memoize_favorable_plan(..., &inner_cost_cardinality)
qo_get_non_unique_residual_lookup_costs(..., inner_cost_cardinality, ...)
```

Then the inner side cost is based on that effective lookup cardinality:

```text
inner_cpu_cost = inner_cost_cardinality * inner->variable_cpu_cost
               + non_unique_residual_lookup_cpu_cost

inner_io_cost  = inner_cost_cardinality * inner->variable_io_cost
               * (1 - ISCAN_IO_HIT_RATIO)
               + non_unique_residual_lookup_io_cost
```

Review note: this separates the number of outer rows produced by the join from the number of expensive inner lookup executions.  That separation is important for parameterized index probes where repeated keys can reuse work, and for non-unique lookups where one probe may read multiple candidate rows.

PostgreSQL alignment: PostgreSQL similarly separates path rows from repeated rescan cost.  `cost_memoize_rescan()` estimates how many calls are expected and how many distinct parameter sets those calls contain, then scales cost by the expected cache hit ratio.

### Memoize-Like Unique Lookup Costing

CUBRID functions and constant:

- `qo_is_memoize_favorable_plan()`
- `qo_estimate_memoize_outer_distinct_keys()`
- `qo_accumulate_memoize_outer_distinct_keys()`
- `QO_NLJOIN_MEMOIZE_HIT_COST_RATIO`

The CUBRID logic applies only to nested loop joins whose inner side is an index scan and whose lookup is unique-family/equality based.  For such plans, repeated outer rows are split into likely memoize misses and hits:

```text
miss_cardinality      = min(outer_cardinality, outer_distinct_keys)
effective_cardinality = miss_cardinality
                      + (outer_cardinality - miss_cardinality)
                        * QO_NLJOIN_MEMOIZE_HIT_COST_RATIO
```

The current hit cost ratio is:

```text
QO_NLJOIN_MEMOIZE_HIT_COST_RATIO = 0.01
```

This constant is no longer used as a fixed hit-rate guess.  It only represents the relative cost of a cached hit.  The hit/miss split is driven by estimated distinct outer lookup keys.

PostgreSQL alignment: PostgreSQL's `cost_memoize_rescan()` explicitly says that Memoize costing needs two values: expected calls and expected distinct parameter sets.  It estimates distinct parameter values using:

```text
ndistinct = estimate_num_groups(root, mpath->param_exprs, est_calls, ...)
```

Then it calculates:

```text
hit_ratio = ((est_calls - ndistinct) / est_calls)
          * (est_cache_entries / max(ndistinct, est_cache_entries))
```

CUBRID's `outer_distinct_keys` estimate is the corresponding concept for NL index lookup costing: miss count should be closer to distinct lookup keys than to total outer rows.

Difference from PostgreSQL: PostgreSQL also models cache entry size, memory capacity, `est_cache_entries`, eviction ratio, cache store cost, and cache lookup overhead.  CUBRID currently models only the effective repeated lookup cardinality and a fixed cached-hit cost ratio.

Review risk: if outer NDV is unavailable, CUBRID falls back to the full outer cardinality, which is conservative and avoids making memoize look favorable without statistics.  For composite keys, CUBRID multiplies component NDVs and caps by outer cardinality, which is a standard independence approximation but can overestimate distinct tuple count when columns are correlated.

### Non-Unique Residual Lookup Cost

CUBRID function:

- `qo_get_non_unique_residual_lookup_costs()`

This applies to non-unique inner index scans with residual `sarged_terms`.  It estimates how many rows a single leading-key lookup reads:

```text
rows_per_lookup = table_cardinality / leading_key_pkeys
```

It then adds residual CPU and heap I/O costs:

```text
cpu_cost = outer_cardinality
         * rows_per_lookup
         * (tuple_visit_weight + residual_predicate_weight)
         * QO_CPU_WEIGHT

io_cost  = outer_cardinality
         * (rows_per_lookup / ISCAN_OID_ACCESS_OVERHEAD)
         * (1 - ISCAN_IO_HIT_RATIO)
```

Unique-family indexes and covering scans are excluded where appropriate.  The CPU term intentionally charges both candidate tuple visitation and residual predicate evaluation, since a non-unique lookup can read several candidate rows before `sarged_terms` discard them.

PostgreSQL alignment: PostgreSQL index costing separates index access, heap access, and CPU predicate evaluation.  The exact formulas differ, but the principle is the same: an index probe is not always a single-row lookup, and residual predicate evaluation should scale with the expected number of tuples fetched before filtering.

Difference from PostgreSQL: this is a targeted NL inner lookup refinement, not a full CUBRID equivalent of PostgreSQL's `btcostestimate()` and `genericcostestimate()`.  It uses leading partial-key statistics and existing predicate cost weights rather than a full page correlation/cache model.

## PostgreSQL MCV Selectivity Comparison

PostgreSQL `eqjoinsel_inner()` handles equality join skew inside selectivity estimation.  When both sides have MCV lists, it directly compares values from both lists:

```text
for each MCV value on side 1:
  for each unmatched MCV value on side 2:
    if values are equal:
      matchprodfreq += freq1 * freq2
```

It then separately accounts for matched MCVs, unmatched MCVs, and non-MCV population.  This is important because skew changes join cardinality before path costing begins.

CUBRID has the same architectural direction in `histogram_get_eqjoin_selectivity()`:

- `histogram_eqjoin_exact_mcv_mass_t()` treats exact-value histogram buckets as MCV buckets.
- It builds a value-to-frequency map for the right side.
- It probes that map with left-side exact-value buckets.
- It tracks matched MCV mass, unmatched MCV mass, and non-MCV mass separately.
- When both sides have MCV buckets, it uses a PostgreSQL-style two-sided estimate: exact matched MCV mass plus unmatched-MCV/non-MCV residual estimates, then chooses the smaller estimate from the two relation perspectives.

If both sides do not have MCV buckets, CUBRID does not try to invent a hot-key signal.  In that case it falls back to the existing histogram/NDV logic:

- `histogram_eqjoin_residual_fallback_mass()` estimates equality join mass from the summed NDV of non-MCV buckets.
- `histogram_eqjoin_residual_overlap_mass_t()` can reduce that fallback only when histogram bucket ranges overlap and provide weak evidence that the common value domain is narrower.

In other words, "MCV is insufficient" means `lhs_mcv_count == 0 || rhs_mcv_count == 0`.  With MCVs on both sides, hot-key joins are handled by direct MCV matching.  Without MCVs on both sides, the estimator deliberately uses NDV/range evidence rather than reintroducing a separate hot-key penalty.

Therefore, removing `qo_apply_mcv_hotkey_join_guard()` is consistent with PostgreSQL's approach.  Skew should be represented by the equality join selectivity estimator, not by a later guard that inflates or caps join cardinality after the estimator has already returned.

## PostgreSQL Memoize Costing Comparison

PostgreSQL `cost_memoize_rescan()` is the closest reference for the CUBRID repeated lookup changes.  The important logic is:

- `est_calls`: expected number of times the memoized path is rescanned.
- `param_exprs`: expressions that form the cache key.
- `ndistinct`: estimated number of distinct parameter values from `estimate_num_groups()`.
- `est_cache_entries`: estimated cache capacity from memory limit and entry size.
- `hit_ratio`: expected cache hit ratio using both `est_calls` and `ndistinct`.
- `evict_ratio`: penalty when distinct keys exceed cache capacity.

CUBRID's current implementation maps to a smaller subset:

```text
est_calls       -> guessed_result_cardinality
param_exprs     -> join/during_join equality terms
ndistinct       -> qo_estimate_memoize_outer_distinct_keys()
hit cost        -> QO_NLJOIN_MEMOIZE_HIT_COST_RATIO
effective calls -> inner_cost_cardinality
```

This is a meaningful improvement over a constant hit ratio because the number of expensive lookups now depends on outer key distinctness.

The remaining gap is cache realism.  CUBRID does not yet estimate:

- cache entry width
- cache memory limit
- maximum cache entries
- eviction ratio
- cache insert overhead
- EXPLAIN-visible estimated unique keys and hit ratio

Those are future improvements if CUBRID adds an explicit Memoize path/node or exposes memoize-like costing diagnostics.

## Sequential Scan Startup Cost

CUBRID also reduces the extra CPU multiplier for sequential scan filters through `QO_SSCAN_FILTER_CPU_FACTOR`.  This keeps a large filtered scan, such as `cast_info` with `note LIKE '%(producer)%'`, from being priced too far above a long chain of repeated index lookups.  This is important for PostgreSQL-like plans where a broad filtered scan is a reasonable starting point, especially when the execution engine can benefit from scan-friendly access patterns or parallel execution.

This adjustment does not make all sequential scans cheap.  The base scan CPU and table I/O cost remain, and predicate-specific weights such as complex `LIKE` are still applied.  It only avoids multiplying filter CPU by an additional large factor before join costing has a chance to compare scan-first and repeated-lookup alternatives.

Follow-up: this factor should eventually be tied to whether the scan can actually use parallel execution.  The current costing change assumes large filtered scans are scan-friendly, but the optimizer should later distinguish parallel-capable scans from scans that must run serially.

## Review Matrix

| Area | CUBRID Change | PostgreSQL Principle | Match Level | Remaining Risk |
| --- | --- | --- | --- | --- |
| MCV skew | Removed hot-key guard; estimate matched/unmatched MCV and non-MCV mass inside histogram equality selectivity | `eqjoinsel_inner()` matches MCV lists inside estimator | High | Histogram coverage and bucket exactness must be good enough |
| Unique join | `qo_apply_unique_join_cardinality()` uses unique side filter ratio | Equality join selectivity should be NDV/unique constrained | Medium | Implemented as local adjustment, not full selectivity rewrite |
| Repeated lookup | `inner_cost_cardinality` separates rows from expensive probes | Memoize cost uses calls and distinct parameter values | Medium-high | No cache capacity or eviction model yet |
| Outer distinct keys | Estimate miss count from outer lookup-key NDV | `estimate_num_groups(param_exprs, est_calls)` | Medium-high | Composite-key NDV assumes independence |
| Non-unique residual lookup | Add CPU/I/O based on rows per lookup and residual terms | Index costing accounts for fetched tuples and qual CPU | Medium | Targeted model, not full PG index cost model |
| Removed penalties | Deleted delayed sarg/skew/fanout penalties | Prefer estimator/path cost formulas over guards | High | Bad stats may still need better estimator fallback |

## Reversibility

The changes are intentionally localized:

- Memoize-like repeated lookup logic is isolated in `qo_is_memoize_favorable_plan()` and `qo_estimate_memoize_outer_distinct_keys()`.
- Non-unique residual lookup cost is isolated in `qo_get_non_unique_residual_lookup_costs()`.
- The only new public constant in this area is `QO_NLJOIN_MEMOIZE_HIT_COST_RATIO`.
- Removed guard constants/functions are not required by the remaining costing flow.

If a regression appears, the memoize and non-unique residual paths can be disabled independently by bypassing their calls in `qo_nljoin_cost()`.

## Verification

The current code was compiled with:

```sh
cmake --build "/home/cubrid/dev/cubrid/build_x86_64_debug" --target cubridsa -j4
```

The build completed successfully.  IDE lint diagnostics for `src/optimizer/query_planner.c` also reported no errors.

## Recommended Follow-Up

1. Capture the chosen plan and estimated cost for JOB 22c before and after these changes.
2. Add diagnostics for `outer_distinct_keys`, `miss_cardinality`, and `effective_outer_cardinality` while tuning.
3. Consider a PostgreSQL-like fallback rule: if outer distinct key estimation used default statistics, assume all calls are distinct.
4. If CUBRID adds an explicit Memoize node, extend costing with cache entry size, cache capacity, eviction ratio, and EXPLAIN-visible estimated hit ratio.
5. Revisit `QO_SSCAN_FILTER_CPU_FACTOR` after the optimizer can check whether a large filtered scan is parallel-capable.
