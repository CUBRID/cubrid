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
 * util_func.c : miscellaneous utility functions
 *
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <chrono>
#include <time.h>
#include <stdarg.h>
#include <sys/timeb.h>
#if !defined (WINDOWS)
#include <sys/time.h>
#endif /* !WINDOWS */

#include "util_func.h"
#include "porting.h"
#include "error_code.h"
#include "utility.h"
#include "system_parameter.h"
#include "environment_variable.h"

#define UTIL_LOG_MAX_HEADER_LEN    (40)
#define UTIL_LOG_MAX_MSG_SIZE       (1024)
#define UTIL_LOG_BUFFER_SIZE   \
  (UTIL_LOG_MAX_MSG_SIZE + UTIL_LOG_MAX_HEADER_LEN)

#define UTIL_LOG_FILENAME  "cubrid_utility.log"

static char *util_Log_filename = NULL;
static char util_Log_filename_buf[PATH_MAX];
static char util_Log_buffer[UTIL_LOG_BUFFER_SIZE];

static FILE *util_log_file_backup (FILE * fp, const char *path);
static FILE *util_log_file_fopen (const char *path);
static FILE *fopen_and_lock (const char *path);
static int util_log_header (char *buf, size_t buf_len);
static int util_log_write_internal (const char *msg, const char *prefix_str);

// *INDENT-OFF*
template <typename Duration>
static void util_get_seconds_and_rest_since_epoch (std::chrono::seconds &secs, Duration &rest);
// *INDENT-ON*

/*
 * hashpjw() - returns hash value of given string
 *   return: hash value
 *   s(in)
 *
 * Note:
 *   This function is adapted from the hashpjw function in Aho, Sethi, and
 *   Ullman (Red Dragon), p. 436.  Unlike that version, this one does not
 *   mod the result; consequently, the result could be any 32-bit value.
 *   The caller should mod the result with a value appropriate for its
 *   environment.
 *
 */
unsigned int
hashpjw (const char *s)
{
  unsigned int h, g;

  assert (s != NULL);

  for (h = 0; *s != '\0'; ++s)
    {
      h = (h << 4) + (*s);

      g = (h & 0xf0000000);

      if (g != 0)
	{
	  h ^= g >> 24;
	  h ^= g;
	}
    }
  return h;
}

/*
 * util_compare_filepath -
 *   return:
 *   file1(in):
 *   file2(in):
 */
int
util_compare_filepath (const char *file1, const char *file2)
{
#if defined (WINDOWS)
  char path1[PATH_MAX], path2[PATH_MAX];
  char *p;

  if (GetLongPathName (file1, path1, sizeof (path1)) == 0 || GetLongPathName (file2, path2, sizeof (path2)) == 0)
    {
      return (stricmp (file1, file2));
    }

  for (p = path1; *p; p++)
    if (*p == '/')
      *p = '\\';
  for (p = path2; *p; p++)
    if (*p == '/')
      *p = '\\';

  return (stricmp (path1, path2));
#else /* WINDOWS */
  return (strcmp (file1, file2));
#endif /* !WINDOWS */
}

/*
 * Signal Handling
 */

static void system_interrupt_handler (int sig);
static void system_quit_handler (int sig);

/*
 * user_interrupt_handler, user_quit_handler -
 *   These variables contain pointers to the user specified handler
 *   functions for the interrupt and quit signals.
 *   If they are NULL, no handlers have been defined.
 */
static SIG_HANDLER user_interrupt_handler = NULL;
static SIG_HANDLER user_quit_handler = NULL;

/*
 * system_interrupt_handler - Internal system handler for SIGINT
 *   return: none
 *   sig(in): signal no
 *
 * Note:  Calls the user interrupt handler after re-arming the signal handler.
 */
static void
system_interrupt_handler (int sig)
{
  (void) os_set_signal_handler (SIGINT, system_interrupt_handler);
  if (user_interrupt_handler != NULL)
    {
      (*user_interrupt_handler) ();
    }
}


/*
 * system_quit_handler - Internal system handler for SIGQUIT
 *   return: none
 *   sig(in): signal no
 *
 * Note: Calls the user quit handler after re-arming the signal handler.
 */
