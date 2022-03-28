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
 * flashback_cl.h
 *
 */

#ifndef _FLASHBACK_CL_H_
#define _FLASHBACK_CL_H_

#include <unordered_map>
#include "dynamic_array.h"
#include "storage_common.h"
#include "log_lsa.hpp"

#define FLASHBACK_FIND_SUMMARY_ENTRY(trid, summary_info, summary_entry) \
  do \
    { \
      auto iter = summary_info.find (trid); \
      if (iter != summary_info.end ()) \
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
  char user[DB_MAX_USER_LENGTH + 1];
  LOG_LSA start_lsa;
  LOG_LSA end_lsa;
} FLASHBACK_SUMMARY_INFO;

// *INDENT-OFF*
typedef std::unordered_map<TRANID, FLASHBACK_SUMMARY_INFO> FLASHBACK_SUMMARY_INFO_MAP;
// *INDENT-ON*

extern int flashback_unpack_and_print_summary (char **summary_buffer, FLASHBACK_SUMMARY_INFO_MAP * summary,
					       dynamic_array * classname_list, OID * oidlist);

#endif
