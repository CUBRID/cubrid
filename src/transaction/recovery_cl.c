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
 * recovery_cl.c - 
 */

#ident "$Id$"

#include "config.h"

#include "recovery_cl.h"
#include "error_manager.h"

static int dummy_redo (const int buffer_size, char *buffer);
static int dummy_undo (const int buffer_size, char *buffer);
static void dummy_dump (FILE * fp, const int buffer_size, void *data);

/*
 * THE ARRAY OF CLIENT RECOVERY FUNCTIONS
 */
struct rvcl_fun RVCL_fun[] = {
  {RVCL_DUMMY,
   dummy_undo,
   dummy_redo,
   dummy_dump,
   dummy_dump},
};

static int
dummy_redo (const int buffer_size, char *buffer)
{
  return true;
}

static int
dummy_undo (const int buffer_size, char *buffer)
{
  return true;
}

static void
dummy_dump (FILE * fp, const int buffer_size, void *data)
{
  return;
}

/*
 * rv_rcvcl_index_string - RETURN STRING ASSOCIATED WITH THE CLIENT LOG_RVINDEX
 *
 * return:
 *
 *   rcvindex(in): Numeric recovery index
 *
 * NOTE:Return the string index associated with the given recovery
 *              index.
 */
const char *
rv_rcvcl_index_string (LOG_RCVCLIENT_INDEX rcvindex)
{
  switch (rcvindex)
    {
    case RVCL_DUMMY:
      return "RVCL_DUMMY";
    default:
      break;
    }

  return "UNKNOWN";

}
