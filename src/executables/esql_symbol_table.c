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
 * esql_symbol_table.c - Symbol table manager for C compiler front-end.
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#include <stdlib.h>

#include "esql_grammar.h"
#include "misc_string.h"
#include "esql_misc.h"
#include "memory_alloc.h"

#define LCHUNK	10

typedef struct linkchunk
{
  struct linkchunk *next;
  LINK link[LCHUNK];
} LINKCHUNK;

enum esql_err_msg
{
  MSG_ILLEGAL_ARRAY = 1,
  MSG_UNKNOWN_CLASS = 2,
  MSG_BAD_NOUN = 3
};

SYMTAB *pp_Symbol_table;
SYMTAB *pp_Struct_table;

static SYMBOL *symbol_free_list;	/* Free list of recycled symbols.    */
static LINK *link_free_list;	/* Free list of recycled links.      */
static STRUCTDEF *struct_free_list;	/* Free list of recycled structdefs. */

static const unsigned int SYMTAB_SIZE = 511;
static LINKCHUNK *link_chunks;

/* 0 if syms should be deallocated via es_ht_free_symbol() instead. */
static int syms_to_free_lists = 1;

static int syms_allocated = 0;	/* Counters for statistics. */
static int syms_deallocated = 0;
static int sdefs_allocated = 0;
static int sdefs_deallocated = 0;
static int links_allocated = 0;
static int links_deallocated = 0;

static void es_print_symbol (SYMBOL * sym, FILE * fp);
static void es_print_struct (STRUCTDEF * sdef, FILE * fp);

/*
 * pp_new_symtab() - Returns a new, initialized SYMTAB structure.
 * return : a new, initialized SYMTAB structure.
 */
SYMTAB *
pp_new_symtab (void)
{
  return es_ht_make_table (SYMTAB_SIZE, pp_generic_hash, pp_generic_cmp);
}

/*
 * pp_free_symtab() - Free a symbol table and all of the symbols in it.
 * return : void
 * symtab(in) : A pointer to the symbol table to be deallocated
 * free_fn(in) :
 */
void
pp_free_symtab (SYMTAB * symtab, HT_FREE_FN free_fn)
{
  if (symtab)
    {
      symtab->free_table (symtab, free_fn);
    }
}

/*
 * pp_new_symbol() - Allocate and initialize a new symbol.
 * return : new allocated symbol
 * name(in): character string for the new symbol
 * scope(in): nesting level of this symbol
 */
SYMBOL *
pp_new_symbol (const char *name, int scope)
{
  SYMBOL *sym;

  if (symbol_free_list == NULL)
    {
      sym = (SYMBOL *) es_ht_alloc_new_symbol (sizeof (SYMBOL));
    }
  else
    {
      sym = symbol_free_list;
      symbol_free_list = symbol_free_list->next;
    }

  ++syms_allocated;

  sym->name = name ? ((unsigned char *) strdup (name)) : NULL;
  sym->level = scope;
  sym->type = NULL;
  sym->etype = NULL;
  sym->next = NULL;

  return sym;
}

/*
 * pp_discard_symbol() - Discard a single symbol structure and any attacked
 *    links and args.
 * return : void
 * sym(in): the symbol to be discarded
 *
 * note : the args field is recycled for initializers.
 */
void
pp_discard_symbol (SYMBOL * sym)
{

  if (sym)
    {
      if (sym->args)
	{
	  pp_discard_symbol (sym->args);
	}
      ++syms_deallocated;

      if (sym->name)
	{
	  free_and_init (sym->name);
	}

      if (sym->type)
	{
	  if (IS_FUNCT (sym->type))
	    {
	      pp_discard_symbol_chain (sym->args);
	    }
	  pp_discard_link_chain (sym->type);
	}

      if (syms_to_free_lists)
	{
	  sym->next = symbol_free_list;
	  symbol_free_list = sym;
	}
      else
	{
	  es_ht_free_symbol (sym);
	}
    }

}

/*
 * pp_discard_symbol_chain() - Discard an entire cross-linked chain of symbols.
 * return : void
 * sym(in): the head of a chain of symbols to discarded
 */
