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

/*
 * network_histogram.hpp - Add instrumentations to the client side to get histogram of network requests
 */

#ifndef _NETWORK_HISTOGRAM_HPP_
#define _NETWORK_HISTOGRAM_HPP_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include <atomic>
#include <array>

#include "network.h"

#if defined (CS_MODE)
struct net_histogram_entry
{
  int request_count;
  int total_size_sent;
  int total_size_received;
  int elapsed_time;

  net_histogram_entry () = default;
};

class net_histo_ctx
{
  public:
    using net_histogram_array_type = std::array<net_histogram_entry, NET_SERVER_REQUEST_END>;

    net_histo_ctx ();

    bool is_started ();
    void clear (void);

    int start_collect (bool for_all_trans);
    int stop_collect (void);

    void add_request (int request, int data_sent);
    void finish_request (int request, int data_received);

    int print_global_stats (FILE *stream, bool cumulative, const char *substr);
    int print_histogram (FILE *stream);

  private:
    bool is_collecting; /* whether collecting histogram is started */
    bool is_perfmon_setup; /* whether perfmon stat should be setup */
    UINT64 call_cnt;
    UINT64 last_call_time;
    UINT64 total_server_time;
    net_histogram_array_type histogram_entries;
};
#endif

/* common histogram API (CS/SA) */
extern bool histo_is_collecting (void);
extern bool histo_is_supported (void);
extern int histo_start (bool for_all_trans);
extern int histo_stop (void);
extern int histo_print (FILE *stream);
extern int histo_print_global_stats (FILE *stream, bool cumulative, const char *substr);
extern void histo_clear (void);

extern void histo_add_request (int request, int sent);
extern void histo_finish_request (int request, int received);

#endif /* _NETWORK_HISTOGRAM_HPP_ */
