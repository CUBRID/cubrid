# src/parser/ — SQL Parsing & Analysis

Client-side only (`#if !defined(SERVER_MODE)`).

## Key Files

| File | Size | Role |
|------|------|------|
| `csql_grammar.y` | 646KB | Bison grammar — all SQL syntax defined here |
| `csql_lexer.l` | — | Flex lexer — tokenization |
| `parse_tree.h` | — | `PT_NODE` definition, all `PT_*` enums, macros |
| `parse_tree_cl.c` | — | Parse tree construction, `parser_create_parser()`, `parser_new_node()` |
| `name_resolution.c` | — | Identifier → schema object resolution |
| `semantic_check.c` | — | Type checking, validation, semantic analysis |
| `type_checking.c` | — | Expression type inference, function signatures |
| `xasl_generation.c` | — | PT_NODE tree → XASL_NODE tree conversion |
| `view_transform.c` | — | View expansion and rewriting |
| `compile.c` | — | Statement compilation orchestration |
| `show_meta.c` | — | `SHOW` statement metadata |
| `method_transform.c` | — | Method call transformation |
| `double_byte_support.c` | — | Multi-byte character handling in lexer |

## Pipeline (this module's portion)

```
SQL text → csql_lexer.l → csql_grammar.y → PT_NODE tree
  → name_resolution.c → semantic_check.c → type_checking.c
  → xasl_generation.c → XASL_NODE (handed to query/ for execution)
```

## Where to Look

| Task | File |
|------|------|
| Add SQL keyword/syntax | `csql_grammar.y` (careful — 646KB, bison regen is slow) |
| Add built-in function | `csql_grammar.y` → `type_checking.c` → `xasl_generation.c` → `query/` |
| Fix name resolution | `name_resolution.c` |
| Fix type mismatch | `type_checking.c` |
| Fix view rewriting | `view_transform.c` |
| Add SHOW variant | `show_meta.c` |

## Core API

```c
PARSER_CONTEXT *parser = parser_create_parser ();
PT_NODE *node = parser_new_node (parser, PT_SELECT);
list = parser_append_node (new_node, list);
copy = parser_copy_tree (parser, original);
parser_walk_tree (parser, node, pre_func, pre_arg, post_func, post_arg);
parser_free_tree (parser, tree);
parser_free_parser (parser);
```

## PT_NODE Structure

Union-based linked list. Key fields:
- `node_type`: `PT_SELECT`, `PT_NAME`, `PT_SPEC`, `PT_EXPR`, `PT_VALUE`, etc.
- `info`: Union — access via `node->info.select`, `node->info.name`, etc.
- `next`: Sibling list pointer
- `data_type`: Type info node
- `line_number`, `column_number`: Source location

## Conventions

- Memory: `parser_alloc(parser, size)` — freed when parser is destroyed, never manually
- String duplication: `pt_append_string()` or `parser_alloc` + copy
- Tree walking: always use `parser_walk_tree()` with pre/post callbacks, not manual recursion
- All identifier comparison: `intl_identifier_casecmp()` (case-insensitive)

## Gotchas

- `csql_grammar.y` at 646KB is one of the largest bison grammars — edits need extreme care
- Parser runs **client-side** — no `THREAD_ENTRY *` available, no server-side APIs
- Bison shift/reduce conflicts: check `y.output` after grammar changes
- `PT_NODE` unions: accessing wrong `info` member = undefined behavior
