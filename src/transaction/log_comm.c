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
 * log_comm.c - log and recovery manager (at client & server)
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#if !defined(WINDOWS)
#include <sys/time.h>
#endif /* !WINDOWS */

#include "log_comm.h"

#include "memory_alloc.h"
#include "storage_common.h"
#include "error_manager.h"
#include "porting.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "misc_string.h"
#include "intl_support.h"
#include "log_common_impl.h"
#if defined (SERVER_MODE)
#include "vacuum.h"
#endif /* SERVER_MODE */
#if !defined(WINDOWS)
#if defined(CS_MODE)
#include "db.h"
#endif /* CS_MODE */
#if defined(SERVER_MODE)
#include "server_support.h"
#include "connection_defs.h"
#endif /* SERVER_MODE */
#endif /* !WINDOWS */
#if defined (SERVER_MODE)
#include "thread_manager.hpp"
#endif // SERVER_MODE
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

struct tran_state_name
{
  TRAN_STATE state;
  const char *name;
};
typedef struct tran_state_name TRAN_STATE_NAME;

static TRAN_STATE_NAME log_Tran_state_names[] = {
  {TRAN_RECOVERY,
   "TRAN_RECOVERY"},
  {TRAN_ACTIVE,
   "TRAN_ACTIVE"},
  {TRAN_UNACTIVE_COMMITTED,
   "TRAN_UNACTIVE_COMMITTED"},
  {TRAN_UNACTIVE_WILL_COMMIT,
   "TRAN_UNACTIVE_WILL_COMMIT"},
  {TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE,
   "TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE"},
  {TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE,
   "TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE"},
  {TRAN_UNACTIVE_ABORTED,
   "TRAN_UNACTIVE_ABORTED"},
  {TRAN_UNACTIVE_UNILATERALLY_ABORTED,
   "TRAN_UNACTIVE_UNILATERALLY_ABORTED"},
  {TRAN_UNACTIVE_2PC_PREPARE,
   "TRAN_UNACTIVE_2PC_PREPARE"},
  {TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES,
   "TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES"},
  {TRAN_UNACTIVE_2PC_ABORT_DECISION,
   "TRAN_UNACTIVE_2PC_ABORT_DECISION"},
  {TRAN_UNACTIVE_2PC_COMMIT_DECISION,
   "TRAN_UNACTIVE_2PC_COMMIT_DECISION"},
  {TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS,
   "TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS"},
  {TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS,
   "TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS"},
  {TRAN_UNACTIVE_UNKNOWN,
   "TRAN_STATE_UNKNOWN"}
};

struct isolation_name
{
  TRAN_ISOLATION isolation;
  const char *name;
};
typedef struct isolation_name TRAN_ISOLATION_NAME;

static TRAN_ISOLATION_NAME log_Isolation_names[] = {
  {TRAN_SERIALIZABLE, "SERIALIZABLE"},
  {TRAN_REPEATABLE_READ, "REPEATABLE READ"},
  {TRAN_READ_COMMITTED, "COMMITTED READ"},
  {TRAN_UNKNOWN_ISOLATION, "TRAN_UNKNOWN_ISOLATION"}
};

const int LOG_MIN_NBUFFERS = 3;

/*
 * log_state_string - Translate state into string representation
 *
 * return:
 *
 *   state(in): Transaction state
 *
 * NOTE: Translate state into a string representation.
 */
const char *
log_state_string (TRAN_STATE state)
{
  int num = sizeof (log_Tran_state_names) / sizeof (TRAN_STATE_NAME);
  int i;

  for (i = 0; i < num; i++)
    {
      if (log_Tran_state_names[i].state == state)
	{
	  return log_Tran_state_names[i].name;
	}
    }

  return "TRAN_STATE_UNKNOWN";

}


/*
 * log_state_short_string - Translate state into string representation
 *                      without TRAN_ prefix
 *
 * return:
 *
 *   state(in): Transaction state
 */
const char *
log_state_short_string (TRAN_STATE state)
{
  const char *state_string;
  int skip_len;

  skip_len = sizeof ("TRAN_") - 1;
  state_string = log_state_string (state) + skip_len;

  return state_string;
}

