# CBRD-24094 — Skewed-index DELETE/INSERT slowdown: overflow OID directory design

**Approach C: keep the per-key overflow OID chain globally OID-sorted and add a
page-routing directory so OID lookups become O(log P) instead of O(P) page fixes.**

Status: design finalized + invariants verified against 11.4 source. Implementation
not yet landed (see §7 risk/scope). Branch: `cbrd_24094` (from upstream/develop).

---

## 1. Problem (reproduced on server 11, 11.4.1, 10M rows)

A non-unique index on a highly duplicated column stores one key with a huge OID
list spread over a **singly-linked chain of overflow pages**
(`BTREE_OVERFLOW_HEADER = { VPID next_vpid }`, 8 bytes).

Measured `dba.tbl2(col6)` where every row has `col6=1`: **1 distinct key,
14,728 overflow pages**, `Max_num_ovf_page_a_key=14728`.

| op | observed | cause |
|----|----------|-------|
| DELETE 1001 rows by pk | 0.017 → 0.198 → 1.359 → **3.104 s** (grows with pk) | `btree_find_oid_and_its_page()` scans the chain head→tail linearly per row |
| INSERT 961 rows (full chain) | **1.916 s** | `btree_find_free_overflow_oids_page()` first-fit linear scan |
| same, index dropped | 0.011 s / 0.012 s | no chain |
| DEDUPLICATE=10 (bucketed) | 0.021–0.025 s | shorter per-key chains — empirical proof the mechanism is chain length |

All timings had **zero disk I/O** — pure CPU/page-fix cost of chain traversal.
Cost is linear in the target OID's position in the chain.

## 2. Verified invariants (from source, 11.4)

These were confirmed by reading the code and gate the whole design:

1. **Overflow pages are OID-sorted *within* a page** (`btree_find_oid_from_ovfl`
   does binary search; `btree_insert_object_ordered_by_oid` keeps insert order).
