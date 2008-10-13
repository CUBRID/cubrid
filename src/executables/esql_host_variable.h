/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * pp.h - Generic definitions and prototypes for the parser
 *        component of the embedded SQL/X preprocessor.
 */

#ifndef _PP_H_
#define _PP_H_

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include "esql_hash.h"
#include "vstr.h"

#define PTR_VEC_CHUNK_SIZE      4
/* The first C_TYPE that is NOT a valid host variable type */
#define NUM_C_VARIABLE_TYPES    C_TYPE_SQLDA

/* c_type : data types which is supported for Embbed SQL/X C programs.
 *
 * translator function implementation is highly dependent on this
 * data structure. The followings are should be preserved.
 *       1. First enum value should be 0.
 *       2. C_TYPE_STRING_CONST should come at the last of allowable
 *          C host variable types to be used for count # of allowable types.
 *       3. Additional data types should comes;
 *          right before C_TYPE_STRING_CONST, if it is C host variable type,
 *          at the last, otherwise. trans.c is dependent on this order.
 */
typedef enum c_pype
{
  C_TYPE_SHORT = 0,		/* should start from 0 */
  C_TYPE_INTEGER = 1,
  C_TYPE_LONG = 2,
  C_TYPE_FLOAT = 3,
  C_TYPE_DOUBLE = 4,
  C_TYPE_CHAR_ARRAY = 5,
  C_TYPE_CHAR_POINTER = 6,
  C_TYPE_OBJECTID = 7,
  C_TYPE_BASICSET = 8,
  C_TYPE_MULTISET = 9,
  C_TYPE_SEQUENCE = 10,
  C_TYPE_COLLECTION = 11,
  C_TYPE_TIME = 12,
  C_TYPE_TIMESTAMP = 13,
  C_TYPE_DATE = 14,
  C_TYPE_MONETARY = 15,
  C_TYPE_DB_VALUE = 16,
  C_TYPE_VARCHAR = 17,
  C_TYPE_BIT = 18,
  C_TYPE_VARBIT = 19,
  C_TYPE_NCHAR = 20,
  C_TYPE_VARNCHAR = 21,
  C_TYPE_STRING_CONST = 22,	/* for string constant, not variable */
  C_TYPE_SQLDA = 23,
  NUM_C_TYPES = 24		/* for # of elements in C_TYPE */
} C_TYPE;

typedef enum specifier_noun
{
  N_INT = 0,
  N_CHR = 1,
  N_VOID = 2,
  N_FLOAT = 3,
  N_STRUCTURE = 4,
  N_LABEL = 5,
  N_VARCHAR = 6,
  N_BIT = 7,
  N_VARBIT = 8
} SPECIFIER_NOUN;

typedef enum storage_class
{
  C_FIXED = 0,			/* Fixed address                */
  C_REGISTER = 1,		/* In a register                */
  C_AUTO = 2,			/* On run-time stack            */
  C_TYPEDEF = 3,		/* typedef                      */
  C_CONSTANT = 4		/* Const decl                   */
} STORAGE_CLASS;

typedef enum alias_class
{
  A_DIRECT = 0,			/* Specifier supplied directly             */
  A_TYPEDEF = 1,		/* Specifier supplied via typedef          */
  A_STRUCT = 2			/* Specifier supplied via tagged struct ref */
} ALIAS_CLASS;

typedef enum dcl_type
{
  D_POINTER = 0,		/* If declarator is pointer             */
  D_ARRAY = 1,			/* If declarator is an array            */
  D_FUNCTION = 2		/* If declarator is a function          */
} DCL_TYPE;

typedef struct ptr_vec PTR_VEC;
typedef struct link LINK;
typedef struct host_var HOST_VAR;
typedef struct host_ref HOST_REF;
typedef struct symbol SYMBOL;
typedef struct specifier specifier;
typedef struct declarator declarator;
typedef struct structdef STRUCTDEF;
typedef union yystype YYSTYPE;
typedef HASH_TAB SYMTAB;
typedef struct cursor CURSOR;
typedef struct stmt STMT;
typedef struct host_lod HOST_LOD;
typedef enum link_class LINK_CLASS;

struct host_lod			/* list or descriptor */
{
  YY_CHAR *desc;
  int n_refs;
  HOST_REF *refs;

  int max_refs;
  int n_real_refs;
  HOST_REF *real_refs;

  HOST_LOD *next;		/* Link to previous descriptor  */
};

union yystype
{
  char *p_char;
  SYMBOL *p_sym;
  LINK *p_link;
  STRUCTDEF *p_sdef;
  HOST_VAR *p_hv;
  HOST_REF *p_hr;
  HOST_LOD *p_lod;
  CURSOR *p_cs;
  STMT *p_stmt;
  PTR_VEC *p_pv;
  int num;
  int ascii;
};

struct structdef
{
  YY_CHAR *tag;			/* Tag part of struct def               */
  const YY_CHAR *type_string;	/* "struct" or "union"                  */
  unsigned char type;		/* 1 if a struct, 0 if a union          */
  unsigned char level;		/* Nesting level of struct decl         */
  SYMBOL *fields;		/* Linked list of field decls           */
  size_t size;			/* Size of the struct in bytes          */
  int by_name;			/* See note below                       */
  STRUCTDEF *next;		/* Link to next structdef at this level */

  /*
   * The by_name field is a bit sleazy: it gets set to 1 in the parser
   * if we looked up the structdef by its name, rather than by giving
   * its full elaboration.  It gets cleared in pp_add_struct_spec()
   * immediately after being transferred to the by_name field in the
   * specifier.  It is therefore valid only during that period where a
   * struct_specifier production is returning its result back up to the
   * type_specifier production.  Thus, even though only one structdef
   * structure is shared among all specifiers that refer to that
   * struct, we can get away with storing a piece of information there
   * that is really unique to each specifier.
   */
};

