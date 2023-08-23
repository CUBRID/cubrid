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
 * memory_monitor_cl.hpp - client structures and functions
 *                         for memory monitoring module
 */

#include <cstring>
#include <string>
#include <stdio.h>

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
  return -1;
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
  fprintf (stdout, "====================Cubrid Memmon====================\n");
  fprintf (stdout, "Server Name: %s\n", server_info.name);
  fprintf (stdout, "Total Memory Usage(KB): %lu\n\n", CONVERT_TO_KB_SIZE (server_info.total_mem_usage));
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
  const std::string init_size_str ("Init Size(KB)");
  const std::string cur_size_str ("Cur Size(KB)");
  const std::string peak_size_str ("Peak Size(KB)");
  const std::string resize_expand_str ("Resize Expand");

  for (const auto &m_info : module_info)
    {
      fprintf (stdout, "Module Name: %s\n\n", m_info.name);
      fprintf (stdout, "%-13s\t: %17lu\n", init_size_str.c_str (), CONVERT_TO_KB_SIZE (m_info.stat.init_stat));
      fprintf (stdout, "%-13s\t: %17lu\n", cur_size_str.c_str (), CONVERT_TO_KB_SIZE (m_info.stat.cur_stat));
      fprintf (stdout, "%-13s\t: %17lu\n", peak_size_str.c_str (), CONVERT_TO_KB_SIZE (m_info.stat.peak_stat));
      fprintf (stdout, "%-13s\t: %17u\n\n", resize_expand_str.c_str (), m_info.stat.expand_resize_count);

      for (const auto &comp_info : m_info.comp_info)
	{
	  fprintf (stdout, "%s\n", comp_info.name);
	  fprintf (stdout, "\t%-17s\t: %17lu\n", init_size_str.c_str (), CONVERT_TO_KB_SIZE (comp_info.stat.init_stat));
	  fprintf (stdout, "\t%-17s\t: %17lu\n", cur_size_str.c_str (), CONVERT_TO_KB_SIZE (comp_info.stat.cur_stat));

	  for (const auto &subcomp_info : comp_info.subcomp_info)
	    {
	      std::string out_name (subcomp_info.name);
	      out_name += "(KB)";
	      fprintf (stdout, "\t  %-15s\t: %17lu\n", out_name.c_str (), CONVERT_TO_KB_SIZE (subcomp_info.cur_stat));
	    }
	  fprintf (stdout, "\t%-17s\t: %17lu\n", peak_size_str.c_str (), CONVERT_TO_KB_SIZE (comp_info.stat.peak_stat));
	  fprintf (stdout, "\t%-17s\t: %17u\n\n", resize_expand_str.c_str (), comp_info.stat.expand_resize_count);
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
  double percent = 0.0;

  fprintf (stdout, "Per Module Memory Usage (KB)\n");
  fprintf (stdout, "\tModule\t\tModule\t\t\tModule\n");
  fprintf (stdout, "\tIndex\t\tName\t\t\tUsage(%%)\n");

  for (const auto &m_info : module_info)
    {
      if (m_info.stat.cur_stat != 0)
	{
	  percent = (double) m_info.stat.cur_stat / (double) server_mem_usage;
	}

      fprintf (stdout, "\t%5d\t%-20s\t: %17lu(%3d%%)\n", mmon_convert_module_name_to_index (m_info.name),
	       m_info.name, CONVERT_TO_KB_SIZE (m_info.stat.cur_stat), (int)percent);
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
  const std::string tran_id_str ("Transaction ID");
  const std::string mem_usage_str ("Memory Usage");

  fprintf (stdout, "Transaction Memory Usage Info (KB)\n");
  fprintf (stdout, "\t%14s | %12s\n", tran_id_str.c_str (), mem_usage_str.c_str ());

  for (const auto &t_stat : tran_info.tran_stat)
    {
      fprintf (stdout, "\t%14d | %17lu\n", t_stat.tranid, t_stat.cur_stat);
    }
  fprintf (stdout, "\n-----------------------------------------------------\n\n");
}

