/*
 *
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

#include "network_histogram.hpp"

#if defined(WINDOWS)
#include <sys/timeb.h>
#else
#include <sys/time.h>
#include <sys/timeb.h>
#endif

#include "perf_monitor.h"
#include "system_parameter.h"

#if defined (CS_MODE)
/* histogram context */
static struct net_histo_ctx net_histo_context;

net_histo_ctx::net_histo_ctx ()
  : is_collecting (false)
  , is_perfmon_setup (false)
  , call_cnt (0)
  , last_call_time (0)
  , total_server_time (0)
  , histogram_entries {}
{
  //
}

bool
net_histo_ctx::is_started ()
{
  return is_collecting;
}

void
net_histo_ctx::clear ()
{
  if (net_histo_context.is_perfmon_setup)
    {
      perfmon_reset_stats ();
    }

  call_cnt = 0;
  last_call_time = 0;
  total_server_time = 0;

  for (auto &entry : histogram_entries)
    {
      entry.request_count = 0;
      entry.total_size_sent = 0;
      entry.total_size_received = 0;
      entry.elapsed_time = 0;
    }
}

/*
 * start_collect -
 *
 * return: NO_ERROR or ER_FAILED
 *
 * Note:
 */
int
net_histo_ctx::start_collect (bool for_all_trans)
{
  if (is_collecting == false)
    {
      clear ();
      is_collecting = true;
    }

  if (is_perfmon_setup == false)
    {
      if (perfmon_start_stats (for_all_trans) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      is_perfmon_setup = true;
    }

  return NO_ERROR;
}

/*
 * stop_collect -
 */
int
net_histo_ctx::stop_collect ()
{
  int err = NO_ERROR;

  if (is_perfmon_setup == true)
    {
      err = perfmon_stop_stats ();
      is_perfmon_setup = false;
    }

  if (is_collecting == true)
    {
      is_collecting = false;
    }

  return err;
}

/*
 * add_request -
 */
void
net_histo_ctx::add_request (int request, int data_sent)
{
  struct timeval tp;

  if (request <= NET_SERVER_REQUEST_START || request >= NET_SERVER_REQUEST_END)
    {
      return;
    }

  net_histogram_entry &entry = histogram_entries[request];
  entry.request_count++;
  entry.total_size_sent += data_sent;

  if (gettimeofday (&tp, NULL) == 0)
    {
      last_call_time = tp.tv_sec * 1000000LL + tp.tv_usec;
    }

  call_cnt++;
}

/*
 * finish_request -
 */
void
net_histo_ctx::finish_request (int request, int data_received)
{
  struct timeval tp;
  INT64 current_time;

  if (request <= NET_SERVER_REQUEST_START || request >= NET_SERVER_REQUEST_END)
    {
      return;
    }

  net_histogram_entry &entry = histogram_entries[request];
  entry.total_size_received += data_received;

  if (gettimeofday (&tp, NULL) == 0)
    {
      current_time = tp.tv_sec * 1000000LL + tp.tv_usec;
      total_server_time = current_time - last_call_time;
      entry.elapsed_time += total_server_time;
    }
}

/*
 * print_global_stats -
 */
int
net_histo_ctx::print_global_stats (FILE *stream, bool cumulative, const char *substr)
{
  int err = NO_ERROR;

  if (is_perfmon_setup)
    {
      err = perfmon_print_global_stats (stream, cumulative, substr);
    }

  return err;
}

int
net_histo_ctx::print_histogram (FILE *stream)
{
  int err = NO_ERROR;

  if (stream == NULL)
    {
      stream = stdout;
    }

  fprintf (stream, "\nHistogram of client requests:\n");
  fprintf (stream, "%-31s %6s  %10s %10s , %10s \n", "Name", "Rcount", "Sent size", "Recv size", "Server time");

  if (call_cnt > 0)
    {
      int total_requests = 0, total_size_sent = 0, total_size_received = 0;
      float server_time, total_server_time = 0;

      /* print each entries time */
      for (int i = 0; i < NET_SERVER_REQUEST_END; i++)
	{
	  auto &entry = histogram_entries[i];
	  if (entry.request_count > 0)
	    {
	      server_time = ((float) entry.elapsed_time / 1000000 / (float) (entry.request_count));
	      fprintf (stream, "%-29s %6d X %10d+%10d b, %10.6f s\n", get_net_request_name (i),
		       entry.request_count, entry.total_size_sent,
		       entry.total_size_received, server_time);
	      total_requests += entry.request_count;
	      total_size_sent += entry.total_size_sent;
	      total_size_received += entry.total_size_received;
	      total_server_time += (server_time * entry.request_count);
	    }
	}

      /* print average time */
      float avg_response_time, avg_client_time;
      fprintf (stream, "-------------------------------------------------------------" "--------------\n");
      fprintf (stream, "Totals:                       %6d X %10d+%10d b  " "%10.6f s\n", total_requests,
	       total_size_sent, total_size_received, total_server_time);
      avg_response_time = total_server_time / total_requests;
      avg_client_time = 0.0;
      fprintf (stream,
	       "\n Average server response time = %6.6f secs \n"
	       " Average time between client requests = %6.6f secs \n", avg_response_time, avg_client_time);
    }
  else
    {
      fprintf (stream, " No server requests made\n");
    }

  if (is_perfmon_setup)
    {
      err = perfmon_print_stats (stream);
    }
  return err;
}
#endif

bool
histo_is_collecting (void)
{
#if defined(CS_MODE)
  return net_histo_context.is_started ();
#else
  return true;
#endif
}

bool
histo_is_supported (void)
{
  return prm_get_bool_value (PRM_ID_ENABLE_HISTO);
}

int
histo_start (bool for_all_trans)
{
#if defined (CS_MODE)
  return net_histo_context.start_collect (for_all_trans);
#else /* CS_MODE */
  return perfmon_start_stats (for_all_trans);
#endif /* !CS_MODE */
}

int
histo_stop (void)
{
#if defined (CS_MODE)
  return net_histo_context.stop_collect ();
#else /* CS_MODE */
  return perfmon_stop_stats ();
#endif /* !CS_MODE */
}

int
histo_print (FILE *stream)
{
  int err = NO_ERROR;

#if defined (CS_MODE)
  err = net_histo_context.print_histogram (stream);
#else /* CS_MODE */
  err = perfmon_print_stats (stream);
#endif /* !CS_MODE */

  return err;
}

int
histo_print_global_stats (FILE *stream, bool cumulative, const char *substr)
{
  int err = NO_ERROR;

#if defined (CS_MODE)
  err = net_histo_context.print_global_stats (stream, cumulative, substr);
#else /* CS_MODE */
  err = perfmon_print_global_stats (stream, cumulative, substr);
#endif /* !CS_MODE */

  return err;
}

void
histo_clear (void)
{
#if defined (CS_MODE)
  net_histo_context.clear ();
#else /* CS_MODE */
  perfmon_reset_stats ();
#endif /* !CS_MODE */
}

void
histo_add_request (int request, int sent)
{
#if defined (CS_MODE)
  net_histo_context.add_request (request, sent);
#endif /* !CS_MODE */
}

void
histo_finish_request (int request, int received)
{
#if defined (CS_MODE)
  net_histo_context.finish_request (request, received);
#endif /* !CS_MODE */
}