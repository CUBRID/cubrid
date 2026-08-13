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
 * broker_log_top.h -
 */

#ifndef	_BROKER_LOG_TOP_H_
#define	_BROKER_LOG_TOP_H_

#ident "$Id$"

#define LINE_BUF_SIZE           30000

enum t_output_mode
{
  OUTPUT_MERGE = 0,
  OUTPUT_SPLIT
};
typedef enum t_output_mode T_OUTPUT_MODE;

enum t_log_top_mode
{
  MODE_PROC_TIME = 0,
  MODE_MAX_HANDLE = 1
};
typedef enum t_log_top_mode T_LOG_TOP_MODE;

enum log_top_error_code
{
  LT_NO_ERROR = 0,
  LT_INVAILD_VERSION = -1,
  LT_OTHER_ERROR = -2
};

#define PREFIX_UNKNOWN       "unknown"
#define SUFFIX_SQL_LOG       ".sql.log"
#define SUFFIX_SQL_LOG_BAK   ".sql.log.bak"
#define SUFFIX_SLOW_LOG      ".slow.log"
#define SUFFIX_SLOW_LOG_BAK  ".slow.log.bak"

extern int check_log_time (char *start_date, char *end_date);
extern int log_top_tran (int argc, char *argv[], int arg_start);
extern int get_file_offset (char *filename, long *start_offset, long *end_offset);
extern void get_brokername_from_filename (const char *filename, char *prefix, int max_len);
extern int make_splitdir (char *splitdir);
extern int make_change_split_brokerdir (char *splitdir, char *broker);
extern const char *get_basename (const char *path);

extern T_LOG_TOP_MODE log_top_mode;
extern T_OUTPUT_MODE output_mode;

#endif /* _BROKER_LOG_TOP_H_ */
