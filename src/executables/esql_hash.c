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
 * esql_hash.c - Generic hash table implementation.
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include "util_func.h"
#include "misc_string.h"
#include "esql_hash.h"
#include "esql_misc.h"
#include "memory_alloc.h"

typedef struct bucket BUCKET;
struct bucket
{
  BUCKET *next;
  BUCKET **prev;
};

typedef struct hash_tab_impl HASH_TAB_IMPL;
struct hash_tab_impl
{
  HASH_TAB ifs;
  int size;			/* Max number of elements in table     */
  int numsyms;			/* Current number of elements in table */
  HT_HASH_FN hash;		/* Hash function                       */
  HT_CMP_FN cmp;		/* Comparison function                 */
  BUCKET *table[1];		/* First element of actual table       */
};

enum
{
  MSG_INTERNAL_ERROR = 1,
  MSG_TABLE_OVERFLOW = 2
};

/* Function pointers used to hold a hash table's comparison function while
   sorting the table for printing. */
static HT_CMP_FN es_User_cmp;

static void es_write_log (const char *fname, const char *msg);
static int es_symcmp (const void *p1, const void *p2);

static void es_ht_free_table (HASH_TAB * table, HT_FREE_FN free_fn);
static void *es_ht_add_symbol (HASH_TAB * table, void *sym);
static void es_ht_remove_symbol (HASH_TAB * table, void *sym);
static void *es_ht_find_symbol (HASH_TAB * table, void *sym);
static void *es_ht_next_symbol (HASH_TAB * tbl, void *last_sym);
static int
es_ht_print_table (HASH_TAB * table, void (*print) (), void *param, int sort);
static int es_ht_get_symbol_count (HASH_TAB * table);

/*
 * es_write_log() - write log message to file
 * return: void
 * fname(in) : log file name to write
 * msg(in) : message string
 */
static void
es_write_log (const char *fname, const char *msg)
{
  char *msg_copy;

  /* We have to copy the message in case it came from pp_get_msg(). */
  msg_copy = pp_strdup (msg);

  fprintf (stderr,
	   pp_get_msg (EX_HASH_SET, MSG_INTERNAL_ERROR), fname, msg_copy);

  free_and_init (msg_copy);
}

/*
 * es_symcmp() - Helper function used while sorting a hash table.
 * return : int
 * p1(in):
 * p2(in):
 */
static int
es_symcmp (const void *p1, const void *p2)
{
  BUCKET **b1 = (BUCKET **) p1, **b2 = (BUCKET **) p2;
  return (*es_User_cmp) (*b1 + 1, *b2 + 1);
}

/*
 * es_ht_free_table() - Frees the table and all symbols in.
 * return : nothing
 * table(in): A pointer to the table to be freed.
 * free_fn(in): A callback function for symbols being freed.
 */
static void
es_ht_free_table (HASH_TAB * table, HT_FREE_FN free_fn)
{
  int i;
  BUCKET **chain, *bucket, *next;
  HASH_TAB_IMPL *tbl = (HASH_TAB_IMPL *) table;

  if (tbl == NULL)
    {
      return;
    }

  for (chain = tbl->table, i = tbl->size; --i >= 0; ++chain)
    {
      for (bucket = *chain; bucket; bucket = next)
	{
	  next = bucket->next;
	  (*free_fn) ((void *) (bucket + 1));
	}
    }

  free_and_init (tbl);
}


/*
 * es_ht_add_symbol() - Add a symbol to the hash table.
 * return : void *
 * table(in): The table to receive the symbol.
 * sym(in): The symbol to be inserted.
 */
static void *
es_ht_add_symbol (HASH_TAB * table, void *sym)
{
  BUCKET **p, *tmp;
  BUCKET *bucket = (BUCKET *) sym - 1;
  HASH_TAB_IMPL *tbl = (HASH_TAB_IMPL *) table;

  p = &(tbl->table)[(*tbl->hash) (sym) % tbl->size];

  tmp = *p;
  *p = bucket;
  bucket->prev = p;
  bucket->next = tmp;

  if (tmp)
    {
      tmp->prev = &bucket->next;
    }

  tbl->numsyms++;

  return (void *) (bucket + 1);
}

