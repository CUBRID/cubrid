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
 * memory_monitor_cl.cpp - Implementation of client functions of memory monitor
 */

#include "memory_monitor_cl.hpp"

void mmon_print_server_info (MMON_SERVER_INFO &server_info, FILE *outfile_fp)
{
  double mem_usage_ratio = 0.0;

  fprintf (outfile_fp, "====================cubrid memmon====================\n");
  fprintf (outfile_fp, "Server Name: %s\n", server_info.server_name);
  fprintf (outfile_fp, "Total Memory Usage: %lu KB (for meta info: %lu KB)\n",
	   MMON_CONVERT_TO_KB_SIZE (server_info.total_mem_usage), MMON_CONVERT_TO_KB_SIZE (server_info.total_metainfo_mem_usage));
  fprintf (outfile_fp, "-----------------------------------------------------\n");

  fprintf (outfile_fp, "\t%-100s | %17s(%s)\n", "File Name", "Memory Usage", "Ratio");

  if (server_info.total_mem_usage != 0)
    {
      for (const auto &stat : server_info.stat_info)
	{
	  mem_usage_ratio = stat.second / (double) server_info.total_mem_usage;
	  mem_usage_ratio *= 100;
	  fprintf (outfile_fp, "\t%-100s | %17lu KB(%3d%%)\n",stat.first.c_str (), MMON_CONVERT_TO_KB_SIZE (stat.second),
		   (int)mem_usage_ratio);
	}
    }
  fprintf (outfile_fp, "-----------------------------------------------------\n");
  fflush (outfile_fp);
}
