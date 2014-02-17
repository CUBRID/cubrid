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
 * log_applier.h - DECLARATIONS FOR LOG APPLIER (AT CLIENT & SERVER)
 */

#ifndef _LOG_APPLIER_HEADER_
#define _LOG_APPLIER_HEADER_

#ident "$Id$"

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
   (error == ER_LK_DEADLOCK_CYCLE_DETECTED))

#if defined (CS_MODE)
int
la_log_page_check (const char *database_name, const char *log_path,
		   INT64 page_num, bool check_applied_info,
		   bool check_copied_info, bool check_replica_info,
		   bool verbose, LOG_LSA * copied_eof_lsa,
		   LOG_LSA * copied_append_lsa, LOG_LSA * applied_final_lsa);
int la_apply_log_file (const char *database_name, const char *log_path,
		       const int max_mem_size);
void la_print_log_header (const char *database_name, struct log_header *hdr,
			  bool verbose);
void la_print_log_arv_header (const char *database_name,
			      struct log_arv_header *hdr, bool verbose);
void la_print_delay_info (LOG_LSA working_lsa, LOG_LSA target_lsa,
			  float process_rate);

#endif /* CS_MODE */

#endif /* _LOG_APPLIER_HEADER_ */
