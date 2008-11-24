/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * esql_misc.h - Prototypes of all interesting inter-module definitions.
 */

#ifndef _ESQL_MISC_H_
#define _ESQL_MISC_H_

#ident "$Id$"

#include <limits.h>
#include "esql_host_variable.h"
#include "esql_hash.h"
#include "parser.h"
#include "variable_string.h"

/* True iff the default size of an int is long. */
#define HOST_N_REFS(p)  ((p) ? (p)->n_refs : 0)
#define HOST_REFS(p)    ((p) ? (p)->refs   : NULL)
#define HOST_DESC(p)    ((p) ? (p)->desc   : NULL)
#define CHECK_HOST_REF(hvars, n)               \
         ((hvars) && (n) < (hvars)->n_refs ?   \
              &(hvars)->refs[(n)] : NULL)
#define MEMBER(set, val)        ((set) & (1 << (val)))
#define NEWSET(val)             (1 << (val))
#define ECHO            (*echo_fn)(zzlextext, zzendexpr-zzbegexpr+1)
#define ECHO_STR(str,length)    (*echo_fn)((str), length)
#define ECHO_SP         ECHO_STR(" ",strlen(" "))
#define ECHO_NL         ECHO_STR("\n",strlen("\n"))

#define IS_SPECIFIER(p)         ( (p)->class_ == SPECIFIER )
#define IS_DECLARATOR(p)        ( (p)->class_ == DECLARATOR )

#define IS_ARRAY(p)             ( IS_DECLARATOR(p) &&       \
                                  (p)->decl.d.dcl_type == D_ARRAY  )
#define IS_POINTER(p)           ( IS_DECLARATOR(p) &&       \
                                  (p)->decl.d.dcl_type == D_POINTER)
#define IS_FUNCT(p)             ( IS_DECLARATOR(p) &&       \
                                  (p)->decl.d.dcl_type == D_FUNCTION)
#define IS_CHAR(p)              ( IS_SPECIFIER(p) && (p)->decl.s.noun == N_CHR )
#define IS_INT(p)               ( IS_SPECIFIER(p) && (p)->decl.s.noun == N_INT )
#define IS_UINT(p)              ( IS_INT(p) && (p)->decl.s.is_unsigned )
#define IS_LONG(p)              ( IS_INT(p) && (p)->decl.s.is_long )
#define IS_ULONG(p)             ( IS_INT(p) && (p)->decl.s.is_long && \
                                  (p)->decl.s.is_unsigned )
#define IS_UNSIGNED(p)          ( (p)->decl.s.is_unsigned )

#define IS_STRUCT(p)            ( IS_SPECIFIER(p) && \
                                  (p)->decl.s.noun == N_STRUCTURE )
#define IS_LABEL(p)             ( IS_SPECIFIER(p) && \
                                  (p)->decl.s.noun == N_LABEL     )
#define IS_VARCHAR(p)           ( IS_SPECIFIER(p) && \
                                  (p)->decl.s.noun == N_VARCHAR   )
#define IS_BIT(p)               ( IS_SPECIFIER(p) && \
                                  (p)->decl.s.noun == N_BIT       )
#define IS_VARBIT(p)            ( IS_SPECIFIER(p) && \
                                  (p)->decl.s.noun == N_VARBIT    )
#define IS_PSEUDO_TYPE(p)       ( IS_SPECIFIER(p) &&         \
                                ((p)->decl.s.noun == N_VARCHAR || \
                                 (p)->decl.s.noun == N_BIT     || \
                                 (p)->decl.s.noun == N_VARBIT)  )
#define IS_VAR_TYPE(p)          ( IS_SPECIFIER(p) &&          \
                                 ((p)->decl.s.noun == N_VARCHAR || \
                                  (p)->decl.s.noun == N_VARBIT)  )

#define IS_AGGREGATE(p)         ( IS_ARRAY(p) || IS_STRUCT(p) )
#define IS_PTR_TYPE(p)          ( IS_ARRAY(p) || IS_POINTER(p) )

#define IS_TYPEDEF(p)           ( IS_SPECIFIER(p) && \
                                 (p)->decl.s.sclass == C_TYPEDEF )
#define IS_CONSTANT(p)          ( IS_SPECIFIER(p) && \
                                 (p)->decl.s.sclass == C_CONSTANT)
#define IS_INT_CONSTANT(p)      ( IS_CONSTANT(p) &&  \
                                 (p)->decl.s.noun == N_INT)

typedef unsigned int BITSET;
typedef void (*ECHO_FN) (const char *, int);
typedef struct whenever_action WHENEVER_ACTION;
typedef struct whenever_scope WHENEVER_SCOPE;

struct whenever_action
{
  WHEN_ACTION action;
  YY_CHAR *name;
};

struct whenever_scope
{
  WHENEVER_ACTION cond[3];
};