/*
 * es_ht_remove_symbol() - Remove a symbol from a hash table.
 * return : void
 * table(in): The table from which the symbol should be deleted.
 * sym(in): The symbol to be deleted.
 */
static void
es_ht_remove_symbol (HASH_TAB * table, void *sym)
{
  HASH_TAB_IMPL *tbl = (HASH_TAB_IMPL *) table;

  if (tbl && sym)
    {
      BUCKET *bucket = (BUCKET *) sym - 1;

      --tbl->numsyms;
      *(bucket->prev) = bucket->next;
      if (*(bucket->prev))
	{
	  bucket->next->prev = bucket->prev;
	}
    }
}

/*
 * es_ht_find_symbol() - Return a pointer to the hash table element
 *    having a particular key, or NULL if the name isn't in the table.
 * return : void *
 * tbl(in): The hash table to be searched.
 * sym(in): The symbol to search for.
 */
static void *
es_ht_find_symbol (HASH_TAB * table, void *sym)
{
  BUCKET *p;
  HASH_TAB_IMPL *tbl = (HASH_TAB_IMPL *) table;

  if (tbl == NULL)
    {
      return NULL;
    }

  p = (tbl->table)[(*tbl->hash) (sym) % tbl->size];

  while (p && (*tbl->cmp) (sym, p + 1))
    {
      p = p->next;
    }

  return (void *) (p ? p + 1 : NULL);
}

/*
 * es_ht_next_symbol() - Return a pointer to the next node in the current
 *   chain that has the same key as the last node found (or NULL if there is
 *   no such node).
 * return : void *
 * table: The table to be searched.
 * last_sym: pointer returned from a previous es_ht_find_symbol() or
 *           es_ht_next_symbol().
 */
static void *
es_ht_next_symbol (HASH_TAB * table, void *last_sym)
{
  BUCKET *last_bucket = (BUCKET *) last_sym - 1;
  HASH_TAB_IMPL *tbl = (HASH_TAB_IMPL *) table;

  for (; last_bucket->next; last_bucket = last_bucket->next)
    {
      if ((tbl->cmp) (last_bucket + 1, last_bucket->next + 1) == 0)
	{
	  return (void *) (last_bucket->next + 1);
	}
    }

  return NULL;
}

/*
 * es_ht_print_table() - This function prints the table with given
 *    print function.
 * return : 0 if a sorted table can't be printed because of insufficient
 *    memory, else return 1 if the table was printed.
 * table(in): The hash table to be printed.
 * print(in): The function used to print a symbol.
 * param(in): Parameter passed to the print function.
 * sort(in): TRUE if the table should be sorted.
 *
 * note : The print function is called with two arguments:
 *       (*print)(sym, param)
 *       'sym' is a pointer to a BUCKET user area, and 'param' is the
 *       third argument to ptab().
 */
