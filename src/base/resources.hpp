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
 * resources.hpp - get machine resource information.
 */

#ifndef _BASE_RESOURCES_HPP_
#define _BASE_RESOURCES_HPP_

#include <set>
#include <string>
#include <vector>
#include <limits>
#include <optional>
#include <sched.h>

namespace os::resources
{
  namespace path
  {
    inline constexpr const char *cpu_online = "/sys/devices/system/cpu/online";
  }

  void initialize ();
  std::optional<std::string> execute_command (const char *cmd);

  namespace cpu
  {
    struct context
    {
      context () :
	affinity { nullptr, 0 },
	max (std::numeric_limits<double>::max ()),
	effective (std::nullopt),
	adjusted_max (0),
	adjusted_effective (std::nullopt)
      {
      }

      ~context ()
      {
	if (affinity.bitmap)
	  {
	    CPU_FREE (affinity.bitmap);
	    affinity.bitmap = nullptr;
	    affinity.size = 0;
	  }
      }

      context (const context &) = delete;
      context &operator= (const context &) = delete;

      context (context &&other) :
	affinity { other.affinity.bitmap, other.affinity.size },
	max (other.max),
	effective (std::move (other.effective)),
	adjusted_max (other.adjusted_max),
	adjusted_effective (std::move (other.adjusted_effective))
      {
	other.affinity.bitmap = nullptr;
	other.affinity.size = 0;
      }

      context &operator= (context &&other)
      {
	if (this != &other)
	  {
	    if (affinity.bitmap)
	      {
		CPU_FREE (affinity.bitmap);
	      }
	    affinity = { other.affinity.bitmap, other.affinity.size };
	    max = other.max;
	    effective = std::move (other.effective);
	    adjusted_max = other.adjusted_max;
	    adjusted_effective = std::move (other.adjusted_effective);

	    other.affinity.bitmap = nullptr;
	    other.affinity.size = 0;
	  }
	return *this;
      }

      struct
      {
	cpu_set_t *bitmap;
	std::size_t size;
      } affinity;

      double max;
      std::optional<std::set<std::size_t>> effective;

      std::size_t adjusted_max;
      std::optional<std::vector<std::size_t>> adjusted_effective;
    };

    std::optional<std::tuple<std::set<std::size_t>, cpu_set_t *, std::size_t>> affinity_cpuset ();
    std::optional<std::set<std::size_t>> online_cpuset ();

    void setaffinity (std::size_t core);
    void clearaffinity ();

    context &effective ();
  }

  namespace net
  {
    bool set_nic_channels (std::string &ifname, unsigned int combined);
    void map_nic_to_index (std::vector<std::size_t> &index);
  }
}

#endif
