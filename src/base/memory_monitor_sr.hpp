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
 * memory_monitor_sr.hpp - memory monitoring module header
 */

#ifndef _MEMORY_MONITOR_SR_HPP_
#define _MEMORY_MONITOR_SR_HPP_

#if !defined (SERVER_MODE)
#error SERVER_MODE macro should be pre-defined(to compile)
#endif /* SERVER_MODE */

#include <cstring>
#include <type_traits>
#include <cstdint>
#include <atomic>
#include <vector>
#include <cassert>

#include "perf_def.hpp"
#include "thread_compat.hpp"
#include "memory_monitor_common.h"

#define MMM_PARSE_MASK 0x0000FFFF
#define MMM_MAKE_INIT_STAT_ID_FOR_MODULE(module_idx) ((module_idx) << 16)
#define MMM_GET_MODULE_IDX_FROM_STAT_ID(stat) ((stat) >> 16)
#define MMM_GET_COMP_INFO_IDX_FROM_STAT_ID(stat) ((stat) & MMM_PARSE_MASK)

typedef enum
{
  MMM_STAT_LAST = MMM_MAKE_INIT_STAT_ID_FOR_MODULE (MMM_MODULE_LAST)
} MMM_STAT_ID;

/* APIs */
int memmon_add_stat (THREAD_ENTRY *thread_p, MMM_STAT_ID stat_id, uint64_t size);
int memmon_sub_stat (THREAD_ENTRY *thread_p, MMM_STAT_ID stat_id, uint64_t size);
int memmon_move_stat (THREAD_ENTRY *thread_p, MMM_STAT_ID src, MMM_STAT_ID dest, uint64_t size);
int memmon_resize_stat (THREAD_ENTRY *thread_p, MMM_STAT_ID stat_id, uint64_t old_size, uint64_t new_size);

namespace cubperf
{
  typedef struct mmm_comp_info
  {
    MMM_STAT_ID id;
    const char *comp_name;
    const char *subcomp_name;
  } MMM_COMP_INFO;

  typedef struct mmm_mem_stat
  {
    std::atomic<uint64_t> init_stat;
    std::atomic<uint64_t> cur_stat;
    std::atomic<uint64_t> peak_stat;
    std::atomic<uint32_t> expand_count;
  } MMM_MEM_STAT;

  class mmm_subcomponent
  {
    public:
      mmm_subcomponent (const char *subcomp_name)
	: m_subcomp_name (subcomp_name) {}
      mmm_subcomponent (const mmm_subcomponent &) = delete;
      mmm_subcomponent (mmm_subcomponent &) = delete;

      ~mmm_subcomponent () {}

      mmm_subcomponent &operator = (const mmm_subcomponent &) = delete;
      mmm_subcomponent &operator = (mmm_subcomponent &) = delete;

      /* function */
      const char *get_name ();
      void add_cur_stat (uint64_t size);
      void sub_cur_stat (uint64_t size);

    private:
      std::string m_subcomp_name;
      std::atomic<uint64_t> m_cur_stat;
  };

  class mmm_component
  {
    public:
      mmm_component (const char *comp_name)
	: m_comp_name (comp_name) {}
      mmm_component (const mmm_component &) = delete;
      mmm_component (mmm_component &) = delete;

      ~mmm_component () {}

      mmm_component &operator = (const mmm_component &) = delete;
      mmm_component &operator = (mmm_component &) = delete;

      /* function */
      const char *get_name ();
      void add_stat (uint64_t size, int subcomp_idx, bool init, bool expand);
      void sub_stat (uint64_t size, int subcomp_idx, bool init);
      int is_subcomp_exist (const char *subcomp_name);
      int add_subcomponent (const char *name);

    private:
      std::string m_comp_name;
      MMM_MEM_STAT m_stat;
      std::vector<std::unique_ptr<mmm_subcomponent>> m_subcomponent;
  };

  class mmm_module
  {
    public:
      /* const */
      /* max index of component or subcomponent is 0x0000FFFF
       * if some stats have max index of component or subcomponent,
       * we don't have to increase it */
      static constexpr int MAX_COMP_IDX = 0x0000FFFF;

