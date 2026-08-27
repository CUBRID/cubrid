# patch_parser_tls.cmake - stamp CSQL_PARSER_TLS onto the mutable globals
# that bison/flex generate for the csql parser.
#
# The hand-written per-parse state in csql_grammar.y / csql_lexer.l carries
# the marker in source; the generator-owned state (bison's yylval/yylloc/
# yynerrs/yychar, flex's scanner bookkeeping) can only be marked after
# generation, which this script does in place.  See src/parser/csql_parser_tls.h.
#
# Every pattern must match exactly once; a mismatch fails the build loudly
# so a bison/flex version change can never ship a silently-shared global.
#
# usage: cmake -DPATCH_FILE=<generated file> -DPATCH_KIND=<grammar_c|grammar_h|lexer_c>
#              -P patch_parser_tls.cmake

if(NOT PATCH_FILE OR NOT PATCH_KIND)
  message(FATAL_ERROR "patch_parser_tls: PATCH_FILE and PATCH_KIND are required")
endif()

file(READ "${PATCH_FILE}" content)

# idempotence: a patched file is left alone (bison/flex rewrite it on regeneration)
string(FIND "${content}" "CSQL_PARSER_TLS_PATCHED" already)
if(NOT already EQUAL -1)
  return()
endif()

# stamp(<line-start literal>): prefix one column-0 declaration with the marker
macro(stamp pattern)
  string(FIND "${content}" "\n${pattern}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "patch_parser_tls(${PATCH_KIND}): pattern not found in ${PATCH_FILE}: '${pattern}' - generator output changed, update this script")
  endif()
  math(EXPR _next "${_pos} + 1")
  string(SUBSTRING "${content}" ${_next} -1 _rest)
  string(FIND "${_rest}" "\n${pattern}" _pos2)
  if(NOT _pos2 EQUAL -1)
    message(FATAL_ERROR "patch_parser_tls(${PATCH_KIND}): pattern not unique in ${PATCH_FILE}: '${pattern}' - generator output changed, update this script")
  endif()
  string(REPLACE "\n${pattern}" "\nCSQL_PARSER_TLS ${pattern}" content "${content}")
endmacro()

if(PATCH_KIND STREQUAL "grammar_c")
  stamp("YYSTYPE yylval;")
  stamp("YYLTYPE yylloc;")
  stamp("int yynerrs;")
  stamp("int yychar;")
elseif(PATCH_KIND STREQUAL "grammar_h")
  stamp("extern YYSTYPE csql_yylval;")
  stamp("extern YYLTYPE csql_yylloc;")
elseif(PATCH_KIND STREQUAL "lexer_c")
  stamp("extern int csql_yyleng;")
  stamp("extern FILE *csql_yyin, *csql_yyout;")
  stamp("static size_t yy_buffer_stack_top = 0;")
  stamp("static size_t yy_buffer_stack_max = 0;")
  stamp("static YY_BUFFER_STATE * yy_buffer_stack = NULL;")
  stamp("static char yy_hold_char;")
  stamp("static int yy_n_chars;")
  stamp("int csql_yyleng;")
  stamp("static char *yy_c_buf_p = NULL;")
  stamp("static int yy_init = 0;")
  stamp("static int yy_start = 0;")
  stamp("static int yy_did_buffer_switch_on_eof;")
  stamp("FILE *csql_yyin = NULL, *csql_yyout = NULL;")
  stamp("extern int csql_yylineno;")
  stamp("int csql_yylineno = 1;")
  stamp("extern char *csql_yytext;")
  stamp("static yy_state_type yy_last_accepting_state;")
  stamp("static char *yy_last_accepting_cpos;")
  stamp("char *csql_yytext;")
else()
  message(FATAL_ERROR "patch_parser_tls: unknown PATCH_KIND '${PATCH_KIND}'")
endif()

string(APPEND content "\n/* CSQL_PARSER_TLS_PATCHED */\n")
file(WRITE "${PATCH_FILE}" "${content}")