static void
system_quit_handler (int sig)
{
#if !defined(WINDOWS)
  (void) os_set_signal_handler (SIGQUIT, system_quit_handler);
  if (user_quit_handler != NULL)
    (*user_quit_handler) ();
#endif
}

/*
 * util_disarm_signal_handlers - Disarms the user interrpt and quit handlers
 *                               if any were specified
 *   return: none
 */
void
util_disarm_signal_handlers (void)
{
  if (user_interrupt_handler != NULL)
    {
      user_interrupt_handler = NULL;
      if (os_set_signal_handler (SIGINT, SIG_IGN) != SIG_IGN)
	{
	  (void) os_set_signal_handler (SIGINT, SIG_DFL);
	}
    }
#if !defined(WINDOWS)
  if (user_quit_handler != NULL)
    {
      user_quit_handler = NULL;
      if (os_set_signal_handler (SIGQUIT, SIG_IGN) != SIG_IGN)
	{
	  (void) os_set_signal_handler (SIGQUIT, SIG_DFL);
	}
    }
#endif
}

/*
 * util_arm_signal_handlers - Install signal handlers for the two most
 *                            important signals SIGINT and SIGQUIT
 *   return: none
 *   sigint_handler(in): SIGINT signal handler
 *   sigquit_handler(in): SIGQUIT signal handler
 */
void
util_arm_signal_handlers (SIG_HANDLER sigint_handler, SIG_HANDLER sigquit_handler)
{
  /* first disarm any existing handlers */
  util_disarm_signal_handlers ();

  if (sigint_handler != NULL)
    {
      (void) os_set_signal_handler (SIGINT, system_interrupt_handler);
      user_interrupt_handler = sigint_handler;
    }
#if !defined(WINDOWS)
  if (sigquit_handler != NULL)
    {
      /* Is this kind of test necessary for the quit signal ? */
      (void) os_set_signal_handler (SIGQUIT, system_quit_handler);
      user_quit_handler = sigquit_handler;
    }
#endif
}

/*
 *  The returned char** is null terminated char* array;
 *    ex: "a,b" --> { "a", "b", NULL }
 */
char **
util_split_string (const char *str, const char *delim)
{
  char *t, *o;
  char *save, *v;
  char **r = NULL;
  int count = 1;

  if (str == NULL)
    {
      return NULL;
    }

  o = strdup (str);
  if (o == NULL)
    {
      return NULL;
    }

  for (t = o;; t = NULL)
    {
      v = strtok_r (t, delim, &save);
      if (v == NULL)
	{
	  break;
	}
      char **const realloc_r = (char **) realloc (r, sizeof (char *) * (count + 1));
      if (realloc_r == NULL)
	{
	  free (o);
	  return NULL;
	}
      else
	{
	  r = realloc_r;
	}
      r[count - 1] = strdup (v);
      r[count] = NULL;
      count++;
    }

  free (o);
  return r;
}

void
util_free_string_array (char **array)
{
  int i;

  for (i = 0; array[i] != NULL; i++)
    {
      free (array[i]);
    }
  free (array);
}

/*
 *  util_str_to_time_since_epoch () - convert time string
 *      return: time since epoch or 0 on error
 *
 *  NOTE: it only accepts YYYY-DD-MM hh:mm:ss format.
 */
