/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * esql_declare.c - Declaration-handling code for esql preprocessor.
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "esql_misc.h"
#define   ZZ_PREFIX em_
#include "zzpref.h"
#define  INSIDE_SCAN_DOT_C
#include "esql_grammar_tokens.h"
#include "memory_alloc.h"
#include "esql_translate.h"
#include "variable_string.h"

#define NFRAMES		8
#define MAX_LONGS_ALLOWED 1

typedef struct spec_state SPEC_STATE;
typedef struct scope SCOPE;

struct spec_state
{
  int noun_seen;		/* 1 iff a type noun has been seen      */
  int longs_seen;		/* # of longs seen                      */
  int shorts_seen;		/* # of shorts seen                     */
  int signed_seen;		/* 1 iff signed or unsigned "    "      */
  int storage_class_seen;	/* 1 iff a storage class    "    "      */
  int typedef_seen;		/* 1 iff typedef is in progress         */
  int sc_allowed;		/* 1 iff storage class specs are allowed */
  int volatile_seen;		/* 1 iff volatile keyword seen          */
  int const_seen;		/* 1 iff const keyword seen             */
  LINK *spec;			/* The specifier under construction     */
};

struct scope
{
  int recognizing_typedef_names;
  SYMBOL *sym_chain;
  STRUCTDEF *struct_chain;
  CURSOR *cursor_chain;
  WHENEVER_SCOPE whenever;
};

enum
{
  MSG_BAD_STORAGE_CLASS = 1,
  MSG_MALFORMED_CHAIN = 2,
  MSG_SPECIFIER_NOT_ALLOWED = 3,
  MSG_ILLEGAL_CLASS_COMBO = 4,
  MSG_ILLEGAL_TYPE_COMBO = 5,
  MSG_ILLEGAL_MODIFIER_COMBO = 6,
  MSG_TYPE_SPEC_UNEXPECTED_CASE = 7,
  MSG_TYPE_ADJ_UNEXPECTED_CASE = 8,
  MSG_BAD_PSEUDO_DECL = 9
};

/* space and C token literal defs */
#define TOK_SPACE     " "
#define TOK_COMMA     ","
#define TOK_LB        "{"
#define TOK_RB        "}"
#define TOK_LP        "("
#define TOK_RP        ")"
#define TOK_SC        ";"
#define TOK_STAR      "*"
#define TOK_AUTO      "auto"
#define TOK_CONST     "const"
#define TOK_EXTERN    "extern"
#define TOK_CHAR      "char"
#define TOK_DOUBLE    "double"
#define TOK_FLOAT     "float"
#define TOK_INT       "int"
#define TOK_LONG      "long"
#define TOK_REGISTER  "register"
#define TOK_SHORT     "short"
#define TOK_STATIC    "static"
#define TOK_TYPEDEF   "typedef"
#define TOK_UNSIGNED  "unsigned"
#define TOK_VOID      "void"
#define TOK_VOLATILE  "volatile"
#define TOK_INVALID   "whoknows"



int pp_recognizing_typedef_names = 1;
int pp_nesting_level = 0;

static SCOPE *pp_current_name_scope;
static SPEC_STATE *pp_current_spec_scope;
static SCOPE *pp_name_scope_base, *pp_name_scope_limit;
static SPEC_STATE *pp_spec_scope_base, *pp_spec_scope_limit;

static void pp_set_class_bit (int storage_class, LINK * p);
static LINK *pp_new_type_spec (void);
static void pp_remove_structdefs_from_table (STRUCTDEF * struct_chain);
static void pp_print_link (LINK * p, varstring * buf, int context,
			   int preechoed);
static void pp_print_decl (SYMBOL * sym, varstring * buf, int preechoed);

/*
 * pp_set_class_bit() - Change the class of the specifier pointed to by p
 *    as indicated by storage_class.
 * return : void
 * storage_class(in): The new storage class for the specifier p.
 * p(out): The specifier to be changed.
 * note :The TYPEDEF class is used here only to remember that the input
 *    storage class was a typedef.
 */
static void
pp_set_class_bit (int storage_class, LINK * p)
{
  switch (storage_class)
    {
    case 0:
      {
	p->decl.s.sclass = C_FIXED;
	p->decl.s.is_static = 0;
	p->decl.s.is_extern = 0;
	p->decl.s.is_const = 0;
	p->decl.s.is_volatile = 0;
      }
      break;

    case TYPEDEF_:
      p->decl.s.sclass = C_TYPEDEF;
      break;

    case REGISTER_:
      p->decl.s.is_register = 1;
      break;

    case AUTO_:
      p->decl.s.is_auto = 1;
      break;

    case EXTERN_:
      p->decl.s.is_extern = 1;
      break;

    case STATIC_:
      p->decl.s.is_static = 1;
      break;

    default:
      {
	yyverror (pp_get_msg (EX_DECL_SET, MSG_BAD_STORAGE_CLASS),
		  storage_class);
	exit (1);
      }
      break;
    }
}

