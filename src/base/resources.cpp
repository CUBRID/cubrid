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

#include <thread>
#include <fstream>
#include <sched.h>
#include <unistd.h>

#include "filesys_parser.hpp"
#include "resources.hpp"
#include "cgroup.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace os::resources
{
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

	return ctx;
      } ();

      return ctx;
    }
  }
}

