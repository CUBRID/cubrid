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

#define MMON_PARSE_MASK 0x0000FFFF
#define MMON_MAKE_INIT_STAT_ID_FOR_MODULE(module_idx) ((module_idx) << 16)
#define MMON_GET_MODULE_IDX_FROM_STAT_ID(stat) ((stat) >> 16)
#define MMON_GET_COMP_INFO_IDX_FROM_STAT_ID(stat) ((stat) & MMON_PARSE_MASK)

typedef enum
{
  MMON_STAT_LAST = MMON_MAKE_INIT_STAT_ID_FOR_MODULE (MMON_MODULE_LAST)
} MMON_STAT_ID;

/* APIs */
int mmon_add_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size);
int mmon_sub_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size);
int mmon_move_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID src, MMON_STAT_ID dest, uint64_t size);
int mmon_resize_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t old_size, uint64_t new_size);

namespace cubperf
{
  typedef struct mmon_comp_info
  {
    MMON_STAT_ID id;
    const char *comp_name;
    const char *subcomp_name;
  } MMON_COMP_INFO;

  typedef struct mmon_stat
  {
    std::atomic<uint64_t> init_stat;
    std::atomic<uint64_t> cur_stat;
    std::atomic<uint64_t> peak_stat;
    std::atomic<uint32_t> expand_count;
  } MMON_STAT;

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

      /* function */
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

      /* function */
      const char *get_name ();
      void add_stat (uint64_t size, int subcomp_idx, bool init, bool expand);
      void sub_stat (uint64_t size, int subcomp_idx, bool init);
      int get_subcomp_index (const char *subcomp_name);
      int add_subcomponent (const char *name);

    private:
      std::string m_comp_name;
      MMON_STAT m_stat;
      std::vector<std::unique_ptr<mmon_subcomponent>> m_subcomponent;
  };

  class mmon_module
  {
    public:
      mmon_module (const char *module_name, const MMON_COMP_INFO *info);
      mmon_module () {} /* for dummy */
      mmon_module (const mmon_module &) = delete;
      mmon_module (mmon_module &&) = delete;

      virtual ~mmon_module () {}

      mmon_module &operator = (const mmon_module &) = delete;
      mmon_module &operator = (mmon_module &&) = delete;

      /* function */
      virtual int aggregate_stats (const MMON_MODULE_INFO &info);
      void add_stat (uint64_t size, int comp_idx, int subcomp_idx, bool init, bool expand);
      void sub_stat (uint64_t size, int comp_idx, int subcomp_idx, bool init);
      int add_component (const char *name);
      inline int MAKE_STAT_IDX_MAP (int comp_idx, int subcomp_idx);

    private:
      /* const */
      /* max index of component or subcomponent is 0x0000FFFF
       * if some stats have max index of component or subcomponent,
       * we don't have to increase it */
      static constexpr int MAX_COMP_IDX = 0x0000FFFF;

    private:
      std::string m_module_name;
      MMON_STAT m_stat;
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

      /* function */
      int add_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size, bool expand);
      int sub_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size);
      int resize_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t old_size, uint64_t new_size);
      int move_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID src, MMON_STAT_ID dest, uint64_t size);
      int aggregate_module_info (MMON_MODULE_INFO *info, int module_index);
      int aggregate_tran_info (MMON_TRAN_INFO *info, int tran_count);
      inline int COMP_IDX_FROM_COMP_IDX_MAP (int comp_idx_map_val);
      inline int SUBCOMP_IDX_FROM_COMP_IDX_MAP (int comp_idx_map_val);

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

	  /* function */
	  int get_server_info (const MMON_SERVER_INFO &info);
	  int get_module_info (const MMON_MODULE_INFO &info, int module_index);
	  int get_transaction_info (const MMON_TRAN_INFO &info, int tran_count);

	private:
	  /* backlink of memory_monitor class */
	  memory_monitor *m_mmon;
      };

    private:
      std::string m_server_name;
      std::atomic<uint64_t> m_total_mem_usage;
      aggregater m_aggregater;

      std::unique_ptr<mmon_module> m_module[MMON_MODULE_LAST];
  };

  inline int mmon_module::MAKE_STAT_IDX_MAP (int comp_idx, int subcomp_idx)
  {
    return (comp_idx << 16 | subcomp_idx);
  }

  inline int memory_monitor::COMP_IDX_FROM_COMP_IDX_MAP (int comp_idx_map_val)
  {
    return (comp_idx_map_val >> 16);
  }

  inline int memory_monitor::SUBCOMP_IDX_FROM_COMP_IDX_MAP (int comp_idx_map_val)
  {
    return (comp_idx_map_val & MMON_PARSE_MASK);
  }

  extern memory_monitor *mmon_Gl;
} /* namespace cubperf */

#endif /* _MEMORY_MONITOR_SR_HPP_ */

