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

#include <cstring>
#include <atomic>
#include <vector>
#include <cassert>

#include "perf.hpp"
#include "error_manager.h"
#include "memory_monitor_sr.hpp"
#include "log_impl.h"

namespace cubperf
{
  class memory_monitor;
  memory_monitor *mmon_Gl = nullptr;

  typedef struct mmon_stat_info
  {
    MMON_STAT_ID id;
    const char *comp_name;
    const char *subcomp_name;
  } MMON_STAT_INFO;

  typedef struct mmon_mem_stat
  {
    std::atomic<uint64_t> init_stat;
    std::atomic<uint64_t> cur_stat;
    std::atomic<uint64_t> peak_stat;
    std::atomic<uint32_t> expand_count;
  } MMON_MEM_STAT;

  class mmon_subcomponent
  {
    public:
      mmon_subcomponent (const char *subcomp_name)
	: m_subcomp_name (subcomp_name) {}
      mmon_subcomponent (const mmon_subcomponent &) = delete;
      mmon_subcomponent (mmon_subcomponent &&) = delete;

      ~mmon_subcomponent () {}

      mmon_subcomponent &operator = (const mmon_subcomponent &) = delete;
      mmon_subcomponent &operator = (mmon_subcomponent &&) = delete;

      const char *get_name ();
      void add_cur_stat (uint64_t size);
      void sub_cur_stat (uint64_t size);

    private:
      std::string m_subcomp_name;
      std::atomic<uint64_t> m_cur_stat;
  };

  class mmon_component
  {
    public:
      mmon_component (const char *comp_name)
	: m_comp_name (comp_name) {}
      mmon_component (const mmon_component &) = delete;
      mmon_component (mmon_component &&) = delete;

      ~mmon_component () {}

      mmon_component &operator = (const mmon_component &) = delete;
      mmon_component &operator = (mmon_component &&) = delete;

      const char *get_name ();
      void add_stat (uint64_t size, int subcomp_idx, bool allocation_at_init);
      void sub_stat (uint64_t size, int subcomp_idx, bool deallocation_at_init);
      void add_expand_count ();
      int get_subcomp_index (const char *subcomp_name);
      int add_subcomponent (const char *name);

    private:
      std::string m_comp_name;
      MMON_MEM_STAT m_stat;
      std::vector<std::unique_ptr<mmon_subcomponent>> m_subcomponent;
  };

  class mmon_module
  {
    public:
      /* max index of component or subcomponent is 0x0000FFFF
       * if some stats have max index of component or subcomponent,
       * we don't have to increase it */
      static constexpr int MAX_COMP_IDX = 0x0000FFFF;

    public:
      mmon_module (const char *module_name, const MMON_STAT_INFO *info);
      mmon_module (const mmon_module &) = delete;
      mmon_module (mmon_module &&) = delete;

      virtual ~mmon_module () {}

      mmon_module &operator = (const mmon_module &) = delete;
      mmon_module &operator = (mmon_module &&) = delete;

      virtual int aggregate_stats (const MMON_MODULE_INFO &info);
      void add_stat (uint64_t size, int comp_idx, int subcomp_idx, bool allocation_at_init);
      void sub_stat (uint64_t size, int comp_idx, int subcomp_idx, bool deallocation_at_init);
      void add_expand_count (int comp_idx);
      int add_component (const char *name);
      int get_comp_idx_map_val (int idx);
      inline int make_comp_idx_map_val (int comp_idx, int subcomp_idx);

    private:
      std::string m_module_name;
      MMON_MEM_STAT m_stat;
      std::vector<std::unique_ptr<mmon_component>> m_component;
      std::vector<int> m_comp_idx_map;
  };


  class memory_monitor
  {
    public:
      memory_monitor (const char *server_name)
	: m_aggregater (this), m_server_name (server_name) {}
      memory_monitor (const memory_monitor &) = delete;
      memory_monitor (memory_monitor &&) = delete;

      ~memory_monitor () {}

      memory_monitor &operator = (const memory_monitor &) = delete;
      memory_monitor &operator = (memory_monitor &&) = delete;

