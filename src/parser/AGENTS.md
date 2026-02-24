# src/parser/ — SQL Parser & XASL Generator

## OVERVIEW

SQL text → parse tree → semantic check → XASL IR. 38 files. Heavy code generation and type checking.

## KEY FILES

| File | Lines | Purpose |
|------|-------|---------|
| `xasl_generation.c` | 28k | Parse tree → XASL translation |
| `type_checking.c` | 23k | Type coercion, operator resolution |
| `parse_tree_cl.c` | 20k | Parse tree manipulation |
| `semantic_check.c` | 17k | Semantic validation |
| `view_transform.c` | 14k | View expansion/rewrite (owner: @shparkcubrid) |
| `name_resolution.c` | 12k | Identifier resolution |
| `parser_support.c` | 12k | Parser utilities |

## CODEOWNERS

- `view_transform.*` → @shparkcubrid
- Everything else → @beyondykk9

## NOTES

- Parser is generated (yacc/lex) — do not hand-edit generated files
- XASL is the IR consumed by `query/query_executor.c`
