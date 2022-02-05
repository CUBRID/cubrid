/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */


/*
 * flashback.h
 *
 */

#ifndef _FLASHBACK_H_
#define _FLASHBACK_H_

#ident "$Id$"

#include "config.h"
#include "error_manager.h"
#include "file_io.h"
#include "log_lsa.hpp"
#include "thread_compat.hpp"

#include <map>

#define FLASHBACK_MAX_SUMMARY   INT_MAX
#define FLASHBACK_MAX_TABLE     32
#define OR_SUMMARY_ENTRY_SIZE   (OR_INT_SIZE + OR_INT64_SIZE * 2 + OR_INT_SIZE * 3 + OR_LOG_LSA_SIZE * 2 + OR_INT_SIZE + FLASHBACK_MAX_TABLE * OR_OID_SIZE)

#define FLASHBACK_CHECK_AND_GET_SUMMARY(summary_list, trid, summary_entry) \
  do \
    { \
      if ((summary_list).count(trid) != 0) \
      { \
        summary_entry = (summary_list).at(trid); \
      } \
      else \
      { \
        summary_entry = NULL; \
      } \
    } \
  while (0)

typedef struct flashback_summary_info
{
  TRANID trid;
  LOG_LSA start_lsa;
  LOG_LSA end_lsa;
} FLASHBACK_SUMMARY_INFO;

typedef struct flashback_summary_entry
{
  TRANID trid;
  time_t start_time;
  time_t end_time;
  int num_insert;
  int num_update;
  int num_delete;
  LOG_LSA start_lsa;
  LOG_LSA end_lsa;
  int num_class;
  OID classlist[FLASHBACK_MAX_TABLE];
} FLASHBACK_SUMMARY_ENTRY;

typedef struct flashback_summary_context
{
  LOG_LSA start_lsa;
  LOG_LSA end_lsa;
  int num_class;
  OID classlist[FLASHBACK_MAX_TABLE];
  char *user;
  int num_summary;
  // *INDENT-OFF*
  std::unordered_map <TRANID, FLASHBACK_SUMMARY_ENTRY*> summary_list;
  // *INDENT-ON*
} FLASHBACK_SUMMARY_CONTEXT;

// *INDENT-OFF*
typedef std::map<TRANID, FLASHBACK_SUMMARY_INFO *> Map_Summary;
// *INDENT-ON*

extern int flashback_make_summary_list (THREAD_ENTRY * thread_p, FLASHBACK_SUMMARY_CONTEXT * context);
extern void flashback_cleanup (THREAD_ENTRY * thread_p, FLASHBACK_SUMMARY_CONTEXT * context);

#endif /* _FLASHBACK_H_ */
