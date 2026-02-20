/*
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
 * resources.hpp - get machine resource information.
 */

#include <fstream>
#include <sched.h>
#include <unistd.h>

#include "resources.hpp"
#include "parser.hpp"
#include "error_manager.h"
#include "system_parameter.h"

namespace os::resources
{
  namespace cpu
  {
    int sysconf_nprocessors ()
    {
      long val;

      val = ::sysconf (_SC_NPROCESSORS_ONLN);
      if (val <= 0)
	{
	  return 0;
	}
      return static_cast<int> (val);
    }

    std::optional<std::set<std::size_t>> affinity_cpuset ()
    {
      std::set<std::size_t> cpuset;
      cpu_set_t *bitmap;
      std::size_t size, bytes;
      std::size_t i, j;

      size = 1024;
      cpuset.clear ();

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

	      _er_log_debug (ARG_FILE_LINE, "failed to sched_getaffinity: %s\n", strerror (errno));
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

      _er_log_debug (ARG_FILE_LINE, "failed to create cpuset: number of cores exceeds 2^18.\n");
      return std::nullopt;
    }

    std::optional<std::set<std::size_t>> online_cpuset ()
    {
      std::ifstream file (path::cpu_online);
      std::set<std::size_t> cpuset;
      std::string_view line, item;
      std::string data;
      std::size_t pos, end;

      if (!file)
	{
	  _er_log_debug (ARG_FILE_LINE, "failed to open %s: %s\n", path::cpu_online, strerror (errno));
	  return std::nullopt;
	}
      cpuset.clear ();

      file >> data;
      if (data.empty ())
	{
	  _er_log_debug (ARG_FILE_LINE, "the file %s is empty.\n", path::cpu_online);
	  return std::nullopt;
	}

      pos = 0;
      line = data;
      while ((end = line.find (',', pos)) != std::string::npos)
	{
	  item = line.substr (pos, end - pos);
	  pos = end + 1;

	  cpuset.merge (parser::range_to_set<std::size_t> (item));
	}
      item = line.substr (pos);
      cpuset.merge (parser::range_to_set<std::size_t> (item));

      return cpuset;
    }

    std::optional<std::set<std::size_t>> effective ()
    {
      std::optional<std::set<std::size_t>> affinity, online;
      int nprocessors;

      nprocessors = sysconf_nprocessors ();
      affinity = affinity_cpuset ();
      online = online_cpuset ();
    }
  }
}