/*
 * pp_new_type_spec() - Return a new, initialized specifier structure.
 * return : LINK *
 */
static LINK *
pp_new_type_spec (void)
{
  LINK *p = pp_new_link ();

  p->class_ = SPECIFIER;
  p->decl.s.noun = N_INT;
  p->decl.s.sclass = C_AUTO;

  return p;
}

/*
 * pp_add_spec_to_decl() - Add the specifier to each of the declarators in
 *    decl_chain. This is accomplished by cloning p_spec and then tacking it
 *    onto the end of every declaration chain in decl_chain.
 * return : void
 * p_spec(in): A pointer to a specifier/declarator chain created by a previous
 *    typedef, or to a single specifier.  It is cloned and then tacked onto
 *    the end of every declaration chain in the list pointed to by decl_chain.
 * decl_chain(out): A chain of declarators, each of which is to receive the
 *    p_spec specifier.
 */
void
pp_add_spec_to_decl (LINK * p_spec, SYMBOL * decl_chain)
{
  LINK *clone_start, *clone_end;

  for (; decl_chain; decl_chain = decl_chain->next)
    {
      clone_start = pp_clone_type (p_spec, &clone_end);
      if (clone_start == NULL)
	{
	  yyverror (pp_get_msg (EX_DECL_SET, MSG_MALFORMED_CHAIN));
	  exit (1);
	}

      if (IS_PSEUDO_TYPE (clone_start)
	  && clone_start->decl.s.val.v_struct == NULL)
	{
	  LINK *old_etype;
	  char tmp[32];

	  old_etype = decl_chain->etype;
	  if (IS_VAR_TYPE (clone_start)
	      && (old_etype == NULL || IS_ARRAY (old_etype)) == 0)
	    {
	      yyverror (pp_get_msg (EX_DECL_SET, MSG_BAD_PSEUDO_DECL),
			pp_type_str (clone_start));
	      exit (1);
	    }

	  clone_start->decl.s.val.v_struct =
	    pp_new_pseudo_def (clone_start->decl.s.noun,
			       old_etype->decl.d.num_ele);
	  if (clone_start->decl.s.noun == N_VARCHAR)
	    {
	      sprintf (tmp, " = { %s, \"\" }", old_etype->decl.d.num_ele);
	    }
	  else
	    {
	      sprintf (tmp, " = { ((%s)+7)/8, \"\" }",
		       old_etype->decl.d.num_ele);
	    }
	  decl_chain->args = pp_new_symbol (tmp, decl_chain->level);
	  pp_discard_link (old_etype);

	  if (decl_chain->type == old_etype)
	    {
	      decl_chain->type = NULL;
	    }
	  else
	    {
	      LINK *parent;
	      for (parent = decl_chain->type;
		   parent->next != old_etype; parent = parent->next)
		{
		  ;
		}
	      parent->next = NULL;
	      decl_chain->etype = parent;
	    }
	}

      if (decl_chain->type == NULL)	/* No declarators */
	{
	  decl_chain->type = clone_start;
	}
      else
	{
	  decl_chain->etype->next = clone_start;
	}

      decl_chain->etype = clone_end;

      /*
       * If the declaration we're looking at is really a typedef,
       * record the symbol itself within the specifier.  This will
       * make it easier to point back to the symbol from other
       * declarations, which will make it easier to print out
       * later declarations using the typedef name rather than its
       * expansion.
       */
      if (IS_TYPEDEF (clone_end))
	{
	  pp_set_class_bit (0, clone_end);
	  decl_chain->type->tdef = decl_chain;
	  decl_chain->type->from_tdef = decl_chain;
	}
    }
}


/*
 * pp_add_symbols_to_table() - Add declarations to the symbol table. Removes
 *    duplicate declarations, complaining if they are not harmless.  All
 *    declarations that are retained are accessible through
 *    current->scope->sym_chain.
 * sym(in): A chain of symbols to be added to the symbol table.
 * return : void
 */
void
pp_add_symbols_to_table (SYMBOL * sym)
{
  SYMBOL *exists, *new, *next = NULL;

  for (new = sym; new; new = next)
    {
      next = new->next;

      if (sym->name == NULL)
	{
	  continue;
	}

      exists = (SYMBOL *) pp_findsym (pp_Symbol_table, new->name);

      if (exists == NULL || exists->level != new->level)
	{
	  pp_Symbol_table->add_symbol (pp_Symbol_table, new);
	  new->next = pp_current_name_scope->sym_chain;
	  pp_current_name_scope->sym_chain = new;
	}
      else
	{
	  int harmless = 0;

	  if (pp_the_same_type (exists->type, new->type, 0))
	    {
	      if (exists->etype->decl.s.is_extern
		  || new->etype->decl.s.is_extern)
		{
		  harmless = 1;
		  if (new->etype->decl.s.is_extern == 0)
		    {
		      exists->etype->decl.s.sclass =
			new->etype->decl.s.sclass;
		      exists->etype->decl.s.is_static =
			new->etype->decl.s.is_static;
		      exists->etype->decl.s.is_extern =
			new->etype->decl.s.is_extern;
		    }
		}
	    }

	  if (harmless == 0)
	    {
	      yyredef (new->name);
	    }

	  pp_discard_symbol (new);
	}
    }
}

