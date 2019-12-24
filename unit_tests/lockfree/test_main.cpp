/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include "test_cqueue_functional.hpp"
#include "test_freelist_functional.hpp"
#include "test_hashmap.hpp"

#include <string>
#include <vector>

int
main (int argc, char **argv)
{
  size_t opt = 0;
  std::vector<std::string> option_map =
  {
    "all",
    "cqueue",
    "freelist",
    "hashmap"
  };
  if (argc >= 2)
    {
      for (size_t i = 0; i < option_map.size (); i++)
	{
	  if (option_map[i] == argv[1])
	    {
	      opt = i;
	    }
	}
    }
  int err = 0;
  if (opt == 0 || opt == 1)
    {
      err = err | test_lockfree::test_cqueue_functional ();
    }
  if (opt == 0 || opt == 2)
    {
      err = err | test_lockfree::test_freelist_functional ();
    }
  if (opt == 0 || opt == 3)
    {
      std::vector<std::string> suboption_map =
      {
	"functional",
	"performance",
	"short"
      };
      bool do_functional = (opt == 0) || (argc == 2);
      bool do_performance = (opt == 0) || (argc == 2);
      bool short_functional_version = false;
      if (opt == 3 && argc >= 3)
	{
	  if (suboption_map[0] == argv[2])
	    {
	      do_functional = true;
	    }
	  else if (suboption_map[1] == argv[2])
	    {
	      do_performance = true;
	    }
	  else if (suboption_map[2] == argv[2])
	    {
	      do_functional = true;
	      short_functional_version = true;
	    }
	}
      if (do_functional)
	{
	  err = err | test_lockfree::test_hashmap_functional (short_functional_version);
	}
      if (do_performance)
	{
	  err = err | test_lockfree::test_hashmap_performance ();
	}
    }

  return err;
}
