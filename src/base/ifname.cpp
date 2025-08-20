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
 * ifname.cpp
 */

#include "ifname.hpp"

#include <vector>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <dirent.h>
#include <sys/stat.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubbase
{
  bool ifname::file_exists (const std::string &path)
  {
    struct stat st;

    return ::stat (path.c_str (), &st) == 0;
  }

  std::string ifname::read_one_line (const std::string &path)
  {
    std::ifstream file (path);
    std::string s;

    if (file.good ())
    {
      getline (file, s);
    }
    return s;
  }

  unsigned long long ifname::read_u64 (const std::string &path)
  {
    std::ifstream file (path);
    unsigned long long v;

    v = 0;
    if (file.good ())
    {
      file >> v;
    }
    return v;
  }

  std::vector<std::string> ifname::list_entries (const std::string &path)
  {
    std::vector<std::string> out;
    struct dirent *e;
    DIR* dir;
    std::string name;

    if ((dir = opendir (path.c_str ())))
    {
      while ((e = readdir (dir)))
      {
	name = e->d_name;
	if (name == "." || name == "..")
	{
	  continue;
	}
	out.push_back (name);
      }
      closedir (dir);
    }
    sort (out.begin (), out.end ());

    return out;
  }

  std::vector<std::string> ifname::list_dirs_with_prefix (const std::string &path, const std::string &prefix)
  {
    std::vector<std::string> out;
    struct dirent *e;
    DIR* dir;
    std::string name;

    if ((dir = opendir (path.c_str ())))
    {
      while ((e = readdir (dir)))
      {
	if (e->d_type != DT_DIR && e->d_type != DT_LNK)
	{
	  continue;
	}
	name = e->d_name;
	if (name == "." || name == "..")
	{
	  continue;
	}
	if (!prefix.empty () && name.rfind (prefix, 0) != 0)
	{
	  continue;
	}
	out.push_back (name);
      }
      closedir (dir);
    }
    sort (out.begin (), out.end ());

    return out;
  }

  bool ifname::name_blacklisted (const std::string &ifname)
  {
    return ifname == "lo" ||
	   ifname.rfind ("docker",0) == 0 ||
	   ifname.rfind ("veth",0) == 0 ||
	   ifname.rfind ("br-",0) == 0 ||
	   ifname.rfind ("virbr",0) == 0 ||
	   ifname.rfind ("tun",0) == 0 ||
	   ifname.rfind ("tap",0) == 0 ||
	   ifname.rfind ("wg",0) == 0 ||
	   ifname.rfind ("podman",0) == 0 ||
	   ifname.rfind ("cni",0) == 0 ||
	   ifname.rfind ("flannel",0) == 0 ||
	   ifname.rfind ("cilium",0) == 0;
  }

  bool ifname::is_physical_iface (const std::string &ifname)
  {
    return file_exists ("/sys/class/net/" + ifname + "/device");
  }

  bool ifname::is_up (const std::string &ifname)
  {
    std::string s;

    s = read_one_line ("/sys/class/net/" + ifname + "/operstate");
    return (s == "up");
  }

  int ifname::rx_queue_count (const std::string &ifname)
  {
    return list_dirs_with_prefix ("/sys/class/net/" + ifname + "/queues", "rx-").size ();
  }

  unsigned long long ifname::traffic_score (const std::string &ifname)
  {
    unsigned long long rx, tx;

    rx = read_u64 ("/sys/class/net/" + ifname + "/statistics/rx_packets");
    tx = read_u64 ("/sys/class/net/" + ifname + "/statistics/tx_packets");
    return rx + tx;
  }

  std::string ifname::auto_select_primary_iface ()
  {
    std::vector<std::string> ifs = list_entries ("/sys/class/net");
    unsigned long long best_score = 0;
    unsigned long long score;
    std::string best;

    for (auto &ifname : ifs)
    {
      if (name_blacklisted (ifname))
      {
	continue;
      }
      if (!is_physical_iface (ifname))
      {
	continue;
      }
      if (!is_up (ifname))
      {
	continue;
      }
      if (rx_queue_count (ifname) == 0)
      {
	continue;
      }

      /* TODO: use multiple NIC */
      score = traffic_score (ifname);
      if (score > best_score)
      {
	best = ifname;
	best_score = score;
      }
    }

    if (best.empty())
    {
      for (auto& ifname : ifs)
      {
	if (name_blacklisted (ifname))
	{
	  continue;
	}
	if (!is_physical_iface (ifname))
	{
	  continue;
	}
	if (!is_up (ifname))
	{
	  continue;
	}
	best = ifname;
	break;
      }
    }

    return best;
  }
}

