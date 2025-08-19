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

#include "tsc_timer.h"

#include "perf_monitor.h"
#include "system_parameter.h"

#if defined (SERVER_MODE)
#include "session.h"
#include "thread_manager.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
#endif

#if defined (CS_MODE)
/* histogram context */
static struct net_histo_ctx net_histo_context;
#endif

void
net_histogram_entry::clear (void)
{
  request_count = 0;
  total_size_sent = 0;
  total_size_received = 0;
  elapsed_time = 0;
}

void
net_histogram_entry::print (FILE *stream)
{
  fprintf (stream, "%d %d %d %d", request_count, total_size_sent, total_size_received, elapsed_time);
}

net_histo_ctx::net_histo_ctx ()
  : is_collecting (false)
  , is_perfmon_setup (false)
  , call_cnt (0)
  , last_call_tick {}
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
#if !defined (SERVER_MODE)
  if (is_perfmon_setup)
    {
      perfmon_reset_stats ();
    }
#endif /* SERVER_MODE */

  call_cnt = 0;
  // reset last call tick
  memset (&last_call_tick, 0, sizeof (last_call_tick));
  total_server_time = 0;

  for (auto &entry : histogram_entries)
    {
      entry.clear ();
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
net_histo_ctx::start_collect ()
{
  if (is_collecting == false)
    {
      clear ();
      is_collecting = true;
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

  if (is_collecting == true)
    {
      is_collecting = false;
    }

  return err;
}

#if !defined (SERVER_MODE)
int
net_histo_ctx::start_perfmon_stats (bool for_all_trans)
{
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

int
net_histo_ctx::stop_perfmon_stats (void)
{
  if (is_perfmon_setup == true)
    {
      int err = perfmon_stop_stats ();
      if (err == NO_ERROR)
	{
	  is_perfmon_setup = false;
	}
      return err;
    }

  return NO_ERROR;
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
#else
int
net_histo_ctx::start_perfmon_stats (bool for_all_trans)
{
  return NO_ERROR;
}

int
net_histo_ctx::stop_perfmon_stats (void)
{
  return NO_ERROR;
}

/*
 * print_global_stats -
 */
int
net_histo_ctx::print_global_stats (FILE *stream, bool cumulative, const char *substr)
{
  return NO_ERROR;
}
#endif /* !SERVER_MODE */

/*
 * add_request -
 */
void
net_histo_ctx::add_request (int request, int data_sent)
{
  if (request <= NET_SERVER_REQUEST_START || request >= NET_SERVER_REQUEST_END)
    {
      assert (false);
      return;
    }

  net_histogram_entry &entry = histogram_entries[request];
  entry.request_count++;

#if defined (SERVER_MODE)
  entry.total_size_received += data_sent;
#else
  entry.total_size_sent += data_sent;
#endif /* SERVER_MODE */

  // mark start tick using TSC
  tsc_getticks (&last_call_tick);

  call_cnt++;
}

/*
 * finish_request -
 */
void
net_histo_ctx::finish_request (int request, int data_received)
{
  if (request <= NET_SERVER_REQUEST_START || request >= NET_SERVER_REQUEST_END)
    {
      assert (false);
      return;
    }

  // compute elapsed usec via TSC to match csql
  TSC_TICKS now_tick;
  TSCTIMEVAL tv_diff;
  tsc_getticks (&now_tick);
  tsc_elapsed_time_usec (&tv_diff, now_tick, last_call_tick);
  total_server_time = (INT64)tv_diff.tv_sec * 1000000LL + (INT64)tv_diff.tv_usec;

  net_histogram_entry &entry = histogram_entries[request];

  entry.elapsed_time += total_server_time;
#if defined (SERVER_MODE)
  entry.total_size_sent += data_received;
#else
  entry.total_size_received += data_received;
#endif /* SERVER_MODE */
}

int
net_histo_ctx::print_histogram (FILE *stream)
{
  int err = NO_ERROR;

  if (stream == NULL)
    {
      stream = stdout;
    }

#if defined (SERVER_MODE)
  fprintf (stream, "\nHistogram of server requests:\n");
#else
  fprintf (stream, "\nHistogram of client requests:\n");
#endif /* SERVER_MODE */
  fprintf (stream, "%-31s %6s  %10s %10s , %10s \n", "Name", "Rcount", "Sent size", "Recv size", "Server time");

  if (call_cnt > 0)
    {
      int total_requests = 0, total_size_sent = 0, total_size_received = 0;
      double server_time_sec = 0.0;
      double total_server_time_sec = 0.0;

      /* print each entries time */
      for (int i = 0; i < NET_SERVER_REQUEST_END; i++)
	{
	  auto &entry = histogram_entries[i];
	  if (entry.request_count > 0)
	    {
	      // average server time per request in seconds (align with csql double formatting)
	      server_time_sec = (double) entry.elapsed_time / 1'000'000.0;
	      fprintf (stream, "%-29s %6d X %10d+%10d b, %10.6f s\n",
		       get_net_request_name (i), entry.request_count,
		       entry.total_size_sent, entry.total_size_received, server_time_sec);
	      total_requests += entry.request_count;
	      total_size_sent += entry.total_size_sent;
	      total_size_received += entry.total_size_received;
	      total_server_time_sec += server_time_sec;
	    }
	}

      /* print average time */
      double avg_response_time, avg_client_time;
      fprintf (stream, "-------------------------------------------------------------" "--------------\n");
      fprintf (stream, "Totals:                       %6d X %10d+%10d b  " "%10.6f s\n",
	       total_requests, total_size_sent, total_size_received, total_server_time_sec);
      avg_response_time = total_server_time_sec / (double)total_requests;
      avg_client_time = 0.0;
      fprintf (stream,
	       "\n Average server response time = %6.6f secs \n"
	       " Average time between client requests = %6.6f secs \n", avg_response_time, avg_client_time);
    }
  else
    {
      fprintf (stream, " No server requests made\n");
    }

#if !defined (SERVER_MODE)
  if (is_perfmon_setup)
    {
      err = perfmon_print_stats (stream);
    }
#endif /* !SERVER_MODE */

  return err;
}

#if !defined (SERVER_MODE)

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
histo_start (bool for_perfmon, bool for_all_trans)
{
#if defined (CS_MODE)
  net_histo_context.start_collect ();
  if (for_perfmon)
    {
      net_histo_context.start_perfmon_stats (for_all_trans);
    }
  return NO_ERROR;
#else /* CS_MODE */
  return perfmon_start_stats (for_all_trans);
#endif /* !CS_MODE */
}

int
histo_stop (void)
{
#if defined (CS_MODE)
  net_histo_context.stop_collect ();
  return net_histo_context.stop_perfmon_stats ();
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
histo_print_string (std::string &str)
{

  return NO_ERROR;
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

#else
void
histo_finish_request (int request, int received)
{
  net_histo_ctx *net_histo_ctx_p = NULL;
  session_get_net_histo_ctx (thread_get_thread_entry_info (), net_histo_ctx_p);
  if (net_histo_ctx_p != NULL)
    {
      net_histo_ctx_p->finish_request (request, received);
    }

  if (request == NET_SERVER_QM_QUERY_EXECUTE || request == NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE)
    {
      // stop collect
      net_histo_ctx_p->stop_collect ();

      if (thread_need_clear_trace (thread_get_thread_entry_info ()) == false)
	{
	  char *histo_str = NULL;
	  size_t sizeloc2;
	  FILE *fp2;

	  fp2 = port_open_memstream (&histo_str, &sizeloc2);
	  if (fp2)
	    {
	      net_histo_ctx *net_histo_ctx_p = NULL;
	      session_get_net_histo_ctx (thread_get_thread_entry_info (), net_histo_ctx_p);
	      if (net_histo_ctx_p != NULL)
		{
		  net_histo_ctx_p->print_histogram (fp2);
		}
	      port_close_memstream (fp2, &histo_str, &sizeloc2);
	    }
	  session_set_comm_histo_sr (thread_get_thread_entry_info (), histo_str);
	}
      else
	{
	  // clear
	  net_histo_ctx_p->clear ();
	}
    }
}
#endif /* !SERVER_MODE */
