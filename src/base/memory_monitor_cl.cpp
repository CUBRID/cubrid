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
 * memory_monitor_cl.cpp - definitions of client functions
 *                         for memory monitoring module
 */

#include <stdio.h>
#include <cstring>
#include <string>
#include <cassert>

#include "memory_monitor_cl.hpp"

/*
 * mmon_convert_module_name_to_index -
 *
 * return: module index
 *
 *   module_name(in):
 */
int
mmon_convert_module_name_to_index (const char *module_name)
{
  for (int i = 0; i < MMON_MODULE_LAST; i++)
    {
      if (!strcmp (module_name, module_names[i]))
	{
	  return i;
	}
    }

  /* for code protection. unreachable */
  assert (false && "module index matching failed");
}

/*
 * mmon_print_server_info -
 *
 * return:
 *
 *   server_info(in/out):
 */
void
mmon_print_server_info (MMON_SERVER_INFO &server_info)
{
  fprintf (stdout, "====================cubrid memmon====================\n");
  fprintf (stdout, "Server Name: %s\n", server_info.name);
  fprintf (stdout, "Total Memory Usage(KB): %lu\n\n", MMON_CONVERT_TO_KB_SIZE (server_info.total_mem_usage));
  fprintf (stdout, "-----------------------------------------------------\n");
}

/*
 * mmon_print_module_info -
 *
 * return:
 *
 *   module_info(in):
 */
void
mmon_print_module_info (std::vector<MMON_MODULE_INFO> &module_info)
{
  const auto init_size_str = std::string {"Initial Size(KB)"};
  const auto cur_size_str = std::string {"Current Size(KB)"};
  const auto peak_size_str = std::string {"Peak Size(KB)"};
  const auto resize_expand_str = std::string {"Resize Expand Count"};

  for (const auto &m_info : module_info)
    {
      // TODO: It's a temporary measure for hiding MMON_OTHERS
      //       It will be deleted when all other modules in CUBRID are registered in memory_monitor.
      if (!strcmp (m_info.name, "OTHERS"))
	{
	  continue;
	}
      fprintf (stdout, "Module Name: %s\n\n", m_info.name);
      fprintf (stdout, "%-19s\t: %17lu\n", init_size_str.c_str (), MMON_CONVERT_TO_KB_SIZE (m_info.stat.init_stat));
      fprintf (stdout, "%-19s\t: %17lu\n", cur_size_str.c_str (), MMON_CONVERT_TO_KB_SIZE (m_info.stat.cur_stat));
      fprintf (stdout, "%-19s\t: %17lu\n", peak_size_str.c_str (), MMON_CONVERT_TO_KB_SIZE (m_info.stat.peak_stat));
      fprintf (stdout, "%-19s\t: %17u\n\n", resize_expand_str.c_str (), m_info.stat.expand_resize_count);

      for (const auto &comp_info : m_info.comp_info)
	{
	  fprintf (stdout, "%s\n", comp_info.name);
	  fprintf (stdout, "\t%-23s\t: %17lu\n", init_size_str.c_str (), MMON_CONVERT_TO_KB_SIZE (comp_info.stat.init_stat));
	  fprintf (stdout, "\t%-23s\t: %17lu\n", cur_size_str.c_str (), MMON_CONVERT_TO_KB_SIZE (comp_info.stat.cur_stat));

	  for (const auto &subcomp_info : comp_info.subcomp_info)
	    {
	      auto out_name = std::string {subcomp_info.name};
	      out_name += "(KB)";
	      fprintf (stdout, "\t  %-20s\t: %17lu\n", out_name.c_str (), MMON_CONVERT_TO_KB_SIZE (subcomp_info.cur_stat));
	    }
	  fprintf (stdout, "\t%-23s\t: %17lu\n", peak_size_str.c_str (), MMON_CONVERT_TO_KB_SIZE (comp_info.stat.peak_stat));
	  fprintf (stdout, "\t%-23s\t: %17u\n\n", resize_expand_str.c_str (), comp_info.stat.expand_resize_count);
	}
      fprintf (stdout, "\n-----------------------------------------------------\n\n");
    }
}

/*
 * mmon_print_module_info_summary -
 *
 * return:
 *
 *   module_info(in):
 */
void
mmon_print_module_info_summary (uint64_t server_mem_usage, std::vector<MMON_MODULE_INFO> &module_info)
{
  double cur_stat_ratio = 0.0;

  fprintf (stdout, "Per Module Memory Usage (KB)\n");
  fprintf (stdout, "\tModule\t\tModule\t\t\tModule\n");
  fprintf (stdout, "\tIndex\t\tName\t\t\tUsage(%%)\n");

  for (const auto &m_info : module_info)
    {
      // TODO: It's a temporary measure for hiding MMON_OTHERS
      //       It will be deleted when all other modules in CUBRID are registered in memory_monitor.
      if (!strcmp (m_info.name, "OTHERS"))
	{
	  continue;
	}
      if (server_mem_usage != 0)
	{
	  cur_stat_ratio = m_info.stat.cur_stat / (double) server_mem_usage;
	  cur_stat_ratio *= 100;
	}

      fprintf (stdout, "\t%5d\t%-20s\t: %17lu(%3d%%)\n", mmon_convert_module_name_to_index (m_info.name),
	       m_info.name, MMON_CONVERT_TO_KB_SIZE (m_info.stat.cur_stat), (int)cur_stat_ratio);
    }
  fprintf (stdout, "\n-----------------------------------------------------\n\n");
}

/*
 * mmon_print_tran_info -
 *
 * return:
 *
 *   tran_info(in):
 */
void
mmon_print_tran_info (MMON_TRAN_INFO &tran_info)
{
  fprintf (stdout, "Transaction Memory Usage Info (KB)\n");
  fprintf (stdout, "\t%14s | %12s\n", "Transaction ID", "Memory Usage");

  for (const auto &t_stat : tran_info.tran_stat)
    {
      // There can be some cases that some transactions' cur_stat can have minus value
      // because they can not allocate memory but free memory.
      // e.g.
      // 1. A thread get object from pool but the pool is full because other threads return object to pool
      //    when the thread return its object to pool. In this case the thread have to free the object.
      // 2. A thread initialize cache(just alloc) and another therad finalize cache(just free).
      // ...
      // So in this case, we just show 0 value to user.
      if (t_stat.cur_stat >= 0)
	{
	  fprintf (stdout, "\t%14d | %17lu\n", t_stat.tranid, t_stat.cur_stat);
	}
      else
	{
	  fprintf (stdout, "\t%14d | %17lu\n", t_stat.tranid, 0);
	}
    }
  fprintf (stdout, "\n-----------------------------------------------------\n\n");
}
