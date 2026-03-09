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
 * resources.cpp - get machine resource information.
 */

#include <cmath>
#include <thread>
#include <fstream>
#include <sched.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <linux/ethtool.h>

#include "resources.hpp"
#include "filesys_parser.hpp"
#include "ifsys.hpp"
#include "cgroup.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace os::resources
{
  std::optional<std::string> execute_command (const char *cmd)
  {
    std::string result;
    char buffer[256];
    FILE *pipe;

    pipe = popen ((std::string (cmd) + " 2>&1").c_str (), "r");
    if (!pipe)
      {
	return std::nullopt;
      }

    result = "";
    while (fgets (buffer, sizeof (buffer), pipe) != nullptr)
      {
	result += buffer;
      }

    pclose (pipe);
    return result;
  }

  namespace cpu
  {
    std::optional<std::set<std::size_t>> affinity_cpuset ()
    {
      std::set<std::size_t> cpuset;
      cpu_set_t *bitmap;
      std::size_t size, bytes;
      std::size_t i, j;

      size = 1024;
      /* scales up to 2^18 */
      for (i = 0; i < 8; i++)
	{
	  bytes = CPU_ALLOC_SIZE (size);
	  bitmap = CPU_ALLOC (size);
	  if (!bitmap)
	    {
	      return std::nullopt;
	    }

	  CPU_ZERO_S (bytes, bitmap);
	  if (sched_getaffinity (0, bytes, bitmap) < 0)
	    {
	      if (errno == EINVAL)
		{
		  size *= 2;

		  CPU_FREE (bitmap);
		  continue;
		}

	      /* _er_log_debug (ARG_FILE_LINE, "failed to sched_getaffinity: %s\n", strerror (errno)); */

	      CPU_FREE (bitmap);
	      return std::nullopt;
	    }

	  for (j = 0; j < size; j++)
	    {
	      if (CPU_ISSET_S (j, bytes, bitmap))
		{
		  cpuset.insert (j);
		}
	    }

	  CPU_FREE (bitmap);
	  return cpuset;
	}

      /* _er_log_debug (ARG_FILE_LINE, "failed to create cpuset: number of cores exceeds 2^18.\n"); */
      return std::nullopt;
    }

    std::optional<std::set<std::size_t>> online_cpuset ()
    {
      std::ifstream file (path::cpu_online);
      std::set<std::size_t> cpuset;
      std::string line;

      if (!file)
	{
	  /* _er_log_debug (ARG_FILE_LINE, "failed to open %s: %s\n", path::cpu_online, strerror (errno)); */
	  return std::nullopt;
	}

      file >> line;
      if (line.empty ())
	{
	  /* _er_log_debug (ARG_FILE_LINE, "the file %s is empty.\n", path::cpu_online); */
	  return std::nullopt;
	}
      return parser::range_set_to_set<std::size_t> (line);
    }

    void setaffinity (std::size_t core)
    {
      cpu_set_t set;
      int status;

      CPU_ZERO (&set);
      CPU_SET (core, &set);
      status = pthread_setaffinity_np (pthread_self (), sizeof (set), &set);
      if (status)
	{
	  _er_log_debug (__FILE__, __LINE__, "pthread_setaffinity_np failed for core %d: %s\n", core, strerror (status));
	}
    }

    context effective ()
    {
      static const context ctx = []() -> context
      {
	std::optional<std::set<std::size_t>> affinity, online;
	cgroup::cpu::context cgroup;
	context ctx;
	int nprocessors;

	affinity = affinity_cpuset ();
	if (affinity)
	  {
	    ctx.max = affinity->size ();
	    ctx.effective = std::move (*affinity);
	  }

	online = online_cpuset ();
	if (online)
	  {
	    if (ctx.effective)
	      {
		ctx.max = ctx.max > online->size () ? online->size () : ctx.max;
		ctx.effective = parser::intersection (*ctx.effective, *online);
	      }
	    else
	      {
		ctx.max = online->size ();
		ctx.effective = std::move (*online);
	      }
	  }

	cgroup = cgroup::cpu::quota_v2 ();
	if (cgroup.max_v2 &&
	    *cgroup.max_v2 != std::numeric_limits<double>::max () && ctx.max > *cgroup.max_v2)
	  {
	    ctx.max = *cgroup.max_v2;
	  }
	if (cgroup.effective_v2)
	  {
	    if (ctx.effective)
	      {
		ctx.effective = parser::intersection (*ctx.effective, *cgroup.effective_v2);
	      }
	    else
	      {
		ctx.effective = *cgroup.effective_v2;
	      }
	  }

	if (ctx.max == std::numeric_limits<double>::max ())
	  {
	    nprocessors = std::thread::hardware_concurrency();
	    if (nprocessors == 0)
	      {
		nprocessors = 1;
	      }
	    ctx.max = nprocessors;
	  }

	/* correction & adjustment */
	ctx.adjusted_max = static_cast<std::size_t> (std::floor (ctx.max));
	if (ctx.adjusted_max <= 0)
	  {
	    ctx.adjusted_max = 1;
	  }

	if (ctx.effective && !ctx.effective->empty ())
	  {
	    ctx.adjusted_effective = std::vector<std::size_t> (
					     ctx.effective->begin (),
					     std::next (ctx.effective->begin (), std::min (ctx.effective->size (), ctx.adjusted_max))
				     );
	    if (ctx.adjusted_effective->size () < ctx.adjusted_max)
	      {
		ctx.adjusted_max = ctx.adjusted_effective->size ();
	      }
	  }

	return ctx;
      } ();

      return ctx;
    }
  }

  namespace net
  {
    bool set_nic_channels (std::string &ifname, unsigned int combined)
    {
      struct ethtool_channels channel;
      struct ifreq ifr;
      std::optional<std::string> output;
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
	  if (!output)
	    {
	      _er_log_debug (__FILE__, __LINE__, "warning: failed to execute the command: %s", command);
	      ::close (fd);
	      return false;
	    }
	}

      ::close (fd);
      return true;
    }

    void map_nic_to_index (std::vector<std::size_t> &index)
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
      if (!set_nic_channels (ifname, index.size ()))
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
	      cubbase::ifsys::set_irq_affinity_list (qs.v[i].irq, index[q % index.size ()]);
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

      cubbase::ifsys::maybe_set_rps_sock_flow_entries (index.size ());

      for (q = 0; q < rx_count; q++)
	{
	  cubbase::ifsys::set_rps_for_queue (ifname.c_str (), q, index[q % index.size ()]);
	}
      for (q = 0; q < tx_count; q++)
	{
	  cubbase::ifsys::set_xps_for_queue (ifname.c_str (), q, index[q % index.size ()]);
	}
    }
  }
}
