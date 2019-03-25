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

//
// log_append - creating & appending log records
//

#ifndef _LOG_APPEND_HPP_
#define _LOG_APPEND_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif

#include "log_lsa.hpp"
#include "log_storage.hpp"
#include "storage_common.h"

#include <atomic>

typedef struct log_crumb LOG_CRUMB;
struct log_crumb
{
  int length;
  const void *data;
};

typedef struct log_data_addr LOG_DATA_ADDR;
struct log_data_addr
{
  const VFID *vfid;		/* File where the page belong or NULL when the page is not associated with a file */
  PAGE_PTR pgptr;
  PGLENGTH offset;		/* Offset or slot */

  log_data_addr () = default;
  log_data_addr (const VFID *vfid, PAGE_PTR pgptr, PGLENGTH offset);
};
#define LOG_DATA_ADDR_INITIALIZER { NULL, NULL, 0 } // todo: remove me

enum LOG_PRIOR_LSA_LOCK
{
  LOG_PRIOR_LSA_WITHOUT_LOCK = 0,
  LOG_PRIOR_LSA_WITH_LOCK = 1
};

typedef struct log_append_info LOG_APPEND_INFO;
struct log_append_info
{
  int vdes;			/* Volume descriptor of active log */
  std::atomic<LOG_LSA> nxio_lsa;  /* Lowest log sequence number which has not been written to disk (for WAL). */
  LOG_LSA prev_lsa;		/* Address of last append log record */
  LOG_PAGE *log_pgptr;		/* The log page which is fixed */

  log_append_info ();
  log_append_info (const log_append_info &other);

  LOG_LSA get_nxio_lsa () const;
  void set_nxio_lsa (const LOG_LSA &next_io_lsa);
};

void LOG_RESET_APPEND_LSA (const LOG_LSA *lsa);
void LOG_RESET_PREV_LSA (const LOG_LSA *lsa);
char *LOG_APPEND_PTR ();

#endif // !_LOG_APPEND_HPP_