/*
 * pp_remove_symbols_from_table() - Remove all of the symbols on the chain
 *    from the table.
 * return : void
 * sym_chain(in): A pointer to a linked chain of symbols to be removed from
 *   the table.
 */
void
pp_remove_symbols_from_table (SYMBOL * sym_chain)
{
  SYMBOL *sym;

  for (sym = sym_chain; sym; sym = sym->next)
    {
      pp_Symbol_table->remove_symbol (pp_Symbol_table, sym);
    }

}

/*
 * pp_remove_structdefs_from_table() - Remove all of the structs on the chain
 *    from the table.
 * return : void
 * struct_chain(in): A pointer to a linked chain of structdefs to be removed
 *    from the struct table.
 */
static void
pp_remove_structdefs_from_table (STRUCTDEF * struct_chain)
{
  STRUCTDEF *sdef;

  for (sdef = struct_chain; sdef; sdef = sdef->next)
    {
      pp_Struct_table->remove_symbol (pp_Struct_table, sdef);
    }
}

/*
 * pp_do_enum() - Enter the symbol as an enumerated constant with type int.
 *   We don't care about its actual value.
 * return : void
 * sym(in): A symbol being defined as an enumerated constant.
 */
void
pp_do_enum (SYMBOL * sym)
{
  if (sym->type)
    {
      yyredef (sym->name);
    }
  else
    {
      LINK *p = pp_new_type_spec ();
      p->decl.s.sclass = C_CONSTANT;
      p->decl.s.val.v_int = 0;	/* We don't care about the value. */
      sym->type = p;
      pp_Symbol_table->add_symbol (pp_Symbol_table, sym);
    }
}

/*
 * pp_push_name_scope() - Push a new symbol table scope.
 * return : void
 */
void
pp_push_name_scope (void)
{
  SCOPE *new_scope;

  new_scope =
    pp_current_name_scope ? (pp_current_name_scope + 1) : pp_name_scope_base;

  if (new_scope >= pp_name_scope_limit)
    {
      int nframes = pp_name_scope_limit - pp_name_scope_base;
      pp_name_scope_base = (SCOPE *) realloc (pp_name_scope_base,
					      sizeof (SCOPE) * (nframes +
								NFRAMES));
      pp_name_scope_limit = pp_name_scope_base + nframes + NFRAMES;
      if (pp_current_name_scope)
	{
	  pp_current_name_scope = pp_name_scope_base + nframes - 1;
	}
      new_scope = pp_name_scope_base + nframes;
    }

  new_scope->recognizing_typedef_names = 1;
  new_scope->sym_chain = NULL;
  new_scope->struct_chain = NULL;
  new_scope->cursor_chain = NULL;
  pp_init_whenever_scope (&new_scope->whenever,
			  pp_current_name_scope ? &pp_current_name_scope->
			  whenever : NULL);

  pp_current_name_scope = new_scope;
  ++pp_nesting_level;

  pp_make_typedef_names_visible (1);
}

/*
 * pp_pop_name_scope() - Pop the current symbol table scope.
 * return : void
 */
void
pp_pop_name_scope (void)
{
  SCOPE *current, *next;

#if !defined(NDEBUG)
  if (pp_dump_scope_info)
    {
      fprintf (yyout, "\n/*\n * Exiting scope level %d\n", pp_nesting_level);
      pp_print_syms (yyout);
      pp_print_cursors (yyout);
      fputs (" */\n", yyout);
    }
#endif

  current = pp_current_name_scope;
  if (pp_current_name_scope == pp_name_scope_base)
    {
      next = NULL;
    }
  else
    {
      next = pp_current_name_scope - 1;
    }

  pp_remove_symbols_from_table (current->sym_chain);
  pp_discard_symbol_chain (current->sym_chain);

  pp_remove_structdefs_from_table (current->struct_chain);
  pp_discard_structdef_chain (current->struct_chain);

  pp_remove_cursors_from_table (current->cursor_chain);
  pp_discard_cursor_chain (current->cursor_chain);

  pp_finish_whenever_scope (&current->whenever,
			    next ? &next->whenever : NULL);

  pp_current_name_scope = next;

  --pp_nesting_level;

  pp_make_typedef_names_visible (pp_current_name_scope == NULL
				 || pp_current_name_scope->
				 recognizing_typedef_names);
}

/*
 * pp_make_typedef_names_visible() - Alter current scope so that typedef names
 *    are treated as such, rather than as ordinary identifiers, (if sense is 1)
 *    or vice versa (if sense is 0).
 * return : void
 * sense: 1 if names should be visible, 0 if not.
 */
