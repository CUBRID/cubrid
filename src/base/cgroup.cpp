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
 * cgroup.cpp - get information about cgroup
 */

#include <fstream>
#include <limits>

#include "filesys_parser.hpp"
#include "cgroup.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace os::cgroup
{
  std::optional<std::filesystem::path> mountpoint ()
  {
    std::ifstream file (path::proc_mountinfo);
    std::size_t separator, whitespace;
    std::string line, name, path;

    if (!file)
      {
	/* _er_log_debug (ARG_FILE_LINE, "failed to open %s: %s\n", path::proc_mountinfo, strerror (errno)); */
	return std::nullopt;
      }

    while (std::getline (file, line))
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

	name = line.substr (separator + 3, whitespace - (separator + 3));
	if (name.empty ())
	  {
	    continue;
	  }

	if (name.compare ("cgroup2"))
	  {
	    continue;
	  }

	auto vec = parser::string_to_vector (line.substr (0, separator), ' ');
	if (vec.size () > 4)
	  {
	    return vec[4];
	  }
	return std::nullopt;
      }

    return std::nullopt;
  }

  std::optional<std::filesystem::path> relative ()
  {
    std::ifstream file (path::proc_cgroup);
    std::string line;

    if (!file)
      {
	/* _er_log_debug (ARG_FILE_LINE, "failed to open %s: %s\n", path::proc_cgroup, strerror (errno)); */
	return std::nullopt;
      }

    while (std::getline (file, line))
      {
	auto vec = parser::string_to_vector (line, ':');
	if (vec.size () <= 2)
	  {
	    continue;
	  }
	if (vec[0].compare ("0"))
	  {
	    continue;
	  }
	return vec[2];
      }

    return std::nullopt;
  }

  namespace cpu
  {
    std::optional<double> max_v2 (std::filesystem::path path)
    {
      std::ifstream file (path / "cpu.max");
      std::string first;
      double quota, period = 0;

      if (!file)
	{
	  return std::nullopt;
	}

      if (! (file >> first >> period) || period <= 0)
	{
	  return std::nullopt;
	}

      if (!first.compare ("max"))
	{
	  return std::numeric_limits<double>::max ();
	}

      quota = std::atof (first.c_str ());
      if (quota > 0)
	{
	  return quota / period;
	}
      return std::nullopt;
    }

    std::optional<std::set<std::size_t>> effective_v2 (std::filesystem::path path)
    {
      std::ifstream file (path / "cpuset.cpus.effective");
      std::string line;

      if (!file)
	{
	  return std::nullopt;
	}

      file >> line;
      if (line.empty ())
	{
	  /* _er_log_debug (ARG_FILE_LINE, "the file %s is empty.\n", path.c_str ()); */
	  return std::nullopt;
	}
      return parser::range_set_to_set<std::size_t> (line);
    }

    context quota_v2 ()
    {
      std::optional<std::filesystem::path> mountpoint, relative;
      std::optional<std::set<std::size_t>> effective;
      std::optional<double> max;
      std::filesystem::path path;
      bool flag = true;
      context ctx;

      mountpoint = cgroup::mountpoint ();
      if (!mountpoint)
	{
	  return ctx;
	}
      relative = cgroup::relative ();
      if (mountpoint && relative)
	{
	  path = *mountpoint / relative->relative_path ().lexically_normal ();
	}
      else
	{
	  path = *mountpoint;
	}

      while (path != mountpoint || flag)
	{
	  max = max_v2 (path);
	  if (max)
	    {
	      if (ctx.max_v2)
		{
		  if (*ctx.max_v2 > *max)
		    {
		      ctx.max_v2 = *max;
		    }
		}
	      else
		{
		  ctx.max_v2 = *max;
		}
	    }

	  effective = effective_v2 (path);
	  if (effective)
	    {
	      if (ctx.effective_v2 && (!effective->empty () && !ctx.effective_v2->empty ()))
		{
		  ctx.effective_v2 = parser::intersection (*ctx.effective_v2, *effective);
		}
	      else
		{
		  ctx.effective_v2 = std::move (effective);
		}
	    }

	  if (path != mountpoint)
	    {
	      path = path.parent_path ();
	    }
	  else
	    {
	      flag = false;
	    }
	}

      return ctx;
    }
  }
}

