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

#ifndef _BASE_RESOURCES_HPP_
#define _BASE_RESOURCES_HPP_

#include <set>
#include <optional>

#define CONST_PATH(name, path) \
  inline constexpr const char *name = path

namespace os::resources
{
  namespace path
  {
    CONST_PATH (cpu_online, "/sys/devices/system/cpu/online");
  }

  namespace cpu
  {
    int sysconf_nprocessors ();
    std::optional<std::set<std::size_t>> affinity_cpuset ();
    std::optional<std::set<std::size_t>> online_cpuset ();

    /* cpuset.cpus.effective */
    std::optional<std::set<std::size_t>> effective ();
  }
}

#endif
