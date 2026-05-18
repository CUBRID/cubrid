# Optimizer CBO Improvement PR Materials

## PR Title Draft

Improve optimizer selectivity and cost estimates for skewed joins and residual filters

## PR Summary Draft

This PR improves CUBRID optimizer estimates for complex JOB-style queries where the previous model favored plans that looked cheap under average selectivity but performed poorly at execution time.

The main changes are:

- Split selectivity-related logic from `query_planner.c` into dedicated planner selectivity modules.
- Add predicate evaluation cost weights for string predicates, `LIKE`, `OR` chains, and residual filters.
- Use histogram/MCV information to guard equality join cardinality when a small filtered side joins a skewed large side.
- Add a cold-fanout extension so joins with low global MCV frequency but high absolute fanout can still be treated as risky.
- Add lookup penalties for delayed residual filters and bridge-like fanout where many rows are read before filtering.
- Increase filtered sequential scan CPU cost to better model residual predicate evaluation.
- Run JOB benchmark queries in a single `csql` session to reduce connection overhead and improve timing consistency.

The intent is not to add query-specific rules. Each adjustment is expressed as a cost/selectivity model extension based on cardinality, selectivity, MCV frequency, fanout, and residual predicate cost.

## Problem Statement

Several JOB queries exposed cost inversion:

- A plan estimated as cheaper performed much slower.
- The bad plan usually joined through a bridge/fact table early.
- The optimizer multiplied average join selectivities and underestimated fanout.
- Strong filters such as `LIKE`, `IN`, or dimension equality predicates were often applied only after many index lookups or heap reads.

The previous model handled many joins as if average selectivity were sufficient:

```text
estimated_join_card = outer_card * inner_card * join_selectivity
```

For skewed or bridge-like joins, this can be too optimistic. A small filtered prefix can hit a high-fanout key in a large table. The expected work is then closer to:

```text
risk_card = small_card * large_card * max_mcv_frequency
```

instead of only:

```text
avg_card = small_card * large_card * avg_frequency
```

where:

```text
avg_frequency ~= 1 / NDV
max_mcv_frequency = frequency of the most common value on the large side
```

## Mathematical Model Notes

### 1. Predicate Evaluation Weight

Predicate evaluation is not uniform. Numeric equality, string equality, and complex pattern matching do not have the same CPU cost.

This PR assigns relative weights:

```text
w(eq_numeric)       = 1.00
w(eq_string)        = 3.00
w(like_prefix)      = 3.50
w(like_contains)    = 10.00
w(like_complex)     = 14.00
```

For a residual predicate set `R`, CPU cost is modeled as:

```text
cpu_filter_cost = rows_evaluated * QO_CPU_WEIGHT * sum(w(r) for r in R)
```

For sequential scans with residual filters:

```text
sscan_filter_cpu_cost =
  scan_rows * QO_CPU_WEIGHT * residual_weight * QO_SSCAN_FILTER_CPU_FACTOR
```

Current factor:

```text
QO_SSCAN_FILTER_CPU_FACTOR = 5.00
```

This reflects measured behavior where full scans with complex residual string predicates are substantially slower than plain row access.

### 2. MCV Hot-Key Join Guard

For equality joins, the classic average model uses:

```text
term_sel ~= 1 / max(NDV(left), NDV(right))
```

This is fragile when a small filtered side joins a large side with skew. The MCV guard estimates an upper-risk fanout:

```text
risk_fanout = large_card * effective_mcv_max_frequency
risk_card   = small_card * risk_fanout
risk_sel    = risk_card / base_cardinality
```

The adjusted selectivity is capped to prevent unstable over-correction:

```text
adjusted_sel = min(risk_sel, term_sel * max_multiplier)
adjusted_sel = min(adjusted_sel, 1.0)
```

For hot MCV cases:

```text
max_multiplier = QO_MCV_GUARD_MAX_SELECTIVITY_MULTIPLIER
```

For cold fanout cases:

```text
max_multiplier = QO_MCV_GUARD_COLD_FANOUT_SELECTIVITY_MULTIPLIER
```

Current constants:

```text
QO_MCV_GUARD_MIN_FREQUENCY                      = 0.1
QO_MCV_GUARD_MIN_RISK_FANOUT                    = 10.0
QO_MCV_GUARD_SMALL_CARD_ABS                     = 20.0
QO_MCV_GUARD_SMALL_CARD_RATIO                   = 0.001
QO_MCV_GUARD_MAX_BASE_SELECTIVITY               = 0.01
QO_MCV_GUARD_MAX_SELECTIVITY_MULTIPLIER         = 25.0
QO_MCV_GUARD_COLD_FANOUT_SELECTIVITY_MULTIPLIER = 100.0
QO_MCV_GUARD_MIN_SELECTIVITY_MULTIPLIER         = 0.5
```

The guard only applies when exactly one side is considered small. This keeps the model focused on dimension-to-fact or filtered-prefix-to-large-side joins rather than broad symmetric joins.

### 3. Cold Fanout Extension

Some `movie_id`-style joins have low global MCV frequency, so they do not look like traditional hot-key skew:

```text
effective_mcv_max_frequency < QO_MCV_GUARD_MIN_FREQUENCY
```

However, the absolute fanout can still be large:

```text
risk_fanout = large_card * effective_mcv_max_frequency
```

The cold fanout extension allows the guard to apply when:

```text
risk_fanout >= QO_MCV_GUARD_MIN_RISK_FANOUT
```

This protects against cases where the frequency is small as a percentage of a very large table, but still represents enough rows to change join-order cost.

### 4. Downward Damping

The MCV guard can also lower selectivity when MCV evidence says the average model is too pessimistic. To avoid over-correction:

```text
if risk_sel < term_sel:
  risk_sel = max(risk_sel, term_sel * QO_MCV_GUARD_MIN_SELECTIVITY_MULTIPLIER)
```

Current lower multiplier:

```text
QO_MCV_GUARD_MIN_SELECTIVITY_MULTIPLIER = 0.5
```

So the guard may reduce a join selectivity estimate, but not below half of the original estimate in one step.

### 5. Delayed Residual Filter Lookup Penalty

Nested-loop index lookup can be under-costed when the inner side has residual filters that are evaluated after repeated lookup.

The penalty uses:

```text
delayed_component =
  residual_filter_weight
  * log10(max(10, outer_cardinality))
  * QO_DELAYED_SARG_PENALTY_FACTOR
```

with:

```text
delayed_component <= QO_DELAYED_SARG_PENALTY_MAX
```

Current constants:

```text
QO_DELAYED_SARG_OUTER_CARD_THRESHOLD = 1.0
QO_DELAYED_SARG_PENALTY_FACTOR       = 0.25
QO_DELAYED_SARG_PENALTY_MAX          = 2.0
```

This is a cost penalty, not a cardinality change. It discourages plans that repeatedly probe an index and only later apply expensive residual predicates.

### 6. Read-Before-Filter and Bridge Fanout

For an inner plan, define:

```text
read_before_filter_ratio = total_rows / cardinality
```

Large values mean many rows are read before residual filters reduce the output.

The model adds bounded components:

```text
read_before_filter_component =
  residual_filter_weight
  * log10(read_before_filter_ratio)
  * QO_READ_BEFORE_FILTER_PENALTY_FACTOR
```

```text
bridge_fanout_component =
  join_term_weight
  * log10(read_before_filter_ratio)
  * QO_BRIDGE_FANOUT_PENALTY_FACTOR
```

Current constants:

```text
QO_READ_BEFORE_FILTER_RATIO_FLOOR    = 1.5
QO_READ_BEFORE_FILTER_PENALTY_FACTOR = 0.20
QO_READ_BEFORE_FILTER_PENALTY_MAX    = 1.0

QO_BRIDGE_FANOUT_RATIO_FLOOR         = 4.0
QO_BRIDGE_FANOUT_PENALTY_FACTOR      = 0.15
QO_BRIDGE_FANOUT_PENALTY_MAX         = 2.0
```