void
pp_discard_symbol_chain (SYMBOL * sym)
{
  SYMBOL *p = sym;

  while (sym)
    {
      p = sym->next;
      pp_discard_symbol (sym);
      sym = p;
    }
}

/*
 * pp_new_link() - Return a new link.  It is initialized to zeros.
 * return : LINK *
 */
LINK *
pp_new_link (void)
{
  LINK *p;
  int i;

  if (link_free_list == NULL)
    {
      LINKCHUNK *new_chunk = malloc (sizeof (LINKCHUNK));
      if (new_chunk == NULL)
	{
	  esql_yyverror (pp_get_msg (EX_MISC_SET, MSG_OUT_OF_MEMORY));
	  exit (1);
	  return NULL;
	}

      new_chunk->next = link_chunks;
      link_chunks = new_chunk;
      link_free_list = new_chunk->link;

      /* This loop executes LCHUNK-1 times. the last (LCHUNK-th) link
         is taken care of after the loop. */
      for (p = link_free_list, i = LCHUNK; --i > 0; ++p)
	{
	  p->next = p + 1;
	}

      p->next = NULL;
    }

  p = link_free_list;
  link_free_list = link_free_list->next;

  ++links_allocated;

  memset ((char *) p, 0, sizeof (LINK));
  p->decl.s.is_long = 0;
  p->decl.s.is_short = 0;
  p->next = NULL;

  return p;
}

/*
 * pp_discard_link_chain() - Discard all links in the chain.
 *    Nothing is removed from the structure table, however.
 * return : void
 * p(in): The head of a chain of links to be discarded.
 */
void
pp_discard_link_chain (LINK * p)
{
  LINK *next;

  while (p)
    {
      next = p->next;
      pp_discard_link (p);
      p = next;
    }
}

/*
 * pp_discard_link() - Discard a single link.
 * return : void
 * p(in): the link to be discarded
 */
void
pp_discard_link (LINK * p)
{
  if (IS_FUNCT (p))
    {
      pp_discard_symbol_chain (p->decl.d.args);
    }

  if (IS_ARRAY (p) && p->decl.d.num_ele)
    {
      free_and_init (p->decl.d.num_ele);
    }

  ++links_deallocated;

  p->next = link_free_list;
  link_free_list = p;
}

/*
 * pp_new_structdef() - Allocate a new structdef.
 * return : STRUCTDEF *
 * tag(in): the tagname for the struct
 */
STRUCTDEF *
pp_new_structdef (const char *tag)
{
  /*
   * Structs, even "anonymous" ones, must always have tag names for the
   * benefit of the symbol table manager, so if none was supplied at
   * creation time we must manufacture one ourselves.  These variables
   * are used in the manufacture of those names.
   */
  static int anon_tag = 0;
  char buf[20];

  STRUCTDEF *sdef;

  if (struct_free_list == NULL)
    {
      sdef = (STRUCTDEF *) es_ht_alloc_new_symbol (sizeof (STRUCTDEF));
    }
  else
    {
      sdef = struct_free_list;
      struct_free_list = struct_free_list->next;
    }

  ++sdefs_allocated;

  if (tag == NULL)
    {
      sprintf (buf, "$%d", anon_tag);
      anon_tag++;
      tag = buf;
    }

  sdef->tag = (unsigned char *) strdup (tag);
  sdef->type_string = (unsigned char *) "struct";
  sdef->type = 1;		/* struct */
  sdef->fields = NULL;
  sdef->next = NULL;
  sdef->by_name = 0;

  return sdef;
}

/*
 * pp_discard_structdef() - Discard a structdef and any attached fields,
 *    but don't discard any linked structure definitions.
 * return : void
 * sdef(in): the structdef to be discarded
 */
void
pp_discard_structdef (STRUCTDEF * sdef)
{
  if (sdef)
    {
      ++sdefs_deallocated;

      if (sdef->tag)
	{
	  free_and_init (sdef->tag);
	}
      pp_discard_symbol_chain (sdef->fields);

      if (syms_to_free_lists)
	{
	  sdef->next = struct_free_list;
	  struct_free_list = sdef;
	}
      else
	{
	  es_ht_free_symbol ((void *) sdef);
	}
    }
}

