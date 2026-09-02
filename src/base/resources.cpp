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
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <thread>
#include <fstream>
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
#include "error_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace os::resources
{
  static bool is_permission_error (int error)
  {
    return error == EACCES || error == EPERM;
  }

  static void guide_hardware_affinity_permission ()
  {
    _er_log_debug (__FILE__, __LINE__,
		   "You must either start the server as the root user or grant the necessary capabilities "
		   "to the server binary as shown below.\n"
		   "(Note: It is recommended to disable irqbalance if you want this server to manage your hardware directly.)\n\n"
		   "  $> sudo -E \"$CUBRID/bin/cubrid\" server start <db_name>\n\n"
		   "or\n\n"
		   "  $> sudo setcap \'cap_net_admin,cap_dac_override+ep\' \"$CUBRID/bin/cub_server\"\n"
		   "  $> getcap \"$CUBRID/bin/cub_server\"\n"
		   "\n");
  }

  void initialize ()
  {
    os::resources::cpu::effective ();
  }

  namespace cpu
  {
    std::optional<std::tuple<std::set<std::size_t>, cpu_set_t *, std::size_t>> affinity_cpuset ()
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

	  return std::tuple { cpuset, bitmap, size };
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
      cpu_set_t *bitmap;
      std::size_t size;
      int status;
      const auto &ctx = effective ();
      if (!ctx.affinity.bitmap)
	{
	  return ;
	}

      size = CPU_ALLOC_SIZE (ctx.affinity.size);
      bitmap = CPU_ALLOC (ctx.affinity.size);
      if (!bitmap)
	{
	  return ;
	}

      CPU_ZERO_S (size, bitmap);
      CPU_SET_S (core, size, bitmap);

      status = pthread_setaffinity_np (pthread_self (), size, bitmap);
      if (status)
	{
	  _er_log_debug (__FILE__, __LINE__, "pthread_setaffinity_np failed for core %d: %s\n", core, strerror (status));
	}

      CPU_FREE (bitmap);
    }

    void clearaffinity ()
    {
      int status;
      const auto &ctx = effective ();
      if (!ctx.affinity.bitmap)
	{
	  return ;
	}

      status = pthread_setaffinity_np (pthread_self (), CPU_ALLOC_SIZE (ctx.affinity.size), ctx.affinity.bitmap);
      if (status)
	{
	  _er_log_debug (__FILE__, __LINE__, "pthread_setaffinity_np failed: %s\n", strerror (status));
	}
    }

    /* This function must be called first in the main thread's entry point */
    /* to initialize CPU and affinity information.			   */
    context &effective ()
    {
      static context ctx = []() -> context
      {
	std::optional<std::set<std::size_t>> online;
	std::set<std::size_t> affinity;
	cgroup::cpu::context cgroup;
	cpu_set_t *bitmap;
	int nprocessors;
	std::size_t size;
	context ctx;

	auto affinity_tuple = affinity_cpuset ();
	if (affinity_tuple)
	  {
	    std::tie (affinity, bitmap, size) = *affinity_tuple;
	    assert (bitmap && size > 0);

	    ctx.max = affinity.size ();
	    ctx.effective = std::move (affinity);

	    if (ctx.affinity.bitmap)
	      {
		CPU_FREE (ctx.affinity.bitmap);
	      }
	    ctx.affinity.bitmap = bitmap;
	    ctx.affinity.size = size;
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
    bool set_nic_channels (std::string &ifname, unsigned int combined, bool *permission_error)
    {
      struct ethtool_channels channel;
      struct ifreq ifr;
      int saved_errno;
      int success;
      int fd;

      if (permission_error)
	{
	  *permission_error = false;
	}

      fd = socket (AF_INET, SOCK_DGRAM, 0);
      if (fd < 0)
	{
	  if (permission_error && is_permission_error (errno))
	    {
	      *permission_error = true;
	    }
	  return false;
	}

      memset (&ifr, 0, sizeof (ifr));
      memset (&channel, 0, sizeof (channel));

      strncpy (ifr.ifr_name, ifname.c_str (), IFNAMSIZ - 1);
      ifr.ifr_data = reinterpret_cast<char *> (&channel);

      channel.cmd = ETHTOOL_GCHANNELS;
      success = ioctl (fd, SIOCETHTOOL, &ifr);
      if (success)
	{
	  saved_errno = errno;
	  if (permission_error && is_permission_error (saved_errno))
	    {
	      *permission_error = true;
	    }
	  ::close (fd);
	  errno = saved_errno;
	  return false;
	}

      channel.cmd = ETHTOOL_SCHANNELS;
      channel.combined_count = combined;
      success = ioctl (fd, SIOCETHTOOL, &ifr);
      if (success)
	{
	  saved_errno = errno;
	  if (permission_error && is_permission_error (saved_errno))
	    {
	      *permission_error = true;
	    }
	  ::close (fd);
	  errno = saved_errno;
	  return false;
	}

      ::close (fd);
      return true;
    }

    void map_nic_to_index (std::vector<std::size_t> &index)
    {
      cubbase::ifsys::qirq_vec qs = { 0, 0, 0 };
      char qbase[256], rxdir[280], txdir[280];
      bool permission_guide_logged;
      bool permission_error;
      int rx_count, tx_count;
      std::string ifname;
      int saved_errno;
      int i, q;

      permission_guide_logged = false;

      /* get ifname */
      ifname = cubbase::ifsys::auto_select_primary_iface ();
      if (ifname.empty ())
	{
	  _er_log_debug (__FILE__, __LINE__, "warning: no interfaces available for selection. (virtual environment)\n");
	  return ;
	}

      /* channel */
      permission_error = false;
      if (!set_nic_channels (ifname, index.size (), &permission_error))
	{
	  _er_log_debug (__FILE__, __LINE__, "warning: NIC channel configuration failed: %s\n\n", strerror (errno));
	  if (permission_error)
	    {
	      guide_hardware_affinity_permission ();
	      permission_guide_logged = true;
	    }
	}
      /* wait until applied */
      usleep (1000 * 1000);

      if (cubbase::ifsys::find_irqs_for_iface (ifname.c_str (), &qs) == 0 && qs.n > 0)
	{
	  for (i = 0; i < qs.n; i++)
	    {
	      q = qs.v[i].q;
	      if (cubbase::ifsys::set_irq_affinity_list (qs.v[i].irq, index[q % index.size ()]) != 0)
		{
		  saved_errno = errno;
		  _er_log_debug (__FILE__, __LINE__, "warning: failed to set IRQ affinity for IRQ %d: %s\n", qs.v[i].irq,
				 strerror (saved_errno));
		  if (!permission_guide_logged && is_permission_error (saved_errno))
		    {
		      guide_hardware_affinity_permission ();
		      permission_guide_logged = true;
		    }
		}
	    }
	}
      else
	{
	  _er_log_debug (__FILE__, __LINE__, "warning: no IRQ found for %s in /proc/interrupts.\n\n", ifname.c_str ());
	}
      free (qs.v);

      /* RPS/XPS */
      snprintf (qbase, sizeof (qbase), "/sys/class/net/%s/queues", ifname.c_str ());
      snprintf (rxdir, sizeof (rxdir), "%s/rx-", qbase);
      snprintf (txdir, sizeof (txdir), "%s/tx-", qbase);
      rx_count = cubbase::ifsys::listdir_count_prefix (qbase, "rx-");
      tx_count = cubbase::ifsys::listdir_count_prefix (qbase, "tx-");

      if (!cubbase::ifsys::maybe_set_rps_sock_flow_entries (index.size ()))
	{
	  saved_errno = errno;
	  _er_log_debug (__FILE__, __LINE__, "warning: failed to set rps_sock_flow_entries: %s\n",
			 strerror (saved_errno));
	  if (!permission_guide_logged && is_permission_error (saved_errno))
	    {
	      guide_hardware_affinity_permission ();
	      permission_guide_logged = true;
	    }
	}

      for (q = 0; q < rx_count; q++)
	{
	  if (!cubbase::ifsys::set_rps_for_queue (ifname.c_str (), q, index[q % index.size ()]))
	    {
	      saved_errno = errno;
	      _er_log_debug (__FILE__, __LINE__, "warning: failed to set RPS for %s rx-%d: %s\n", ifname.c_str (), q,
			     strerror (saved_errno));
	      if (!permission_guide_logged && is_permission_error (saved_errno))
		{
		  guide_hardware_affinity_permission ();
		  permission_guide_logged = true;
		}
	    }
	}
      for (q = 0; q < tx_count; q++)
	{
	  if (!cubbase::ifsys::set_xps_for_queue (ifname.c_str (), q, index[q % index.size ()]))
	    {
	      saved_errno = errno;
	      _er_log_debug (__FILE__, __LINE__, "warning: failed to set XPS for %s tx-%d: %s\n", ifname.c_str (), q,
			     strerror (saved_errno));
	      if (!permission_guide_logged && is_permission_error (saved_errno))
		{
		  guide_hardware_affinity_permission ();
		  permission_guide_logged = true;
		}
	    }
	}
    }
  }
}