enum ex_msg
{
  EX_ARGS_SET = 1,
  EX_CURSOR_SET = 2,
  EX_DECL_SET = 3,
  EX_ENVIRON_SET = 4,
  EX_ESQLM_SET = 5,
  EX_ARG_NAME_SET = 6,
  EX_ARG_DESCRIPTION_SET = 7,
  EX_ESQLMMAIN_SET = 8,
  EX_ESQLMSCANSUPPORT_SET = 9,
  EX_HASH_SET = 10,
  EX_HOSTVAR_SET = 11,
  EX_MISC_SET = 12,
  EX_SYMBOL_SET = 13,
  EX_TRANS_SET = 14
};

enum scanner_mode
{
  ECHO_MODE = 0,
  SQLX_MODE = 1,
  C_MODE = 2,
  EXPR_MODE = 3,
  VAR_MODE = 4,
  HV_MODE = 5,
  BUFFER_MODE = 6,
  COMMENT_MODE = 7
};

extern int pp_recognizing_typedef_names;
extern int pp_nesting_level;

#if defined(WANT_YACC)
extern int yydebug;
extern int yychar;
extern int yyerrflag;
extern int yyparse (void);
extern void yyrestart (FILE *);
extern YY_CHAR *yytext;
extern int yylex (void);
extern void yysync (void);
#endif

extern char *pt_buffer;
extern unsigned int pp_uci_opt;
extern int pp_emit_line_directives;
extern int pp_dump_scope_info;
extern int pp_dump_malloc_info;
extern const char *pp_include_path;
extern const char *pp_include_file;
extern int pp_enable_uci_trace;
extern int pp_disable_varchar_length;
extern int pp_varchar2;
extern int pp_unsafe_null;
extern int pp_internal_ind;
extern const char *prog_name;
extern varstring pt_statement_buf;

extern varstring pp_subscript_buf;
extern varstring pp_host_var_buf;

extern YYSTYPE yylval;
extern FILE *yyin;
extern FILE *yyout;
extern YY_CHAR *yyfilename;
extern int yylineno;
extern int errors;
extern ECHO_FN echo_fn;


extern const char *VARCHAR_ARRAY_NAME;
extern const char *VARCHAR_LENGTH_NAME;
extern SYMTAB *pp_Symbol_table;	/* The table for C identifiers.         */
extern SYMTAB *pp_Struct_table;	/* The table for struct definitions     */

extern CURSOR *pp_new_cursor (YY_CHAR * name,
			      YY_CHAR * static_stmt,
			      int stmtLength,
			      STMT * dynamic_stmt, HOST_LOD * host_refs);
extern void pp_free_cursor (CURSOR * cursor);
extern CURSOR *pp_lookup_cursor (YY_CHAR * name);
extern void pp_cursor_init (void);
extern void pp_cursor_finish (void);
extern void pp_print_cursors (FILE * fp);
extern void pp_remove_cursors_from_table (CURSOR * chain);
extern void pp_discard_cursor_chain (CURSOR * chain);
extern STMT *pp_new_stmt (YY_CHAR * name);
extern void pp_free_stmt (STMT * stmt);
extern void pp_add_spec_to_decl (LINK * p_spec, SYMBOL * decl_chain);
extern void pp_add_symbols_to_table (SYMBOL * sym);
extern void pp_remove_symbols_from_table (SYMBOL * sym_chain);
extern void pp_do_enum (SYMBOL * sym);
extern void pp_push_name_scope (void);
extern void pp_pop_name_scope (void);
extern void pp_make_typedef_names_visible (int);
extern void pp_decl_init (void);
extern void pp_decl_finish (void);
extern void pp_reset_current_type_spec (void);
extern LINK *pp_current_type_spec (void);
extern void pp_add_storage_class (int sc);
extern void pp_add_struct_spec (STRUCTDEF * sdef);
extern void pp_add_type_noun (int type);
extern void pp_add_type_adj (int adj);
extern void pp_add_typedefed_spec (LINK * spec);
extern void pp_add_initializer (SYMBOL * sym);
extern void pp_push_spec_scope (void);
extern void pp_pop_spec_scope (void);
extern void pp_disallow_storage_classes (void);
extern void pp_add_cursor_to_scope (CURSOR * cursor);
extern void pp_add_whenever_to_scope (WHEN_CONDITION cond,
				      WHEN_ACTION action, YY_CHAR * name);
extern void pp_print_decls (SYMBOL * sym_chain, int preechoed);
extern void pp_print_specs (LINK * link);
extern void pp_suppress_echo (int);
extern void pp_gather_input_refs (void);
extern void pp_gather_output_refs (void);
extern HOST_LOD *pp_input_refs (void);
extern HOST_LOD *pp_output_refs (void);
extern void pp_clear_host_refs (void);
extern HOST_VAR *pp_new_host_var (HOST_VAR * var, SYMBOL * sym);
extern void pp_free_host_var (HOST_VAR * var);
extern HOST_REF *pp_add_host_ref (HOST_VAR *, HOST_VAR *, bool, int *);
extern void pp_free_host_ref (HOST_REF * ref);
extern HOST_LOD *pp_copy_host_refs (void);
extern HOST_LOD *pp_detach_host_refs (void);
extern HOST_REF *pp_check_type (HOST_REF * ref, BITSET typeset,
				const YY_CHAR * msg);