void
pp_make_typedef_names_visible (int sense)
{
  if (pp_current_name_scope)
    {
      pp_current_name_scope->recognizing_typedef_names = sense;
    }
  pp_recognizing_typedef_names = sense;

}

/*
 * pp_decl_init() - Set up various initial conditions for the module.
 * return : void
 */
void
pp_decl_init (void)
{
  pp_nesting_level = -1;	/* It will get bumped by push_name_scope(). */

  pp_name_scope_base = pp_malloc (NFRAMES * sizeof (SCOPE));
  memset (pp_name_scope_base, 0, NFRAMES * sizeof (SCOPE));
  pp_name_scope_limit = pp_name_scope_base + NFRAMES;
  pp_current_name_scope = NULL;
  pp_push_name_scope ();

  pp_spec_scope_base = pp_malloc (NFRAMES * sizeof (SPEC_STATE));
  memset (pp_spec_scope_base, 0, NFRAMES * sizeof (SPEC_STATE));
  pp_spec_scope_limit = pp_spec_scope_base + NFRAMES;
  pp_current_spec_scope = NULL;
  pp_push_spec_scope ();

  pp_make_typedef_names_visible (1);

}

/*
 * pp_decl_finish() - Tear down various module structures.
 * return : void
 */
void
pp_decl_finish (void)
{
  while (pp_current_name_scope)
    {
      pp_pop_name_scope ();
    }

  while (pp_current_spec_scope)
    {
      pp_pop_spec_scope ();
    }

  free_and_init (pp_name_scope_base);
  free_and_init (pp_spec_scope_base);
}

/*
 * pp_push_spec_scope() - Start a new scope for type specifiers. This type of
 *    scoped behavior is necessary for processing struct definitions correctly.
 * return : void
 */
void
pp_push_spec_scope (void)
{
  SPEC_STATE *p;

  p =
    pp_current_spec_scope ? (pp_current_spec_scope + 1) : pp_spec_scope_base;

  if (p >= pp_spec_scope_limit)
    {
      int nframes = pp_spec_scope_limit - pp_spec_scope_base;
      pp_spec_scope_base = (SPEC_STATE *) realloc (pp_spec_scope_base,
						   sizeof (SPEC_STATE) *
						   (nframes + NFRAMES));
      pp_spec_scope_limit = pp_spec_scope_base + nframes + NFRAMES;
      if (pp_current_spec_scope)
	{
	  pp_current_spec_scope = pp_spec_scope_base + nframes - 1;
	}
      p = pp_spec_scope_base + nframes;
    }

  p->noun_seen = false;
  p->longs_seen = 0;
  p->shorts_seen = 0;
  p->signed_seen = false;
  p->storage_class_seen = false;
  p->typedef_seen = false;
  p->sc_allowed = true;
  p->volatile_seen = false;
  p->const_seen = false;
  p->spec = NULL;
  pp_current_spec_scope = p;

}

/*
 * pp_pop_spec_scope() - Return to a previous scope for type specifiers.
 * return : void
 */
void
pp_pop_spec_scope (void)
{
  assert (pp_current_spec_scope != NULL);
  pp_current_spec_scope = (pp_current_spec_scope == pp_spec_scope_base
			   ? NULL : (pp_current_spec_scope - 1));

}

/*
 * pp_reset_current_type_spec() - Reset the working variables for
 *    the current type spec.
 * return : void
 */
void
pp_reset_current_type_spec (void)
{
  SPEC_STATE *p = pp_current_spec_scope;

  p->noun_seen = false;
  p->longs_seen = 0;
  p->shorts_seen = 0;
  p->signed_seen = false;
  p->storage_class_seen = false;
  p->typedef_seen = false;
  p->sc_allowed = true;
  p->volatile_seen = false;
  p->const_seen = false;

  p->spec = pp_new_type_spec ();

}

/*
 * pp_current_type_spec() - Return the type specifier currently
 *    under construction.
 * return : LINK *
 */
LINK *
pp_current_type_spec (void)
{
  return pp_current_spec_scope->spec;

}

/*
 * pp_add_storage_class() -
 * return : void
 * sc(in): The storage class to be added to the current type specifier.
 *
 * note : Side effects pp_current_spec_scope.
 */
void
pp_add_storage_class (int sc)
{
  SPEC_STATE *p = pp_current_spec_scope;

  if (!p->sc_allowed)
    {
      yyverror (pp_get_msg (EX_DECL_SET, MSG_SPECIFIER_NOT_ALLOWED));
      return;
    }

  if (p->storage_class_seen)
    {
      yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_CLASS_COMBO));
      return;
    }

  p->storage_class_seen = sc != TYPEDEF_;
  p->typedef_seen = sc == TYPEDEF_;
  pp_set_class_bit (sc, p->spec);
}

/*
 * pp_add_struct_spec() -
 * return : void
 * sdef(in): The struct definition that serves as the type noun of the
 *    current type spec.
 *
 * note : Side effects pp_current_spec_scope.
 */
