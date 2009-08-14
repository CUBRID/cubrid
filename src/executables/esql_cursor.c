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
 * esql_cursor.c - Preprocessor routines for manipulating and
 *                 storing cursor definitions.
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include "esql_misc.h"
#include "memory_alloc.h"

enum
{
  MSG_REDEFINITION = 1,
  MSG_STMT_TITLE = 2,
  MSG_TABLE_TITLE = 3
};

static HASH_TAB *pp_cursor_table;
static HASH_TAB *pp_stmt_table;
static int next_cid = 0;

static void pp_print_cursor (void *cp, void *fp);
/*
 * pp_new_cursor() - Allocate (and enter in pp_cursor_table) a new CURSOR
 *    struct, initializing it with the given name and initialize the static
 *    and dynamic fields accordingly.
 * return : CURSOR *
 * name(in): A character string.
 * static_stmt(in): A prepared SQL/X SELECT statement, or NULL.
 * length(in) :
 * dynamic_stmt(in): A STMT pointer, or NULL.
 * host_refs(in):
 */
CURSOR *
pp_new_cursor (char *name, char *static_stmt, int length,
	       STMT * dynamic_stmt, HOST_LOD * host_refs)
{
  CURSOR *cursor;

  cursor = pp_lookup_cursor (name);
  if (cursor && cursor->level >= pp_nesting_level)
    {
      esql_yyverror (pp_get_msg (EX_CURSOR_SET, MSG_REDEFINITION),
		     cursor->name);
      return NULL;
    }

  cursor = es_ht_alloc_new_symbol (sizeof (CURSOR));

  cursor->name = (unsigned char*)strdup (name);
  cursor->cid = next_cid++;
  cursor->level = pp_nesting_level;
  cursor->next = NULL;
  cursor->host_refs = host_refs;
  cursor->static_stmt = (unsigned char*)(static_stmt ? strdup (static_stmt) : NULL);
  cursor->stmtLength = length;
  cursor->dynamic_stmt = dynamic_stmt;

  pp_cursor_table->add_symbol (pp_cursor_table, cursor);
  pp_add_cursor_to_scope (cursor);

  return cursor;
}

/*
 * pp_free_cursor() - Free the given cursor.
 * return : void
 * cur(in): The cursor object to be freed.
 */
void
pp_free_cursor (CURSOR * cursor)
{
  free_and_init (cursor->name);

  if (cursor->static_stmt)
    {
      free_and_init (cursor->static_stmt);
    }
  if (cursor->host_refs)
    {
      pp_free_host_lod (cursor->host_refs);
    }
  /*
   * Don't free any dynamic statement associated with this cursor;
   * those statements have lifetimes that differ from the lifetimes of
   * cursors.
   */

  es_ht_free_symbol (cursor);
}

/*
 * pp_lookup_cursor() - Return the cursor structure with the given name,
 *    or NULL if not found.
 * return : CURSOR *
 * name(in): The name of the cursor to be found.
 */
CURSOR *
pp_lookup_cursor (char *name)
{
  CURSOR dummy;

  dummy.name = (unsigned char*)name;
  return pp_cursor_table->find_symbol (pp_cursor_table, &dummy);

}

/*
 * pp_cursor_init() - Initialize various module data.
 * return : void
 */
void
pp_cursor_init (void)
{
  pp_cursor_table =
    es_ht_make_table (37, pp_generic_case_hash, pp_generic_case_cmp);
  pp_stmt_table =
    es_ht_make_table (37, pp_generic_case_hash, pp_generic_case_cmp);
}

/*
 * pp_cursor_finish() - Teardown various module data.
 * return : nothing
 */
void
pp_cursor_finish (void)
{
  if (pp_cursor_table)
    {
      pp_cursor_table->free_table (pp_cursor_table,
				   (HT_FREE_FN) pp_free_cursor);
    }
  if (pp_stmt_table)
    {
      pp_stmt_table->free_table (pp_stmt_table, (HT_FREE_FN) pp_free_stmt);
    }
  pp_cursor_table = NULL;
  pp_stmt_table = NULL;
}

