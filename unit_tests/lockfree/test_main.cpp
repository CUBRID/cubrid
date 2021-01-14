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
