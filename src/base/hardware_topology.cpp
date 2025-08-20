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
 * hardware_topology.cpp
 */

#include "ifname.hpp"
#include "hardware_topology.hpp"
#include "error_manager.h"

#include <hwloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubbase
{
  hardware_topology topology;

  hardware_topology::hardware_topology ()
  {
    hwloc_topology_init (&m_topology);
    hwloc_topology_load (m_topology);

    this->load_cpu ();
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
      perror ("pthread_setaffinity_np");
    }
  }

  std::vector<std::vector<int>> &hardware_topology::get_cores ()
  {
    return m_cores;
  }

  void hardware_topology::load_cpu ()
  {
    hwloc_const_cpuset_t online;
    hwloc_obj_t core;
    std::vector<int> pus;
    hwloc_obj_t pu;
    int ncores, npus;
    int i, j;

    online = hwloc_topology_get_allowed_cpuset (m_topology);
    ncores = hwloc_get_nbobjs_by_type (m_topology, HWLOC_OBJ_CORE);

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
  }
}