/*
 * pp_discard_structdef_chain() -
 * return : void
 * sdef_chain(in): A chain of structdefs to be discarded.
 */
void
pp_discard_structdef_chain (STRUCTDEF * sdef_chain)
{
  STRUCTDEF *sdef, *next = NULL;

  for (sdef = sdef_chain; sdef; sdef = next)
    {
      next = sdef->next;
      pp_discard_structdef (sdef);
    }
}

/*
 * pp_new_pseudo_def() - Allocate a new structdef that describes
 *    the declaration
 *      struct {
 *          long length;
 *          char array[n];
 *      };
 *   This is used during the conversion of 'varchar' declarations into
 *   anonymous structs.
 *
 * returns/side-effects: STRUCTDEF *
 * type(in): the type of the pseudo def (bit, varbit, varchar)
 * subscript(in) : an expression for the array bound of the pseudo def
 */
STRUCTDEF *
pp_new_pseudo_def (SPECIFIER_NOUN type, const char *subscript)
{
  STRUCTDEF *sdef;
  SYMBOL *length_field, *array_field, *db_int32;

  pp_push_spec_scope ();

  array_field = pp_new_symbol (VARCHAR_ARRAY_NAME, pp_nesting_level);
  pp_reset_current_type_spec ();
  pp_add_type_noun (CHAR_);
  pp_add_declarator (array_field, D_ARRAY);
  if (type == N_VARCHAR)
    {
      array_field->etype->decl.d.num_ele = pp_strdup (subscript);
    }
  else
    {
      varstring new_subscript;
      vs_new (&new_subscript);
      vs_sprintf (&new_subscript, "((%s)+7)/8", subscript);
      array_field->etype->decl.d.num_ele =
	pp_strdup (vs_str (&new_subscript));
      vs_free (&new_subscript);
    }
  pp_add_spec_to_decl (pp_current_type_spec (), array_field);
  pp_discard_link (pp_current_type_spec ());

  length_field = pp_new_symbol (VARCHAR_LENGTH_NAME, pp_nesting_level);
  db_int32 = pp_findsym (pp_Symbol_table, (unsigned char *) "int");
  pp_reset_current_type_spec ();
  pp_add_typedefed_spec (db_int32->type);
  pp_add_spec_to_decl (pp_current_type_spec (), length_field);
  /*
   * Don't use pp_discard_link(pp_current_spec()) here, since it came
   * from a typedef.
   */

  pp_pop_spec_scope ();

  length_field->next = array_field;

  sdef = pp_new_structdef (NULL);
  sdef->fields = length_field;
  pp_Struct_table->add_symbol (pp_Struct_table, sdef);

  return sdef;
}

/*
 * pp_add_declarator() - Add a declarator link to the end of a chain, the head
 *    of which is pointed to by sym->type and the tail of which is pointed to
 *    by sym->etype.  *head must be NULL when the chain is empty.  Both
 *    pointers are modified as necessary.
 * return : void
 * sym(in) : the symbol whose type description is to be modified
 * type(in) : the new type to be added to the declarator chain
 */
void
pp_add_declarator (SYMBOL * sym, int type)
{
  LINK *link;

  if (type == D_FUNCTION && sym->etype && IS_ARRAY (sym->etype))
    {
      esql_yyverror (pp_get_msg (EX_SYMBOL_SET, MSG_ILLEGAL_ARRAY));
      pp_add_declarator (sym, D_POINTER);
    }

  link = pp_new_link ();
  if (link == NULL)
    {
      return;
    }

  link->decl.d.dcl_type = type;

  if (sym->type == NULL)
    {
      sym->type = sym->etype = link;
    }
  else if (sym->etype != NULL)
    {
      sym->etype->next = link;
      sym->etype = link;
    }

  if (type == D_FUNCTION && sym->etype != NULL)
    {
      sym->args = NULL;
      sym->etype->decl.d.args = NULL;
    }
}

/*
 * pp_clone_symbol() -
 * return:
 * sym(in) :
 */
