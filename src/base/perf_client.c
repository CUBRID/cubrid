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
 * perf_monitor.c - Monitor execution statistics at Client
 * 					Monitor execution statistics
 */

#include <stdio.h>
#include <time.h>
#include <assert.h>
#if !defined (WINDOWS)
#include <sys/time.h>
#include <sys/resource.h>
#endif /* WINDOWS */
#include "perf_client.h"
#include "error_manager.h"

#if !defined(SERVER_MODE)
#include "memory_alloc.h"
#include "server_interface.h"
#endif /* !SERVER_MODE */

#include "network_interface_cl.h"

bool perfmon_Iscollecting_stats = false;

/* Client execution statistics */
static PERFMON_CLIENT_STAT_INFO perfmon_Stat_info;

static void perfmon_get_current_times (time_t * cpu_usr_time, time_t * cpu_sys_time, time_t * elapsed_time);

/*
 *   perfmon_get_current_times - Get current CPU and elapsed times
 *   return:
 *   cpu_user_time(out):
 *   cpu_sys_time(out):
 *   elapsed_time(out):
 *
 * Note:
 */
static void
perfmon_get_current_times (time_t * cpu_user_time, time_t * cpu_sys_time, time_t * elapsed_time)
{
#if defined (WINDOWS)
  *cpu_user_time = 0;
  *cpu_sys_time = 0;
  *elapsed_time = 0;

  *elapsed_time = time (NULL);
#else /* WINDOWS */
  struct rusage rusage;

  *cpu_user_time = 0;
  *cpu_sys_time = 0;
  *elapsed_time = 0;

  *elapsed_time = time (NULL);

  if (getrusage (RUSAGE_SELF, &rusage) == 0)
    {
      *cpu_user_time = rusage.ru_utime.tv_sec;
      *cpu_sys_time = rusage.ru_stime.tv_sec;
    }
#endif /* WINDOWS */
}

/*
 * perfmon_start_stats - Start collecting client execution statistics
 *   return: NO_ERROR or ERROR
 */