time_t
util_str_to_time_since_epoch (char *str)
{
  int status = NO_ERROR;
  int date_index = 0;
  char *save_ptr, *token, *date_string;
  const char *delim = "-: ";
  struct tm time_data, tmp_time_data;
  time_t result_time;

  if (str == NULL)
    {
      return 0;
    }

  date_string = strdup (str);
  if (date_string == NULL)
    {
      return 0;
    }

  token = strtok_r (date_string, delim, &save_ptr);
  while (status == NO_ERROR && token != NULL)
    {
      switch (date_index)
	{
	case 0:		/* year */
	  time_data.tm_year = atoi (token) - 1900;
	  if (time_data.tm_year < 0)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 1:		/* month */
	  time_data.tm_mon = atoi (token) - 1;
	  if (time_data.tm_mon < 0 || time_data.tm_mon > 11)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 2:		/* month-day */
	  time_data.tm_mday = atoi (token);
	  if (time_data.tm_mday < 1 || time_data.tm_mday > 31)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 3:		/* hour */
	  time_data.tm_hour = atoi (token);
	  if (time_data.tm_hour < 0 || time_data.tm_hour > 23)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 4:		/* minute */
	  time_data.tm_min = atoi (token);
	  if (time_data.tm_min < 0 || time_data.tm_min > 59)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	case 5:		/* second */
	  time_data.tm_sec = atoi (token);
	  if (time_data.tm_sec < 0 || time_data.tm_sec > 59)
	    {
	      status = ER_GENERIC_ERROR;
	    }
	  break;
	default:
	  status = ER_GENERIC_ERROR;
	  break;
	}
      date_index++;
      token = strtok_r (NULL, delim, &save_ptr);
    }
  time_data.tm_isdst = -1;

  free (date_string);

  if (date_index != 6 || status != NO_ERROR)
    {
      return 0;
    }

  tmp_time_data = time_data;

  result_time = mktime (&tmp_time_data);
  if (result_time < (time_t) 0)
    {
      return 0;
    }

  time_data.tm_isdst = tmp_time_data.tm_isdst;
  result_time = mktime (&time_data);
  if (result_time < (time_t) 0)
    {
      return 0;
    }

  return result_time;
}

void
util_shuffle_string_array (char **array, int count)
{
  struct timeval t;
  int i, j;
  double r;
  struct drand48_data buf;
  char *temp;

  gettimeofday (&t, NULL);

  /* tv_usec returned by gettimeofday on WINDOWS is millisec * 1000 and seeding it would result in generating an even
   * random number at first. To avoid such a pattern in generating a random number, tv_usec/1000 is used on WINDOWS. */
#if defined (WINDOWS)
  srand48_r (t.tv_usec / 1000, &buf);
#else /* WINDOWS */
  srand48_r (t.tv_usec, &buf);
#endif /* !WINDOWS */

  /* Fisher-Yates shuffle */
  for (i = count - 1; i > 0; i--)
    {
      drand48_r (&buf, &r);
      j = (int) ((i + 1) * r);

      temp = array[j];
      array[j] = array[i];
      array[i] = temp;
    }
}

/*
 * util_log_write_result () -
 *
 * error (in) :
 */
int
util_log_write_result (int error)
{
  if (error == NO_ERROR)
    {
      return util_log_write_internal ("SUCCESS\n", NULL);
    }
  else
    {
      /* skip failed log */
    }

  return 0;
}

/*
 * util_log_write_errid () -
 *
 * message_id (in) :
 */
int
util_log_write_errid (int message_id, ...)
{
  int n;
  char msg_buf[UTIL_LOG_MAX_MSG_SIZE];
  const char *format;
  va_list arg_list;

  format = utility_get_generic_message (message_id);
  va_start (arg_list, message_id);
  n = vsnprintf (msg_buf, UTIL_LOG_MAX_MSG_SIZE, format, arg_list);
  if (n >= UTIL_LOG_MAX_MSG_SIZE)
    {
      msg_buf[UTIL_LOG_MAX_MSG_SIZE - 1] = '\0';
    }
  va_end (arg_list);

  return util_log_write_internal (msg_buf, "FAILURE: ");
}

/*
 * util_log_write_errstr () -
 *
 * format (in) :
 */
int
util_log_write_errstr (const char *format, ...)
{
  int n;
  char msg_buf[UTIL_LOG_MAX_MSG_SIZE];
  va_list arg_list;

  va_start (arg_list, format);
  n = vsnprintf (msg_buf, UTIL_LOG_MAX_MSG_SIZE, format, arg_list);
  if (n >= UTIL_LOG_MAX_MSG_SIZE)
    {
      msg_buf[UTIL_LOG_MAX_MSG_SIZE - 1] = '\0';
    }
  va_end (arg_list);

  return util_log_write_internal (msg_buf, "FAILURE: ");
}

/*
 * util_log_write_warnstr () -
 *
 * format (in) :
 */