void
pp_add_struct_spec (STRUCTDEF * sdef)
{
  SPEC_STATE *p = pp_current_spec_scope;

  if (sdef == NULL)
    {
      /*
       * There was already some sort of error during parsing, and we've
       * wound up here in a sort of no-op mode.  Just ignore it.
       */
      return;
    }

  if (p->noun_seen)
    {
      yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_TYPE_COMBO));
      pp_discard_structdef (sdef);
      return;
    }

  if ((p->longs_seen > 0) || (p->shorts_seen > 0) || p->signed_seen)
    {
      yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
      pp_discard_structdef (sdef);
      return;
    }

  p->spec->decl.s.noun = N_STRUCTURE;
  p->spec->decl.s.val.v_struct = sdef;
  p->spec->decl.s.is_by_name = sdef->by_name;
  p->noun_seen = true;
  p->longs_seen = 1;
  p->shorts_seen = 1;
  p->signed_seen = true;

  sdef->by_name = false;

}

/*
 * pp_add_type_noun() - Add the appropriate type to the current type specifier.
 * return : void
 * type(in): The type specifier to be added.
 *
 * note : Side effects pp_current_spec_scope.
 */
void
pp_add_type_noun (int type)
{
  SPEC_STATE *p = pp_current_spec_scope;

  if (p->noun_seen)
    {
      yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_TYPE_COMBO));
      return;
    }

  switch (type)
    {
    case VOID_:
      {
	if ((p->longs_seen > 0) || (p->shorts_seen > 0) || p->signed_seen)
	  {
	    yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
	    return;
	  }
	p->longs_seen = 1;
	p->shorts_seen = 1;
	p->signed_seen = true;
	p->spec->decl.s.noun = N_VOID;
      }
      break;

    case CHAR_:
      {
	if ((p->longs_seen > 0) || (p->shorts_seen > 0))
	  {
	    yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_TYPE_COMBO));
	    return;
	  }
	p->longs_seen = 1;
	p->spec->decl.s.noun = N_CHR;
      }
      break;

    case INT_:
      {
	p->spec->decl.s.noun = N_INT;
	p->spec->decl.s.is_long = (p->longs_seen > 0);
	p->spec->decl.s.is_short = (p->shorts_seen > 0);
      }
      break;

    case FLOAT_:
      {
	p->spec->decl.s.noun = N_FLOAT;
	p->spec->decl.s.is_long = (p->longs_seen > 0)
	  && p->spec->decl.s.is_long;
      }
      break;

    case DOUBLE_:
      {
	if ((p->longs_seen > 0) && !p->spec->decl.s.is_long)
	  {
	    yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
	    return;
	  }
	p->longs_seen = 1;
	p->spec->decl.s.noun = N_FLOAT;
	p->spec->decl.s.is_long = 1;
      }
      break;

    case VARCHAR_:
    case BIT_:
    case VARBIT_:
      {
	if ((p->longs_seen > 0) || (p->shorts_seen) || (p->signed_seen))
	  {
	    yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
	    return;
	  }
	p->spec->decl.s.noun = (type == VARCHAR_) ? N_VARCHAR :
	  (type == BIT_) ? N_BIT : N_VARBIT;
	p->spec->decl.s.val.v_struct = NULL;
      }
      break;

    default:
      {
	yyverror (pp_get_msg (EX_DECL_SET, MSG_TYPE_SPEC_UNEXPECTED_CASE),
		  "pp_add_type_spec", type);
	exit (1);
      }
      break;
    }

  p->spec->decl.s.is_volatile = p->volatile_seen;
  p->spec->decl.s.is_const = p->const_seen;

  p->noun_seen = true;
}

/*
 * pp_add_type_adj() - Add the indicated modifier to the current type
 *    specifier, if appropriate.
 * return : void
 * adj(in): The type modifier to be applied.
 *
 * note : Side effects pp_current_spec_scope.
 */