static int
es_ht_print_table (HASH_TAB * table, void (*print) (), void *param, int sort)
{
  BUCKET **outtab, **outp, *sym, **symtab;
  int i;
  HASH_TAB_IMPL *tbl = (HASH_TAB_IMPL *) table;

  if (tbl == NULL || tbl->size == 0)	/* Table is empty */
    {
      return 1;
    }

  if (!sort)
    {
      for (symtab = tbl->table, i = tbl->size; --i >= 0; ++symtab)
	{
	  /*
	   * Print all symbols in the current chain.  The +1 in the
	   * print call adjusts the bucket pointer to point to the user
	   * area of the bucket.
	   */
	  for (sym = *symtab; sym; sym = sym->next)
	    {
	      (*print) (sym + 1, param);
	    }
	}
    }
  else
    {
      /*
       * Allocate enough memory for 'outtab', an array of pointers to
       * BUCKETs, and initialize it.  'outtab' is different from the
       * actual hash table in that every 'outtab' element points to a
       * single BUCKET structure, rather than to a linked list of them.
       *
       * Go ahead and just use malloc() here; it's not a terrible
       * problem if we can't get enough memory.
       */
      outtab = (BUCKET **) pp_malloc (tbl->numsyms * sizeof (BUCKET *));
      outp = outtab;

      for (symtab = tbl->table, i = tbl->size; --i >= 0; ++symtab)
	{
	  for (sym = *symtab; sym; sym = sym->next)
	    {
	      if (outp > outtab + tbl->numsyms)
		{
		  es_write_log ("es_ht_print_table",
				pp_get_msg (EX_HASH_SET, MSG_TABLE_OVERFLOW));
		  free_and_init (outtab);
		  return 0;
		}
	      *outp++ = sym;
	    }
	}

      /*
       * Sort 'outtab' and then print it.
       */
      es_User_cmp = tbl->cmp;
      qsort (outtab, tbl->numsyms, sizeof (BUCKET *), es_symcmp);

      for (outp = outtab, i = tbl->numsyms; --i >= 0; ++outp)
	{
	  (*print) ((*outp) + 1, param);
	}

      free_and_init (outtab);
    }

  return 1;
}

/*
 * es_ht_get_symbol_count() - Gets the number of table entries.
 * return : number of table entries
 * table(in): Table to be checked.
 */
static int
es_ht_get_symbol_count (HASH_TAB * table)
{
  HASH_TAB_IMPL *tbl = (HASH_TAB_IMPL *) table;

  return tbl->numsyms;
}

/*
 * es_ht_alloc_new_symbol() - Allocate space for a new symbol.
 * return : pointer to the user space
 * size(in): size of symbol
 */
void *
es_ht_alloc_new_symbol (int size)
{
  BUCKET *sym = (BUCKET *) pp_malloc (size + sizeof (BUCKET));
  if (sym == NULL)
    {
      return NULL;
    }

  memset (sym, 0, size + sizeof (BUCKET));
  return (void *) (sym + 1);
}

/*
 * es_ht_free_symbol() - Free the bucket that holds sym.
 * return : void
 * sym(in): symbol to be freed
 */
void
es_ht_free_symbol (void *sym)
{
  BUCKET *bucket = (BUCKET *) sym - 1;
  free_and_init (bucket);
}

/*
 * es_ht_make_table() - Make a new table of the indicated size.
 * return : HASH_TAB *
 * maxsym(in): The number of hash slots in the table.
 * hash_function(in): The hash function used to map keys to slots.
 * cmp_function(in): The comparison function used to compare two entries.
 */
HASH_TAB *
es_ht_make_table (unsigned maxsym, HT_HASH_FN hash_function,
		  HT_CMP_FN cmp_function)
{
  HASH_TAB_IMPL *p;
  int n;

  if (maxsym == 0)
    {
      maxsym = 127;
    }

  n = sizeof (HASH_TAB_IMPL) + (maxsym * sizeof (BUCKET *));
  p = (HASH_TAB_IMPL *) pp_malloc (n);
  memset ((char *) p, 0, n);

  p->ifs.free_table = es_ht_free_table;
  p->ifs.add_symbol = es_ht_add_symbol;
  p->ifs.remove_symbol = es_ht_remove_symbol;
  p->ifs.find_symbol = es_ht_find_symbol;
  p->ifs.next_symbol = es_ht_next_symbol;
  p->ifs.print_table = es_ht_print_table;
  p->ifs.get_symbol_count = es_ht_get_symbol_count;

  p->size = maxsym;
  p->numsyms = 0;
  p->hash = hash_function;
  p->cmp = cmp_function;

  return (HASH_TAB *) p;
}
