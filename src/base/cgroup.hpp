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
 * cgroup.hpp - get information about cgroup
 */

#ifndef _CGROUP_HPP_
#define _CGROUP_HPP_

#include <set>
#include <optional>
#include <filesystem>

namespace os::cgroup
{
  namespace path
  {
    inline constexpr const char *proc_mountinfo = "/proc/self/mountinfo";
    inline constexpr const char *proc_cgroup = "/proc/self/cgroup";
  }

  std::optional<std::filesystem::path> mountpoint ();
  std::optional<std::filesystem::path> relative ();

  namespace cpu
  {
    struct context
    {
      context () :
	max_v2 (std::nullopt),
	effective_v2 (std::nullopt)
      {
      }

      std::optional<double> max_v2;
      std::optional<std::set<std::size_t>> effective_v2;
    };

    /* cgroup v2 */
    std::optional<double> max_v2 (std::filesystem::path path);
    std::optional<std::set<std::size_t>> effective_v2 (std::filesystem::path path);

    context quota_v2 ();
  }
}

#endif
