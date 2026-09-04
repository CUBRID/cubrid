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

  std::optional<std::filesystem::path> mountpoint_v2 (std::filesystem::path *root = nullptr);
  std::optional<std::filesystem::path> relative_v2 ();

  std::optional<std::filesystem::path> mountpoint_v1 (const std::string &controller,
      std::filesystem::path *root = nullptr);
  std::optional<std::filesystem::path> relative_v1 (const std::string &controller);

  namespace cpu
  {
    struct context
    {
      context () :
	max (std::nullopt),
	effective (std::nullopt)
      {
      }

      /* cpu bandwidth limit (in cores) and effective cpuset. filled from cgroup v2 or v1 */
      std::optional<double> max;
      std::optional<std::set<std::size_t>> effective;
    };

    /* cgroup v2 */
    std::optional<double> max_v2 (std::filesystem::path path);
    std::optional<std::set<std::size_t>> effective_v2 (std::filesystem::path path);

    context quota_v2 ();

    /* cgroup v1 */
    std::optional<double> max_v1 (std::filesystem::path path);
    std::optional<std::set<std::size_t>> effective_v1 (std::filesystem::path path);

    context quota_v1 ();

    /* dispatch: prefer cgroup v2 (unified), fall back to v1 (legacy / hybrid) */
    context quota ();
  }
}

#endif
