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

#include "log_impl.h"

namespace cubperf
{
  memory_monitor *mmon_Gl = nullptr;

  const char *mmon_subcomponent::get_name ()
  {
    return m_subcomp_name.c_str ();
  }

  void mmon_subcomponent::add_cur_stat (uint64_t size)
  {
    m_cur_stat.fetch_add (size);
  }

  void mmon_subcomponent::sub_cur_stat (uint64_t size)
  {
    m_cur_stat.fetch_sub (size);
  }

  const char *mmon_component::get_name ()
  {
    return m_comp_name.c_str ();
  }

  void mmon_component::add_stat (uint64_t size, int subcomp_idx, bool init, bool expand)
  {
    /* description of add_stat(..).
     * 1) if init == true, add init_stat
     * 2) then, add cur_stat
     * 3) compare with peak_stat
     * 4) if cur_stat(new) > peak_stat, update peak_stat
     * 5) if expand == true, expand_count++
     * 6) call subcomponent->add_cur_stat(size) */
    return;
  }

  void mmon_component::sub_stat (uint64_t size, int subcomp_idx, bool init)
  {
    /* description of sub_stat(size, init).
     * 1) if init == true, sub init_stat
     * 2) then, sub cur_stat
     * 3) call component->sub_stat(size, subcomp_idx, init) */
    return;
  }

  int mmon_component::is_subcomp_exist (const char *subcomp_name)
  {
    for (int i = 0; i < m_subcomponent.size (); i++)
      {
	if (!strcmp (subcomp_name, m_subcomponent[i]->get_name()))
	  {
	    return i;
	  }
      }

    /* if not exist, return mmon_module::MAX_COMP_IDX */
    return mmon_module::MAX_COMP_IDX;
  }

  int mmon_component::add_subcomponent (const char *name)
  {
    return 0;
  }

  mmon_module::mmon_module (const char *module_name, const MMON_COMP_INFO *info)
    : m_module_name (module_name)
  {
    /* register component and subcomponent information
     * add component and subcomponent */
    int cnt = 0;
    while (info[cnt].id != MMON_STAT_LAST)
      {
	bool comp_skip = false;
	bool subcomp_skip = false;
	int comp_idx = mmon_module::MAX_COMP_IDX, subcomp_idx = mmon_module::MAX_COMP_IDX;
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
		subcomp_idx = m_component[comp_idx]->is_subcomp_exist (info[cnt].subcomp_name);
		if (subcomp_idx != mmon_module::MAX_COMP_IDX)
		  {
		    subcomp_skip = true;
		  }

		if (!subcomp_skip)
		  {
		    subcomp_idx = m_component[comp_idx]->add_subcomponent (info[cnt].subcomp_name);
		  }
	      }
	  }

	m_comp_idx_map.push_back (MAKE_STAT_IDX_MAP (comp_idx, subcomp_idx));

	cnt++;
      }
  }

  int mmon_module::aggregate_stats (const MMON_MODULE_INFO &info)
  {
    return 0;
  }

  void mmon_module::add_stat (uint64_t size, int comp_idx, int subcomp_idx, bool init, bool expand)
  {
    /* description of add_stat(..).
     * 1) if init == true, add init_stat
     * 2) then, add cur_stat
     * 3) compare with peak_stat
     * 4) if cur_stat(new) > peak_stat, update peak_stat
     * 5) if expand == true, expand_count++
     * 6) call component->add_stat(size, subcomp_idx, init, expand) */
    return;
  }

  void mmon_module::sub_stat (uint64_t size, int comp_idx, int subcomp_idx, bool init)
  {
    /* description of sub_stat(size, init).
     * 1) if init == true, sub init_stat
     * 2) then, sub cur_stat
     * 3) call component->sub_stat(size, subcomp_idx, init) */
    return;
  }

  int mmon_module::add_component (const char *name)
  {
    return 0;
  }

  memory_monitor::aggregater::aggregater (memory_monitor *mmon)
  {
    m_mmon = mmon;
  }

  int memory_monitor::aggregater::get_server_info (const MMON_SERVER_INFO &info)
  {
    return 0;
  }

  int memory_monitor::aggregater::get_module_info (const MMON_MODULE_INFO &info, int module_index)
  {
    return 0;
  }

  int memory_monitor::aggregater::get_transaction_info (const MMON_TRAN_INFO &info, int tran_count)
  {
    return 0;
  }

  memory_monitor::~memory_monitor ()
  {
    for (int i = 0; i < MMON_MODULE_LAST; i++)
      {
	delete m_module[i];
      }
  }

  int memory_monitor::add_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size, bool expand)
  {
    return 0;
  }

  int memory_monitor::sub_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size)
  {
    return 0;
  }

  int memory_monitor::resize_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t old_size,
				   uint64_t new_size)
  {
    return 0;
  }

  int memory_monitor::move_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID src, MMON_STAT_ID dest, uint64_t size)
  {
    return 0;
  }

  int memory_monitor::aggregate_module_info (MMON_MODULE_INFO *info, int module_index)
  {
    return 0;
  }

  int memory_monitor::aggregate_tran_info (MMON_TRAN_INFO *info, int tran_count)
  {
    return 0;
  }
} // namespace cubperf
