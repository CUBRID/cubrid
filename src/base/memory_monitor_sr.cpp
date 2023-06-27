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
  memory_monitoring_manager *mmm_Gl = nullptr;

  const char *mmm_subcomponent::get_name ()
  {
    return m_subcomp_name;
  }

  void mmm_subcomponent::add_cur_stat (uint64_t size)
  {
    m_cur_stat.fetch_add (size);
  }

  void mmm_subcomponent::sub_cur_stat (uint64_t size)
  {
    m_cur_stat.fetch_sub (size);
  }

  const uint64_t mmm_subcomponent::get_cur_stat ()
  {
    return m_cur_stat.load ();
  }

  const char *mmm_component::get_name ()
  {
    return m_comp_name;
  }

  void mmm_component::add_stat (uint64_t size, bool init)
  {
    return;
  }

  void mmm_component::sub_stat (uint64_t size, bool init)
  {
    return;
  }

  void mmm_component::add_expand_count ()
  {
    m_stat.expand_count++;
  }

  const MMM_MEM_STAT &mmm_component::get_stat ()
  {
    return m_stat;
  }

  const std::vector<mmm_subcomponent *> &mmm_component::get_subcomp_vec ()
  {
    return m_subcomponent;
  }

  int mmm_component::add_subcomponent (const char *name)
  {
    return 0;
  }

  mmm_module::mmm_module (const char *name, const MMM_COMP_INFO *info)
  {
    m_module_name = new char[strlen (name) + 1];
    strcpy (m_module_name, name);

    /* register component and subcomponent information
     * add component and subcomponent */
    int cnt = 0;
    while (info[cnt].id != MMM_STAT_LAST)
      {
	bool comp_skip = false;
	bool subcomp_skip = false;
	int comp_idx = mmm_module::MAX_COMP_IDX, subcomp_idx = mmm_module::MAX_COMP_IDX;
	int i;
	if (info[cnt].comp_name)
	  {
	    for (i = 0; i < m_component.size (); i++)
	      {
		if (!strcmp (info[cnt].comp_name, m_component[i]->get_name ()))
		  {
		    comp_idx = i;
		    comp_skip = true;
		    break;
		  }
	      }

	    if (!comp_skip)
	      {
		comp_idx = add_component (info[cnt].comp_name);
	      }

	    if (info[cnt].subcomp_name)
	      {
		std::vector<mmm_subcomponent *> subcomp_vec = m_component[comp_idx]->get_subcomp_vec();
		for (i = 0; i < subcomp_vec.size (); i++)
		  {
		    if (!strcmp (info[cnt].subcomp_name, subcomp_vec[i]->get_name ()))
		      {
			subcomp_idx = i;
			subcomp_skip = true;
			break;
		      }
		  }

		if (!subcomp_skip)
		  {
		    subcomp_idx = m_component[comp_idx]->add_subcomponent (info[cnt].subcomp_name);
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

  const char *mmm_module::get_name ()
  {
    return m_module_name;
  }

  void mmm_module::add_stat (uint64_t size, bool init)
  {
    /* description of add_stat(size, init). (mmm_component::add_stat(..) works same)
     * 1) if init == true, add init_stat
     * 2) then, add cur_stat
     * 3) compare with peak_stat
     * 4) if cur_stat(new) > peak_stat, update peak_stat */
    return;
  }

  void mmm_module::sub_stat (uint64_t size, bool init)
  {
    /* description of sub_stat(size, init). (mmm_component::sub_stat(..) works same)
     * 1) if init == true, sub init_stat
     * 2) then, sub cur_stat */
    return;
  }

  void mmm_module::add_expand_count ()
  {
    m_stat.expand_count++;
  }

  const MMM_MEM_STAT &mmm_module::get_stat ()
  {
    return m_stat;
  }

  int mmm_module::add_component (const char *name)
  {
    return 0;
  }

  const std::vector<mmm_component *> &mmm_module::get_comp_vec ()
  {
    return m_component;
  }

  const std::vector<int> &mmm_module::get_comp_idx_map ()
  {
    return m_comp_idx_map;
  }

  void mmm_module::add_comp_idx_map (int compound)
  {
    m_comp_idx_map.push_back (compound);
  }

  int memory_monitoring_manager::mmm_aggregater::get_server_info (MEMMON_SERVER_INFO *info)
  {
    return 0;
  }

  int memory_monitoring_manager::mmm_aggregater::get_module_info (MEMMON_MODULE_INFO *info, int module_index)
  {
    return 0;
  }

  int memory_monitoring_manager::mmm_aggregater::get_transaction_info (MEMMON_TRAN_INFO *info, int tran_count)
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

  int memory_monitoring_manager::move_stat (THREAD_ENTRY *thread_p, MMM_STAT_ID src, MMM_STAT_ID dest, uint64_t size)
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

