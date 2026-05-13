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
 * db_oos.h - OOS (Out-Of-row Storage) client-side API.
 */

#ifndef _DB_OOS_H_
#define _DB_OOS_H_

#ident "$Id$"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct db_oos_stats DB_OOS_STATS;
  struct db_oos_stats
  {
    int has_oos_file;		/* 0 if class has no OOS file, 1 otherwise */
    int oos_vfid_volid;
    int oos_vfid_fileid;
    int num_user_pages;		/* physical user pages in OOS file */
    int page_size;		/* DB_PAGESIZE */
    int num_recs;		/* live OOS records (slots on pages) */
    int64_t recs_sumlen;	/* sum of live OOS record body bytes */
  };

  extern int db_oos_stats (const char *class_name, DB_OOS_STATS * stats);

#ifdef __cplusplus
}
#endif

#endif				/* _DB_OOS_H_ */
