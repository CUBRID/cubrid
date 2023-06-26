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
 * memory_monitor_sr.cpp - implementation of memory monitoring manager
 */

#include <stdexcept>
#include <cstring>

#include "perf.hpp"
#include "error_manager.h"
#include "memory_monitor_sr.hpp"

#if defined (SERVER_MODE)
#include "log_impl.h"
#endif


#ifdef SERVER_MODE
namespace cubperf
{
  memory_monitoring_manager *mmm_Gl = NULL;

  mmm_module::mmm_module (const char *name, MMM_COMP_INFO *info)
  {
    m_module_name = new char[strlen (name) + 1];
    strcpy (m_module_name, name);

    /* register component and subcomponent information
     * add component or subcomponent */
    int cnt = 0;
    while (info[cnt].idx != MMM_STAT_LAST)
      {
	bool comp_skip = false;
	bool subcomp_skip = false;
	int comp_idx = this->m_max_idx, subcomp_idx = this->m_max_idx;
	int i;
	if (info[cnt].comp_name.size())
	  {
	    for (i = 0; i < m_component.size(); i++)
	      {
		if (!strcmp (info[cnt].comp_name.c_str(), m_component[i].m_comp_name))
		  {
		    comp_idx = i;
		    comp_skip = true;
		    break;
		  }
	      }

	    if (!comp_skip)
	      {
		comp_idx = add_component (info[cnt].comp_name.c_str());
	      }

	    if (info[cnt].subcomp_name.size())
	      {
		for (i = 0; i < m_component[comp_idx].m_subcomponent.size(); i++)
		  {
		    if (!strcmp (info[cnt].subcomp_name.c_str(), m_component[comp_idx].m_subcomponent[i].m_subcomp_name))
		      {
			subcomp_idx = i;
			subcomp_skip = true;
			break;
		      }
		  }

		if (!subcomp_skip)
		  {
		    subcomp_idx = m_component[comp_idx].add_subcomponent (info[cnt].subcomp_name.c_str());
		  }
	      }
	  }

	m_comp_idx_map.push_back (MMM_MAKE_STAT_IDX_MAP (comp_idx, subcomp_idx));

	cnt++;
      }
  }

  int mmm_module::aggregate_stats (MEMMON_MODULE_INFO *info)
  {
    return 0;
  }

  int mmm_aggregater::get_server_info (MEMMON_SERVER_INFO *info)
  {
    return 0;
  }

  int mmm_aggregater::get_module_info (MEMMON_MODULE_INFO *info, int module_index)
  {
    return 0;
  }

  int mmm_aggregater::get_transaction_info (MEMMON_TRAN_INFO *info, int tran_count)
  {
    return 0;
  }

  int memory_monitoring_manager::add_stat (THREAD_ENTRY *thread_p, MMM_STAT_ID stat_id, uint64_t size)
  {
    return 0;
  }

  int memory_monitoring_manager::sub_stat (THREAD_ENTRY *thread_p, MMM_STAT_ID stat_id, uint64_t size)
  {
    return 0;
  }

  int memory_monitoring_manager::resize_stat (THREAD_ENTRY *thread_p, MMM_STAT_ID stat_id, uint64_t old_size,
      uint64_t new_size)
  {
    return 0;
  }

  int memory_monitoring_manager::move_stat (THREAD_ENTRY *thread_p, MMM_STAT_ID src, MMM_STAT_ID dest,uint64_t size)
  {
    return 0;
  }

  int memory_monitoring_manager::aggregate_module_info (MEMMON_MODULE_INFO *info, int module_index)
  {
    return 0;
  }

  int memory_monitoring_manager::aggregate_tran_info (MEMMON_TRAN_INFO *info, int tran_count)
  {
    return 0;
  }
} // namespace cubperf

#endif /* SERVER_MODE */