int
util_log_write_warnstr (const char *format, ...)
{
  int n;
  char msg_buf[UTIL_LOG_MAX_MSG_SIZE];
  va_list arg_list;

  va_start (arg_list, format);
  n = vsnprintf (msg_buf, UTIL_LOG_MAX_MSG_SIZE, format, arg_list);
  if (n >= UTIL_LOG_MAX_MSG_SIZE)
    {
      msg_buf[UTIL_LOG_MAX_MSG_SIZE - 1] = '\0';
    }
  va_end (arg_list);

  return util_log_write_internal (msg_buf, "WARNING: ");
}

/*
 * util_log_write_command () -
 *
 * argc (in) :
 * argv (in) :
 */
int
util_log_write_command (int argc, char *argv[])
{
  int i;
  size_t remained_buf_length, str_len;
  char command_buf[UTIL_LOG_MAX_MSG_SIZE];
  char *p;

  memset (command_buf, '\0', UTIL_LOG_MAX_MSG_SIZE);
  p = command_buf;
  remained_buf_length = UTIL_LOG_MAX_MSG_SIZE - 1;

  for (i = 0; i < argc && remained_buf_length > 0; i++)
    {
      str_len = strlen (argv[i]);
      if (str_len > remained_buf_length)
	{
	  break;
	}
      strcpy (p, argv[i]);
      remained_buf_length -= str_len;
      p += str_len;
      if (i < argc - 1 && remained_buf_length > 0)
	{
	  /* add white space */
	  *p = ' ';
	  p++;
	  remained_buf_length--;
	}
      else if (i == argc - 1 && remained_buf_length > 0)
	{
	  *p = '\n';
	  p++;
	  remained_buf_length--;
	}
    }

  return util_log_write_internal (command_buf, NULL);
}

/*
 * util_log_write_internal () -
 *
 * msg (in) :
 * prefix_str(in) :
 *
 */
static int
util_log_write_internal (const char *msg, const char *prefix_str)
{
  char *p;
  int ret = -1;
  int len, n;
  FILE *fp;

  if (util_Log_filename == NULL)
    {
      util_Log_filename = util_Log_filename_buf;
      envvar_logdir_file (util_Log_filename, PATH_MAX, UTIL_LOG_FILENAME);
    }

  p = util_Log_buffer;
  len = UTIL_LOG_BUFFER_SIZE;
  n = util_log_header (p, len);
  len -= n;
  p += n;

  if (len > 0)
    {
      n = snprintf (p, len, "%s%s", (prefix_str ? prefix_str : ""), msg);
      if (n >= len)
	{
	  p[len - 1] = '\0';
	}
    }

  fp = util_log_file_fopen (util_Log_filename);
  if (fp == NULL)
    {
      return -1;
    }

  ret = fprintf (fp, "%s", util_Log_buffer);
  fclose (fp);

  return ret;
}

/*
 * util_log_header () -
 *
 * buf (out) :
 *
 */
static int
util_log_header (char *buf, size_t buf_len)
{
  struct tm tm, *tm_p;
  time_t sec;
  int len;
  char *p;
  const char *pid;
  int millisec;

  if (buf == NULL)
    {
      return 0;
    }

  /* current time */
  util_get_second_and_ms_since_epoch (&sec, &millisec);

  tm_p = localtime_r (&sec, &tm);

  len = (int) strftime (buf, buf_len, "%y-%m-%d %H:%M:%S", tm_p);
  p = buf + len;
  buf_len -= len;

  pid = envvar_get (UTIL_PID_ENVVAR_NAME);
  len += snprintf (p, buf_len, ".%03d (%s) ", millisec, ((pid == NULL) ? "    " : pid));

  assert (len <= UTIL_LOG_MAX_HEADER_LEN);

  return len;
}

/*
 * util_log_file_fopen () -
 *
 * path (in) :
 *
 */
static FILE *
util_log_file_fopen (const char *path)
{
  FILE *fp;

  assert (path != NULL);

  fp = fopen_and_lock (path);
  if (fp != NULL)
    {
      fseek (fp, 0, SEEK_END);
      if (ftell (fp) > prm_get_integer_value (PRM_ID_ER_LOG_SIZE))
	{
	  fp = util_log_file_backup (fp, path);
	}
    }

  return fp;
}

