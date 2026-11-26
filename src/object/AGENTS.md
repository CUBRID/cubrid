# src/object/ — Schema, Catalog, Auth, Information Schema

Schema management, system catalog, authorization, triggers, the client workspace, and the domain/type engine.

## Key Files

| File | Role |
|------|------|
| `schema_manager.c` | Class/attribute/index create, alter, drop |
| `schema_template.c` | Schema edit templates (`smt_*`) |
| `schema_system_catalog_install.cpp` | System catalog tables and system views (`db_*`) |
| `schema_system_catalog_install_query_spec.cpp` | System view query specs |
| `schema_system_catalog_constants.h` | Catalog table names (`CT_CLASS_NAME`, ...) |
| `schema_information_schema_install.cpp` | Information schema views |
| `schema_information_schema_install_query_spec.cpp` | Information schema query specs |
| `schema_system_metadata_definition.hpp` | Definition structs shared by the catalog and information schema |
| `schema_system_metadata_builder.cpp` | Creates and rebuilds system classes and views from their definitions |
| `schema_system_metadata_query_spec.hpp` | Query spec formatting rules and the `AUTH_CHECK_*` macros |
| `system_metadata_version.h` | `SYSTEM_METADATA_VERSION`, stored in the log header |
| `authenticate*.c/.cpp` | Users, passwords, grants, privilege checks |
| `trigger_manager.c` | Triggers |
| `object_domain.c` | Domains and value coercion/cast (`tp_value_coerce`, `tp_value_cast`) |
| `object_primitive.c` | Per-type primitive operations (compare, copy, size, `pr_clear_value`) |
| `class_object.c`, `object_accessor.c`, `object_print.c` | In-memory classes, attribute get/set, schema printing |
| `schema_class_truncator.cpp` | TRUNCATE |
| `work_space.c`, `quick_fit.c` | Client object cache (workspace) and its allocator |
| `transform.c`, `transform_cl.c` | Meta-class definitions for class storage and catalog entries; storage of instances and classes |

## Where to Look

| Task | File |
|------|------|
| Catalog table or column | `schema_system_catalog_install.cpp` + `schema_system_catalog_constants.h` |
| System view | `schema_system_catalog_install.cpp` + `schema_system_catalog_install_query_spec.cpp` |
| Information schema view | `schema_information_schema_install.cpp` + `schema_information_schema_install_query_spec.cpp` |
| Auth / privilege | `authenticate*.c/.cpp` |
| Trigger | `trigger_manager.c` |
| Schema DDL | `schema_manager.c`, `schema_template.c` |
| Type conversion | `object_domain.c` |
| Object cache | `work_space.c` |

## Rules

- **Query specs**: follow the numbered rules at the top of `schema_system_metadata_query_spec.hpp` exactly. That header is the only source; read it before you edit a query spec. Use its `AUTH_CHECK_USER ()` / `AUTH_CHECK_PRIVILEGE ()` macros for privilege filters.
- **System metadata version**: a change to a catalog table, system view, or information schema view can make existing databases need `cubrid upgradedb`. `SYSTEM_METADATA_VERSION` (`system_metadata_version.h`) is stored in the log header and, per its header comment, is incremented on release. `upgradedb` runs versioned scripts up to it (`upgradedb_process_versioned_scripts ()` in `src/executables/util_sa.c`). State the metadata change in the PR and agree the versioning with the release owners.
- **Schema change pattern**: `smt_edit_class_mop ()` → modify the template → `sm_update_class ()`.
- **Serial limits**: values are limited to ±(10^38 − 1) (`DB_SERIAL_MAX` / `DB_SERIAL_MIN` in `src/query/execute_statement.c`). Older text, such as the user manual, gives 10^36 / 10^37; the code wins.

## System Catalog Tables

| Constant | Table | Content |
|----------|-------|---------|
| `CT_CLASS_NAME` | `_db_class` | Tables, views, system tables |
| `CT_ATTRIBUTE_NAME` | `_db_attribute` | Columns |
| `CT_DOMAIN_NAME` | `_db_domain` | Data types |
| `CT_INDEX_NAME` | `_db_index` | Indexes |
| `CT_SERIAL_NAME` | `_db_serial` | Serials, including AUTO_INCREMENT serials |
| `CT_CLASSAUTH_NAME` | `_db_auth` | Grants |
| `AU_USER_CLASS_NAME` | `_db_user` | Users |

Class type constants: `SM_CLASS_CT` (table), `SM_VCLASS_CT` (view). Prefixes: `sm_` (schema), `au_` (auth), `tr_` (triggers), `ws_` (workspace).
