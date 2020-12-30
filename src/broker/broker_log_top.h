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

extern int check_log_time (char *start_date, char *end_date);
extern int log_top_tran (int argc, char *argv[], int arg_start);
extern int get_file_offset (char *filename, long *start_offset, long *end_offset);

extern T_LOG_TOP_MODE log_top_mode;

#endif /* _BROKER_LOG_TOP_H_ */
