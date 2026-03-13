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
 * hardware_topology.cpp
 */

#include "error_manager.h"
#include "ifsys.hpp"
#include "hardware_topology.hpp"

#include <iostream>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cinttypes>

#include <hwloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubbase
{
  hardware_topology topology;

  hardware_topology::hardware_topology ()
  {
    hwloc_topology_init (&m_topology);
    hwloc_topology_load (m_topology);
  }

  hardware_topology::~hardware_topology ()
  {
    hwloc_topology_destroy (m_topology);
  }

  void hardware_topology::pin_core (int core)
  {
    cpu_set_t set;
    int fail;

    CPU_ZERO (&set);
    CPU_SET (core, &set);
    fail = pthread_setaffinity_np (pthread_self (), sizeof (set), &set);
    if (fail)
      {
	errno = fail;
	_er_log_debug (__FILE__, __LINE__, "pthread_setaffinity_np failed for core %d: %s\n", core, strerror (errno));
      }
  }

  std::vector<int> &hardware_topology::get_cores ()
  {
    return m_selected;
  }

  void hardware_topology::map_nic_to_core ()
  {
    cubbase::ifsys::qirq_vec qs = { 0, 0, 0 };
    char qbase[256], rxdir[280], txdir[280];
    std::string ifname;
    int rx_count, tx_count;
    int i, q;

    /* get ifname */
    ifname = cubbase::ifsys::auto_select_primary_iface ();
    if (ifname.empty ())
      {
	_er_log_debug (__FILE__, __LINE__, "warning: no interfaces available for selection. (virtual environment)\n");
	return ;
      }

    /* channel */
    if (!this->set_nic_channels (ifname, m_selected.size ()))
      {
	_er_log_debug (__FILE__, __LINE__, "warning: NIC channel configuration failed. (driver may limit)\n");
      }
    /* wait until applied */
    usleep (1000 * 1000);

    if (cubbase::ifsys::find_irqs_for_iface (ifname.c_str (), &qs) == 0 && qs.n > 0)
      {
	for (i = 0; i < qs.n; i++)
	  {
	    q = qs.v[i].q;
	    cubbase::ifsys::set_irq_affinity_list (qs.v[i].irq, m_selected[q % m_selected.size ()]);
	  }
      }
    else
      {
	_er_log_debug (__FILE__, __LINE__, "warning: no IRQ found for %s in /proc/interrupts.\n", ifname.c_str ());
      }
    free (qs.v);

    /* RPS/XPS */
    snprintf (qbase, sizeof (qbase), "/sys/class/net/%s/queues", ifname.c_str ());
    snprintf (rxdir, sizeof (rxdir), "%s/rx-", qbase);
    snprintf (txdir, sizeof (txdir), "%s/tx-", qbase);
    rx_count = cubbase::ifsys::listdir_count_prefix (qbase, "rx-");
    tx_count = cubbase::ifsys::listdir_count_prefix (qbase, "tx-");

    cubbase::ifsys::maybe_set_rps_sock_flow_entries (m_selected.size ());

    for (q = 0; q < rx_count; q++)
      {
	cubbase::ifsys::set_rps_for_queue (ifname.c_str (), q, m_selected[q % m_selected.size ()]);
      }
    for (q = 0; q < tx_count; q++)
      {
	cubbase::ifsys::set_xps_for_queue (ifname.c_str (), q, m_selected[q % m_selected.size ()]);
      }
  }

  void hardware_topology::load_cpu (int limit)
  {
    hwloc_const_cpuset_t online;
    hwloc_obj_t core;
    std::vector<int> pus;
    hwloc_obj_t pu;
    int ncores, npus;
    int i, j;

    online = hwloc_topology_get_allowed_cpuset (m_topology);
    ncores = hwloc_get_nbobjs_by_type (m_topology, HWLOC_OBJ_CORE);

    m_cores.clear ();
    m_selected.clear ();
    m_cores.reserve (ncores);
    for (i = 0; i < ncores; i++)
      {
	core = hwloc_get_obj_by_type (m_topology, HWLOC_OBJ_CORE, i);
	if (!core || !core->cpuset)
	  {
	    continue;
	  }

	pus.clear ();
	npus = hwloc_get_nbobjs_inside_cpuset_by_type (m_topology, core->cpuset, HWLOC_OBJ_PU);
	for (j = 0; j < npus; j++)
	  {
	    pu = hwloc_get_obj_inside_cpuset_by_type (m_topology, core->cpuset, HWLOC_OBJ_PU, j);
	    if (pu && pu->cpuset && hwloc_bitmap_isincluded (pu->cpuset, online))
	      {
		pus.push_back (pu->os_index);
	      }
	  }
	std::sort (pus.begin (), pus.end ());
	m_cores.push_back (std::move (pus));
      }

    /* consider up to SMT of 8 */
    /* TODO: add selection strategy */
    for (i = 0; i < 8; i++)
      {
	for (j = 0; j < ncores; j++)
	  {
	    if (m_cores[j].size () < static_cast<size_t> (i + 1))
	      {
		continue;
	      }
	    if (limit > static_cast<int> (m_selected.size ()))
	      {
		m_selected.emplace_back (m_cores[j][i]);
	      }
	  }
      }
  }

  std::string hardware_topology::execute_command (const char *cmd)
  {
    std::string pipe_cmd;
    std::string result;
    char buffer[256];
    FILE *pipe;

    pipe_cmd = std::string (cmd) + " 2>&1";
    pipe = popen (pipe_cmd.c_str (), "r");
    if (!pipe)
      {
	return "failed to popen ()";
      }

    result = "";
    while (fgets (buffer, sizeof (buffer), pipe) != nullptr)
      {
	result += buffer;
      }

    pclose (pipe);

    return result;
  }

  bool hardware_topology::set_nic_channels (std::string &ifname, unsigned int combined)
  {
    struct ethtool_channels channel;
    struct ifreq ifr;
    std::string output;
    char command[256];
    int success;
    int fd;

    fd = socket (AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
      {
	perror ("socket");
	return false;
      }

    memset (&ifr, 0, sizeof (ifr));
    memset (&channel, 0, sizeof (channel));

    channel.cmd = ETHTOOL_SCHANNELS;
    channel.combined_count = combined;
    strncpy (ifr.ifr_name, ifname.c_str (), IFNAMSIZ - 1);
    ifr.ifr_data = reinterpret_cast<char *> (&channel);

    success = ioctl (fd, SIOCETHTOOL, &ifr);
    if (success)
      {
	snprintf (command, sizeof (command), "ethtool -L %s combined %u", ifname.c_str (), combined);
	output = execute_command (command);
	if (!output.empty ())
	  {
	    _er_log_debug (__FILE__, __LINE__, "warning: %s", output.c_str ());
	    ::close (fd);
	    return false;
	  }
      }

    ::close (fd);
    return true;
  }
}