extern void pp_check_host_var_list (void);
extern HOST_VAR *pp_ptr_deref (HOST_VAR * var, int style);
extern HOST_VAR *pp_struct_deref (HOST_VAR * var,
				  YY_CHAR * field, int indirect);
extern HOST_VAR *pp_addr_of (HOST_VAR * var);
extern void pp_hv_init (void);
extern void pp_hv_finish (void);
extern HOST_REF *pp_add_host_str (YY_CHAR * str);
extern HOST_LOD *pp_new_host_lod (void);
extern void pp_free_host_lod (HOST_LOD * lod);
extern void pp_clear_host_lod (HOST_LOD * lod);
extern char *pp_switch_to_descriptor (void);
extern void pp_translate_string (varstring * vstr,
				 const char *str, int in_string);
extern unsigned int pp_generic_case_hash (void *p);
extern unsigned int pp_generic_hash (void *p);
extern int pp_generic_case_cmp (void *p1, void *p2);
extern int pp_generic_cmp (void *p1, void *p2);
extern void pp_startup (void);
extern void pp_finish (void);
extern void *pp_malloc (int n);
extern char *pp_strdup (const char *str);
extern PTR_VEC *pp_new_ptr_vec (PTR_VEC * vec);
extern void pp_free_ptr_vec (PTR_VEC * vec);
extern PTR_VEC *pp_add_ptr (PTR_VEC * vec, void *new_elem);
extern int pp_ptr_vec_n_elems (PTR_VEC * vec);
extern void **pp_ptr_vec_elems (PTR_VEC * vec);
extern const char *pp_get_msg (int msg_set, int msg_num);
extern void emit_line_directive (void);
extern void yyinit (void);
extern void yyerror (const YY_CHAR *);
extern void yyverror (const char *, ...);
extern void yyvwarn (const YY_CHAR *, ...);
extern void yyredef (YY_CHAR *);
extern void yy_enter (enum scanner_mode);
extern void yy_push_mode (enum scanner_mode);
extern void yy_pop_mode (void);
extern void yy_check_mode (void);
extern void yy_sync_lineno (void);
extern void yy_set_buf (varstring * vstr);
extern void yy_echo (const YY_CHAR * str);
extern void yy_erase_last_token (void);
extern enum scanner_mode yy_mode ();
extern varstring *yy_get_buf ();
extern void echo_stream (const char *, int);
extern void echo_vstr (const char *, int);
extern void echo_devnull (const char *, int);
extern ECHO_FN pp_set_echo (ECHO_FN);
extern SYMTAB *pp_new_symtab (void);
extern void pp_free_symtab (SYMTAB *, HT_FREE_FN);
extern SYMBOL *pp_new_symbol (const YY_CHAR * name, int scope);
extern void pp_discard_symbol (SYMBOL * sym);
extern void pp_discard_symbol_chain (SYMBOL * sym);
extern LINK *pp_new_link (void);
extern void pp_discard_link_chain (LINK * p);
extern void pp_discard_link (LINK * p);
extern STRUCTDEF *pp_new_structdef (const YY_CHAR * tag);
extern void pp_discard_structdef (STRUCTDEF * sdef);
extern void pp_discard_structdef_chain (STRUCTDEF * sdef);
extern STRUCTDEF *pp_new_pseudo_def (SPECIFIER_NOUN type,
				     const char *subscript);
extern void pp_add_declarator (SYMBOL * sym, int type);
extern LINK *pp_clone_type (LINK * tchain, LINK ** endp);
extern SYMBOL *pp_clone_symbol (SYMBOL * sym);
extern int pp_the_same_type (LINK * p1, LINK * p2, int relax);
extern YY_CHAR *pp_sclass_str (int class_);
extern YY_CHAR *pp_attr_str (LINK * type);
extern const YY_CHAR *pp_type_str (LINK * link);
extern void pp_print_syms (FILE * fp);
extern SYMBOL *pp_findsym (SYMTAB * symtab, YY_CHAR * name);
extern void pp_symbol_init (void);
extern void pp_symbol_finish (void);
extern void pp_symbol_stats (FILE * fp);
extern void pp_init_whenever_scope (WHENEVER_SCOPE * scope,
				    WHENEVER_SCOPE * old_scope);
extern void pp_finish_whenever_scope (WHENEVER_SCOPE * scope,
				      WHENEVER_SCOPE * new_scope);

#endif /* _ESQL_MISC_H_ */
