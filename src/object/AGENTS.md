# src/object/ — Schema, Catalog, Auth, Information Schema

Schema management, system catalog, authorization, triggers.

## Key Files

| File | Role |
|------|------|
| `schema_manager.c` | Class/attribute/index creation, modification, dropping |
| `schema_template.c` | Schema edit templates (transactional schema changes) |
| `schema_system_catalog_install.cpp` | System catalog table creation + information schema view definitions |
| `schema_system_catalog_constants.h` | Catalog table name constants (`CT_CLASS_NAME`, etc.) |
| `schema_system_catalog_install_query_spec.cpp` | Information schema SQL query generation |
| `authenticate.c` | User authentication, privilege checking |
| `trigger_manager.c` | Trigger creation, execution, event handling |
| `class_object.c` | In-memory class representation manipulation |
| `object_accessor.c` | Object attribute get/set |
| `object_print.c` | Object/schema display formatting |
| `object_primitive.c` | Primitive type operations (compare, copy, size) |
| `schema_class_truncator.cpp` | TRUNCATE TABLE implementation |
| `work_space.c` | Workspace (client-side object cache) management |
| `quick_fit.c` | Workspace memory allocator |
| `transform.c/cl.c` | Disk ↔ memory object transformation |

## Where to Look

| Task | File |
|------|------|
| Add catalog table/column | `schema_system_catalog_install.cpp` + `schema_system_catalog_constants.h` |
| Add info schema view | `schema_system_catalog_install.cpp` + `*_install_query_spec.cpp` |
| Fix auth/privilege | `authenticate.c` |
| Fix trigger | `trigger_manager.c` |
| Modify schema DDL | `schema_manager.c`, `schema_template.c` |
| Fix object cache | `work_space.c` |

## Information Schema Rules (STRICT)

Query specs in `schema_system_catalog_install_query_spec.cpp` have CI-enforced formatting:

1. Indent: 1 tab + 2 spaces for statements, 2 spaces for CASE
2. Lines end with space EXCEPT before `)` or after `(`
3. Space before `(` and after `)`, `{`, `}`
4. Line breaks after `SELECT`, `FROM`, `WHERE`, `ORDER BY` and before `AND`, `OR`
5. `WHEN` and `THEN` on one line if < 120 chars
6. Always use `AS` for aliases
7. Comment data type conversions: `CAST (x AS VARCHAR(255)) /* string -> varchar(255) */`
8. Comment format specifiers: `"[%s] AS [cls] " /* CT_CLASS_NAME */`
9. Use auth macros: `AUTH_CHECK_CLASS()`, `AUTH_CHECK_OWNER()`, `AUTH_CHECK_DBA`, `CURRENT_USER_GROUPS_SUBQUERY`

## System Catalog Tables

| Constant | Table | Content |
|----------|-------|---------|
| `CT_CLASS_NAME` | `_db_class` | Tables, views, system tables |
| `CT_ATTRIBUTE_NAME` | `_db_attribute` | Column definitions |
| `CT_DOMAIN_NAME` | `_db_domain` | Data type info |
| `CT_INDEX_NAME` | `_db_index` | Index metadata |
| `CT_SERIAL_NAME` | `_db_serial` | AUTO_INCREMENT sequences |
| `CT_CLASSAUTH_NAME` | `_db_auth` | Authorization grants |
| `AU_USER_CLASS_NAME` | `_db_user` | Database users/schemas |

## Conventions

- Functions prefixed `sm_` (schema manager), `au_` (auth), `tr_` (triggers), `ws_` (workspace)
- Schema changes use template pattern: `smt_edit_class_mop()` → modify → `sm_update_class()`
- Class type constants: `SM_CLASS_CT` (base table), `SM_VCLASS_CT` (view)
- SERIAL ranges: MINVALUE = -10^36, MAXVALUE = 10^37 (NOT ±10^38)

