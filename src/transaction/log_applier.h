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
 * log_applier.h - DECLARATIONS FOR LOG APPLIER (AT CLIENT & SERVER)
 */

#ifndef _LOG_APPLIER_HEADER_
#define _LOG_APPLIER_HEADER_

#ident "$Id$"

#if defined (CS_MODE)
#include "log_common_impl.h"
#include "log_lsa.hpp"
#include "log_storage.hpp"
#endif /* CS_MODE */

#define LA_RETRY_ON_ERROR(error) \
  ((error == ER_LK_UNILATERALLY_ABORTED)              || \
   (error == ER_LK_OBJECT_TIMEOUT_SIMPLE_MSG)         || \
   (error == ER_LK_OBJECT_TIMEOUT_CLASS_MSG)          || \
   (error == ER_LK_OBJECT_TIMEOUT_CLASSOF_MSG)        || \
   (error == ER_LK_PAGE_TIMEOUT)                      || \
   (error == ER_PAGE_LATCH_TIMEDOUT)                  || \
   (error == ER_PAGE_LATCH_ABORTED)                   || \
   (error == ER_LK_OBJECT_DL_TIMEOUT_SIMPLE_MSG)      || \
   (error == ER_LK_OBJECT_DL_TIMEOUT_CLASS_MSG)       || \
   (error == ER_LK_OBJECT_DL_TIMEOUT_CLASSOF_MSG)     || \
   (error == ER_TDE_CIPHER_IS_NOT_LOADED)             || \
   (error == ER_LK_DEADLOCK_CYCLE_DETECTED))

typedef enum
{
  REPL_FILTER_NONE,
  REPL_FILTER_INCLUDE_TBL,
  REPL_FILTER_EXCLUDE_TBL
} REPL_FILTER_TYPE;

#if defined (CS_MODE)
int la_get_applyinfo_applied_log_info (const char *database_name, const char *log_path, bool check_replica_info,
				       bool verbose, LOG_LSA * applied_final_lsa);
int la_get_applyinfo_copied_log_info (const char *database_name, const char *log_path, INT64 page_num, bool verbose,
				      LOG_LSA * copied_eof_lsa, LOG_LSA * copied_append_lsa);
int la_apply_log_file (const char *database_name, const char *log_path, const int max_mem_size);
void la_print_log_header (const char *database_name, LOG_HEADER * hdr, bool verbose);
void la_print_log_arv_header (const char *database_name, LOG_ARV_HEADER * hdr, bool verbose);
void la_print_delay_info (LOG_LSA working_lsa, LOG_LSA target_lsa, float process_rate);
#ifdef UNSTABLE_TDE_FOR_REPLICATION_LOG
extern int la_start_dk_sharing ();
#endif /* UNSTABLE_TDE_FOR_REPLICATION_LOG */

extern bool la_force_shutdown (void);
#endif /* CS_MODE */

#endif /* _LOG_APPLIER_HEADER_ */