/*
 * log_isolation_string - Translate isolation level into string representation
 *
 * return:
 *
 *   isolation(in): Isolation level. One of the following:
 *                         TRAN_REPEATABLE_READ
 *                         TRAN_READ_COMMITTED
 *                         TRAN_SERIALIZABLE
 *
 * NOTE:Translate degree of consistency into a string representation.
 */
const char *
log_isolation_string (TRAN_ISOLATION isolation)
{
  int num = sizeof (log_Isolation_names) / sizeof (TRAN_ISOLATION_NAME);
  int i;

  for (i = 0; i < num; i++)
    {
      if (log_Isolation_names[i].isolation == isolation)
	{
	  return log_Isolation_names[i].name;
	}
    }

  return "TRAN_UNKNOWN_ISOLATION";
}

/*
 * log_dump_log_info - Dump log information
 *
 * return: nothing
 *
 *   logname_info(in): Name of the log information file
 *   also_stdout(in):
 *   fmt(in): Format for the variable list of arguments (like printf)
 *   va_alist: Variable number of arguments
 *
 * NOTE:Dump some log information
 */
int
log_dump_log_info (const char *logname_info, bool also_stdout, const char *fmt, ...)
{
  FILE *fp;			/* Pointer to file */
  va_list ap;			/* Point to each unnamed arg in turn */
  time_t log_time;
  struct tm log_tm;
  struct tm *log_tm_p = &log_tm;
  struct timeval tv;
  char time_array[128];
  char time_array_of_log_info[255];

  va_start (ap, fmt);

  if (logname_info == NULL)
    {
      return ER_FAILED;
    }

  fp = fopen (logname_info, "a");
  if (fp == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LOG_MOUNT_FAIL, 1, logname_info);
      va_end (ap);
      return ER_LOG_MOUNT_FAIL;
    }

  log_time = time (NULL);
  log_tm_p = localtime_r (&log_time, &log_tm);
  if (log_tm_p == NULL)
    {
      strcpy (time_array_of_log_info, "Time: 00/00/00 00:00:00.000 - ");
    }
  else
    {
      gettimeofday (&tv, NULL);
      strftime (time_array, 128, "%m/%d/%y %H:%M:%S", log_tm_p);
      snprintf (time_array_of_log_info, 255, "Time: %s.%03ld - ", time_array, tv.tv_usec / 1000);
    }

  if (strlen (time_array_of_log_info) != TIME_SIZE_OF_DUMP_LOG_INFO)
    {
      strcpy (time_array_of_log_info, "Time: 00/00/00 00:00:00.000 - ");
    }

  fprintf (fp, "%s", time_array_of_log_info);

  (void) vfprintf (fp, fmt, ap);
  fflush (fp);
  fclose (fp);

#if !defined(NDEBUG)
  if (also_stdout && prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
    {
      va_start (ap, fmt);
      (void) vfprintf (stdout, fmt, ap);
      fflush (stdout);
    }
#endif

  va_end (ap);

  return NO_ERROR;
}

bool
log_does_allow_replication (void)
{
#if defined(WINDOWS) || defined(SA_MODE)
  return false;

#elif defined(CS_MODE)		/* WINDOWS || SA_MODE */
  int client_type;

  client_type = db_get_client_type ();
  if (client_type == DB_CLIENT_TYPE_LOG_COPIER || client_type == DB_CLIENT_TYPE_LOG_APPLIER)
    {
      return false;
    }

  return true;

#elif defined(SERVER_MODE)	/* CS_MODE */
  HA_SERVER_STATE ha_state;

  /* Vacuum workers are not allowed to reach this code */
  if (LOG_FIND_CURRENT_TDES () == NULL || !LOG_FIND_CURRENT_TDES ()->is_active_worker_transaction ())
    {
      return false;
    }

  if (HA_DISABLED ())
    {
      return false;
    }

  ha_state = css_ha_server_state ();
  if (ha_state != HA_SERVER_STATE_ACTIVE && ha_state != HA_SERVER_STATE_TO_BE_STANDBY)
    {
      return false;
    }

  assert (db_Disable_modifications == 0);

  return true;
#else /* SERVER_MODE */

  return false;
#endif
}