/*
 * util_log_file_backup ()
 * fp (in) :
 * path (in) :
 *
 */
static FILE *
util_log_file_backup (FILE * fp, const char *path)
{
  char backup_file[PATH_MAX];

  assert (fp != NULL);
  assert (path != NULL);

  fclose (fp);
  sprintf (backup_file, "%s.bak", path);
  (void) unlink (backup_file);
  (void) rename (path, backup_file);

  return fopen_and_lock (path);
}

/*
 * fopen_and_lock ()
 * path (in) :
 *
 */
static FILE *
fopen_and_lock (const char *path)
{
#define MAX_RETRY_COUNT 10

  int retry_count = 0;
  FILE *fp;

retry:
  fp = fopen (path, "a+");
  if (fp != NULL)
    {
      if (lockf (fileno (fp), F_TLOCK, 0) < 0)
	{
	  fclose (fp);

	  if (retry_count < MAX_RETRY_COUNT)
	    {
	      SLEEP_MILISEC (0, 100);
	      retry_count++;
	      goto retry;
	    }

	  return NULL;
	}
    }

  return fp;
}

/*
 * util_bsearch () - generic binary search function. besides searching for key, it also returns the right key position
 *                   even when it is not found. the caller may then decide to insert the key.
 *                   if you only want to find key if it exists, it's better to use bsearch.
 *
 * return            : position of found key or the right position if key would be inserted.
 * key (in)          : point to key value
 * base (in)         : pointer to base array
 * n_elems (in)      : number of elements in array
 * size_elem (in)    : memory size of one element
 * func_compare (in) : compare function
 * out_found (out)   : output true if key was found, false otherwise
 *
 * note: the array of elements should be ordered by func_compare. duplicate keys are not allowed.
 */
int
util_bsearch (const void *key, const void *base, int n_elems, unsigned int sizeof_elem,
	      int (*func_compare) (const void *, const void *), bool * out_found)
{
  int min = 0;
  int max = n_elems - 1;
  int mid = 0;
  int compare = 0;

  const void *elem;

  *out_found = false;

  /* binary search */
  /* keep searching and reducing the range until key is found, or until the range is reduced to 0 */
  while (min <= max)
    {
      /* get range midpoint */
      mid = (min + max) >> 1;

      /* get mid element */
      elem = (char *) base + (mid * sizeof_elem);

      /* compare with key */
      compare = func_compare (elem, key);

      /* did we find key? */
      if (compare == 0)
	{
	  *out_found = true;
	  return mid;
	}
      /* not found */
      /* reduce the search range */
      if (compare > 0)
	{
	  /* search in lower range */
	  max = mid - 1;
	}
      else
	{
	  /* search in upper range */
	  /* we also have to increment mid. if range is reduced to 0, the right position for key is next. */
	  min = ++mid;
	}
    }

  /* not found */
  /* mid is the right position for key */
  return mid;
}

// *INDENT-OFF*
template <typename Duration>
void
util_get_seconds_and_rest_since_epoch (std::chrono::seconds &secs, Duration &rest)
{
  using clock_t = std::chrono::system_clock;
  using timept_secs = std::chrono::time_point<clock_t, std::chrono::seconds>;
  auto now_timepoint = clock_t::now ();
  timept_secs now_in_secs = std::chrono::time_point_cast<std::chrono::seconds> (now_timepoint);
  secs = now_in_secs.time_since_epoch ();
  rest = std::chrono::duration_cast<Duration> (now_timepoint - now_in_secs);
}

void
util_get_second_and_ms_since_epoch (time_t * secs, int *msec)
{
  assert (secs != NULL && msec != NULL);
  std::chrono::seconds secs_since_epoch;
  std::chrono::milliseconds rest_in_msec;
  util_get_seconds_and_rest_since_epoch<std::chrono::milliseconds> (secs_since_epoch, rest_in_msec);
  *secs = static_cast<time_t> (secs_since_epoch.count ());
  *msec = static_cast<int> (rest_in_msec.count ());
  assert (*msec < 1000);
}
// *INDENT-ON*