      void add_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size);
      void sub_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size);
      void add_expand_count (MMON_STAT_ID stat_id);
      void resize_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t old_size, uint64_t new_size);
      void move_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID src, MMON_STAT_ID dest, uint64_t size);
      int aggregate_module_info (MMON_MODULE_INFO *info, int module_index);
      int aggregate_tran_info (MMON_TRAN_INFO *info, int tran_count);
      inline int get_comp_idx_from_comp_idx_map (int comp_idx_map_val);
      inline int get_subcomp_idx_from_comp_idx_map (int comp_idx_map_val);

    private:
      class aggregater
      {
	public:
	  aggregater (memory_monitor *mmon);
	  aggregater (const aggregater &) = delete;
	  aggregater (aggregater &&) = delete;

	  ~aggregater () {}

	  aggregater &operator = (const aggregater &) = delete;
	  aggregater &operator = (aggregater &&) = delete;

	  int get_server_info (const MMON_SERVER_INFO &info);
	  int get_module_info (const MMON_MODULE_INFO &info, int module_index);
	  int get_transaction_info (const MMON_TRAN_INFO &info, int tran_count);

	private:
	  memory_monitor *m_mmon;
      };

    private:
      std::string m_server_name;
      std::atomic<uint64_t> m_total_mem_usage;
      aggregater m_aggregater;

      std::unique_ptr<mmon_module> m_module[MMON_MODULE_LAST];
  };

  inline int mmon_module::make_comp_idx_map_val (int comp_idx, int subcomp_idx)
  {
    return (comp_idx << 16 | subcomp_idx);
  }

  inline int memory_monitor::get_comp_idx_from_comp_idx_map (int comp_idx_map_val)
  {
    return (comp_idx_map_val >> 16);
  }

  inline int memory_monitor::get_subcomp_idx_from_comp_idx_map (int comp_idx_map_val)
  {
    return (comp_idx_map_val & MMON_PARSE_MASK);
  }

  const char *mmon_subcomponent::get_name ()
  {
    return m_subcomp_name.c_str ();
  }

  void mmon_subcomponent::add_cur_stat (uint64_t size)
  {
    m_cur_stat += size;
  }

  void mmon_subcomponent::sub_cur_stat (uint64_t size)
  {
    m_cur_stat -= size;
  }

  const char *mmon_component::get_name ()
  {
    return m_comp_name.c_str ();
  }

  void mmon_component::add_stat (uint64_t size, int subcomp_idx, bool allocation_at_init)
  {
    /* description of add_stat(..).
     * 1) if allocation_at_init == true, add init_stat
     * 2) then, add cur_stat
     * 3) compare with peak_stat
     * 4) if cur_stat(new) > peak_stat, update peak_stat
     * 5) call subcomponent->add_cur_stat(size) */

    if (allocation_at_init)
      {
	m_stat.init_stat += size;
      }

    m_stat.cur_stat += size;

    if (m_stat.cur_stat > m_stat.peak_stat)
      {
	m_stat.peak_stat = m_stat.cur_stat.load ();
      }

    /* If there is a subcomponent info */
    if (subcomp_idx != mmon_module::MAX_COMP_IDX)
      {
	m_subcomponent[subcomp_idx]->add_cur_stat (size);
      }
  }

  void mmon_component::sub_stat (uint64_t size, int subcomp_idx, bool deallocation_at_init)
  {
    /* description of sub_stat(..).
     * 1) if deallocation_at_init == true, sub init_stat
     * 2) then, sub cur_stat
     * 3) call subcomponent->sub_stat(size) */

    if (deallocation_at_init)
      {
	m_stat.init_stat -= size;
      }

    m_stat.cur_stat -= size;

    /* If there is a subcomponent info */
    if (subcomp_idx != mmon_module::MAX_COMP_IDX)
      {
	m_subcomponent[subcomp_idx]->sub_cur_stat (size);
      }
  }

  void mmon_component::add_expand_count ()
  {
    m_stat.expand_count++;
  }

  int mmon_component::get_subcomp_index (const char *subcomp_name)
  {
    for (size_t i = 0; i < m_subcomponent.size (); i++)
      {
	if (!strcmp (subcomp_name, m_subcomponent[i]->get_name()))
	  {
	    return i;
	  }
      }

    /* if not exist, return -1 */
    return -1;
  }

  int mmon_component::add_subcomponent (const char *name)
  {
    m_subcomponent.push_back (std::make_unique<mmon_subcomponent> (name));
    return (m_subcomponent.size () - 1);
  }

  mmon_module::mmon_module (const char *module_name, const MMON_STAT_INFO *info)
    : m_module_name (module_name)
  {
    /* register component and subcomponent information
     * add component and subcomponent */
    int idx = 0;
    while (info[idx].id != MMON_STAT_LAST)
      {
	bool comp_skip = false;
	bool subcomp_skip = false;
	int comp_idx = mmon_module::MAX_COMP_IDX, subcomp_idx = mmon_module::MAX_COMP_IDX;

	if (info[idx].comp_name)
	  {
	    for (size_t i = 0; i < m_component.size (); i++)
	      {
		if (!strcmp (info[idx].comp_name, m_component[i]->get_name ()))
		  {
		    comp_idx = i;
		    comp_skip = true;
		    break;
		  }
	      }

	    if (!comp_skip)
	      {
		comp_idx = add_component (info[idx].comp_name);
	      }
	    assert (comp_idx < MAX_COMP_IDX);

	    if (info[idx].subcomp_name)
	      {
		subcomp_idx = m_component[comp_idx]->get_subcomp_index (info[idx].subcomp_name);
		if (subcomp_idx != -1)
		  {
		    subcomp_skip = true;
		  }

		if (!subcomp_skip)
		  {
		    subcomp_idx = m_component[comp_idx]->add_subcomponent (info[idx].subcomp_name);
		  }
		assert (subcomp_idx < MAX_COMP_IDX);
	      }
	  }
	else
	  {
	    assert (info[idx].subcomp_name == NULL);
	  }

	m_comp_idx_map.push_back (make_comp_idx_map_val (comp_idx, subcomp_idx));

	idx++;
      }
  }

  int mmon_module::aggregate_stats (const MMON_MODULE_INFO &info)
  {
    int error = NO_ERROR;
    return error;
  }

  void mmon_module::add_stat (uint64_t size, int comp_idx, int subcomp_idx, bool allocation_at_init)
  {
    /* description of add_stat(..).
     * 1) if allocation_at_init == true, add init_stat
     * 2) then, add cur_stat
     * 3) compare with peak_stat
     * 4) if cur_stat(new) > peak_stat, update peak_stat
     * 5) call component->add_stat(size, subcomp_idx, allocation_at_init) */

    if (allocation_at_init)
      {
	m_stat.init_stat += size;
      }

    m_stat.cur_stat += size;

    if (m_stat.cur_stat > m_stat.peak_stat)
      {
	m_stat.peak_stat = m_stat.cur_stat.load ();
      }

    /* If there is a component info */
    if (comp_idx != mmon_module::MAX_COMP_IDX)
      {
	m_component[comp_idx]->add_stat (size, subcomp_idx, allocation_at_init);
      }
  }

  void mmon_module::sub_stat (uint64_t size, int comp_idx, int subcomp_idx, bool deallocation_at_init)
  {
    /* description of sub_stat(..).
     * 1) if deallocation_at_init == true, sub init_stat
     * 2) then, sub cur_stat
     * 3) call component->sub_stat(size, subcomp_idx, deallocation_at_init) */

    if (deallocation_at_init)
      {
	m_stat.init_stat -= size;
      }

    m_stat.cur_stat -= size;

    /* If there is a component info */
    if (comp_idx != mmon_module::MAX_COMP_IDX)
      {
	m_component[comp_idx]->sub_stat (size, subcomp_idx, deallocation_at_init);
      }
  }

  void mmon_module::add_expand_count (int comp_idx)
  {
    m_stat.expand_count++;

    /* If there is a component info */
    if (comp_idx != mmon_module::MAX_COMP_IDX)
      {
	m_component[comp_idx]->add_expand_count ();
      }
  }

  int mmon_module::add_component (const char *name)
  {
    m_component.push_back (std::make_unique<mmon_component> (name));
    return (m_component.size () - 1);
  }

  int mmon_module::get_comp_idx_map_val (int idx)
  {
    return m_comp_idx_map[idx];
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

  void memory_monitor::add_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size)
  {
    LOG_TDES *tdes;
    TRANID trid = NULL_TRANID;
    int module_idx = MMON_GET_MODULE_IDX_FROM_STAT_ID (stat_id);
    int comp_map_idx = MMON_GET_COMP_MAP_IDX_FROM_STAT_ID (stat_id);
    int idx_map_val, comp_idx, subcomp_idx;
    bool allocation_at_init = !LOG_ISRESTARTED ();

    idx_map_val = m_module[module_idx]->get_comp_idx_map_val (comp_map_idx);
    comp_idx = get_comp_idx_from_comp_idx_map (idx_map_val);
    subcomp_idx = get_subcomp_idx_from_comp_idx_map (idx_map_val);

    m_total_mem_usage += size;

    m_module[module_idx]->add_stat (size, comp_idx, subcomp_idx, allocation_at_init);

    if (thread_p)
      {
	tdes = LOG_FIND_CURRENT_TDES (thread_p);
	tdes->cur_mem_usage += size;
      }
  }

  void memory_monitor::sub_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size)
  {
    LOG_TDES *tdes;
    TRANID trid = NULL_TRANID;
    int module_idx = MMON_GET_MODULE_IDX_FROM_STAT_ID (stat_id);
    int comp_map_idx = MMON_GET_COMP_MAP_IDX_FROM_STAT_ID (stat_id);
    int idx_map_val, comp_idx, subcomp_idx;
    bool deallocation_at_init = !LOG_ISRESTARTED ();

    idx_map_val = m_module[module_idx]->get_comp_idx_map_val (comp_map_idx);
    comp_idx = get_comp_idx_from_comp_idx_map (idx_map_val);
    subcomp_idx = get_subcomp_idx_from_comp_idx_map (idx_map_val);

    m_total_mem_usage -= size;

    m_module[module_idx]->sub_stat (size, comp_idx, subcomp_idx, deallocation_at_init);

    if (thread_p)
      {
	tdes = LOG_FIND_CURRENT_TDES (thread_p);
	tdes->cur_mem_usage -= size;
      }
  }

  void memory_monitor::add_expand_count (MMON_STAT_ID stat_id)
  {
    int module_idx = MMON_GET_MODULE_IDX_FROM_STAT_ID (stat_id);
    int comp_map_idx = MMON_GET_COMP_MAP_IDX_FROM_STAT_ID (stat_id);
    int idx_map_val, comp_idx, subcomp_idx;

    idx_map_val = m_module[module_idx]->get_comp_idx_map_val (comp_map_idx);
    comp_idx = get_comp_idx_from_comp_idx_map (idx_map_val);
    subcomp_idx = get_subcomp_idx_from_comp_idx_map (idx_map_val);

    m_module[module_idx]->add_expand_count (comp_idx);
  }

  void memory_monitor::resize_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t old_size,
				    uint64_t new_size)
  {
    this->sub_stat (thread_p, stat_id, old_size);
    this->add_stat (thread_p, stat_id, new_size);
    if (new_size - old_size > 0) /* expand */
      {
	this->add_expand_count (stat_id);
      }
  }

  void memory_monitor::move_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID src, MMON_STAT_ID dest, uint64_t size)
  {
    this->sub_stat (thread_p, src, size);
    this->add_stat (thread_p, dest, size);
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

/* APIs */
int mmon_initialize (const char *server_name)
{
  int error = NO_ERROR;
  cubperf::mmon_Gl = new cubperf::memory_monitor (server_name);

  if (cubperf::mmon_Gl == nullptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (cubperf::memory_monitor));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
    }

  return error;
}

void mmon_finalize ()
{
  delete cubperf::mmon_Gl;
}

void mmon_add_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size)
{
  cubperf::mmon_Gl->add_stat (thread_p, stat_id, size);
}

void mmon_sub_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size)
{
  cubperf::mmon_Gl->sub_stat (thread_p, stat_id, size);
}

void mmon_move_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID src, MMON_STAT_ID dest, uint64_t size)
{
  cubperf::mmon_Gl->move_stat (thread_p, src, dest, size);
}

void mmon_resize_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t old_size, uint64_t new_size)
{
  cubperf::mmon_Gl->resize_stat (thread_p, stat_id, old_size, new_size);
}

int mmon_aggregate_module_info (MMON_MODULE_INFO *info, int module_index)
{
  int error = NO_ERROR;

  return error;
}

int mmon_aggregate_tran_info (MMON_TRAN_INFO *info, int tran_count)
{
  int error = NO_ERROR;

  return error;
}