    public:
      mmm_module (const char *module_name, const MMM_COMP_INFO *info);
      mmm_module () {} /* for dummy */
      mmm_module (const mmm_module &) = delete;
      mmm_module (mmm_module &) = delete;

      virtual ~mmm_module () {}

      mmm_module &operator = (const mmm_module &) = delete;
      mmm_module &operator = (mmm_module &) = delete;

      /* function */
      virtual int aggregate_stats (const MEMMON_MODULE_INFO &info);
      void add_stat (uint64_t size, int comp_idx, int subcomp_idx, bool init, bool expand);
      void sub_stat (uint64_t size, int comp_idx, int subcomp_idx, bool init);
      int add_component (const char *name);
      inline int MAKE_STAT_IDX_MAP (int comp_idx, int subcomp_idx);

    private:
      std::string m_module_name;
      MMM_MEM_STAT m_stat;
      std::vector<std::unique_ptr<mmm_component>> m_component;
      std::vector<int> m_comp_idx_map;
  };


  class memory_monitoring_manager
  {
    public:
      memory_monitoring_manager (const char *server_name)
	: m_aggregater (this), m_server_name (server_name) {}
      memory_monitoring_manager (const memory_monitoring_manager &) = delete;
      memory_monitoring_manager (memory_monitoring_manager &) = delete;

      ~memory_monitoring_manager ();

      memory_monitoring_manager &operator = (const memory_monitoring_manager &) = delete;
      memory_monitoring_manager &operator = (memory_monitoring_manager &) = delete;

      /* function */
      int add_stat (THREAD_ENTRY *thread_p, MMM_STAT_ID stat_id, uint64_t size, bool expand);
      int sub_stat (THREAD_ENTRY *thread_p, MMM_STAT_ID stat_id, uint64_t size);
      int resize_stat (THREAD_ENTRY *thread_p, MMM_STAT_ID stat_id, uint64_t old_size, uint64_t new_size);
      int move_stat (THREAD_ENTRY *thread_p, MMM_STAT_ID src, MMM_STAT_ID dest, uint64_t size);
      int aggregate_module_info (MEMMON_MODULE_INFO *info, int module_index);
      int aggregate_tran_info (MEMMON_TRAN_INFO *info, int tran_count);
      inline int COMP_IDX_FROM_COMP_IDX_MAP (int comp_idx_map_val);
      inline int SUBCOMP_IDX_FROM_COMP_IDX_MAP (int comp_idx_map_val);

    private:
      class mmm_aggregater
      {
	public:
	  mmm_aggregater (memory_monitoring_manager *mmm);
	  mmm_aggregater (const mmm_aggregater &) = delete;
	  mmm_aggregater (mmm_aggregater &) = delete;

	  ~mmm_aggregater () {}

	  mmm_aggregater &operator = (const mmm_aggregater &) = delete;
	  mmm_aggregater &operator = (mmm_aggregater &) = delete;

	  /* function */
	  int get_server_info (const MEMMON_SERVER_INFO &info);
	  int get_module_info (const MEMMON_MODULE_INFO &info, int module_index);
	  int get_transaction_info (const MEMMON_TRAN_INFO &info, int tran_count);

	private:
	  /* backlink of memory_monitoring_manager class */
	  memory_monitoring_manager *m_memmon_mgr;
      };

    private:
      const char *m_server_name;
      std::atomic<uint64_t> m_total_mem_usage;
      mmm_aggregater m_aggregater;

      mmm_module *m_module[MMM_MODULE_LAST] =
      {
	new mmm_module ()				  /* dummy for aligning module index (1 ~) */
      };
  };

  inline int mmm_module::MAKE_STAT_IDX_MAP (int comp_idx, int subcomp_idx)
  {
    return (comp_idx << 16 | subcomp_idx);
  }

  inline int memory_monitoring_manager::COMP_IDX_FROM_COMP_IDX_MAP (int comp_idx_map_val)
  {
    return (comp_idx_map_val >> 16);
  }

  inline int memory_monitoring_manager::SUBCOMP_IDX_FROM_COMP_IDX_MAP (int comp_idx_map_val)
  {
    return (comp_idx_map_val & MMM_PARSE_MASK);
  }

  extern memory_monitoring_manager *mmm_Gl;
} /* namespace cubperf */

#endif /* _MEMORY_MONITOR_SR_HPP_ */