/*
 * pp_print_cursor() - Dump a printed representation of the cursor structure
 *    to the designated stream.
 * return : nothing
 * cursor(in): A pointer to a cursor to be printed.
 * fp(in): The stream to print it on.
 */
static void
pp_print_cursor (void *cp, void *fp)
{
  CURSOR *cursor = (CURSOR *) cp;
  FILE *stream = (FILE *) fp;

  fprintf (stream, " * %s (cid %d, level %d):\n",
	   cursor->name, cursor->cid, cursor->level);

  if (cursor->static_stmt)
    {
      int i;
      fprintf (stream, " *\t%s\n", cursor->static_stmt);
      if (cursor->host_refs)
	{
	  for (i = 0; i < cursor->host_refs->n_refs; ++i)
	    {
	      fprintf (stream, " *\thv[%d]: ", i);
	      pp_print_host_ref (&cursor->host_refs->refs[i], stream);
	      fputs ("\n", stream);
	    }
	}
    }
  else
    fprintf (stream, pp_get_msg (EX_CURSOR_SET, MSG_STMT_TITLE),
	     cursor->dynamic_stmt->name);

  fputs (" *\n", stream);
}

/*
 * pp_print_cursors() - Print a representation of every declared cursor
 *    on the given stream.
 * return : void
 * fp(in): The stream on which to print the contents of the cursor table.
 */
void
pp_print_cursors (FILE * fp)
{
  if (!fp)
    {
      fp = stdout;
    }

  if (pp_cursor_table->get_symbol_count (pp_cursor_table))
    {
      fputs (pp_get_msg (EX_CURSOR_SET, MSG_TABLE_TITLE), fp);
      pp_cursor_table->print_table (pp_cursor_table, pp_print_cursor, fp, 1);
    }
}

/*
 * pp_remove_cursors_from_table() -
 * returns/side-effects: nothing
 * chain(in): The head of a list of cursors to be removed from
 *    the cursor table.
 */
void
pp_remove_cursors_from_table (CURSOR * chain)
{
  CURSOR *next = NULL;

  while (chain)
    {
      next = chain->next;
      pp_cursor_table->remove_symbol (pp_cursor_table, chain);
      chain = next;
    }

}

/*
 * pp_discard_cursor_chain() - Discard all of the cursors in the chain.
 * return : void
 * chain(in): The head of a list of cursors to be discarded.
 */
void
pp_discard_cursor_chain (CURSOR * chain)
{
  CURSOR *next = NULL;

  while (chain)
    {
      next = chain->next;
      pp_free_cursor (chain);
      chain = next;
    }

}

/*
 * pp_new_stmt() - Build a new STMT record for an embedded dynamic statement
 *    (e.g., the thing that arises from an esql statement such as
 *     "PREPARE stmt FROM :buffer").
 * return : STMT *
 * name(in): The name of the dynamic statement.
 *
 */
STMT *
pp_new_stmt (char *name)
{
  static int sid = 0;
  STMT *stmt;
  STMT dummy;

  dummy.name = (unsigned char*)name;
  stmt = pp_stmt_table->find_symbol (pp_stmt_table, &dummy);
  if (stmt != NULL)
    {
      return stmt;
    }

  stmt = es_ht_alloc_new_symbol (sizeof (STMT));

  stmt->name = (unsigned char*)strdup (name);
  stmt->sid = sid++;

  pp_stmt_table->add_symbol (pp_stmt_table, stmt);

  return stmt;
}

/*
 * pp_free_stmt() - Free the indicated structure.
 *   It is assumed to have already been removed from pp_stmt_table
 *   if it was ever in there.
 * return : void
 * stmt(in): A pointer to a STMT struct to be freed.
 */
void
pp_free_stmt (STMT * stmt)
{
  if (stmt == NULL)
    {
      return;
    }

  free_and_init (stmt->name);
  es_ht_free_symbol (stmt);
}
