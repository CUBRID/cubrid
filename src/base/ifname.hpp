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
 * ifname.hpp
 */

#ifndef _IFNAME_HPP_
#define _IFNAME_HPP_

#ident "$Id$"
#include "assert.h"

#include <vector>
#include <string>
#include <hwloc.h>

namespace cubbase
{
  class ifname
  {
    public:
      static std::string auto_select_primary_iface ();

    private:
      static bool file_exists (const std::string &path);
      static std::string read_one_line (const std::string &path);
      static unsigned long long read_u64 (const std::string &path);
      static std::vector<std::string> list_entries (const std::string &path);
      static std::vector<std::string> list_dirs_with_prefix (const std::string &path, const std::string &prefix);

      static bool name_blacklisted (const std::string &ifname);
      static bool is_physical_iface (const std::string &ifname);
      static bool is_up (const std::string &ifname);
      static int rx_queue_count (const std::string &ifname);
      static unsigned long long traffic_score (const std::string &ifname);
  };
}

#endif