SYMBOL *
pp_clone_symbol (SYMBOL * sym)
{
  SYMBOL *newsym;

  newsym = pp_new_symbol ((char *) sym->name, sym->level);
  if (newsym == NULL)
    {
      return NULL;
    }

  newsym->type = pp_clone_type (sym->type, &newsym->etype);
  newsym->next = NULL;

  return newsym;
}

/*
 * pp_clone_type() - Manufacture a clone of the type chain in the input symbol.
 *    The tdef bit in the copy is always cleared.
 * return : A pointer to the cloned chain, NULL if there were no declarators
 *          to clone.
 * tchain(in): the type chain to duplicate
 * endp(in): pointer to the last node in the cloned chain
 */
LINK *
pp_clone_type (LINK * tchain, LINK ** endp)
{
  LINK *last = NULL, *head = NULL;

  for (; tchain; tchain = tchain->next)
    {
      if (head == NULL && last == NULL)	/* 1st node in the chain */
	{
	  head = last = pp_new_link ();
	}
      else			/* Subsequent node */
	{
	  last->next = pp_new_link ();
	  if (last->next == NULL)
	    {
	      return NULL;
	    }
	  last = last->next;
	}

      memcpy ((char *) last, (char *) tchain, sizeof (*last));
      if (IS_ARRAY (tchain) && tchain->decl.d.num_ele)
	{
	  last->decl.d.num_ele = pp_strdup (tchain->decl.d.num_ele);
	}
      last->next = NULL;
      last->tdef = NULL;

      if (IS_FUNCT (last))
	{
	  SYMBOL *sym, **lastsym;
	  lastsym = &last->decl.d.args;
	  sym = last->decl.d.args;
	  while (sym)
	    {
	      *lastsym = pp_clone_symbol (sym);
	      lastsym = &(*lastsym)->next;
	      sym = sym->next;
	    }
	}
    }

  *endp = last;

  return head;
}

/*
 * pp_the_same_type() - Return 1 if the types match, 0 if they don't.
 *    Ignore the storage class.  If 'relax' is true and the array declarator
 *    is the first link in the chain, then a pointer is considered equivalent
 *    to an array.
 * return : 1 if the types match, 0 if they don't.
 * p1(in): a type chain
 * p2(in): another type chain
 * relax(in): true iff arrays should be considered equivalent to pointers
 */
int
pp_the_same_type (LINK * p1, LINK * p2, int relax)
{
  if (relax && IS_PTR_TYPE (p1) && IS_PTR_TYPE (p2))
    {
      p1 = p1->next;
      p2 = p2->next;
    }

  for (; p1 && p2; p1 = p1->next, p2 = p2->next)
    {
      if (p1->class_ != p2->class_)
	{
	  return 0;
	}

      if (p1->class_ == DECLARATOR)
	{
	  if (p1->decl.d.dcl_type != p2->decl.d.dcl_type)
	    {
	      return 0;
	    }
	}
      else
	{
	  if ((p1->decl.s.noun == p2->decl.s.noun)
	      && (p1->decl.s.is_long == p2->decl.s.is_long)
	      && (p1->decl.s.is_short == p2->decl.s.is_short)
	      && (p1->decl.s.is_unsigned == p2->decl.s.is_unsigned))
	    {
	      return (p1->decl.s.noun == N_STRUCTURE) ?
		p1->decl.s.val.v_struct == p2->decl.s.val.v_struct : 1;
	    }
	  return 0;
	}
    }

  esql_yyerror (pp_get_msg (EX_SYMBOL_SET, MSG_UNKNOWN_CLASS));
  return 0;
}

/*
 * pp_attr_str() - Return a string representing the interesting attributes
 *    in a specifier other than the noun and storage class.
 * return : char *
 * spec(in): a type link
 */
char *
pp_attr_str (LINK * type)
{
  static char str[32];

  assert (IS_SPECIFIER (type));

  str[0] = '\0';

  switch (type->decl.s.noun)
    {
    case N_CHR:
      if (type->decl.s.is_unsigned)
	{
	  strcpy (str, "unsigned ");
	}
      break;

    case N_INT:
      if (type->decl.s.is_unsigned)
	{
	  strcpy (str, "unsigned ");
	}

      strcat (str,
	      type->decl.s.is_long ? "long " : type->decl.s.
	      is_short ? "short " : "int ");
      break;

    case N_FLOAT:
      if (type->decl.s.is_long)
	{
	  strcpy (str, "long ");
	}
      break;

    default:
      break;
    }

  return str;
}

