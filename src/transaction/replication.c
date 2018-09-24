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
 * replication.c -
 */

#ident "$Id$"

#include "replication.h"

#if defined(SERVER_MODE) || defined(SA_MODE)

#include "error_code.h"
#include "log_impl.h"
#include "storage_common.h"

/*
 * repl_log_abort_after_lsa -
 *
 * return:
 *
 *   tdes (in) :
 *   start_lsa (in) :
 *
 */
int
repl_log_abort_after_lsa (LOG_TDES * tdes, LOG_LSA * start_lsa)
{
  // todo - abort partial
  // http://jira.cubrid.org/browse/CBRD-22214

  return NO_ERROR;
}

#endif /* SERVER_MODE || SA_MODE */
