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
#include <cassert>

#include "error_manager.h"
#include "system_parameter.h"
#include "perf.hpp"
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
    std::atomic<uint32_t> expand_resize_count;
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
      void add_cur_stat (int64_t size);

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
      void add_stat (int64_t size, int subcomp_idx);
      void add_expand_resize_count ();
      void end_init_phase ();
      int add_subcomponent (const char *name, bool &subcomp_exist);

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
      void add_stat (MMON_STAT_ID stat_id, int64_t size);
      void add_expand_resize_count (MMON_STAT_ID stat_id);
      void end_init_phase ();
      int add_component (const char *name, bool &comp_exist);

    private:
      void get_comp_and_subcomp_idx (MMON_STAT_ID stat_id, int &comp_idx, int &subcomp_idx) const;
      inline int make_comp_idx_map_val (int comp_idx, int subcomp_idx) const;
      inline int get_comp_map_idx (MMON_STAT_ID stat_id) const;
      inline int get_comp_idx (int comp_idx_map_val) const;
      inline int get_subcomp_idx (int comp_idx_map_val) const;

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

      void add_stat (MMON_STAT_ID stat_id, int64_t size);
      void add_tran_stat (THREAD_ENTRY *thread_p, int64_t size);
      void resize_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, int64_t old_size, int64_t new_size);
      void move_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID src, MMON_STAT_ID dest, int64_t size);
      void end_init_phase ();
      int aggregate_module_info (MMON_MODULE_INFO *info, int module_index);
      int aggregate_tran_info (MMON_TRAN_INFO *info, int tran_count);

    private:
      inline int get_module_idx (MMON_STAT_ID stat_id) const;

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

  inline int mmon_module::make_comp_idx_map_val (int comp_idx, int subcomp_idx) const
  {
    return (comp_idx << 16 | subcomp_idx);
  }

  inline int mmon_module::get_comp_map_idx (MMON_STAT_ID stat_id) const
  {
    return (((int) stat_id) & MMON_PARSE_MASK);
  }

  inline int mmon_module::get_comp_idx (int comp_idx_map_val) const
  {
    return (comp_idx_map_val >> 16);
  }

  inline int mmon_module::get_subcomp_idx (int comp_idx_map_val) const
  {
    return (comp_idx_map_val & MMON_PARSE_MASK);
  }

  inline int memory_monitor::get_module_idx (MMON_STAT_ID stat_id) const
  {
    return (((int) stat_id) >> 16);
  }

  const char *mmon_subcomponent::get_name ()
  {
    return m_subcomp_name.c_str ();
  }

  void mmon_subcomponent::add_cur_stat (int64_t size)
  {
    assert ((size >= 0) || (- (size) <= m_cur_stat));

    m_cur_stat += size;
  }

  const char *mmon_component::get_name ()
  {
    return m_comp_name.c_str ();
  }

  void mmon_component::add_stat (int64_t size, int subcomp_idx)
  {
    assert ((size >= 0) || (- (size) <= m_stat.cur_stat));

    m_stat.cur_stat += size;

    if (m_stat.cur_stat > m_stat.peak_stat)
      {
	m_stat.peak_stat = m_stat.cur_stat.load ();
      }

    /* If there is a subcomponent info */
    if (subcomp_idx != mmon_module::MAX_COMP_IDX)
      {
	assert (subcomp_idx < m_subcomponent.size ());
	m_subcomponent[subcomp_idx]->add_cur_stat (size);
      }
  }

  void mmon_component::add_expand_resize_count ()
  {
    m_stat.expand_resize_count++;
  }

  void mmon_component::end_init_phase ()
  {
    assert (m_stat.init_stat == 0);
    m_stat.init_stat = m_stat.cur_stat.load ();
  }

  int mmon_component::add_subcomponent (const char *name, bool &subcomp_exist)
  {
    for (size_t i = 0; i < m_subcomponent.size (); i++)
      {
	if (!strcmp (name, m_subcomponent[i]->get_name ()))
	  {
	    subcomp_exist = true;
	    return i;
	  }
      }

    m_subcomponent.push_back (std::make_unique<mmon_subcomponent> (name));

    return m_subcomponent.size () - 1;
  }

  mmon_module::mmon_module (const char *module_name, const MMON_STAT_INFO *info)
    : m_module_name (module_name)
  {
    /* register component and subcomponent information
     * add component and subcomponent */
    int idx = 0;
    while (info[idx].id != MMON_STAT_LAST)
      {
	bool comp_exist = false;
	bool subcomp_exist = false;
	int comp_idx = mmon_module::MAX_COMP_IDX, subcomp_idx = mmon_module::MAX_COMP_IDX;

	if (info[idx].comp_name)
	  {
	    comp_idx = add_component (info[idx].comp_name, comp_exist);
	    assert (comp_idx < MAX_COMP_IDX);

	    if (info[idx].subcomp_name)
	      {
		subcomp_idx = m_component[comp_idx]->add_subcomponent (info[idx].subcomp_name, subcomp_exist);
		assert (subcomp_idx < MAX_COMP_IDX);
	      }
	  }
	else
	  {
	    assert (info[idx].subcomp_name == NULL);
	  }

	assert ((comp_exist && subcomp_exist) == false);
	m_comp_idx_map.push_back (make_comp_idx_map_val (comp_idx, subcomp_idx));

	idx++;
      }
  }

  int mmon_module::aggregate_stats (const MMON_MODULE_INFO &info)
  {
    int error = NO_ERROR;
    return error;
  }

  void mmon_module::get_comp_and_subcomp_idx (MMON_STAT_ID stat_id, int &comp_idx, int &subcomp_idx) const
  {
    /* 32-bit MMON_STAT_ID: [module_idx: 16 | comp_idx_map_idx: 16]
     * 32-bit mmon_module::m_comp_idx_map[comp_idx_map_idx] value: [comp_idx: 16 | subcomp_idx: 16]
     */
    int comp_map_idx = get_comp_map_idx (stat_id);
    int idx_map_val;

    idx_map_val = m_comp_idx_map[comp_map_idx];
    comp_idx = get_comp_idx (idx_map_val);
    subcomp_idx = get_subcomp_idx (idx_map_val);
  }

  void mmon_module::add_stat (MMON_STAT_ID stat_id, int64_t size)
  {
    int comp_idx, subcomp_idx;

    assert ((size >= 0) || (- (size) <= m_stat.cur_stat));

    get_comp_and_subcomp_idx (stat_id, comp_idx, subcomp_idx);

    m_stat.cur_stat += size;

    if (m_stat.cur_stat > m_stat.peak_stat)
      {
	m_stat.peak_stat = m_stat.cur_stat.load ();
      }

    /* If there is a component info */
    if (comp_idx != mmon_module::MAX_COMP_IDX)
      {
	assert (comp_idx < m_component.size ());
	m_component[comp_idx]->add_stat (size, subcomp_idx);
      }
  }

  void mmon_module::add_expand_resize_count (MMON_STAT_ID stat_id)
  {
    int comp_idx, subcomp_idx;

    m_stat.expand_resize_count++;

    get_comp_and_subcomp_idx (stat_id, comp_idx, subcomp_idx);

    /* If there is a component info */
    if (comp_idx != mmon_module::MAX_COMP_IDX)
      {
	assert (comp_idx < m_component.size ());
	m_component[comp_idx]->add_expand_resize_count ();
      }
  }

  void mmon_module::end_init_phase ()
  {
    assert (m_stat.init_stat == 0);
    m_stat.init_stat = m_stat.cur_stat.load ();

    for (const auto &comp : m_component)
      {
	comp->end_init_phase ();
      }
  }

  int mmon_module::add_component (const char *name, bool &comp_exist)
  {
    for (size_t i = 0; i < m_component.size (); i++)
      {
	if (!strcmp (name, m_component[i]->get_name ()))
	  {
	    comp_exist = true;
	    return i;
	  }
      }

    m_component.push_back (std::make_unique<mmon_component> (name));

    return m_component.size () - 1;
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

  void memory_monitor::add_stat (MMON_STAT_ID stat_id, int64_t size)
  {
    int module_idx = get_module_idx (stat_id);

    if (size < 0)
      {
	assert (size < m_total_mem_usage);
      }

    m_total_mem_usage += size;

    m_module[module_idx]->add_stat (stat_id, size);
  }

  void memory_monitor::add_tran_stat (THREAD_ENTRY *thread_p, int64_t size)
  {
    LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);

    if (size < 0)
      {
	assert (size <= tdes->cur_mem_usage);
      }

    tdes->cur_mem_usage += size;
  }

  void memory_monitor::resize_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, int64_t old_size,
				    int64_t new_size)
  {
    int module_idx = get_module_idx (stat_id);

    add_stat (stat_id, - (old_size));
    add_tran_stat (thread_p, - (old_size));
    add_stat (stat_id, new_size);
    add_tran_stat (thread_p, new_size);
    if (new_size - old_size > 0) /* expand */
      {
	m_module[module_idx]->add_expand_resize_count (stat_id);
      }
  }

  void memory_monitor::move_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID src, MMON_STAT_ID dest, int64_t size)
  {
    add_stat (src, - (size));
    add_stat (dest, size);
  }

  void memory_monitor::end_init_phase ()
  {
    for (int i = 0; i < MMON_MODULE_LAST; i++)
      {
	m_module[i]->end_init_phase ();
      }
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
// TODO: API parameter check (after error code setting)
using namespace cubperf;

int mmon_initialize (const char *server_name)
{
  int error = NO_ERROR;

  assert (server_name != NULL);
  assert (mmon_Gl == nullptr);

  if (prm_get_bool_value (PRM_ID_MEMORY_MONITORING))
    {
      mmon_Gl = new memory_monitor (server_name);

      if (mmon_Gl == nullptr)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (memory_monitor));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  return error;
}

void mmon_notify_server_start ()
{
  if (mmon_Gl != nullptr)
    {
      mmon_Gl->end_init_phase ();
    }
}

void mmon_finalize ()
{
  if (mmon_Gl != nullptr)
    {
      delete mmon_Gl;
    }
}

void mmon_add_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, int64_t size)
{
  if (mmon_Gl != nullptr)
    {
      mmon_Gl->add_stat (stat_id, size);
      mmon_Gl->add_tran_stat (thread_p, size);
    }
}

void mmon_sub_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, int64_t size)
{
  if (mmon_Gl != nullptr)
    {
      mmon_Gl->add_stat (stat_id, - (size));
      mmon_Gl->add_tran_stat (thread_p, - (size));
    }
}

void mmon_move_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID src, MMON_STAT_ID dest, int64_t size)
{
  if (mmon_Gl != nullptr)
    {
      mmon_Gl->move_stat (thread_p, src, dest, size);
    }
}

void mmon_resize_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, int64_t old_size, int64_t new_size)
{
  if (mmon_Gl != nullptr)
    {
      mmon_Gl->resize_stat (thread_p, stat_id, old_size, new_size);
    }
}

void mmon_aggregate_server_info (MMON_SERVER_INFO &info)
{
  return;
}

int mmon_aggregate_module_info (MMON_MODULE_INFO *&info, int module_index)
{
  int error = NO_ERROR;

  return error;
}

int mmon_aggregate_module_info_summary (MMON_MODULE_INFO *&info)
{
  return NO_ERROR;
}

int mmon_aggregate_tran_info (MMON_TRAN_INFO &info, int tran_count)
{
  int error = NO_ERROR;

  return error;
}
