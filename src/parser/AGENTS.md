# src/parser/ — SQL Parsing & Analysis

Builds client-side only (`CS_MODE`/`SA_MODE`). `parse_tree.h`, `parser.h`, and `xasl_generation.h` raise `#error` in `SERVER_MODE`, so there is no `THREAD_ENTRY` and no server-only API here.

## Key Files

| File | Role |
|------|------|
| `csql_grammar.y` | Bison grammar (`%glr-parser`) for all SQL syntax. Very large: use symbol search |
| `csql_lexer.l` | Flex lexer: returns keyword tokens |
| `keyword.c` | `keywords[]` (reserved/unreserved keyword table) and `functions[]` (function name → `PT_*` op for `parser_keyword_func ()`) |
| `parse_tree.h` | `PT_NODE`, `PT_*` enums, macros |
| `parse_tree.c` | `parser_create_parser ()`, `parser_free_parser ()`, `pt_append_string ()` |
| `parse_tree_cl.c` | `parser_new_node ()`, `parser_append_node ()`, `parser_copy_tree ()`, `parser_walk_tree ()`, `parser_free_tree ()` |
| `name_resolution.c` | Identifier → schema object resolution |
| `semantic_check.c` | Semantic validation |
| `type_checking.c` | Expression type inference, function signatures |
| `view_transform.c` | View expansion; calls `mq_rewrite ()` (`src/optimizer/rewriter/`) |
| `xasl_generation.c` | `PT_NODE` → `XASL_NODE`; `pt_to_regu_variable ()` maps `PT_*` ops to `T_*` opcodes |
| `xasl_regu_alloc.cpp` | XASL node allocation; `regu_init ()` sets non-zero defaults |
| `compile.c` | Statement compilation orchestration |
| `show_meta.c` | `SHOW` statement metadata |

## Pipeline

```
SQL text → csql_lexer.l → csql_grammar.y → PT_NODE
  → name_resolution.c → semantic_check.c / type_checking.c → view_transform.c
  → xasl_generation.c (+ src/optimizer/) → XASL_NODE
```

## Where to Look

| Task | File |
|------|------|
| New reserved keyword | `%token` and rules in `csql_grammar.y`; a lexer rule in `csql_lexer.l`; an entry in `keywords[]` in `keyword.c` (third field nonzero = unreserved). An unreserved keyword must also be added to the `identifier` rule in the grammar |
| New built-in function called as `name (args)` | A `PT_*` op in `parse_tree.h` (printed by `pt_show_binopcode ()` and the other `PT_*` switches in `parse_tree_cl.c`); `functions[]` in `keyword.c` and its case in `parser_keyword_func ()` in `csql_grammar.y`; `type_checking.c`; `xasl_generation.c`; then the query side ([src/query/AGENTS.md](../query/AGENTS.md) "Adding an arithmetic opcode") |
| Name resolution | `name_resolution.c` |
| Type mismatch | `type_checking.c` |
| View rewriting | `view_transform.c` |
| SHOW variant | `show_meta.c` |

## Conventions

- Memory: `parser_alloc (parser, size)` lives as long as the parser context. Never free it by hand.
- Walk trees with `parser_walk_tree ()` (pre/post callbacks).
- Compare identifiers case-insensitively with the existing helpers: `intl_identifier_casecmp ()`, `pt_name_equal ()`, or `pt_str_compare (..., CASE_INSENSITIVE)`. Some sites compare with `CASE_SENSITIVE` on purpose; keep them.
- `PT_NODE.info` is a union. Access only the member that matches `node_type`.

## Grammar Gotchas

- Bison runs at build time with `-r all` on a copy rewritten by `cmake/upgrade_bison_grammar.cmake`. The conflict report is `csql_grammar.output` in the CMake build directory, next to the generated `csql_grammar.c`.
- The grammar is a GLR parser with no `%expect`, so it has conflicts on purpose. Compare the report before and after your change, and explain any new conflict.
