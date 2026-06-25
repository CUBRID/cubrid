# Storage Foundations

This reference covers shared storage identifiers and small helper modules used by the larger managers. Read it when a
change touches object/page/file identity, record descriptors, OID helpers, or low-level byte/layout assumptions.

## Files

| File | Role |
|------|------|
| `storage_common.h/c` | Common page/file/object identifiers, page types, `RECDES`, record data helpers |
| `oid.c/h` | Well-known system OIDs, OID comparison, class-OID cache helpers |
| `vpid.hpp`, `vpid_utilities.hpp` | C++ helpers for `VPID` comparison, hashing, and formatting |
| `record_descriptor.cpp/hpp` | C++ wrapper around `RECDES` with owned/borrowed buffer handling |
| `byte_order.c/h` | Disk byte-order conversion helpers |

## Core Types

| Type | Defined In | Notes |
|------|------------|-------|
| `VPID` | `dbtype_def.h`, used here | Physical page address: `volid`, `pageid` |
| `VFID` | `dbtype_def.h`, file manager | Logical file identity: `volid`, `fileid` |
| `HFID` | `storage_common.h` | Heap file identity: `VFID` plus heap header page |
| `BTID` | `storage_common.h` | B-tree identity: `VFID` plus root page |
| `EHID` | `storage_common.h` | Extendible hash identity: `VFID` plus directory page |
| `OID` | `dbtype_def.h`, `oid.h` | Object identity: `volid`, `pageid`, `slotid` |
| `RECDES` | `storage_common.h` | Record descriptor: buffer capacity, length, record type, data pointer |
| `PAGE_PTR` | `storage_common.h` | Buffer-pool page pointer (`char *`) |

## Record Descriptors

`RECDES` is the common record transport for heap, B-tree, catalog, overflow, and slotted-page code.

- `area_size` is the capacity of `data`; it can be negative when the descriptor points into another buffer, for example
  a peeked slotted-page record.
- `length` is the payload length, not including record header/type metadata.
- `type` carries record kind (`REC_HOME`, relocation records, overflow records, etc.).
- `recdes_allocate_data_area()`, `recdes_free_data_area()`, and `recdes_set_data_area()` in `storage_common.c` manage
  basic buffers.
- `record_descriptor` adds safer C++ buffer ownership and mutation helpers. Use it where surrounding code already does;
  do not convert broad C paths just to get RAII.

## OID Helpers

Use `oid.h` helpers rather than open-coding comparisons or system OID checks.

- `oid_compare()` / `oid_compare_equals()` are the comparison entry points.
- `oid_Root_class_oid`, `oid_Serial_class_oid`, and related globals identify core system classes.
- `oid_set_cached_class_oid()`, `oid_check_cached_class_oid()`, and `oid_is_cached_class_oid()` manage class-OID cache
  shortcuts.

## Page And File Identity

- `VPID` locates a physical page, but page access still goes through `pgbuf_*`.
- `VFID` locates a logical file table entry. File allocation/deallocation belongs to `file_manager.c`.
- `HFID` and `BTID` add subsystem-specific root/header pages on top of `VFID`.
- `PAGE_TYPE` in `storage_common.h` is used for validation/debugging and must match subsystem page initialization.

## Navigation Anchors

- `storage_common.h`: `PAGE_TYPE`, `HFID`, `BTID`, `EHID`, `RECDES`.
- `storage_common.c`: `recdes_allocate_data_area()`, `recdes_free_data_area()`, `oid_to_string()`.
- `oid.h`: system OID globals and cache helpers.
- `record_descriptor.cpp`: `record_descriptor::get()`, `copy()`, `peek()`, `modify_data()`, `release_buffer()`.

## Gotchas

- Do not treat `OID` as just a heap address in generic code; system classes, catalog objects, and cache helpers attach
  meaning to specific OIDs.
- Do not mutate `RECDES::data` unless ownership and capacity are clear. Peeked descriptors may point into a page frame.
- Changing identifier layout or serialized sizes is a disk/network compatibility issue, not a local refactor.