void
pp_add_type_adj (int adj)
{
  SPEC_STATE *p = pp_current_spec_scope;

  switch (adj)
    {
      /* Used for ENUM */
    case INT_:
      if (p->noun_seen && (p->spec->decl.s.noun == N_INT))
	{
	  p->spec->decl.s.is_long = 0;
	  p->spec->decl.s.is_short = 0;
	}
      else
	{
	  yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
	  return;
	}
    case SHORT_:
      {
	if ((p->shorts_seen > 1) || (p->longs_seen > 0))
	  {
	    yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
	    return;
	  }
	if (p->noun_seen)
	  {
	    switch (p->spec->decl.s.noun)
	      {
	      case N_INT:
	      case N_FLOAT:
		break;

	      default:
		yyverror (pp_get_msg
			  (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
		return;
	      }
	  }
	++p->shorts_seen;
	p->spec->decl.s.is_short = 1;
	p->spec->decl.s.is_long = 0;
      }
      break;

    case LONG_:
      {
	if ((p->longs_seen > MAX_LONGS_ALLOWED) || p->shorts_seen > 0)
	  {
	    yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
	    return;
	  }
	if (p->noun_seen)
	  {
	    switch (p->spec->decl.s.noun)
	      {
	      case N_INT:
	      case N_FLOAT:
		break;

	      default:
		yyverror (pp_get_msg
			  (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
		return;
	      }
	  }
	++p->longs_seen;
	p->spec->decl.s.is_long = 1;
	p->spec->decl.s.is_short = 0;
      }
      break;

    case SIGNED_:
    case UNSIGNED_:
      {
	if (p->signed_seen)
	  {
	    yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
	    return;
	  }
	if (p->noun_seen)
	  {
	    switch (p->spec->decl.s.noun)
	      {
	      case N_INT:
	      case N_CHR:
		break;

	      default:
		yyverror (pp_get_msg
			  (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
		return;
	      }
	  }
	p->signed_seen = true;
	p->spec->decl.s.is_unsigned = adj == UNSIGNED_;
      }
      break;

    case CONST_:
      {
	if (p->volatile_seen)
	  {
	    yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
	    return;
	  }
	p->const_seen = true;
      }
      break;

    case VOLATILE_:
      {
	if (p->volatile_seen)
	  {
	    yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
	    return;
	  }
	p->volatile_seen = true;
      }
      break;

    default:
      {
	yyverror (pp_get_msg (EX_DECL_SET, MSG_TYPE_ADJ_UNEXPECTED_CASE),
		  "pp_add_type_adj", adj);
	exit (1);
      }
      break;
    }

}

/*
 * pp_add_typedefed_spec() - Set the type of the current definition
 *    (i.e., the specifier being built in pp_current_spec_scope) to a copy
 *    of the specifier/declarator chain associated with the given typedef.
 * return : void
 * spec(in): The type descriptor provided from a typedef definition.
 *
 * note : Side effects pp_current_spec_scope.
 */
void
pp_add_typedefed_spec (LINK * spec)
{
  SPEC_STATE *p = pp_current_spec_scope;
  LINK *q;

  if (p->noun_seen)
    {
      yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_TYPE_COMBO));
      return;
    }
  if ((p->longs_seen > 0) || (p->shorts_seen > 0) || p->signed_seen)
    {
      yyverror (pp_get_msg (EX_DECL_SET, MSG_ILLEGAL_MODIFIER_COMBO));
      return;
    }

  /*
   * Reset any class info in the new spec; this is ok since this spec
   * will be cloned onto every declarator that it modifies, and then
   * reset before every future use.  The clones will keep any specific
   * changes that they need.
   */
  for (q = spec; q->next; q = q->next)
    {
      ;
    }
  if (p->spec->decl.s.sclass)
    {
      q->decl.s.sclass = p->spec->decl.s.sclass;
    }
  q->decl.s.is_static = p->spec->decl.s.is_static;
  q->decl.s.is_extern = p->spec->decl.s.is_extern;
  q->decl.s.is_const = p->spec->decl.s.is_const;
  q->decl.s.is_volatile = p->spec->decl.s.is_volatile;

  if (p->spec->tdef == NULL)
    {
      pp_discard_link_chain (p->spec);
    }

  p->spec = spec;
  p->noun_seen = true;
  p->longs_seen = 1;
  p->shorts_seen = 1;
  p->signed_seen = true;

}

/*
 * pp_add_initializer() - Record the fact that this symbol has an initializer.
 *   At present, this is interesting only for array declarators. it lets us
 *   know if sizeof(array) is meaningful or not. It works a little bit by
 *   accident. at the point where it is called from the parser, sym doesn't
 *   yet have a specifier (so sym->type might be NULL), but in that case we
 *   don't care about recording the initializer anyway.  If that ever changes
 *   the initializer stuff probably ought to be put in the SYMBOL structure
 *   itself.
 * return : void
 * sym(in): A pointer to a symbol.
 */
void
pp_add_initializer (SYMBOL * sym)
{
  if (sym == NULL)
    {
      return;
    }

  if (sym->type && IS_ARRAY (sym->type))
    {
      sym->type->decl.d.num_ele = NULL;
    }

}

/*
 * pp_disallow_storage_classes() - Disallow storage class specifiers in
 *    specifier lists.
 * return : void
 */
void
pp_disallow_storage_classes (void)
{
  pp_current_spec_scope->sc_allowed = false;

}

/*
 * pp_add_cursor_to_scope() - Add the cursor to the current name scope.
 *    The cursor will be deleted when the scope is exited.
 * return : void
 * cursor: A pointer to a cursor structure to be added to the current
 *   name scope.
 */
void
pp_add_cursor_to_scope (CURSOR * cursor)
{
  cursor->next = pp_current_name_scope->cursor_chain;
  pp_current_name_scope->cursor_chain = cursor;

}

/*
 * pp_add_whenever_to_scope() - Establish the action to be taken when the
 *    specified condition is detected.  This condition/action pair remains
 *    in effect until overridden by another assignment or until the current
 *    block is exited.
 * return : void
 * when(in): The condition to be monitored.
 * action(in): The action to be taken when the condition is detected.
 * name(in): An optional name to be used by the action.
 */
void
pp_add_whenever_to_scope (WHEN_CONDITION when, WHEN_ACTION action, char *name)
{
  WHENEVER_ACTION *p = &pp_current_name_scope->whenever.cond[when];

  p->action = action;
  p->name = name ? strdup (name) : NULL;

  esql_Translate_table.tr_whenever (when, action, p->name);

}

/*
 * pp_print_link() -
 * return:
 * p(in) :
 * buf(out) :
 * context(in) :
 * preechoed(in) :
 */
static void
pp_print_link (LINK * p, varstring * buf, int context, int preechoed)
{
  if (p == NULL)
    {
      return;
    }

  if (p->from_tdef && p->tdef == NULL)
    {
      vs_prepend (buf, TOK_SPACE);
      vs_prepend (buf, p->from_tdef->name);

      /*
       * Now find the terminal specifier in the link chain and extract
       * any information about storage class specifiers.
       */
      while (p->next)
	{
	  p = p->next;
	}

      if (preechoed == 0)
	{
	  if (p->decl.s.is_extern)
	    {
	      vs_prepend (buf, TOK_EXTERN TOK_SPACE);
	    }
	  if (p->decl.s.is_static)
	    {
	      vs_prepend (buf, TOK_STATIC TOK_SPACE);
	    }
	  if (p->decl.s.is_volatile)
	    {
	      vs_prepend (buf, TOK_VOLATILE TOK_SPACE);
	    }
	  if (p->decl.s.is_const)
	    {
	      vs_prepend (buf, TOK_CONST TOK_SPACE);
	    }
	  if (p->decl.s.is_register)
	    {
	      vs_prepend (buf, TOK_REGISTER TOK_SPACE);
	    }
	  if (p->decl.s.is_auto)
	    {
	      vs_prepend (buf, TOK_AUTO TOK_SPACE);
	    }
	}
    }
  else if (IS_SPECIFIER (p))
    {
      switch (p->decl.s.noun)
	{
	case N_INT:
	  {
	    if (p->decl.s.is_unsigned)
	      {
		vs_prepend (buf, TOK_UNSIGNED TOK_SPACE);
	      }
	    if (p->decl.s.is_long)
	      {
		vs_prepend (buf, TOK_LONG TOK_SPACE);
	      }
	    if (p->decl.s.is_short)
	      {
		vs_prepend (buf, TOK_SHORT TOK_SPACE);
	      }
	    vs_prepend (buf, TOK_INT TOK_SPACE);
	  }
	  break;
	case N_CHR:
	  {
	    if (p->decl.s.is_unsigned)
	      {
		vs_prepend (buf, TOK_UNSIGNED TOK_SPACE);
	      }
	    vs_prepend (buf, TOK_CHAR TOK_SPACE);
	  }
	  break;
	case N_VOID:
	  vs_prepend (buf, TOK_VOID TOK_SPACE);
	  break;
	case N_FLOAT:
	  vs_prepend (buf, p->decl.s.is_long ? TOK_DOUBLE TOK_SPACE :
		      TOK_FLOAT TOK_SPACE);
	  break;
	case N_VARCHAR:
	case N_BIT:
	case N_VARBIT:
	case N_STRUCTURE:
	  {
	    if (p->decl.s.val.v_struct->fields && !p->decl.s.is_by_name)
	      {
		varstring fields, tmp;
		SYMBOL *field;

		vs_new (&fields);
		vs_new (&tmp);
		vs_strcpy (&fields, TOK_LB TOK_SPACE);
		for (field = p->decl.s.val.v_struct->fields; field;
		     field = field->next)
		  {
		    pp_print_decl (field, &tmp, preechoed);
		    vs_strcat (&fields, vs_str (&tmp));
		    vs_strcat (&fields, TOK_SC TOK_SPACE);
		  }
		vs_strcat (&fields, TOK_RB TOK_SPACE);
		vs_prepend (buf, vs_str (&fields));
		vs_free (&fields);
		vs_free (&tmp);
	      }
	    /*
	     * Don't print the tags that we have invented for "anonymous"
	     * structs.  Probably ought to encapsulate this better.
	     */
	    if (p->decl.s.val.v_struct->tag
		&& p->decl.s.val.v_struct->tag[0] != '$')
	      {
		vs_prepend (buf, TOK_SPACE);
		vs_prepend (buf, p->decl.s.val.v_struct->tag);
	      }
	    vs_prepend (buf, TOK_SPACE);
	    vs_prepend (buf, p->decl.s.val.v_struct->type_string);
	  }
	  break;
	case N_LABEL:
	default:
	  vs_prepend (buf, TOK_INVALID TOK_SPACE);
	  break;
	}

      if (preechoed == 0)
	{
	  if (p->decl.s.is_extern)
	    {
	      vs_prepend (buf, TOK_EXTERN TOK_SPACE);
	    }
	  if (p->decl.s.is_static)
	    {
	      vs_prepend (buf, TOK_STATIC TOK_SPACE);
	    }
	  if (p->decl.s.is_volatile)
	    {
	      vs_prepend (buf, TOK_VOLATILE TOK_SPACE);
	    }
	  if (p->decl.s.is_const)
	    {
	      vs_prepend (buf, TOK_CONST TOK_SPACE);
	    }
	  if (p->decl.s.is_register)
	    {
	      vs_prepend (buf, TOK_REGISTER TOK_SPACE);
	    }
	  if (p->decl.s.is_auto)
	    {
	      vs_prepend (buf, TOK_AUTO TOK_SPACE);
	    }
	}
    }
  else
    {
      switch (p->decl.d.dcl_type)
	{
	case D_POINTER:
	  {
	    vs_prepend (buf, TOK_STAR);
	    pp_print_link (p->next, buf, D_POINTER, preechoed);
	  }
	  break;
	case D_ARRAY:
	  {
	    if (context == D_POINTER)
	      {
		vs_prepend (buf, TOK_LP);
		vs_append (buf, TOK_RP);
	      }
	    vs_sprintf (buf, "[%s]",
			p->decl.d.num_ele ? p->decl.d.num_ele : "");
	    pp_print_link (p->next, buf, D_ARRAY, preechoed);
	  }
	  break;
	case D_FUNCTION:
	  {
	    SYMBOL *arg;
	    varstring tmp;

	    /*
	     * If this is a complex declaration like
	     *
	     *      int (*f)(int);
	     *
	     * then we'll need parens around the inner declarators.
	     * If not (i.e., the next link is a specifier) we won't
	     * need anything.
	     */
	    if (context == D_POINTER)
	      {
		vs_prepend (buf, TOK_LP);
		vs_append (buf, TOK_RP);
	      }

	    vs_append (buf, TOK_LP);
	    vs_new (&tmp);
	    for (arg = p->decl.d.args; arg; arg = arg->next)
	      {
		vs_clear (&tmp);
		pp_print_decl (arg, &tmp, preechoed);
		vs_append (buf, vs_str (&tmp));
		if (arg->next)
		  {
		    vs_append (buf, TOK_COMMA TOK_SPACE);
		  }
	      }
	    vs_free (&tmp);
	    vs_append (buf, TOK_RP);
	    pp_print_link (p->next, buf, D_FUNCTION, preechoed);
	  }
	  break;

	default:
	  vs_prepend (buf, TOK_INVALID);
	  break;
	}
    }
}


/*
 * pp_print_decl() -
 * return:
 * sym(in) :
 * buf(out) :
 * preechoed(in) :
 */
static void
pp_print_decl (SYMBOL * sym, varstring * buf, int preechoed)
{
  vs_clear (buf);
  vs_strcpy (buf, sym->name);
  pp_print_link (sym->type, buf, D_ARRAY, preechoed);
  if (preechoed == 0)
    {
      if (sym->type->tdef)
	{
	  vs_prepend (buf, TOK_TYPEDEF TOK_SPACE);
	}
    }
}

/*
 * pp_print_decls() - Print out C text for the declarations represented in
 *    decl_chain.
 * return : void
 * decl_chain(in): a list of symbols to be printed.
 * preechoed(in):  true if called from esql parser, false otherwise.
 *
 * note :   Because we echo most declarations while parsing them, this
 *    implementation currently concerns itself ONLY with VARCHAR declarations.
 *    The esql parser sees and echoes storage class specifiers (auto,
 *    register, static, extern) and type qualifiers (const, volatile) before
 *    noting the VARCHAR token. therefore we don't want to reprint them and
 *    preechoed will always be true. If pp_print_decls is called from anywhere
 *    other than the esql parser (e.g. test programs), preechoed should be
 *    specified as false.
 */
void
pp_print_decls (SYMBOL * decl_chain, int preechoed)
{
  varstring buf;

  vs_new (&buf);
  for (; decl_chain; decl_chain = decl_chain->next)
    {
      pp_print_decl (decl_chain, &buf, preechoed);
      fputs (vs_str (&buf), yyout);
      if (IS_PSEUDO_TYPE (decl_chain->type) && !pp_disable_varchar_length)
	{
	  fputs (decl_chain->args->name, yyout);
	}
      fputs (TOK_SC TOK_SPACE, yyout);
    }
  vs_free (&buf);
}

/*
 * pp_print_specs() -
 * return:
 * link(in) :
 */
void
pp_print_specs (LINK * link)
{
  varstring buf;

  vs_new (&buf);
  pp_print_link (link, &buf, D_ARRAY, true);
  fputs (vs_str (&buf), yyout);
  vs_free (&buf);
}
