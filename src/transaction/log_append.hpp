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
#include "log_record.hpp"
#include "log_storage.hpp"
#include "storage_common.h"

#include <atomic>
#include <mutex>

typedef struct log_crumb LOG_CRUMB;
struct log_crumb
{
  int length;
  const void *data;
};

typedef struct log_data_addr LOG_DATA_ADDR;
struct log_data_addr
{
  using offset_type = PGLENGTH;

  const VFID *vfid;		/* File where the page belong or NULL when the page is not associated with a file */
  PAGE_PTR pgptr;
  offset_type offset;		/* Offset or slot */

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

typedef struct log_prior_node LOG_PRIOR_NODE;
struct log_prior_node
{
  LOG_RECORD_HEADER log_header;
  LOG_LSA start_lsa;		/* for assertion */

  /* data header info */
  int data_header_length;
  char *data_header;

  /* data info */
  int ulength;
  char *udata;
  int rlength;
  char *rdata;

  LOG_PRIOR_NODE *next;
};

typedef struct log_prior_lsa_info LOG_PRIOR_LSA_INFO;
struct log_prior_lsa_info
{
  LOG_LSA prior_lsa;
  LOG_LSA prev_lsa;

  /* list */
  LOG_PRIOR_NODE *prior_list_header;
  LOG_PRIOR_NODE *prior_list_tail;

  INT64 list_size;		/* bytes */

  /* flush list */
  LOG_PRIOR_NODE *prior_flush_list_header;

  std::mutex prior_lsa_mutex;

  log_prior_lsa_info ();
};

//
// log record partial updates logging
//
using log_rv_record_flag_type = log_data_addr::offset_type;
const log_rv_record_flag_type LOG_RV_RECORD_INSERT = (log_rv_record_flag_type) 0x8000;
const log_rv_record_flag_type LOG_RV_RECORD_DELETE = 0x4000;
const log_rv_record_flag_type LOG_RV_RECORD_UPDATE_ALL = (log_rv_record_flag_type) 0xC000;
const log_rv_record_flag_type LOG_RV_RECORD_UPDATE_PARTIAL = 0x0000;
const log_rv_record_flag_type LOG_RV_RECORD_MODIFY_MASK = (log_rv_record_flag_type) 0xC000;

inline bool LOG_RV_RECORD_IS_INSERT (log_rv_record_flag_type flags);
inline bool LOG_RV_RECORD_IS_DELETE (log_rv_record_flag_type flags);
inline bool LOG_RV_RECORD_IS_UPDATE_ALL (log_rv_record_flag_type flags);
inline bool LOG_RV_RECORD_IS_UPDATE_PARTIAL (log_rv_record_flag_type flags);

#define LOG_RV_RECORD_SET_MODIFY_MODE(addr, mode) \
  ((addr)->offset = ((addr)->offset & (~LOG_RV_RECORD_MODIFY_MASK)) | (mode))

#define LOG_RV_RECORD_UPDPARTIAL_ALIGNED_SIZE(new_data_size) \
  (DB_ALIGN (new_data_size + OR_SHORT_SIZE + 2 * OR_BYTE_SIZE, INT_ALIGNMENT))

bool log_prior_has_worker_log_records (THREAD_ENTRY *thread_p);

void LOG_RESET_APPEND_LSA (const LOG_LSA *lsa);
void LOG_RESET_PREV_LSA (const LOG_LSA *lsa);
char *LOG_APPEND_PTR ();

//////////////////////////////////////////////////////////////////////////
//
// Inline/templates
//
//////////////////////////////////////////////////////////////////////////

bool
LOG_RV_RECORD_IS_INSERT (log_rv_record_flag_type flags)
{
  return (flags & LOG_RV_RECORD_MODIFY_MASK) == LOG_RV_RECORD_INSERT;
}

bool
LOG_RV_RECORD_IS_DELETE (log_rv_record_flag_type flags)
{
  return (flags & LOG_RV_RECORD_MODIFY_MASK) == LOG_RV_RECORD_INSERT;
}

bool
LOG_RV_RECORD_IS_UPDATE_ALL (log_rv_record_flag_type flags)
{
  return (flags & LOG_RV_RECORD_MODIFY_MASK) == LOG_RV_RECORD_UPDATE_ALL;
}

bool
LOG_RV_RECORD_IS_UPDATE_PARTIAL (log_rv_record_flag_type flags)
{
  return (flags & LOG_RV_RECORD_MODIFY_MASK) == LOG_RV_RECORD_UPDATE_PARTIAL;
}

#endif // !_LOG_APPEND_HPP_
