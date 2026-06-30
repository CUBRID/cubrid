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
  std::optional<std::filesystem::path> mountpoint_v2 (std::filesystem::path *root)
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
	    if (root)
	      {
		*root = vec[3];
	      }
	    return vec[4];
	  }
	return std::nullopt;
      }

    return std::nullopt;
  }

  std::optional<std::filesystem::path> relative_v2 ()
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

  std::optional<std::filesystem::path> mountpoint_v1 (const std::string &controller, std::filesystem::path *root)
  {
    std::ifstream file (path::proc_mountinfo);
    std::size_t separator;
    std::string line;

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

	/* fields after " - ": <fstype> <mount source> <super options> */
	auto post = parser::string_to_vector (line.substr (separator + 3), ' ');
	if (post.size () < 3)
	  {
	    continue;
	  }

	if (post[0].compare ("cgroup"))
	  {
	    continue;
	  }

	auto options = parser::string_to_vector (post[post.size () - 1], ',');
	bool found = false;
	for (const auto &option : options)
	  {
	    if (!option.compare (controller))
	      {
		found = true;
		break;
	      }
	  }
	if (!found)
	  {
	    continue;
	  }

	auto vec = parser::string_to_vector (line.substr (0, separator), ' ');
	if (vec.size () > 4)
	  {
	    if (root)
	      {
		*root = vec[3];
	      }
	    return vec[4];
	  }
	return std::nullopt;
      }

    return std::nullopt;
  }

  namespace
  {
    std::filesystem::path remove_mount_root (const std::filesystem::path &relative, const std::filesystem::path &root)
    {
      std::filesystem::path normalized_relative = relative.relative_path ().lexically_normal ();
      std::filesystem::path normalized_root = root.relative_path ().lexically_normal ();
      std::filesystem::path result;
      auto relative_it = normalized_relative.begin ();
      auto root_it = normalized_root.begin ();

      for (; relative_it != normalized_relative.end () && root_it != normalized_root.end (); ++relative_it, ++root_it)
	{
	  if (*relative_it != *root_it)
	    {
	      return normalized_relative;
	    }
	}

      if (root_it != normalized_root.end ())
	{
	  return normalized_relative;
	}

      for (; relative_it != normalized_relative.end (); ++relative_it)
	{
	  result /= *relative_it;
	}

      return result;
    }

    std::filesystem::path make_path (const std::filesystem::path &mountpoint,
				     const std::optional<std::filesystem::path> &relative, const std::filesystem::path &root)
    {
      std::filesystem::path path;

      if (!relative)
	{
	  return mountpoint;
	}

      path = remove_mount_root (*relative, root);
      if (path.empty ())
	{
	  return mountpoint;
	}

      return mountpoint / path;
    }
  }

  std::optional<std::filesystem::path> relative_v1 (const std::string &controller)
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
	/* format: <hierarchy-id>:<controller-list>:<cgroup-path> */
	auto vec = parser::string_to_vector (line, ':');
	if (vec.size () <= 2)
	  {
	    continue;
	  }

	auto controllers = parser::string_to_vector (vec[1], ',');
	for (const auto &name : controllers)
	  {
	    if (!name.compare (controller))
	      {
		return vec[2];
	      }
	  }
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
      std::filesystem::path path, root;
      bool flag = true;
      context ctx;

      mountpoint = cgroup::mountpoint_v2 (&root);
      if (!mountpoint)
	{
	  return ctx;
	}
      relative = cgroup::relative_v2 ();
      path = make_path (*mountpoint, relative, root);

      while (path != mountpoint || flag)
	{
	  max = max_v2 (path);
	  if (max)
	    {
	      if (ctx.max)
		{
		  if (*ctx.max > *max)
		    {
		      ctx.max = *max;
		    }
		}
	      else
		{
		  ctx.max = *max;
		}
	    }

	  effective = effective_v2 (path);
	  if (effective && !effective->empty ())
	    {
	      if (ctx.effective && !ctx.effective->empty ())
		{
		  ctx.effective = parser::intersection (*ctx.effective, *effective);
		}
	      else
		{
		  ctx.effective = std::move (effective);
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

    std::optional<double> max_v1 (std::filesystem::path path)
    {
      std::ifstream quota_file (path / "cpu.cfs_quota_us");
      std::ifstream period_file (path / "cpu.cfs_period_us");
      double quota = 0, period = 0;

      if (!quota_file || !period_file)
	{
	  return std::nullopt;
	}

      if (! (quota_file >> quota))
	{
	  return std::nullopt;
	}
      if (! (period_file >> period) || period <= 0)
	{
	  return std::nullopt;
	}

      if (quota < 0)
	{
	  /* a quota of -1 means "no limit" in cgroup v1 */
	  return std::numeric_limits<double>::max ();
	}
      if (quota > 0)
	{
	  return quota / period;
	}
      return std::nullopt;
    }

    std::optional<std::set<std::size_t>> effective_v1 (std::filesystem::path path)
    {
      auto read_cpuset_file = [&path] (const std::string &filename) -> std::optional<std::string>
      {
	std::ifstream file (path / filename);
	std::string line;

	if (!file)
	  {
	    return std::nullopt;
	  }

	file >> line;
	return line.empty () ? std::nullopt : std::make_optional (line);
      };

      auto line = read_cpuset_file ("cpuset.effective_cpus");
      if (!line)
	{
	  line = read_cpuset_file ("cpuset.cpus");
	}
      if (!line)
	{
	  return std::nullopt;
	}

      return parser::range_set_to_set<std::size_t> (*line);
    }

    context quota_v1 ()
    {
      std::optional<std::filesystem::path> mountpoint, relative;
      std::optional<std::set<std::size_t>> effective;
      std::optional<double> max;
      std::filesystem::path path, root;
      bool flag = true;
      context ctx;

      /* cpu controller: CFS bandwidth limit (cpu.cfs_quota_us / cpu.cfs_period_us) */
      mountpoint = mountpoint_v1 ("cpu", &root);
      if (mountpoint)
	{
	  relative = relative_v1 ("cpu");
	  path = make_path (*mountpoint, relative, root);

	  flag = true;
	  while (path != mountpoint || flag)
	    {
	      max = max_v1 (path);
	      if (max)
		{
		  if (ctx.max)
		    {
		      if (*ctx.max > *max)
			{
			  ctx.max = *max;
			}
		    }
		  else
		    {
		      ctx.max = *max;
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
	}

      /* cpuset controller: effective cpus (cpuset.effective_cpus, or cpuset.cpus on older v1) */
      mountpoint = mountpoint_v1 ("cpuset", &root);
      if (mountpoint)
	{
	  relative = relative_v1 ("cpuset");
	  path = make_path (*mountpoint, relative, root);

	  flag = true;
	  while (path != mountpoint || flag)
	    {
	      effective = effective_v1 (path);
	      if (effective && !effective->empty ())
		{
		  if (ctx.effective && !ctx.effective->empty ())
		    {
		      ctx.effective = parser::intersection (*ctx.effective, *effective);
		    }
		  else
		    {
		      ctx.effective = std::move (effective);
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
	}

      return ctx;
    }

    context quota ()
    {
      /* prefer cgroup v2 (unified hierarchy); fall back to v1 (legacy / hybrid setups) */
      context ctx = quota_v2 ();
      if (ctx.max && ctx.effective)
	{
	  return ctx;
	}

      context v1 = quota_v1 ();
      if (!ctx.max)
	{
	  ctx.max = v1.max;
	}
      if (!ctx.effective)
	{
	  ctx.effective = std::move (v1.effective);
	}
      return ctx;
    }
  }
}
