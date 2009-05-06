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
 * esql_misc.c - Generic helper functions for the esql preprocessor
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include "chartype.h"
#include "util_func.h"
#include "misc_string.h"
#include "memory_alloc.h"
#include "intl_support.h"
#include "esql_misc.h"
#include "esql_translate.h"

extern FILE *esql_yyout;

/*
 * This struct is used as a catchall struct for all of the various
 * structures that are put in hash tables. We take care that each such
 * structure is defined with its name field first, so that we can safely
 * cast a pointer to such a struct as a pointer to a GENERIC struct.
 * This allows all of the hash tables to use the same hash and comparison
 * functions.
 */
typedef struct generic GENERIC;

struct generic
{
  char *name;
};

enum
{
  MSG_OUT_OF_MEMORY = 1
};

varstring pp_subscript_buf;
varstring pp_host_var_buf;

static void tr_prelude (void);
static void grow_ptr_vec (PTR_VEC * vec);

/*
 * pp_generic_case_hash() - Assumes that p points to a GENERIC-compatible
 *    struct, and hashes on the lowercased name field of that struct.
 * return : hash value
 * p(in): A pointer to a generic structure to be hashed.
 */
unsigned int
pp_generic_case_hash (void *p)
{
  char *s, *d, nam[256];
  int i;
  GENERIC *gp;

  gp = (GENERIC *) p;
  s = gp->name;
  d = nam;
  if (s)
    {
      for (i = 0; *s && i < 256; s++, d++, i++)
	{
	  *d = char_tolower (*s);
	}
    }
  *d = '\0';

  return hashpjw (nam);
}

/*
 * pp_generic_hash() - Assumes that p points to a GENERIC-compatible struct,
 *    and hashes on the name field of that struct.
 * return : hash value
 * p(in): A pointer to a generic structure to be hashed.
 */

unsigned int
pp_generic_hash (void *p)
{
  GENERIC *gp = (GENERIC *) p;
  return hashpjw (gp->name);
}

/*
 * pp_generic_case_cmp() - Assumes that both pointers point to
 *    GENERIC-compatible structs. returns -1, 0, or 1 according to
 *    the standard case-insensitive comparison of the name fields
 *    of those structs.
 * return : int
 * p1(in): A pointer to a structure to be compared.
 * p2(in): Another pointer to a structure to be compared.
 */
int
pp_generic_case_cmp (void *p1, void *p2)
{
  GENERIC *gp1 = (GENERIC *) p1;
  GENERIC *gp2 = (GENERIC *) p2;

  return intl_mbs_casecmp (gp1->name, gp2->name);
}

/*
 * pp_generic_cmp() - Assumes that both pointers point to GENERIC-compatible
 *    structs; returns -1, 0, or 1 according to the standard comparison of
 *    the name fields of those structs.
 * return : int
 * p1(in): A pointer to a structure to be compared.
 * p2(in): Another pointer to a structure to be compared.
 */
int
pp_generic_cmp (void *p1, void *p2)
{
  GENERIC *gp1 = (GENERIC *) p1;
  GENERIC *gp2 = (GENERIC *) p2;

  return strcmp (gp1->name, gp2->name);
}

/*
 * pp_startup() - Global initialization to be called before parsing a file.
 * return : void
 */
void
pp_startup (void)
{
  esql_Translate_table.tr_set_line_terminator ("\n");
  esql_Translate_table.tr_set_out_stream (esql_yyout);
  vs_new (&pp_subscript_buf);
  vs_new (&pp_host_var_buf);
  pp_symbol_init ();
  pp_cursor_init ();
  pp_decl_init ();
  pp_hv_init ();
  tr_prelude ();
}

/*
 * pp_finish() - Global teardown to be called after parsing a file.
 * return : void
 */
void
pp_finish (void)
{
  vs_free (&pp_subscript_buf);
  vs_free (&pp_host_var_buf);
  pp_hv_finish ();
  pp_decl_finish ();
  pp_cursor_finish ();
  pp_symbol_finish ();
  pp_symbol_stats (esql_yyout);
}

/*
 * tr_prelude() - Emit the prelude jive (include files, etc.) required by the
 *    generated code.
 * return : void
 */
static void
tr_prelude (void)
{
  const char *default_include_file = "cubrid_esql.h";

  if (pp_varchar2)
    {
      fprintf (esql_yyout, "#define _ESQLX_VARCHAR2_STYLE_\n");
    }
  fprintf (esql_yyout, "#include \"%s\"\n",
	   pp_include_file ? pp_include_file : default_include_file);
}