struct ptr_vec
{
  int n_elems;
  int max_elems;
  void **elems;

  bool heap_allocated;
  size_t chunk_size;
  void *inline_elems[PTR_VEC_CHUNK_SIZE];
};

struct declarator
{
  DCL_TYPE dcl_type;		/* POINTER, ARRAY, or FUNCTION          */
  const char *num_ele;		/* # of elements for ARRAY              */
  SYMBOL *args;			/* arg decls for a FUNCTION             */
};

struct specifier
{
  SPECIFIER_NOUN noun;		/* INT CHR VOID STRUCTURE LABEL         */
  STORAGE_CLASS sclass;		/* FIXED REGISTER AUTO TYPEDEF CONSTANT */
  unsigned char is_long;	/* 1 = long.                            */
  unsigned char is_short;	/* 1 = short.                           */
  unsigned char is_unsigned;	/* 1 = unsigned.        0 = signed.     */
  unsigned char is_static;	/* 1 if static keyword in declaration   */
  unsigned char is_extern;	/* 1 if extern keyword in declaration   */
  unsigned char is_volatile;	/* 1 if volatile keyword in declaration */
  unsigned char is_const;	/* 1 if const keyword in declaration    */
  unsigned char is_auto;	/* 1 if auto keyword in declaration     */
  unsigned char is_register;	/* 1 if register keyword in declaration */
  unsigned char is_by_name;	/* 1 if this is a STRUCTURE specifier   */

  union
  {				/* Value if constant:                   */
    int v_int;			/* Int & char values.  If a string,     */
    /* is numeric component of the label.   */
    unsigned int v_uint;	/* Unsigned int constant value.         */
    long v_long;		/* Signed long constant value.          */
    unsigned long v_ulong;	/* Unsigned long constant value.        */

    STRUCTDEF *v_struct;	/* If this is a struct or a varchar,    */
    /* points at a structure-table element  */

    SYMBOL *v_tdef;		/* The typedef-table entry for this     */
    /* link if it belongs to a typedef.     */
  } val;
};

enum link_class
{
  DECLARATOR = 0,
  SPECIFIER = 1
};

struct link
{
  LINK_CLASS class_;		/* DECLARATOR or SPECIFIER              */
  SYMBOL *tdef;			/* Points to typedef used to create the */
  SYMBOL *from_tdef;		/* link if it was created by cloning a  */
  /* typedef.                             */
  union
  {
    specifier s;		/* If class == SPECIFIER               */
    declarator d;		/* if class == DECLARATOR              */
  } decl;			/* declaration */

  LINK *next;			/* Next element of chain.               */
};

struct host_var
{
  int heap_allocated;
  LINK *type;
  LINK *etype;
  varstring expr;
  varstring addr_expr;
};

struct host_ref
{
  HOST_VAR *var;
  HOST_VAR *ind;
  C_TYPE uci_type;
  varstring *precision_buf;
  varstring *input_size_buf;
  varstring *output_size_buf;
  varstring *expr_buf;
  varstring *addr_expr_buf;
  varstring *ind_expr_buf;
  varstring *ind_addr_expr_buf;
};

typedef enum when_condition
{
  SQLWARNING = 0,		/* warning */
  SQLERROR = 1,			/* error */
  NOT_FOUND = 2			/* no object affected / end of search */
} WHEN_CONDITION;

typedef enum when_action
{
  CONTINUE = 0,			/* continue */
  STOP = 1,			/* stop and exit program */
  GOTO = 2,			/* goto specified label */
  CALL = 3			/* call specified procedure */
} WHEN_ACTION;

struct symbol
{
  YY_CHAR *name;		/* Input variable name.                 */
  int level;			/* Decl level, field offset             */
  LINK *type;			/* First link in declarator chain       */
  LINK *etype;			/* Last link in declarator chain        */
  SYMBOL *args;			/* If a func decl, the arg list         */
  /* If a var, the initializer            */
  SYMBOL *next;			/* Cross link to next variable at the   */
  /* current nesting level                */
};

struct cursor
{
  YY_CHAR *name;		/* The name of the cursor               */
  int cid;			/* A unique cursor id                   */
  int level;			/* The nesting level of the declaration */
  CURSOR *next;			/* A pointer to the next cursor at the  */
  /* same nesting level                   */
  /*
   * These two groups ({static_stmt, host_refs} and {dynamic_stmt}) are
   * actually something of a union, but it is convenient to leave them
   * broken out separately because of the way we interface with the
   * translation routines.  The translation routines want both sets of
   * information: they examine one set to see whether it is valid, and
   * then use either that set or the other.  We simply maintain both
   * sets here.  Thus, when static_stmt is non-NULL, dynamic_stmt is
   * NULL, and vice versa.
   */
  YY_CHAR *static_stmt;		/* The prepared SELECT statement */
  int stmtLength;
  HOST_LOD *host_refs;		/* The host variables           */
  STMT *dynamic_stmt;		/* The dynamic statement        */
};

struct stmt
{
  YY_CHAR *name;
  int sid;
};

extern C_TYPE pp_get_type (HOST_REF * ref);
extern char *pp_get_precision (HOST_REF * ref);
extern char *pp_get_input_size (HOST_REF * ref);
extern char *pp_get_output_size (HOST_REF * ref);
extern char *pp_get_expr (HOST_REF * ref);
extern char *pp_get_addr_expr (HOST_REF * ref);
extern char *pp_get_ind_expr (HOST_REF * ref);
extern char *pp_get_ind_addr_expr (HOST_REF * ref);
extern void pp_print_host_ref (HOST_REF * ref, FILE * fp);

#endif /* _PP_H_ */