The final lookup multiplier is:

```text
lookup_penalty = 1.0 + capped_component
```

and it is applied to repeated inner lookup CPU/IO cost.

### 7. Skew Uncertainty Penalty

When a small side joins a large side and the joined value is not known exactly during planning, using only average fanout can understate risk.

The model compares MCV frequency to average join selectivity:

```text
skew_ratio = large_mcv_frequency / avg_frequency
```

If:

```text
skew_ratio > QO_SKEW_UNCERTAINTY_RATIO_FLOOR
```

then:

```text
component += log10(skew_ratio) * QO_SKEW_UNCERTAINTY_PENALTY_FACTOR
```

Current constants:

```text
QO_SKEW_UNCERTAINTY_RATIO_FLOOR    = 4.0
QO_SKEW_UNCERTAINTY_PENALTY_FACTOR = 0.20
QO_SKEW_UNCERTAINTY_PENALTY_MAX    = 1.0
```

This remains a cost-side uncertainty penalty. It avoids forcing a single cardinality estimate when the exact joined value is unknown.

## Why This Is CBO-Oriented

The changes are model-based:

- They use selectivity, cardinality, NDV-like frequency, MCV frequency, residual predicate cost, and fanout.
- They do not match query text, table names, or JOB query IDs.
- Thresholds are expressed as cost-model guard rails.
- Corrections are capped to reduce plan instability.

The model distinguishes:

- Cheap scalar comparisons vs expensive pattern predicates.
- Output cardinality vs rows read before residual filtering.
- Average join fanout vs MCV/skew-risk fanout.
- Hot-key skew vs cold absolute fanout.

## Review Notes

Key review points:

- The MCV guard is limited to equality joins with exactly one small side.
- Broad joins are skipped using `QO_MCV_GUARD_MAX_BASE_SELECTIVITY`.
- Cold fanout is activated only when absolute expected fanout crosses `QO_MCV_GUARD_MIN_RISK_FANOUT`.
- Residual filter penalties affect cost, not selectivity.
- Sequential scan filter CPU cost is isolated from base sequential scan I/O cost.

Potential follow-up work:

- Add value-specific join fanout estimation when a small dimension value is known.
- Track multi-column or equivalence-class fanout more explicitly.
- Add planner debug counters showing when each guard/penalty fires.
- Calibrate constants using a broader JOB benchmark run.

## Suggested PR Body

### Summary

- Improve selectivity and cost estimation for skewed equality joins, cold fanout joins, delayed residual filters, and complex string predicates.
- Add bounded MCV/fanout guard rails to reduce cost inversion in complex JOB-style join orders.
- Refactor selectivity logic into dedicated optimizer modules and improve JOB benchmark timing stability.

### Technical Details

The previous cost model often relied on average equality selectivity. This is too optimistic when a small filtered prefix joins a large fact or bridge table through a key with skew or high absolute fanout.

This PR adds an MCV-based risk estimate:

```text
risk_fanout = large_card * max_mcv_frequency
risk_card   = small_card * risk_fanout
risk_sel    = risk_card / base_cardinality
```

The adjusted selectivity is bounded by configurable multipliers. A separate cold-fanout path handles cases where global MCV frequency is below the hot-key threshold but the absolute fanout remains large.

The PR also improves cost modeling for residual predicates:

```text
filter_cpu_cost = rows_evaluated * QO_CPU_WEIGHT * predicate_weight
```

and adds bounded penalties for repeated nested-loop lookups where rows are read before residual filters can discard them.

### Test Plan

- Run targeted JOB queries that previously showed cost inversion.
- Compare optimizer-selected plans against known faster `LEADING(...)` variants.
- Run the JOB benchmark script with multiple iterations in a single `csql` session.
- Verify that plans with complex residual filters and bridge/fact fanout are no longer systematically under-costed.

