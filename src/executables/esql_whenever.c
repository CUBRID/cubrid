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
 * esql_whenever.c - Routines for preprocessor handling of whenever conditions.
 */

#ident "$Id$"

#include <stdlib.h>
#include "misc_string.h"
#include "esql_misc.h"
#include "memory_alloc.h"
#include "esql_translate.h"


static void
free_last_referenced_name (WHENEVER_SCOPE * scope,
			   WHENEVER_SCOPE * new_scope);

static WHENEVER_SCOPE default_whenever_scope_initializer =
  { {{CONTINUE, NULL}, {CONTINUE, NULL}, {CONTINUE, NULL}} };

#define LAST_REF(condition)                                             \
    ((name = scope->cond[condition].name) != NULL                       \
     && (new_scope == NULL || name != new_scope->cond[condition].name))

static void
free_last_referenced_name (WHENEVER_SCOPE * scope, WHENEVER_SCOPE * new_scope)
{
  char *name;

  if (LAST_REF (SQLWARNING))
    {
      free_and_init (name);
    }
  if (LAST_REF (SQLERROR))
    {
      free_and_init (name);
    }
  if (LAST_REF (NOT_FOUND))
    {
      free_and_init (name);
    }
}

/*
 * pp_init_whenever_scope() - Allocate a new whenever scope block,
 *    initializing it from the old scope.
 * return : void
 * scope(in) :
 * old_scope(in) : A pointer to another scope block.
 */
void
pp_init_whenever_scope (WHENEVER_SCOPE * scope, WHENEVER_SCOPE * old_scope)
{
  if (old_scope == NULL)
    {
      old_scope = &default_whenever_scope_initializer;
    }
  /*
   * note : this copies any existing pointers to names associated
   * with GOTO and CALL actions, so you can't just blindly free_and_init()
   * the pointers when the scope is exited.
   */
  *scope = *old_scope;
}

/*
 * pp_finish_whenever_scope() - Deallocate the scope if necessary,
 *    and free all strings it holds.
 * return : void
 * scope(in): A pointer to a scope to be freed.
 * new_scope(in):
 */
void
pp_finish_whenever_scope (WHENEVER_SCOPE * scope, WHENEVER_SCOPE * new_scope)
{

  free_last_referenced_name (scope, new_scope);

  if (new_scope == NULL)
    {
      new_scope = &default_whenever_scope_initializer;
    }

  esql_Translate_table.tr_whenever (SQLWARNING,
				    new_scope->cond[SQLWARNING].action,
				    new_scope->cond[SQLWARNING].name);
  esql_Translate_table.tr_whenever (SQLERROR,
				    new_scope->cond[SQLERROR].action,
				    new_scope->cond[SQLERROR].name);
  esql_Translate_table.tr_whenever (NOT_FOUND,
				    new_scope->cond[NOT_FOUND].action,
				    new_scope->cond[NOT_FOUND].name);
}