2. **Across pages there is no global order.** New pages prepend at the chain head
   (`btree_start_overflow_page`: "inserted after leaf and before other existing
   overflow pages"); runtime inserts use first-fit
   (`btree_find_free_overflow_oids_page`). The loader (`btree_load.c`,
   `bt_load_nospace_for_new_oid`) tail-appends in ascending OID, so a freshly
   built chain *is* globally sorted — but the first runtime insert breaks that.
3. **Leaf-record objects are NOT OID-sorted** for non-unique keys
   (`btree_key_append_object_non_unique` calls `btree_record_append_object` =
   append at end). The leaf is a single page, searched separately and cheaply, so
   the directory concerns the **overflow chain only** — the leaf is untouched.
4. **UNIQUE keys use a different removal path**
   (`btree_key_remove_object_and_keep_visible_first`, asserts `BTREE_IS_UNIQUE`,
   purpose `BTREE_OP_DELETE_UNDO_INSERT_UNQ_MULTIUPD`). Their chains are short
   (version lists). → **restrict the new format to non-unique indexes.**
5. **The leaf-first-object swap on delete preserves global order.**
   `btree_replace_first_oid_with_ovfl_oid` pulls the *last object of the first
   overflow page* into the leaf and removes it from overflow. Removal never
   inserts an out-of-range object, so a globally-sorted chain stays sorted.
6. The delete path **removes emptied overflow pages and relinks the chain**
   (`btree_overflow_remove_object` → `btree_modify_leaf_ovfl_vpid` /
   `btree_modify_overflow_link`). So the reproduction (CREATE INDEX → DELETE)
   *shrinks* the chain: a directory **must be maintained on delete** — it cannot
   be a load-time-only, read-only add-on.

## 3. Design

Maintain the overflow chain **globally OID-sorted across pages** (it already is
sorted within a page; only the insert-placement rule changes), and add a
**separator directory** for O(log P) page routing. Non-unique indexes only.

### 3.1 Separator invariant (the key idea)

The directory holds one entry `{ OID sep; VPID data_vpid }` per data page,
ascending by `sep`, where `sep_i` = the OID at page *i*'s creation (its min).
Page *i* owns exactly the OID range `[sep_i, sep_{i+1})`; page 0 is the catch-all
for anything `< sep_1`.

Why this is robust: objects are only ever inserted into the page whose range
contains them, and deletions only remove objects — **an object never leaves its
page's range.** Therefore fixed separators route correctly for the entire life of
the chain. The directory changes **only** on:
- **page split** → add one entry, and
- **empty-page removal** → delete one entry.

Both already occur inside existing system operations. Tail appends and ordinary
deletions need **no** directory update.

### 3.2 Storage layout

- `leaf_rec.ovfl` keeps pointing at the **first data page** → all sequential
  traversal (range scan, capacity, `first_visible`, vacuum full-scan) is unchanged.
- **Data-page header** carries `dir_vpid` after `next_vpid`. On-disk detection is
  by header-record length: 8 B = legacy (no directory), 16 B = v2. `next_vpid`
  stays at offset 0, so `btree_get_next_overflow_vpid` and every sequential walk
  are byte-compatible. `dir_vpid` is written once when the directory is born and
  never moves (see §3.4), so every data page can carry the same value with no
  fan-out updates.
- **Directory page**: a `PAGE_BTREE` page; HEADER slot = `{ VPID next_dir_vpid }`,
  slot 1 = a packed, sorted `{ OID sep; VPID vpid }[]` (16 B/entry → ~1000
  entries per 16 KB page; the 14,728-page chain needs ~15 directory pages =
  2 levels of fan-out at most).

### 3.3 Algorithms

- **Search** (`btree_find_oid_and_its_page`, new branch): read first data page
  header; if 8 B (legacy) → existing linear scan unchanged. Else walk the
  directory (≤2 pages) binary-searching separators to the target data page, fix
  it, binary-search within (existing `btree_find_oid_from_ovfl`). For delete
  callers needing `prev_page`, fix `entry[i-1].vpid` first to keep latch order.
- **Insert** (`btree_key_append_object_into_ovf`, new branch): route to page P.
  Space → `btree_insert_object_ordered_by_oid` (existing). Full → **split** upper
  half into new page Q, link P→Q, add `(Q.min, Q)` to directory, insert into P or
  Q by comparing to Q.min. Rightmost-ascending appends degenerate to
  append-new-tail-page (loader-like fill).
- **Delete/vacuum** (`btree_overflow_remove_object`, extended): object removal
  unchanged. When a page empties and is removed, also delete its directory entry
  (and unlink+dealloc an emptied directory page).

### 3.4 Directory birth/death

Born when the chain grows 1→2 data pages: allocate the directory head, write both
separators, stamp `dir_vpid` into data-page headers. To keep `dir_vpid` immutable,
the directory grows at the **tail** and a head split keeps the head vpid (lower
half stays, upper half moves to a new appended page). Torn down when the chain
shrinks to 1 data page (dealloc directory, `dir_vpid = NULL`).

### 3.5 Recovery / concurrency (why this is tractable)

- Every page mutation reuses the **generic `RVBT_RECORD_MODIFY_UNDOREDO`** replay
  (`btree_rv_record_modify_internal`) plus `file_alloc`/`file_dealloc` logging —
  **no new recovery handlers.** Split / page-removal / directory birth-death each
  run inside the **existing insert_helper / delete_helper system operation**, so a
  crash rolls the whole structural change back atomically.
- All structural changes to one key's chain already happen under that key's **leaf
  write latch**, so there are no concurrent writers to the chain or directory.
  Sequential readers only follow `next_vpid` and never read the directory →
  unchanged.
- `btree_modify_overflow_link` must become read-modify-write (update `next_vpid`,
  preserve any trailing `dir_vpid`) so it never silently shrinks a v2 header.

### 3.6 Compatibility

Existing indexes stay legacy (linear) until rebuilt; new/rebuilt non-unique
indexes are v2. Old servers reading a v2 page still see `next_vpid` at offset 0,
so cross-version sequential traversal works (a downgrade that rewrites the header
8 B would leak directory pages — not corruption).

## 4. Expected effect (P = 14,728)

| op | now | with directory |
|----|-----|-----------------|
| DELETE per-row lookup | ≤ 14,728 fixes | ≤ ~2 dir + 1–2 data fixes |
| INSERT free-space lookup | ≤ 14,728 fixes | ≤ ~2 dir + 1 data fixes |
| vacuum per-row lookup | O(P) | O(log P) |
| range scan | unchanged | unchanged |
| space | — | +8 B/data page, ~1 dir page per ~1000 data pages |

## 5. Files / estimated size

- `src/storage/btree_load.h` — v2 header offset macros, directory entry struct,
  accessors (~40 lines).
- `src/storage/btree.c` — format detect, directory search/insert/delete,
  ordered-insert routing, split, unlink extension, `find` branch,
  `btree_modify_overflow_link` read-modify-write (~500–600 lines).
- `src/storage/btree_load.c` — build directory at key end for v2 chains (~120 lines).

## 6. Test plan

1. `cubrid checkdb` after each of: bulk load, targeted deletes, full-table delete,
   vacuum, random interleaved insert/delete.
2. Reproduction A/B: DELETE at pk 3k/900k/9M and full-chain INSERT — must be flat
   and match the index-dropped baseline within noise.
3. Recovery smoke: kill server mid-DELETE, restart, `checkdb` + row counts.
4. Concurrency: N sessions deleting/inserting the same skewed key.
5. Regression: existing btree SQL/shell suites.

## 7. Risk & scope (honest assessment)

This changes a core on-disk B-tree structure *and* the delete WAL path — the two
are intrinsically coupled (§2.6), which is why the 2021 attempt concluded there
was "no simple way." The design above resolves the hard parts (fixed-separator
routing that survives deletion, header-length format detection, recovery by
reusing the generic undoredo handler under existing sysops), but a landed patch
must clear `checkdb` and crash-recovery testing before it can be trusted. That
verification bar is why the implementation is staged rather than asserted done
here. DEDUPLICATE (CBRD-24478) remains the orthogonal, already-shipped mitigation
(bucketing, capped at level 14); this directory approach removes the cap and is
complementary.