int
perfmon_start_stats (bool for_all_trans)
{
  int err = NO_ERROR;

  if (perfmon_Iscollecting_stats == true)
    {
      goto exit;
    }

  perfmon_Stat_info.old_global_stats = NULL;
  perfmon_Stat_info.current_global_stats = NULL;
  perfmon_Stat_info.base_server_stats = NULL;
  perfmon_Stat_info.current_server_stats = NULL;

  err = perfmon_server_start_stats ();
  if (err != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  perfmon_Iscollecting_stats = true;

  perfmon_get_current_times (&perfmon_Stat_info.cpu_start_usr_time, &perfmon_Stat_info.cpu_start_sys_time,
			     &perfmon_Stat_info.elapsed_start_time);

  if (for_all_trans)
    {
      perfmon_Stat_info.old_global_stats = perfmeta_allocate_values ();
      if (perfmon_Stat_info.old_global_stats == NULL)
	{
	  ASSERT_ERROR ();
	  err = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}
      perfmon_Stat_info.current_global_stats = perfmeta_allocate_values ();

      if (perfmon_Stat_info.current_global_stats == NULL)
	{
	  ASSERT_ERROR ();
	  err = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}

      if (perfmon_get_global_stats () == NO_ERROR)
	{
	  perfmeta_copy_values (perfmon_Stat_info.old_global_stats, perfmon_Stat_info.current_global_stats);
	}
    }
  else
    {
      perfmon_Stat_info.base_server_stats = perfmeta_allocate_values ();
      if (perfmon_Stat_info.base_server_stats == NULL)
	{
	  ASSERT_ERROR ();
	  err = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}
      perfmon_Stat_info.current_server_stats = perfmeta_allocate_values ();
      if (perfmon_Stat_info.current_server_stats == NULL)
	{
	  ASSERT_ERROR ();
	  err = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}

      if (perfmon_get_stats () == NO_ERROR)
	{
	  perfmeta_copy_values (perfmon_Stat_info.base_server_stats, perfmon_Stat_info.current_server_stats);
	}
    }
exit:
  return err;
}

/*
 * perfmon_stop_stats - Stop collecting client execution statistics
 *   return: NO_ERROR or ER_FAILED
 */
int
perfmon_stop_stats (void)
{
  int err = NO_ERROR;

  if (perfmon_Iscollecting_stats != false)
    {
      err = perfmon_server_stop_stats ();
      perfmon_Iscollecting_stats = false;
    }

  if (perfmon_Stat_info.old_global_stats != NULL)
    {
      free_and_init (perfmon_Stat_info.old_global_stats);
    }
  if (perfmon_Stat_info.current_global_stats != NULL)
    {
      free_and_init (perfmon_Stat_info.current_global_stats);
    }
  if (perfmon_Stat_info.base_server_stats != NULL)
    {
      free_and_init (perfmon_Stat_info.base_server_stats);
    }

  if (perfmon_Stat_info.current_server_stats != NULL)
    {
      free_and_init (perfmon_Stat_info.current_server_stats);
    }

  return err;
}

/*
 * perfmon_reset_stats - Reset client statistics
 *   return: none
 */
void
perfmon_reset_stats (void)
{
  if (perfmon_Iscollecting_stats != false)
    {
      perfmon_get_current_times (&perfmon_Stat_info.cpu_start_usr_time, &perfmon_Stat_info.cpu_start_sys_time,
				 &perfmon_Stat_info.elapsed_start_time);

      if (perfmon_get_stats () == NO_ERROR)
	{
	  perfmeta_copy_values (perfmon_Stat_info.base_server_stats, perfmon_Stat_info.current_server_stats);
	}
    }
}

/*
 * perfmon_get_stats - Get the recorded client statistics
 *   return: client statistics
 */
int
perfmon_get_stats (void)
{
  int err = NO_ERROR;

  if (perfmon_Iscollecting_stats != true)
    {
      return ER_FAILED;
    }

  err = perfmon_server_copy_stats (perfmon_Stat_info.current_server_stats);
  return err;
}

/*
 *   perfmon_get_global_stats - Get the recorded client statistics
 *   return: client statistics
 */
int
perfmon_get_global_stats (void)
{
  UINT64 *tmp_stats;
  int err = NO_ERROR;

  if (perfmon_Iscollecting_stats != true)
    {
      return ER_FAILED;
    }

  tmp_stats = perfmon_Stat_info.current_global_stats;
  perfmon_Stat_info.current_global_stats = perfmon_Stat_info.old_global_stats;
  perfmon_Stat_info.old_global_stats = tmp_stats;

  /* Refresh statistics from server */
  err = perfmon_server_copy_global_stats (perfmon_Stat_info.current_global_stats);
  if (err != NO_ERROR)
    {
      ASSERT_ERROR ();
    }
  return err;
}

/*
 *   perfmon_print_stats - Print the current client statistics
 *   return: error or no error
 *   stream(in): if NULL is given, stdout is used
 */
int
perfmon_print_stats (FILE * stream)
{
  time_t cpu_total_usr_time;
  time_t cpu_total_sys_time;
  time_t elapsed_total_time;
  UINT64 *diff_result = NULL;
  int err = NO_ERROR;

  if (perfmon_Iscollecting_stats != true)
    {
      return err;
    }

  diff_result = perfmeta_allocate_values ();

  if (diff_result == NULL)
    {
      ASSERT_ERROR ();
      err = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }

  if (stream == NULL)
    {
      stream = stdout;
    }

  if (perfmon_get_stats () != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  perfmon_get_current_times (&cpu_total_usr_time, &cpu_total_sys_time, &elapsed_total_time);

  fprintf (stream, "\n *** CLIENT EXECUTION STATISTICS ***\n");

  fprintf (stream, "System CPU (sec)              = %10d\n",
	   (int) (cpu_total_sys_time - perfmon_Stat_info.cpu_start_sys_time));
  fprintf (stream, "User CPU (sec)                = %10d\n",
	   (int) (cpu_total_usr_time - perfmon_Stat_info.cpu_start_usr_time));
  fprintf (stream, "Elapsed (sec)                 = %10d\n",
	   (int) (elapsed_total_time - perfmon_Stat_info.elapsed_start_time));

  if (perfmeta_diff_stats (diff_result, perfmon_Stat_info.current_server_stats,
			       perfmon_Stat_info.base_server_stats) != NO_ERROR)
    {
      assert (false);
      goto exit;
    }
  perfmon_server_dump_stats (diff_result, stream, NULL);

exit:
  if (diff_result != NULL)
    {
      free_and_init (diff_result);
    }
  return err;
}

/*
 *   perfmon_print_global_stats - Print the global statistics
 *   return: error or no error
 *   stream(in): if NULL is given, stdout is used
 */
int
perfmon_print_global_stats (FILE * stream, FILE * bin_stream, bool cumulative, const char *substr)
{
  UINT64 *diff_result = NULL;
  int err = NO_ERROR;

  if (stream == NULL)
    {
      stream = stdout;
    }
  diff_result = perfmeta_allocate_values ();

  if (diff_result == NULL)
    {
      ASSERT_ERROR ();
      err = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }
  err = perfmon_get_global_stats ();
  if (err != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }
  if (cumulative)
    {
      if (bin_stream != NULL)
	{
	  char *packed_stats = (char *) malloc (sizeof (UINT64) * perfmeta_get_values_count ());
	  perfmon_pack_stats (packed_stats, perfmon_Stat_info.current_global_stats);
	  fwrite (packed_stats, sizeof (UINT64), (size_t) perfmeta_get_values_count (), bin_stream);
	  free (packed_stats);
	}
      perfmon_server_dump_stats (perfmon_Stat_info.current_global_stats, stream, substr);
    }
  else
    {
      if (perfmeta_diff_stats (diff_result, perfmon_Stat_info.current_global_stats,
				   perfmon_Stat_info.old_global_stats) != NO_ERROR)
	{
	  assert (false);
	  goto exit;
	}
      perfmon_server_dump_stats (diff_result, stream, substr);
    }

exit:
  if (diff_result != NULL)
    {
      free_and_init (diff_result);
    }
  return err;
}
