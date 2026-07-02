# CBRD-27006 Read-Path Follow-up Plan

This handoff is for a new session continuing after the small Greptile cleanup
commit on `CBRD-27006-oos-recdes-locality`.

## Context to Read First

1. `/home/vimkim/.agents/skills/cubrid-oos-context/SKILL.md`
2. `/home/vimkim/gh/cubrid-oos-context/OOS-CONTEXT.md`
3. `docs/plans/CBRD-27006-oos-recdes-locality.md`
4. `docs/adr/0001-oos-insert-publication-owned-by-api.md`
5. The current code around:
   - `src/storage/heap_file.c`: `heap_attrvalue_read_oos_inline`,
     `heap_attrvalue_point_variable`, `heap_attrvalue_read`,
     `heap_attrvalue_prepare_batched_oos_read`,
     `heap_attrinfo_read_dbvalues_batched_oos`,
     `heap_attrinfo_read_dbvalues`,
     `heap_attrinfo_read_dbvalues_without_oid`
   - `src/storage/oos_file.cpp`: `oos_read`, `oos_read_within_page`,
     `oos_read_head_from_fixed_page`, `oos_read_many`

Validate the worktree:

```bash
bash /home/vimkim/.agents/skills/cubrid-oos-context/scripts/validate-env.sh "$PWD"
```

## Why This Follow-up Exists

The PR already improves page locality by batching OOS reads with
`oos_read_many()`, but the review reports found three read-path trade-offs:

1. `heap_attrinfo_read_dbvalues()` enters the batched wrapper whenever the
   record has `HAS_OOS`, even if the requested attributes do not include an OOS
   value. This causes per-row vector allocation and a prepare pass for
   non-OOS-only projections.
2. A single requested OOS value gets routed through the batched path, losing the
   scalar path's `IO_MAX_PAGE_SIZE` stack scratch fast path. For one OOS value,
   there is no page-fix batching benefit.
3. Inline OOS header parsing and OOS head-page validation are duplicated across
   scalar and batched paths. This is mostly maintainability risk, not an
   immediate correctness blocker.

## Goal

Make lazy OOS Resolve batching pay only when it has a real batching opportunity.

Required outcome:

- Non-OOS-only projections on a record that has unrelated OOS columns should
  use the existing scalar attribute-read loop.
- Exactly one requested OOS value should use the existing scalar path, preserving
  the stack scratch fast path for values up to `IO_MAX_PAGE_SIZE`.
- Two or more requested OOS values should still use `oos_read_many()`.
- Correctness and requested-column behavior must remain unchanged.

Non-goals for this follow-up:

- Do not change the OOS inline format.
- Do not change `oos_read_many()` public API.
- Do not change replication behavior.
- Do not solve the full peak-memory trade-off by streaming DB_VALUE conversion;
  keep that as a later optimization unless the small gating change forces it.
- Do not refactor all heap attribute parsing unless the small gating helper
  becomes more complex than the code it replaces.

## Suggested Implementation

### Step 1: Extract a Small Probe Helper

Add a helper in `heap_file.c` near `heap_attrvalue_prepare_batched_oos_read()`.
It should inspect one requested `HEAP_ATTRVALUE` and answer whether that
requested attribute is an inline-OOS variable attribute in this `recdes`.

Suggested shape:

```c
static int
heap_attrvalue_probe_oos_inline (RECDES * recdes, HEAP_ATTRVALUE * value,
                                 HEAP_CACHE_ATTRINFO * attr_info,
                                 OR_ATTRIBUTE **attrepr_out,
                                 OID *oos_oid_out,
                                 DB_BIGINT *oos_len_out,
                                 bool *is_oos_out);
```

Rules:

- Initialize all out parameters on entry.
- Return `NO_ERROR` for non-OOS requested values with `*is_oos_out = false`.
- Preserve the current skip behavior for duplicate-key attrs, shared/class attrs,
  fixed attrs, NULL variable attrs, missing `recdes`, and missing
  `read_attrepr`.
- Use the same offset-size switch and inline header validation currently in
  `heap_attrvalue_prepare_batched_oos_read()`.
