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

#include <map>
#include <unordered_map>
#include <unordered_set>

#include "config.h"
#include "error_manager.h"
#include "file_io.h"
#include "log_lsa.hpp"
#include "thread_compat.hpp"
#include "oid.h"
#include "connection_defs.h"

#define FLASHBACK_MAX_NUM_TRAN_TO_SUMMARY   INT_MAX

#define FLASHBACK_CHECK_AND_GET_SUMMARY(summary_list, trid, summary_entry) \
  do \
    { \
      auto iter = (summary_list).find (trid); \
      if (iter != (summary_list).end ()) \
        { \
          summary_entry = &(iter->second); \
        } \
      else \
        { \
          summary_entry = NULL; \
        } \
    } \
  while (0)

/* flashback summary information stored in utility side */
typedef struct flashback_summary_info
{
  TRANID trid;
  LOG_LSA start_lsa;
  LOG_LSA end_lsa;
} FLASHBACK_SUMMARY_INFO;

// *INDENT-OFF*
typedef std::unordered_map<TRANID, FLASHBACK_SUMMARY_INFO *> FLASHBACK_SUMMARY_INFO_MAP;
// *INDENT-ON*

/* flashback summary information for each transaction generated in server side */
typedef struct flashback_summary_entry
{
  TRANID trid;
  char user[DB_MAX_USER_LENGTH + 1];
  time_t start_time;
  time_t end_time;
  int num_insert;
  int num_update;
  int num_delete;
  LOG_LSA start_lsa;
  LOG_LSA end_lsa;
  // *INDENT-OFF*
  std::unordered_set<OID> classoid_set;
  // *INDENT-ON*
} FLASHBACK_SUMMARY_ENTRY;

#define OR_SUMMARY_ENTRY_SIZE_WITHOUT_CLASS   (OR_INT_SIZE \
                                              + DB_MAX_USER_LENGTH + MAX_ALIGNMENT \
                                              + OR_INT64_SIZE * 2 \
                                              + OR_INT_SIZE * 3 \
                                              + OR_LOG_LSA_SIZE * 2 \
                                              + OR_INT_SIZE)

/* context for making summary information */
typedef struct flashback_summary_context
{
  LOG_LSA start_lsa;
  LOG_LSA end_lsa;
  char *user;
  int num_summary;
  int num_class;
  // *INDENT-OFF*
  std::unordered_set<OID> classoid_set;
  std::map <TRANID, FLASHBACK_SUMMARY_ENTRY> summary_list;
  // *INDENT-ON*
} FLASHBACK_SUMMARY_CONTEXT;

extern int flashback_make_summary_list (THREAD_ENTRY * thread_p, FLASHBACK_SUMMARY_CONTEXT * context);
extern void flashback_cleanup (THREAD_ENTRY * thread_p, FLASHBACK_SUMMARY_CONTEXT * context);

extern int flashback_initialize (THREAD_ENTRY * thread_p);

extern LOG_PAGEID flashback_min_log_pageid_to_keep ();
extern bool flashback_is_needed_to_keep_archive ();
extern bool flashback_check_time_exceed_threshold ();
extern bool flashback_is_loginfo_generation_finished (LOG_LSA * start_lsa, LOG_LSA * end_lsa);

extern void flashback_set_min_log_pageid_to_keep (LOG_LSA * lsa);
extern void flashback_set_request_done_time ();
extern void flashback_set_status_active ();
extern void flashback_set_status_inactive ();
extern void flashback_reset ();

#endif /* _FLASHBACK_H_ */
