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
 * cgroup.cpp - get information about cgroup
 */

#include <fstream>

#include "cgroup.hpp"
#include "error_manager.h"

namespace os::cgroup
{
  std::string mountpoint ()
  {
    std::ifstream mountinfo (path::proc_mountinfo);
    std::size_t separator, whitespace;
    std::string line, name, path;

    if (!mountinfo.is_open ())
      {
	_er_log_debug (ARG_FILE_LINE, "failed to open %s: %s\n", path::proc_mountinfo, strerror (errno));
	return "";
      }

    while (std::getline (mountinfo, line))
      {
	separator = line.find (" - ");
	if (separator == std::string::npos)
	  {
	    continue;
	  }
	whitespace = line.find (' ', separator + 3);
	if (whitespace == std::string::npos)
	  {
	    continue;
	  }
	name = line.substr (separator + 3, whitespace);
	if (name.empty ())
	  {
	    continue;
	  }

	if (name.compare ("cgroup2"))
	  {
	    continue;
	  }

      }
  }

  namespace cpu
  {
    cache::cache ()
    {
    };

    cache::~cache ()
    {
    };

    double max ()
    {
      return 0;
    }

    std::optional<std::set<std::size_t>> effective ()
    {
      return std::nullopt;
    }
  }
}

