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

#if !defined (NDEBUG)
thread_local char mmon_err_msg[DB_MAX_IDENTIFIER_LENGTH];
thread_local int mmon_err_msg_size = 0;
#endif

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

  MMON_STAT_INFO heap_stat_info[] =
  {
    {MMON_HEAP_SCAN, "SCANCACHE", NULL},
    {MMON_HEAP_BESTSPACE, "BESTSPACE CACHE", NULL},
    {MMON_HEAP_CLASSREPR, "CLASSREPR CACHE", NULL},
    {MMON_HEAP_CLASSREPR_HASH, "CLASSREPR CACHE", "HASHTABLE"},
    {MMON_HEAP_ATTRINFO, "ATTRINFO CACHE", NULL},
    {MMON_HEAP_HFIDTABLE, "HFID TABLE", NULL},
    {MMON_HEAP_HFIDTABLE_HASH, "HFID TABLE", "HASHTABLE"},
    {MMON_HEAP_CHNGUESS, "CHNGUESS", NULL},
    {MMON_HEAP_CHNGUESS_HASH, "CHNGUESS", "HASHTABLE"},
    {MMON_HEAP_OTHERS, "OTHERS", NULL},
    {MMON_STAT_LAST, NULL, NULL} /* for recognizing end */
  };

  MMON_STAT_INFO common_stat_info[] =
  {
    {MMON_COMMON, NULL, NULL},
    {MMON_STAT_LAST, NULL, NULL}
  };

  MMON_STAT_INFO others_stat_info[] =
  {
    {MMON_OTHERS, NULL, NULL},
    {MMON_STAT_LAST, NULL, NULL}
  };

  class mmon_subcomponent
  {
    public:
      mmon_subcomponent (const char *subcomp_name)
	: m_subcomp_name {subcomp_name}
	, m_cur_stat {0}
      {}
      mmon_subcomponent (const mmon_subcomponent &) = delete;
      mmon_subcomponent (mmon_subcomponent &&) = delete;

      ~mmon_subcomponent () {}

      mmon_subcomponent &operator = (const mmon_subcomponent &) = delete;
      mmon_subcomponent &operator = (mmon_subcomponent &&) = delete;

      const char *get_name ();
      void add_cur_stat (int64_t size);
      void get_stat (MMON_SUBCOMP_INFO &info) const;

    private:
      std::string m_subcomp_name;
      std::atomic<uint64_t> m_cur_stat;
  };

  class mmon_component
  {
    public:
      mmon_component (const char *comp_name)
	: m_comp_name {comp_name}
	, m_stat {0, 0, 0, 0}
      {}
      mmon_component (const mmon_component &) = delete;
      mmon_component (mmon_component &&) = delete;

      ~mmon_component () {}

      mmon_component &operator = (const mmon_component &) = delete;
      mmon_component &operator = (mmon_component &&) = delete;

      const char *get_name ();
      void add_stat (int subcomp_idx, int64_t size);
      void add_expand_resize_count ();
      void end_init_phase ();
      void get_stat (MMON_COMP_INFO &info) const;
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

      virtual void aggregate_stats (bool is_summary, MMON_MODULE_INFO &info) const;
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
      memory_monitor (const char *server_name);
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
      void aggregate_server_info (MMON_SERVER_INFO &info) const;
      void aggregate_module_info (int module_index, std::vector<MMON_MODULE_INFO> &info) const;
      void aggregate_module_info_summary (std::vector<MMON_MODULE_INFO> &info) const;
      void aggregate_tran_info (int tran_count, MMON_TRAN_INFO &info) const;
      void stat_dump_debug ();

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

	  void get_server_info (MMON_SERVER_INFO &info) const;
	  void get_module_info (int module_index, std::vector<MMON_MODULE_INFO> &info) const;
	  void get_module_info_summary (std::vector<MMON_MODULE_INFO> &info) const;
	  void get_transaction_info (int tran_count, MMON_TRAN_INFO &info) const;

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
    return (int) (((uint32_t) comp_idx_map_val) >> 16);
  }

  inline int mmon_module::get_subcomp_idx (int comp_idx_map_val) const
  {
    return (comp_idx_map_val & MMON_PARSE_MASK);
  }

  inline int memory_monitor::get_module_idx (MMON_STAT_ID stat_id) const
  {
    return (int) (((uint32_t) stat_id) >> 16);
  }

  const char *mmon_subcomponent::get_name ()
  {
    return m_subcomp_name.c_str ();
  }

  void mmon_subcomponent::add_cur_stat (int64_t size)
  {
    assert ((size >= 0) || ((uint64_t) (-size) <= m_cur_stat));

    m_cur_stat += size;

#if !defined (NDEBUG)
    mmon_err_msg_size += sprintf (mmon_err_msg + mmon_err_msg_size,
				  "_%s", m_subcomp_name.c_str ());
#endif
  }

  void mmon_subcomponent::get_stat (MMON_SUBCOMP_INFO &info) const
  {
    strncpy (info.name, m_subcomp_name.c_str (), m_subcomp_name.size ());
    info.cur_stat = m_cur_stat.load ();
  }

  const char *mmon_component::get_name ()
  {
    return m_comp_name.c_str ();
  }

  void mmon_component::add_stat (int subcomp_idx, int64_t size)
  {
    assert ((size >= 0) || ((uint64_t) (-size) <= m_stat.cur_stat));

    m_stat.cur_stat += size;

#if !defined (NDEBUG)
    mmon_err_msg_size += sprintf (mmon_err_msg + mmon_err_msg_size,
				  "_%s", m_comp_name.c_str ());
#endif

    if (m_stat.cur_stat > m_stat.peak_stat)
      {
	m_stat.peak_stat = m_stat.cur_stat.load ();
      }

    /* If there is a subcomponent info */
    if (subcomp_idx != mmon_module::MAX_COMP_IDX)
      {
	assert ((size_t)subcomp_idx < m_subcomponent.size ());
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

  void mmon_component::get_stat (MMON_COMP_INFO &info) const
  {
    strncpy (info.name, m_comp_name.c_str (), m_comp_name.size ());

    info.stat.init_stat = m_stat.init_stat.load ();
    info.stat.cur_stat = m_stat.cur_stat.load ();
    info.stat.peak_stat = m_stat.peak_stat.load ();
    info.stat.expand_resize_count = m_stat.expand_resize_count.load ();

    info.num_subcomp = m_subcomponent.size ();
    info.subcomp_info.resize (info.num_subcomp);

    for (uint32_t i = 0; i < info.num_subcomp; i++)
      {
	m_subcomponent[i]->get_stat (info.subcomp_info[i]);
      }
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
    : m_module_name {module_name}
    , m_stat {0, 0, 0, 0}
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

  void mmon_module::aggregate_stats (bool is_summary, MMON_MODULE_INFO &info) const
  {
    int error = NO_ERROR;

    strncpy (info.name, m_module_name.c_str (), m_module_name.size ());

    if (is_summary)
      {
	info.stat.cur_stat = m_stat.cur_stat.load ();
      }
    else
      {
	info.stat.init_stat = m_stat.init_stat.load ();
	info.stat.cur_stat = m_stat.cur_stat.load ();
	info.stat.peak_stat = m_stat.peak_stat.load ();
	info.stat.expand_resize_count = m_stat.expand_resize_count.load ();


	info.num_comp = m_component.size ();
	info.comp_info.resize (info.num_comp);

	for (uint32_t i = 0; i < info.num_comp; i++)
	  {
	    m_component[i]->get_stat (info.comp_info[i]);
	  }
      }
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

    assert ((size >= 0) || ((uint64_t) (-size) <= m_stat.cur_stat));

    get_comp_and_subcomp_idx (stat_id, comp_idx, subcomp_idx);

    m_stat.cur_stat += size;

#if !defined (NDEBUG)
    mmon_err_msg_size = sprintf (mmon_err_msg, "[MMON-ADD] %s", m_module_name.c_str ());
#endif

    if (m_stat.cur_stat > m_stat.peak_stat)
      {
	m_stat.peak_stat = m_stat.cur_stat.load ();
      }

    /* If there is a component info */
    if (comp_idx != mmon_module::MAX_COMP_IDX)
      {
	assert ((size_t)comp_idx < m_component.size ());
	m_component[comp_idx]->add_stat (subcomp_idx, size);
      }
#if !defined (NDEBUG)
    mmon_err_msg_size += sprintf (mmon_err_msg + mmon_err_msg_size, ": %ld\n", size);
    er_log_debug (ARG_FILE_LINE, mmon_err_msg);
    memset (mmon_err_msg, 0, DB_MAX_IDENTIFIER_LENGTH);
#endif
  }

  void mmon_module::add_expand_resize_count (MMON_STAT_ID stat_id)
  {
    int comp_idx, subcomp_idx;

    m_stat.expand_resize_count++;

    get_comp_and_subcomp_idx (stat_id, comp_idx, subcomp_idx);

    /* If there is a component info */
    if (comp_idx != mmon_module::MAX_COMP_IDX)
      {
	assert ((size_t)comp_idx < m_component.size ());
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

  void memory_monitor::aggregater::get_server_info (MMON_SERVER_INFO &info) const
  {
    strncpy (info.name, m_mmon->m_server_name.c_str (), m_mmon->m_server_name.size ());
    info.total_mem_usage = m_mmon->m_total_mem_usage.load ();

    // TODO: It's a temporary measure for hiding MMON_OTHERS
    MMON_MODULE_INFO module_info;
    m_mmon->m_module[MMON_MODULE_OTHERS]->aggregate_stats (true, module_info);
    info.total_mem_usage -= module_info.stat.cur_stat;
  }

  void memory_monitor::aggregater::get_module_info (int module_index, std::vector<MMON_MODULE_INFO> &info) const
  {
    if (module_index == MMON_MODULE_ALL)
      {
	// aggregate all detail memory information of modules
	for (int idx = 0; idx < MMON_MODULE_LAST; idx++)
	  {
	    m_mmon->m_module[idx]->aggregate_stats (false, info[idx]);
	  }

	const auto &comp = [] (const auto& module_info1, const auto& module_info2)
	{
	  return module_info1.stat.cur_stat > module_info2.stat.cur_stat;
	};
	std::sort (info.begin (), info.end (), comp);
      }
    else
      {
	m_mmon->m_module[module_index]->aggregate_stats (false, info[0]);
      }
  }

  void memory_monitor::aggregater::get_module_info_summary (std::vector<MMON_MODULE_INFO> &info) const
  {
    for (int i = 0; i < MMON_MODULE_LAST; i++)
      {
	m_mmon->m_module[i]->aggregate_stats (true, info[i]);
      }

    const auto &comp = [] (const auto& module_info1, const auto& module_info2)
    {
      return module_info1.stat.cur_stat > module_info2.stat.cur_stat;
    };
    std::sort (info.begin (), info.end (), comp);
  }

  void memory_monitor::aggregater::get_transaction_info (int tran_count, MMON_TRAN_INFO &info) const
  {
    LOG_TRAN_MEM_INFO tran_info;

    logtb_get_tran_memory_info_nolatch (tran_info);

    info.num_tran = std::min (tran_count, (int) tran_info.size ());

    // sort by memory usage in descending order
    const auto &comp = [] (const auto& tran_pair1, const auto& tran_pair2)
    {
      return tran_pair1.second > tran_pair2.second;
    };
    std::sort (tran_info.begin (), tran_info.end (), comp);

    info.tran_stat.resize (info.num_tran);

    for (uint32_t i = 0; i < info.num_tran; i++)
      {
	info.tran_stat[i].tranid = tran_info[i].first;
	info.tran_stat[i].cur_stat = tran_info[i].second;
      }
  }

  memory_monitor::memory_monitor (const char *server_name)
    : m_server_name {server_name}
    , m_total_mem_usage {0}
    , m_aggregater {this}
  {
    m_module[MMON_MODULE_HEAP] = std::make_unique<mmon_module>
				 (module_names[MMON_MODULE_HEAP], heap_stat_info);
    m_module[MMON_MODULE_COMMON] = std::make_unique<mmon_module>
				   (module_names[MMON_MODULE_COMMON], common_stat_info);
    m_module[MMON_MODULE_OTHERS] = std::make_unique<mmon_module>
				   (module_names[MMON_MODULE_OTHERS], others_stat_info);
  }

  void memory_monitor::add_stat (MMON_STAT_ID stat_id, int64_t size)
  {
    int module_idx = get_module_idx (stat_id);

    assert ((size >= 0) || ((uint64_t) (-size) <= m_total_mem_usage));

    m_total_mem_usage += size;

    m_module[module_idx]->add_stat (stat_id, size);
  }

  void memory_monitor::add_tran_stat (THREAD_ENTRY *thread_p, int64_t size)
  {
    if (thread_p)
      {
	LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);

	/* there are some case that has no LOG_TDES in THREAD_ENTRY
	 * when server booting */
	if (tdes)
	  {
	    tdes->cur_mem_usage += size;
	  }
      }
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

  void memory_monitor::aggregate_server_info (MMON_SERVER_INFO &info) const
  {
    m_aggregater.get_server_info (info);
  }

  void memory_monitor::aggregate_module_info (int module_index, std::vector<MMON_MODULE_INFO> &info) const
  {
    // If utility module index == MMON_MODULE_ALL, it means show all detailed info of module.
    m_aggregater.get_module_info (module_index, info);
  }

  void memory_monitor::aggregate_module_info_summary (std::vector<MMON_MODULE_INFO> &info) const
  {
    m_aggregater.get_module_info_summary (info);
  }

  void memory_monitor::aggregate_tran_info (int tran_count, MMON_TRAN_INFO &info) const
  {
    m_aggregater.get_transaction_info (tran_count, info);
  }

  void memory_monitor::stat_dump_debug ()
  {
    MMON_SERVER_INFO server_info;
    std::vector<MMON_MODULE_INFO> module_info;
    MMON_TRAN_INFO tran_info;
    const auto init_size_str = std::string {"Initial Size(KB)"};
    const auto cur_size_str = std::string {"Current Size(KB)"};
    const auto peak_size_str = std::string {"Peak Size(KB)"};
    const auto resize_expand_str = std::string {"Resize Expand Count"};
    FILE *fp = fopen ("memory_monitor_dump.txt", "w+");

    auto MMON_CONVERT_TO_KB_SIZE = [] (uint64_t size)
    {
      return ((size) / 1024);
    };

    module_info.resize (MMON_MODULE_LAST);

    m_aggregater.get_server_info (server_info);
    m_aggregater.get_module_info ((int) MMON_MODULE_ALL, module_info);

    fprintf (fp, "====================cubrid memmon====================\n");
    fprintf (fp, "Server Name: %s\n", server_info.name);
    fprintf (fp, "Total Memory Usage(KB): %lu\n\n", server_info.total_mem_usage);
    fprintf (fp, "-----------------------------------------------------\n");

    for (const auto &m_info : module_info)
      {
	fprintf (fp, "Module Name: %s\n\n", m_info.name);
	fprintf (fp, "%-19s\t: %17lu\n", init_size_str.c_str (), m_info.stat.init_stat);
	fprintf (fp, "%-19s\t: %17lu\n", cur_size_str.c_str (), m_info.stat.cur_stat);
	fprintf (fp, "%-19s\t: %17lu\n", peak_size_str.c_str (), m_info.stat.peak_stat);
	fprintf (fp, "%-19s\t: %17u\n\n", resize_expand_str.c_str (), m_info.stat.expand_resize_count);

	for (const auto &comp_info : m_info.comp_info)
	  {
	    fprintf (fp, "%s\n", comp_info.name);
	    fprintf (fp, "\t%-23s\t: %17lu\n", init_size_str.c_str (), comp_info.stat.init_stat);
	    fprintf (fp, "\t%-23s\t: %17lu\n", cur_size_str.c_str (), comp_info.stat.cur_stat);

	    for (const auto &subcomp_info : comp_info.subcomp_info)
	      {
		auto out_name = std::string {subcomp_info.name};
		out_name += "(KB)";
		fprintf (fp, "\t  %-20s\t: %17lu\n", out_name.c_str (), subcomp_info.cur_stat);
	      }
	    fprintf (fp, "\t%-23s\t: %17lu\n", peak_size_str.c_str (), comp_info.stat.peak_stat);
	    fprintf (fp, "\t%-23s\t: %17u\n\n", resize_expand_str.c_str (), comp_info.stat.expand_resize_count);
	  }
	fprintf (fp, "\n-----------------------------------------------------\n\n");
      }

    fclose (fp);
  }
} // namespace cubperf

/* APIs */
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
#if !defined (NDEBUG)
      memset (mmon_err_msg, 0, DB_MAX_IDENTIFIER_LENGTH);
#endif
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
#if !defined (NDEBUG)
      mmon_Gl->stat_dump_debug ();
#endif
      delete mmon_Gl;
      mmon_Gl = nullptr;
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
  assert (mmon_Gl != nullptr);
  mmon_Gl->aggregate_server_info (info);
}

void mmon_aggregate_module_info (int module_index, std::vector<MMON_MODULE_INFO> &info)
{
  assert (mmon_Gl != nullptr);
  assert (module_index < (int)MMON_MODULE_LAST && module_index >= (int)MMON_MODULE_ALL);

  mmon_Gl->aggregate_module_info (module_index, info);
}

void mmon_aggregate_module_info_summary (std::vector<MMON_MODULE_INFO> &info)
{
  assert (mmon_Gl != nullptr);

  mmon_Gl->aggregate_module_info_summary (info);
}

void mmon_aggregate_tran_info (int tran_count, MMON_TRAN_INFO &info)
{
  assert (mmon_Gl != nullptr);

  mmon_Gl->aggregate_tran_info (tran_count, info);
}

MMON_STAT_ID mmon_set_tracking_tag (MMON_STAT_ID new_tag)
{
  MMON_STAT_ID prev_tag = MMON_STAT_LAST;
  THREAD_ENTRY *cur_thread_p;

  if (mmon_Gl != nullptr)
    {
      cur_thread_p = thread_get_thread_entry_info ();
      prev_tag = cur_thread_p->mmon_tracking_tag;
      cur_thread_p->mmon_tracking_tag = new_tag;
    }

  return prev_tag;
}

void mmon_add_stat_with_tracking_tag (int64_t size)
{
  THREAD_ENTRY *cur_thread_p;

  if (mmon_Gl != nullptr)
    {
      cur_thread_p = thread_get_thread_entry_info ();
      assert (cur_thread_p->mmon_tracking_tag != MMON_STAT_LAST);
      mmon_add_stat (cur_thread_p, cur_thread_p->mmon_tracking_tag, size);
    }
}

void mmon_sub_stat_with_tracking_tag (int64_t size)
{
  THREAD_ENTRY *cur_thread_p;

  if (mmon_Gl != nullptr)
    {
      cur_thread_p = thread_get_thread_entry_info ();
      assert (cur_thread_p->mmon_tracking_tag != MMON_STAT_LAST);
      mmon_sub_stat (cur_thread_p, cur_thread_p->mmon_tracking_tag, size);
    }
}

void mmon_move_stat_with_tracking_tag (int64_t size, bool tag_is_src)
{
  THREAD_ENTRY *cur_thread_p;

  if (mmon_Gl != nullptr)
    {
      cur_thread_p = thread_get_thread_entry_info ();
      assert (cur_thread_p->mmon_tracking_tag != MMON_STAT_LAST);

      /* WARNING: this function is only for base data structures that can use MMON_COMMON tag
       * If move is occurred from other modules to MMON_COMMON,
       * it means that memory is returned to common structures for keeping.
       * ex) pool, free list, reserved, etc..
       * In this case, transaction's memory usage will be subtracted. */
      if (tag_is_src)
	{
	  mmon_move_stat (cur_thread_p, cur_thread_p->mmon_tracking_tag, MMON_COMMON, size);
	  mmon_Gl->add_tran_stat (cur_thread_p, - (size));
	}
      else
	{
	  mmon_move_stat (cur_thread_p, MMON_COMMON, cur_thread_p->mmon_tracking_tag, size);
	  mmon_Gl->add_tran_stat (cur_thread_p, size);
	}
    }
}

void mmon_resize_stat_with_tracking_tag (int64_t old_size, int64_t new_size)
{
  THREAD_ENTRY *cur_thread_p;

  if (mmon_Gl != nullptr)
    {
      cur_thread_p = thread_get_thread_entry_info ();
      assert (cur_thread_p->mmon_tracking_tag != MMON_STAT_LAST);
      mmon_resize_stat (cur_thread_p, cur_thread_p->mmon_tracking_tag, old_size, new_size);
    }
}