/*
 * pp_type_str() - Return a string representing the type represented by
 *    the link chain.
 * return : char *
 * link(in): a type chain
 */
const char *
pp_type_str (LINK * link)
{
  static char target[80];
  static char buf[256];

  target[0] = '\0';
  buf[0] = '\0';

  if (link == NULL)
    {
      return "(NULL)";
    }

  if (link->tdef)
    {
      strcpy (target, "tdef ");
    }

  for (; link; link = link->next)
    {
      if (IS_DECLARATOR (link))
	{
	  switch (link->decl.d.dcl_type)
	    {
	    case D_ARRAY:
	      strncpy (buf, "array of ", sizeof (buf));
	      break;
	    case D_POINTER:
	      strncpy (buf, "ptr to ", sizeof (buf));
	      break;
	    case D_FUNCTION:
	      strncpy (buf, "function returning ", sizeof (buf));
	      break;
	    default:
	      strncpy (buf, "BAD DECL", sizeof (buf));
	      break;
	    }
	}
      else
	{
	  /* it's a specifier */
	  const char *noun_str;

	  strncpy (buf, pp_attr_str (link), sizeof (buf));

	  switch (link->decl.s.noun)
	    {
	    case N_VOID:
	      noun_str = "void";
	      break;
	    case N_CHR:
	      noun_str = "char";
	      break;
	    case N_INT:
	      noun_str = "int";
	      break;
	    case N_FLOAT:
	      noun_str = "float";
	      break;
	    case N_LABEL:
	      noun_str = "label";
	      break;
	    case N_STRUCTURE:
	      noun_str =
		(const char *) link->decl.s.val.v_struct->type_string;
	      break;
	    case N_VARCHAR:
	      noun_str = "varchar";
	      break;
	    case N_BIT:
	      noun_str = "bit";
	      break;
	    case N_VARBIT:
	      noun_str = "bit varying";
	      break;
	    default:
	      noun_str = "BAD NOUN";
	      break;
	    }
	  strncat (buf, noun_str, sizeof (buf) - strnlen (buf, sizeof (buf)));

	  if (link->decl.s.noun == N_STRUCTURE)
	    {
	      strncat (target, buf,
		       sizeof (target) - strnlen (target, sizeof (target)));
	      snprintf (buf, sizeof (buf), " %s",
			(link->decl.s.val.v_struct->tag ? link->decl.s.val.
			 v_struct->tag : ((unsigned char *) "untagged")));
	    }
	}

      strncat (target, buf,
	       sizeof (target) - strnlen (target, sizeof (target)));
    }

  return target;
}

/*
 * es_print_symbol() -
 * return : void
 * sym(in): a symbol to be printed
 * fp(in): the stream to print on
 */
static void
es_print_symbol (SYMBOL * sym, FILE * fp)
{
  fprintf (fp, " * %-18.18s %4d  %s\n",
	   sym->name, sym->level, pp_type_str (sym->type));
}

/*
 * es_print_struct() -
 * return : void
 * sdef(in): a structure definition
 * fp(in): the stream to print it on
 */
static void
es_print_struct (STRUCTDEF * sdef, FILE * fp)
{
  SYMBOL *field;

  fprintf (fp, " * %s %s:\n", sdef->type_string,
	   (sdef->tag ? sdef->tag : ((unsigned char *) "<anon>")));

  for (field = sdef->fields; field; field = field->next)
    {
      fprintf (fp, " *     %-20s %s\n",
	       field->name, pp_type_str (field->type));
    }
}

/*
 * pp_print_syms() - Print the entire symbol table to the file.
 *    Previous contents of the file (if any) are destroyed.  Prints to stdout
 *    if filename is NULL.
 * return : void
 * fp(in): the file pointer to print the symbol table to
 */
