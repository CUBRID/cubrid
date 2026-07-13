# PR #7382 Review Feedback Plan

## Goal

Make `SHOW HEAP OOS` report reusable OOS data-page space instead of treating file and page metadata as unused space, and add a compact page free-space distribution for diagnostics.

## Metric definitions

- Keep `Oos_physical_bytes` as the full OOS file footprint: `Oos_num_user_pages * Oos_page_size`. This intentionally includes the OOS header page and page metadata.
- Replace `Oos_unused_bytes` with `Oos_free_bytes`, calculated as the sum of `SPAGE_HEADER.total_free` for the OOS data pages that the statistics scan can latch.
- Report `Oos_num_pages_skipped` because the existing diagnostic scan uses conditional read latches and may skip a busy page.
- Classify every scanned, nonempty OOS data page into one mutually exclusive free-space range, using the usable data-page capacity (`DB_PAGESIZE - SPAGE_HEADER_SIZE`) as the denominator:
  - `Oos_num_pages_free_0_25`
  - `Oos_num_pages_free_25_50`
  - `Oos_num_pages_free_50_75`
  - `Oos_num_pages_free_75_100`
- Count pages with no OOS chunk records separately as `Oos_num_empty_pages`. An empty anchored page can retain slot metadata, so emptiness is determined by its record count rather than by requiring a literal 100% free ratio.

## Implementation

1. Extend `OOS_STATS_INFO` with free bytes, the four range counters, empty pages, and skipped pages.
2. Collect the new values during the existing `oos_get_stats_by_vfid()` page walk.
3. Update `SHOW HEAP OOS` metadata and row construction. Keep derived calculations inside the block where an OOS VFID exists.
4. Update the SQL tests for the no-OOS case and for a deterministic one-data-page OOS case.
5. Record the final column semantics and verification result in this plan.

## Verification

- Run `direnv exec . just build` after the code change.
- Run the focused `test_oos_sql_show` test through CTest.
- Run `direnv exec . just ctest` for the configured unit-test suite.
- Review the final diff and stage only files belonging to this feedback change.

## Delivery

- Commit with a CBRD-26972 message and push `CBRD-26972-oos-show-heap-oos`.
- Reply to both original review comments with the implemented behavior and verification result, using simple language.

## Execution result

- `direnv exec . just build`: passed.
- Focused `test_oos_sql_show`: 4 tests passed.
- `direnv exec . just ctest`: all 24 configured tests passed.