- On corrupt inline OOS headers, return the existing error code and set the
  existing error stack.

Then rewrite `heap_attrvalue_prepare_batched_oos_read()` to call this helper.
For OOS values, it should allocate the destination buffer and fill
`oos_read_request` as it does today. For non-OOS values, it should only leave the
prepared slot initialized for the later scalar read.

### Step 2: Gate the Batched Wrapper

Before calling `heap_attrinfo_read_dbvalues_batched_oos()` from
`heap_attrinfo_read_dbvalues()` and
`heap_attrinfo_read_dbvalues_without_oid()`, count requested OOS attributes.

Suggested helper:

```c
static int
heap_attrinfo_count_requested_oos_values (RECDES * recdes,
                                          HEAP_CACHE_ATTRINFO * attr_info,
                                          int *count_out);
```

This helper can use the probe helper above and should stop early after count
reaches 2.

Dispatch rule:

```c
if (recdes != NULL && recdes->data != NULL && heap_recdes_contains_oos (recdes))
  {
    int requested_oos_count = 0;
    ret = heap_attrinfo_count_requested_oos_values (recdes, attr_info, &requested_oos_count);
    if (ret != NO_ERROR)
      {
        goto exit_on_error;
      }

    if (requested_oos_count >= 2)
      {
        ret = heap_attrinfo_read_dbvalues_batched_oos (thread_p, recdes, attr_info);
      }
    else
      {
        ret = heap_attrinfo_read_dbvalues_scalar_loop (recdes, attr_info);
      }
  }
```

Do not duplicate the scalar loop twice. Add a tiny static helper for the existing
loop body and use it from both `heap_attrinfo_read_dbvalues()` and
`heap_attrinfo_read_dbvalues_without_oid()`.

### Step 3: Keep the Batched Path Simple

Do not add a second pre-scan inside `heap_attrinfo_read_dbvalues_batched_oos()`
unless needed. The dispatch helper already guarantees there are at least two
requested OOS values.

Still keep the batched function robust:

- It should tolerate non-OOS requested values and read them with
  `heap_attrvalue_read()`.
- It should free all allocated raw buffers on every error path.
- It should keep `oos_read_many()` request order as requested-attribute order.

### Step 4: Optional Small Deduplication

If Step 1 is clean, use the probe helper to remove the duplicated inline OOS
header parsing inside `heap_attrvalue_prepare_batched_oos_read()`.

Do not refactor `heap_attrvalue_point_variable()` in the same patch unless it is
straightforward. The scalar path also needs non-OOS raw pointer/length behavior,
so forcing it through the same helper can easily widen the change.

For `oos_file.cpp`, leave `oos_read_head_from_fixed_page()` vs.
`oos_read_within_page()` deduplication as a separate follow-up unless a reviewer
explicitly asks for it. It is maintainability cleanup, not needed for the
dispatch/performance fix.

## Tests

Prefer narrow checks first:

```bash
just test unit oos
```

If the local just target differs, inspect `justfile` and use the existing OOS
unit-test target.

Add or adjust tests to prove the dispatch rules:

- A record with OOS columns, selecting only non-OOS requested attributes, should
  not increment `read_many_calls`.
- A record with exactly one requested OOS value should not increment
  `read_many_calls` and should still return the correct value.
- A record with two requested OOS values on the same OOS page should increment
  `read_many_calls` and keep the grouped-read behavior already covered by the
  PR.
- Existing SQL CRUD tests for partial-column selects must still pass.

Use existing `oos_debug_counters` bridge helpers in
`unit_tests/oos/test_oos_server.cpp` where possible. If a pure server-unit test
cannot reach the lazy attribute path cleanly, add a SQL CRUD test for correctness
and document the counter gap in the final summary.

## Review Response After Implementation

Suggested response text:

> I changed lazy Resolve dispatch so batching is used only when at least two
> requested attributes are OOS values. Non-OOS-only projections on an OOS record
> now stay on the scalar loop, and single-OOS projections keep the scalar
> stack-scratch fast path. Multi-OOS projections continue through
> `oos_read_many()`.

Also mention any tests run and whether the helper refactor was kept narrow or
deferred.