void
pp_print_syms (FILE * fp)
{
  if (fp == NULL)
    {
      fp = stdout;
    }

  if (pp_Symbol_table->get_symbol_count (pp_Symbol_table))
    {
      fprintf (fp, " *\n * Symbol table:\n *\n");
      pp_Symbol_table->print_table (pp_Symbol_table, es_print_symbol, fp, 1);
    }

  if (pp_Struct_table->get_symbol_count (pp_Struct_table))
    {
      fprintf (fp, " *\n * Structure table:\n *\n");
      pp_Struct_table->print_table (pp_Struct_table,
				    (void (*)()) es_print_struct, fp, 1);
    }
}

/*
 * pp_findsym() - Find a pointer to the symbol with the given name, or NULL if
 *   there is no such symbol
 * return : SYMBOL *
 * symtab(in): The symbol table to be searched.
 * name(in): The name to be searched for.
 */
SYMBOL *
pp_findsym (SYMTAB * symtab, unsigned char *name)
{
  SYMBOL dummy;

  dummy.name = name;
  return symtab->find_symbol (symtab, &dummy);
}

/*
 * pp_symbol_init() - Initialize interesting variables before starting off.
 * return : void
 */
void
pp_symbol_init (void)
{
  pp_Symbol_table = pp_new_symtab ();
  pp_Struct_table = pp_new_symtab ();

  symbol_free_list = NULL;
  struct_free_list = NULL;
  link_free_list = NULL;
  link_chunks = NULL;

  syms_allocated = 0;
  syms_deallocated = 0;
  sdefs_allocated = 0;
  sdefs_deallocated = 0;
  links_allocated = 0;
  links_deallocated = 0;

  syms_to_free_lists = 1;
}

/*
 * pp_symbol_finish() - Clean up all of the various local data structures.
 * return : void
 */
void
pp_symbol_finish (void)
{
  /*
   * Make sure that the symbols that are about to be freed from the
   * symbol tables are returned to the malloc pool rather than to our
   * free lists.
   */
  syms_to_free_lists = 0;
  pp_free_symtab (pp_Symbol_table, (HT_FREE_FN) pp_discard_symbol);
  pp_free_symtab (pp_Struct_table, (HT_FREE_FN) pp_discard_structdef);

  /* However, there may still be some symbols left on our free lists;
     get rid of them. */
  {
    SYMBOL *sym, *next = NULL;

    for (sym = symbol_free_list; sym; sym = next)
      {
	next = sym->next;
	es_ht_free_symbol (sym);
      }
    symbol_free_list = NULL;
  }

  {
    STRUCTDEF *sdef, *next = NULL;

    for (sdef = struct_free_list; sdef; sdef = next)
      {
	next = sdef->next;
	es_ht_free_symbol ((void *) sdef);
      }
    struct_free_list = NULL;
  }

  {
    LINKCHUNK *chunk, *next = NULL;

    for (chunk = link_chunks; chunk; chunk = next)
      {
	next = chunk->next;
	free_and_init (chunk);
      }
    link_chunks = NULL;
  }
}

/*
 * pp_symbol_stats() - Print rudimentary statistics information.
 * return : void
 * fp(in): A stream to print the statistics on.
 */
void
pp_symbol_stats (FILE * fp)
{
#if !defined(NDEBUG)

  if (syms_allocated != syms_deallocated)
    {
      fprintf (stderr, "symbol accounting problem: %d/%d\n",
	       syms_allocated, syms_deallocated);
    }
  if (links_allocated != links_deallocated)
    {
      fprintf (stderr, "link accounting problem: %d/%d\n",
	       links_allocated, links_deallocated);
    }
  if (sdefs_allocated != sdefs_deallocated)
    {
      fprintf (stderr, "structdef accounting problem: %d/%d\n",
	       sdefs_allocated, sdefs_deallocated);
    }

  if (pp_dump_malloc_info)
    {
      fputs ("\n/*\n", fp);
      fprintf (fp, " * %d/%d symbols allocated/deallocated\n",
	       syms_allocated, syms_deallocated);
      fprintf (fp, " * %d/%d links allocated/deallocated\n",
	       links_allocated, links_deallocated);
      fprintf (fp, " * %d/%d structdefs allocated/deallocated\n",
	       sdefs_allocated, sdefs_deallocated);

      fputs (" */\n", fp);
    }
#endif
}
