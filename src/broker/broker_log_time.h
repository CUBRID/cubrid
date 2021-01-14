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
 * broker_log_time.h -
 */

#ifndef _BROKER_LOG_TIME_H_
#define _BROKER_LOG_TIME_H_

#ident "$Id$"

typedef struct t_log_time T_LOG_TIME;
struct t_log_time
{
  int hour;
  int min;
  int sec;
  int msec;
};

#if defined (ENABLE_UNUSED_FUNCTION)
extern int log_time_make (char *str, T_LOG_TIME * ltm);
extern int log_time_diff (T_LOG_TIME * t1, T_LOG_TIME * t2);
#endif

#endif /* _BROKER_LOG_TIME_H_ */