/*
 * pp_new_ptr_vec() - Create a new counted vector, and initialize it as empty.
 * return : PTR_VEC *
 * vec(in): The address of a PTR_VEC structure to be initialized, or NULL.
 */
PTR_VEC *
pp_new_ptr_vec (PTR_VEC * vec)
{
  if (vec == NULL)
    {
      vec = malloc (sizeof (PTR_VEC));
      vec->heap_allocated = TRUE;
    }
  else
    {
      vec->heap_allocated = FALSE;
    }

  vec->n_elems = 0;
  vec->max_elems = DIM (vec->inline_elems);
  vec->elems = vec->inline_elems;
  vec->chunk_size = PTR_VEC_CHUNK_SIZE;

  return vec;
}

/*
 * pp_free_ptr_vec() - Free the internal structures used by the PTR_VEC,
 *    and the PTR_VEC itself if it was heap-allocated.
 * return : void
 * vec(in): A pointer to a PTR_VEC, or NULL.
 */
void
pp_free_ptr_vec (PTR_VEC * vec)
{
  int i;

  if (vec == NULL)
    {
      return;
    }

  for (i = 0; i < vec->n_elems; ++i)
    {
      if (vec->elems[i])
	{
	  free_and_init (vec->elems[i]);
	}
    }

  if (vec->elems != vec->inline_elems)
    {
      free_and_init (vec->elems);
    }

  if (vec->heap_allocated)
    {
      free_and_init (vec);
    }
}

static void
grow_ptr_vec (PTR_VEC * vec)
{
  int new_max_elems;
  void **new_elems;

  new_max_elems = vec->max_elems + vec->chunk_size;
  if (vec->elems == vec->inline_elems)
    {
      new_elems =
	(void **) pp_malloc (sizeof (vec->elems[0]) * new_max_elems);
      memcpy ((char *) new_elems, (char *) vec->elems,
	      sizeof (vec->elems[0]) * vec->n_elems);
    }
  else
    {
      new_elems = (void **)
	realloc (vec->elems, sizeof (vec->elems[0]) * new_max_elems);
    }

  if (new_elems == NULL)
    {
      esql_yyverror (pp_get_msg (EX_MISC_SET, MSG_OUT_OF_MEMORY));
      exit (1);
    }

  vec->elems = new_elems;
  vec->max_elems = new_max_elems;
}

/*
 * pp_add_ptr() - Add another element to the end of the vector, reallocating
 *    and extending the internal array vector if necessary.
 * return : PTR_VEC *
 * vec(in): A pointer to a PTR_VEC that should receive a new element.
 * new_elem(in): A pointer to be added to the vector.
 */
PTR_VEC *
pp_add_ptr (PTR_VEC * vec, void *new_elem)
{
  if (vec == NULL)
    {
      return NULL;
    }
  else if (vec->n_elems >= vec->max_elems)
    {
      grow_ptr_vec (vec);
    }

  vec->elems[vec->n_elems++] = new_elem;
  return vec;
}

/*
 * pp_ptr_vec_n_elems() - Return the number of elements currently stored
 *    in the vector.
 * return : int
 * vec(in): A pointer to some PTR_VEC, or NULL.
 */
int
pp_ptr_vec_n_elems (PTR_VEC * vec)
{
  if (vec != NULL)
    {
      return vec->n_elems;
    }
  else
    {
      return 0;
    }
}

/*
 * pp_ptr_vec_elems() - Return a pointer to the internal vector of void*
 *     pointers, or NULL.
 * return : void **
 * vec(in): A pointer to a PTR_VEC, or NULL.
 */
void **
pp_ptr_vec_elems (PTR_VEC * vec)
{
  if (vec != NULL && vec->n_elems > 0)
    {
      return vec->elems;
    }
  else
    {
      return NULL;
    }
}

/*
 * pp_malloc() - Safe malloc. prints a message and exits if no memory.
 * return : void *
 * int(in): number of bytes needed
 */
void *
pp_malloc (int n)
{
  char *tmp;

  tmp = malloc (n);
  if (tmp == NULL)
    {
      fprintf (stderr, "%s: %s\n",
	       prog_name, pp_get_msg (EX_MISC_SET, MSG_OUT_OF_MEMORY));
      exit (1);
    }
  return tmp;
}

/*
 * pp_strdup() - Safe strdup. prints a message and exits if no memory.
 * return : char *
 * str: string to be copied
 */
char *
pp_strdup (const char *str)
{
  long n;
  char *tmp;

  if (str == NULL)
    {
      return NULL;
    }
  /* includes the null terminator of 'str' */
  n = strlen (str) + 1;
  tmp = pp_malloc (n);
  memcpy (tmp, str, n);
  return tmp;
}
