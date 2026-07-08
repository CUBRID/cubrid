heap_attrvalue_read_oos_inline ()

 /* Cases 1-3: corrupted inline header; raw->data points at the inline region here. */
  error = heap_oos_parse_inline_ref (recdes, raw->data, &oos_oid, &oos_len);
  if (error != NO_ERROR)

This comment is awkward.

Reply:

- Agreed. The "Cases 1-3" phrasing was test-contract language leaking into production comments.
- Reworded the `heap_attrvalue_read_oos_inline()` local comment to describe the actual local invariant: `raw->data` still points at the heap record's OOS inline reference, so it must be parsed before attaching any scratch/heap buffer to `raw`.
- Removed the same "Cases 1-3 of the OOS inline-reference read contract" parenthetical from `heap_oos_parse_inline_ref()`.

---

heap_oos_attr_inline_ptr () - Probe whether a requested attribute is an inline-OOS variable
 *   attribute in this recdes.
 *
 *   return: pointer to the attribute's inline OOS reference, or NULL when it is not an inline-OOS
 *           value here (any condition the scalar read path either skips or reports itself,
 *           including a corrupt offset size).
 */
const char *
heap_oos_attr_inline_ptr


I don't understand wha inline-OOS variable is. What is it? function name seems ambiguous.

Reply:

- Agreed. "inline-OOS variable" is ambiguous.
- Renamed `heap_oos_attr_inline_ptr()` to `heap_oos_find_attr_inline_ref()`.
- Rewrote the comment to say it finds the 16-byte `[OOS OID | full length]` reference stored in the heap record's variable area for a requested OOS-marked variable attribute.
- Reworded grouped-read comments from "inline-OOS" to "grouped lazy OOS Resolve" / "OOS-marked attributes".

---

why is heap_attrvalue_transform_to_dbvalue () static int -> int?
Analyze static int -> int cases ( I see two)

Reply:

- Confirmed two formerly-static heap helpers were exported: `heap_attrvalue_transform_to_dbvalue()` and `heap_attrvalue_read()`.
- `heap_oos.cpp` needs `heap_attrvalue_transform_to_dbvalue()` after grouped `oos_read_many()` returns serialized OOS payloads; it must transform those payloads with the same scalar heap read contract instead of duplicating conversion logic.
- `heap_oos.cpp` needs `heap_attrvalue_read()` as the grouped Resolve fallback for non-OOS/default attributes in the same requested attribute set.
- Kept both implementations in `heap_file.c` and added header comments marking them as narrow exports for `heap_oos.cpp`, not general-purpose heap APIs.

---

heap_attrinfo_insert_to_oos () <- I think this function or portion of it can be migrated to heap_oos.cpp ?

Reply:

- Partially migrated it.
- Kept DB_VALUE-to-RECDES serialization in `heap_file.c` because it shares heap attribute conversion with the inline record writer, including the BLOB/CLOB ELO-locator copy step. OOS stores the serialized locator bytes for BLOB/CLOB columns, not the actual LOB payload.
- Moved the OOS-specific portion into `heap_oos_insert_serialized_values()` in `heap_oos.cpp`: class OOS VFID lookup, transaction descriptor validation, OOS insert publication-state reset, and `oos_insert_many()`.
- `heap_attrinfo_insert_to_oos()` now builds serialized `oos_insert_request` objects and delegates insertion to `heap_oos.cpp`.

---

Validate each and write fix plan.

Create plan with trackers.

Reply:

- [x] Reword awkward OOS inline-reference comments.
- [x] Rename and clarify `heap_oos_attr_inline_ptr()`.
- [x] Analyze the two `static int` to exported `int` cases and document their narrow export scope.
- [x] Move the OOS insertion side of `heap_attrinfo_insert_to_oos()` into `heap_oos.cpp`.
- [x] Clarify that BLOB/CLOB handling here means ELO-locator serialization, not OOS-owned LOB payload storage.
- [x] Verify locally with `just build`.
- [x] Verify locally with `just ctest`.
- [x] Verify locally with `just test-oos-sql`.
- [x] Verify formatting with `git diff --check`.

Note: `just test unit oos` was tried first but this worktree has no such recipe, so verification used the available OOS-relevant `ctest` and SQL test recipes.
