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
 * util_func.h : miscellaneous utility functions interface
 *
 */

#ifndef _UTIL_FUNC_H_
#define _UTIL_FUNC_H_

#ident "$Id$"

#include <sys/types.h>
#include <math.h>
#include <time.h>

#define UTIL_PID_ENVVAR_NAME         "UTIL_PID"
#define UTIL_infinity()     (HUGE_VAL)

#if defined(WINDOWS)
#define SLEEP_MILISEC(SEC, MSEC)        Sleep((SEC) * 1000 + (MSEC))
#else
#define SLEEP_MILISEC(sec, msec)                        \
        do {                                            \
          struct timeval sleep_time_val;                \
          sleep_time_val.tv_sec = sec;                  \
          sleep_time_val.tv_usec = (msec) * 1000;       \
          select(0, 0, 0, 0, &sleep_time_val);          \
        } while(0)
#endif

#define PRINT_AND_LOG_ERR_MSG(...) \
  do {\
    fprintf(stderr, __VA_ARGS__);\
    util_log_write_errstr(__VA_ARGS__);\
  }while (0)

extern unsigned int hashpjw (const char *);

extern int util_compare_filepath (const char *file1, const char *file2);


typedef void (*SIG_HANDLER) (void);
extern void util_arm_signal_handlers (SIG_HANDLER DB_INT32_handler, SIG_HANDLER quit_handler);
extern void util_disarm_signal_handlers (void);
extern char **util_split_string (const char *str, const char *delim);
extern void util_free_string_array (char **array);
extern time_t util_str_to_time_since_epoch (char *str);
extern void util_shuffle_string_array (char **array, int count);

extern int util_log_write_result (int error);
extern int util_log_write_errid (int message_id, ...);
extern int util_log_write_errstr (const char *format, ...);
extern int util_log_write_warnstr (const char *format, ...);
extern int util_log_write_command (int argc, char *argv[]);

extern int util_bsearch (const void *key, const void *base, int n_elems, unsigned int sizeof_elem,
			 int (*func_compare) (const void *, const void *), bool * out_found);

extern void util_get_second_and_ms_since_epoch (time_t * secs, int *msec);

#endif /* _UTIL_FUNC_H_ */
