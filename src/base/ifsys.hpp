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
 * ifsys.hpp
 */

#ifndef _IFSYS_HPP_
#define _IFSYS_HPP_

#ident "$Id$"

#include <vector>
#include <string>
#include <hwloc.h>

namespace cubbase
{
  class hardware_topology;

  class ifsys
  {
      friend hardware_topology;

    private:
      struct qirq
      {
	int q;
	int irq;
      };

      struct qirq_vec
      {
	struct qirq *v;
	int n;
	int cap;
      };

    public:
      static std::string auto_select_primary_iface ();

      static int find_irqs_for_iface (const char *ifname, struct qirq_vec *out);
      static int set_irq_affinity_list (int irq, int cpu);

      static bool set_rps_for_queue (const char *ifname, int rxq, int cpu);
      static bool set_xps_for_queue (const char *ifname, int txq, int cpu);
      static void maybe_set_rps_sock_flow_entries (int ncores);

    private:
      static bool file_exists (const std::string &path);

      static std::vector<std::string> list_entries (const std::string &path);
      static std::vector<std::string> list_dirs_with_prefix (const std::string &path, const std::string &prefix);
      static int listdir_count_prefix (const char *path, const char *prefix);

      static std::string read_one_line (const std::string &path);
      static unsigned long long read_u64 (const std::string &path);

      static int write_text (const char *path, const char *text);
      static int write_int (const char *path, long long int v);

      static char *cpumask_hex_for_single_cpu (int cpu);

      static bool name_blacklisted (const std::string &ifname);
      static bool is_physical_iface (const std::string &ifname);
      static bool is_up (const std::string &ifname);
      static int rx_queue_count (const std::string &ifname);
      static unsigned long long traffic_score (const std::string &ifname);

      static void qirq_push (struct qirq_vec *a, int q, int irq);
      static int parse_queue_suffix (const char *s);
  };
}

#endif
