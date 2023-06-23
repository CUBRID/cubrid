/*
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
 * memory_monitor.cpp - implementation of memory monitoring manager
 */

#include "perf.hpp"

#include "error_manager.h"

#include "memory_monitor_sr.hpp"
#if defined (SERVER_MODE)
#include "log_impl.h"
#endif

#include <stdexcept>

#include <cstring>

#ifdef SERVER_MODE
namespace cubperf
{
  MMM *MMM_global = NULL;
#if defined(NDEBUG)
  std::atomic<uint64_t> test_meminfo;
#endif
  MMM_module::MMM_module (char *name, MMM_comp_info *info)
  {
    m_module_name = new char[strlen (name) + 1];
    strcpy (m_module_name, name);

    /* register component and subcomponent information
     * add component or subcomponent */
    int cnt = 0;
    while (info[cnt].idx != MMM_STAT_END)
      {
	bool comp_skip = false;
	bool subcomp_skip = false;
	int comp_idx = this->m_max_idx, subcomp_idx = this->m_max_idx;
	int i;
	if (info[cnt].comp_name.size())
	  {
	    for (i = 0; i < m_component.size(); i++)
	      {
		if (!strcmp (info[cnt].comp_name, m_component[i].m_comp_name))
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

	    if (info[cnt].subcomp_name.size())
	      {
		for (i = 0; i < m_component[comp_idx].m_subcomponent.size(); i++)
		  {
		    if (!strcmp (info[cnt].subcomp_name, m_component[comp_idx].m_subcomponent[i].m_subcomp_name))
		      {
			subcomp_idx = j;
			subcomp_skip = true;
			break;
		      }
		  }

		if (!subcomp_skip)
		  {
		    subcomp_idx = add_subcomponent (info[cnt].subcomp_name);
		  }
	      }
	  }

	m_comp_idx_map.push_back (MMM_MAKE_STAT_IDX_MAP (comp_idx, subcomp_idx));

	cnt++;
      }
  }

  int MMM_module::aggregate_stats (MEMMON_MODULE_INFO *info)
  {
    return 0;
  }

  int MMM_heap_module::aggregate_stats (MEMMON_MODULE_INFO *info)
  {
    return 0;
#if 0
    //타고타고내려가면서 출력
    char temp[512] = { 0, };
    sprintf (temp, "Module Memory Usage Info : %s\n\n", module_name);
    strcat (buffer, temp);

    sprintf (temp, "Init Size (kb)\t: %lu\n", base_stat[0].init_stat.load());
    strcat (buffer, temp);
    sprintf (temp, "Cur Size (kb)\t: %lu\n", base_stat[0].cur_stat.load());
    strcat (buffer, temp);
    sprintf (temp, "Peak Size (kb)\t: %lu\n", base_stat[0].peak_stat.load());
    strcat (buffer, temp);
    sprintf (temp, "Expand Occur\t: %lu\n", base_stat[0].expands.load());
    strcat (buffer, temp);

    for (int i = 1; i <= TOTAL_COMP; i++)
      {
	sprintf (temp, "%s\n\t", comp_list[i]);
	strcat (buffer, temp);
	sprintf (temp, "Init Size (kb)\t: %lu\n\t", base_stat[i].init_stat.load());
	strcat (buffer, temp);
	sprintf (temp, "Cur Size (kb)\t: %lu\n\t", base_stat[i].cur_stat.load());
	strcat (buffer, temp);

	for (int idx = 0; idx < TOTAL_SUBCOMP; idx++)
	  {
	    if (subcomp[idx]->comp_index != i)
	      {
		continue;
	      }

	    sprintf (temp, "\t%s (kb)\t: %lu\n\t", subcomp[idx]->subcomp_name, subcomp[idx]->cur_stat.load());
	    strcat (buffer, temp);
	  }
	sprintf (temp, "Peak Size (kb)\t: %lu\n\t", base_stat[i].peak_stat.load());
	strcat (buffer, temp);
	sprintf (temp, "Expand Occur\t: %lu\n", base_stat[i].expands.load());
	strcat (buffer, temp);
      }
#endif
  }

  int MMM_aggregater::get_server_info (MEMMON_SERVER_INFO *info)
  {
#if 0
    //aa
    char temp[512] = { 0, };
    sprintf (temp, "print baseline of memmon\n");
    strcat (buffer, temp);
    sprintf (temp, "Server name: %s\n", this->mmm->server_name);
    strcat (buffer, temp);
    sprintf (temp, "server memory: %lu\n", this->mmm->total_mem_usage.load());
    strcat (buffer, temp);
#endif
    return 0;
  }

  int MMM_aggregater::get_module_info (MEMMON_MODULE_INFO *info, int module_index)
  {
    return 0;
  }

  int MMM_aggregater::get_transaction_info (MEMMON_TRAN_INFO *info, int tran_count)
  {
#if 0
    char temp[512] = { 0, };
    int num_total_indices = 0;
    LOG_TDES **trantable;
    LOG_TDES *tdes = NULL;
    std::vector <std::pair<int, uint64_t>> tran_meminfo;
    //tdes 배열 불러와서 active한 애들 다 출력
    trantable = xlogtb_get_trantable_nolatch (&num_total_indices);

    sprintf (temp, "Transaction Memory Usage Info\n");
    strcat (buffer, temp);
    sprintf (temp, "   Transaction ID    |  Memory Usage (kb)\n");
    strcat (buffer, temp);

    //print all live transaction's meminfo
    for (int i = 0; i < num_total_indices; i++)
      {
	tdes = trantable[i];
	if (tdes != NULL && tdes->trid != NULL_TRANID && tdes->state == TRAN_ACTIVE)
	  {
	    if (display_size == 0)
	      {
		sprintf (temp, "%20d | %20lu\n", tdes->trid, tdes->cur_meminfo.load());
		strcat (buffer, temp);
	      }
	    else
	      {
		tran_meminfo.push_back (std::make_pair (tdes->trid, tdes->cur_meminfo.load()));
	      }
	  }
      }

    if (display_size != 0)
      {
	qsort (&tran_meminfo.front(), tran_meminfo.size(), sizeof (tran_meminfo[0]), this->comp);
	for (int i = 0; i < display_size; i++)
	  {
	    sprintf (temp, "%20d | %20lu\n", tran_meminfo[i].first, tran_meminfo[i].second);
	    strcat (buffer, temp);
	  }
      }
#endif
    return 0;
  }

#if 0
  void MMM_printer::print_help (char *buffer)
  {
    sprintf (buffer, "CUBRID Memory Monitoring Utility\n\n"
	     "cubrid memmon <option> <server_name>\n\n"
	     "memmon will show you basic information\n"
	     "if you don't enter an option(s).\n"
	     "Ex) cubrid memmon <server_name>\n\n"
	     "<option>\n\t"
	     "-m <module_name>\t: Show detailed memory usage for <module_name>\n\t"
	     "   <brief/b>    \t: Show total memory usage by module for all modules\n\t"
	     "-t              \t: Show memory occupancy for each transaction\n\t"
	     "   --display-list/-d <num>\n\t"
	     "                \t:Show only the top <num> transactions in memory occupancy\n\t"
	     "-a              \t: Show all information\n\t"
	     "-h              \t: Show help\n");
    return;
  }
#endif

  int memory_monitoring_manager::add_stat (THREAD_ENTRY *thread_p, MMM_STATS stat_name, uint64_t size)
  {
    return 0;
  }

  int memory_monitoring_manager::sub_stat (THREAD_ENTRY *thread_p, MMM_STATS stat_name, uint64_t size)
  {
    return 0;
  }

  int memory_monitoring_manager::resize_stat (THREAD_ENTRY *thread_p, MMM_STATS stat_name, uint64_t before_size,
      uint64_t after_size)
  {
    return 0;
  }

  int memory_monitoring_manager::move_stat (THREAD_ENTRY *thread_p, MMM_STATS before_stat_name, MMM_STATS after_stat_name,
      uint64_t size)
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

  /* prototype code (will be removed) */
#if 0
  int MMM::memmon_add_stat (uint64_t size, MMM_STAT_NAME stat_name, THREAD_ENTRY *thread_p, bool init)
  {
    LOG_TDES *tdes;		/* Transaction descriptor */
    TRANID trid = NULL_TRANID;	/* Transaction index */

    //주어진 스탯 네임을 stats_info에서 찾아서 다 넣어줌
    //stat_name의 인덱스와 stats_info의 인덱스는 동일하게
    //init_stat에 누적시키는 조건을 정해야 할 것 같다.
    total_mem_usage.fetch_add (size);

    if (stats_info[stat_name].component_index != 0)
      {
	modules[stats_info[stat_name].module]->base_stat[TYPE_MODULE].cur_stat.fetch_add (size);

	if (init)
	  {
	    modules[stats_info[stat_name].module]->base_stat[TYPE_MODULE].init_stat.fetch_add (size);
	  }

	if (modules[stats_info[stat_name].module]->base_stat[TYPE_MODULE].cur_stat >
	    modules[stats_info[stat_name].module]->base_stat[TYPE_MODULE].peak_stat)
	  modules[stats_info[stat_name].module]->base_stat[TYPE_MODULE].peak_stat.store
	  (modules[stats_info[stat_name].module]->base_stat[TYPE_MODULE].cur_stat.load());

      }

    modules[stats_info[stat_name].module]->base_stat[stats_info[stat_name].component_index].cur_stat.fetch_add (size);

    if (init)
      {
	modules[stats_info[stat_name].module]->base_stat[stats_info[stat_name].component_index].init_stat.fetch_add (size);
      }

    if (modules[stats_info[stat_name].module]->base_stat[stats_info[stat_name].component_index].cur_stat >
	modules[stats_info[stat_name].module]->base_stat[stats_info[stat_name].component_index].peak_stat)
      modules[stats_info[stat_name].module]->base_stat[stats_info[stat_name].component_index].peak_stat.store
      (modules[stats_info[stat_name].module]->base_stat[stats_info[stat_name].component_index].cur_stat.load());

    if (stats_info[stat_name].subcomp_index > -1)
      {
	modules[stats_info[stat_name].module]->subcomp_fetch_add (stats_info[stat_name].subcomp_index, size);
      }

    //그 후 쓰레드에 트랜잭션 여부 확인 후 있다면 LOG_TDES 찾아봐서 누적시켜줌
    if (thread_p)
      {
	tdes = LOG_FIND_TDES (thread_p->tran_index);
	if (tdes != NULL)
	  {
	    tdes->cur_meminfo.fetch_add (size);
	  }
      }
    //peak 상황인지 확인 후 peak면 갱신

    //성공시 0, 에러상황 시 음수
    return 0;
  }

  int MMM::memmon_add_stat (uint64_t size, MMM_STAT_NAME stat_name, THREAD_ENTRY *thread_p)
  {
    LOG_TDES *tdes;		/* Transaction descriptor */
    TRANID trid = NULL_TRANID;	/* Transaction index */
    //auto module = modules[stats_info[stat_name].module];

    //주어진 스탯 네임을 stats_info에서 찾아서 다 넣어줌
    //stat_name의 인덱스와 stats_info의 인덱스는 동일하게
    //init_stat에 누적시키는 조건을 정해야 할 것 같다.
    total_mem_usage.fetch_add (size);

    if (stats_info[stat_name].component_index != 0)
      {
	modules[stats_info[stat_name].module]->base_stat[TYPE_MODULE].cur_stat.fetch_add (size);
      }

    modules[stats_info[stat_name].module]->base_stat[stats_info[stat_name].component_index].cur_stat.fetch_add (size);
    if (stats_info[stat_name].subcomp_index > -1)
      {
	modules[stats_info[stat_name].module]->subcomp_fetch_add (stats_info[stat_name].subcomp_index, size);
      }
    //module->subcomp[stats_info[stat_name].subcomp_index]->cur_stat.fetch_add(size);
    //modules[stats_info[stat_name].module]->subcomp[stats_info[stat_name].subcomp_index]->cur_stat.fetch_add(size);

    //그 후 쓰레드에 트랜잭션 여부 확인 후 있다면 LOG_TDES 찾아봐서 누적시켜줌
    if (thread_p)
      {
	tdes = LOG_FIND_TDES (thread_p->tran_index);
	if (tdes != NULL)
	  {
	    tdes->cur_meminfo.fetch_add (size);
	  }
      }
    //peak 상황인지 확인 후 peak면 갱신

    //성공시 0, 에러상황 시 음수
    return 0;
  }

  int MMM::memmon_sub_stat (uint64_t size, MMM_STAT_NAME stat_name, THREAD_ENTRY *thread_p)
  {
    LOG_TDES *tdes;		/* Transaction descriptor */
    TRANID trid = NULL_TRANID;	/* Transaction index */

    total_mem_usage.fetch_sub (size);

    if (stats_info[stat_name].component_index != 0)
      {
	modules[stats_info[stat_name].module]->base_stat[TYPE_MODULE].cur_stat.fetch_sub (size);
      }

    modules[stats_info[stat_name].module]->base_stat[stats_info[stat_name].component_index].cur_stat.fetch_add (size);
    if (stats_info[stat_name].subcomp_index > -1)
      {
	modules[stats_info[stat_name].module]->subcomp_fetch_sub (stats_info[stat_name].subcomp_index, size);
      }
    //modules[stats_info[stat_name].module]->subcomp[stats_info[stat_name].subcomp_index]->cur_stat.fetch_add(size);

    //그 후 쓰레드에 트랜잭션 여부 확인 후 있다면 LOG_TDES 찾아봐서 누적시켜줌
    if (thread_p)
      {
	tdes = LOG_FIND_TDES (thread_p->tran_index);
	if (tdes != NULL)
	  {
	    tdes->cur_meminfo.fetch_sub (size);
	  }
      }
    //peak 상황인지 확인 후 peak면 갱신

    //성공시 0, 에러상황 시 음수
    return 0;
  }

  int MMM::memmon_resize_stat (uint64_t before_size, uint64_t after_size,
			       MMM_STAT_NAME stat_name, THREAD_ENTRY *thread_p)
  {
    //aa
    total_mem_usage.fetch_sub (before_size);
    total_mem_usage.fetch_add (after_size);

    if (stats_info[stat_name].component_index != 0)
      {
	modules[stats_info[stat_name].module]->base_stat[TYPE_MODULE].cur_stat.fetch_sub (before_size);
	modules[stats_info[stat_name].module]->base_stat[TYPE_MODULE].cur_stat.fetch_add (after_size);
      }

    modules[stats_info[stat_name].module]->base_stat[stats_info[stat_name].component_index].cur_stat.fetch_sub (
	    before_size);
    modules[stats_info[stat_name].module]->base_stat[stats_info[stat_name].component_index].cur_stat.fetch_add (after_size);

    // if subcomp exist, check
    if (stats_info[stat_name].subcomp_index > -1)
      {
	modules[stats_info[stat_name].module]->subcomp_fetch_sub (stats_info[stat_name].subcomp_index, before_size);
	modules[stats_info[stat_name].module]->subcomp_fetch_add (stats_info[stat_name].subcomp_index, after_size);
	//modules[stats_info[stat_name].module]->subcomp[stats_info[stat_name].subcomp_index]->cur_stat.fetch_sub(before_size);
	//modules[stats_info[stat_name].module]->subcomp[stats_info[stat_name].subcomp_index]->cur_stat.fetch_add(after_size);
      }

    //TODO:peak check && tran check
    return 0;
  }

  int MMM::memmon_move_stat (uint64_t size, MMM_STAT_NAME before_stat_name,
			     MMM_STAT_NAME after_stat_name, THREAD_ENTRY *thread_p)
  {
    if (stats_info[before_stat_name].component_index != 0)
      {
	modules[stats_info[before_stat_name].module]->base_stat[TYPE_MODULE].cur_stat.fetch_sub (size);
      }

    if (stats_info[after_stat_name].component_index != 0)
      {
	modules[stats_info[after_stat_name].module]->base_stat[TYPE_MODULE].cur_stat.fetch_add (size);
      }

    //aa
    modules[stats_info[before_stat_name].module]->base_stat[stats_info[before_stat_name].component_index].cur_stat.fetch_sub (
	    size);
    modules[stats_info[after_stat_name].module]->base_stat[stats_info[after_stat_name].component_index].cur_stat.fetch_add (
	    size);

    // if subcomp exist, check
    if (stats_info[before_stat_name].subcomp_index > -1)
      {
	modules[stats_info[before_stat_name].module]->subcomp_fetch_sub (stats_info[before_stat_name].subcomp_index, size);
	//modules[stats_info[before_stat_name].module]->subcomp[stats_info[before_stat_name].subcomp_index]->cur_stat.fetch_sub(size);
      }

    if (stats_info[after_stat_name].subcomp_index > -1)
      {
	modules[stats_info[after_stat_name].module]->subcomp_fetch_add (stats_info[after_stat_name].subcomp_index, size);
	//modules[stats_info[after_stat_name].module]->subcomp[stats_info[after_stat_name].subcomp_index]->cur_stat.fetch_add(size);
      }
    return 0;
  }
  int MMM::print_meminfo (MEMMON_INFO_TYPE type, int module_index, char *module_name, int display_size, char *buffer)
  {
    buffer = (char *)malloc (8192 * 5);
    char *ret = NULL;
    int index;
    int error_code = NO_ERROR;

    switch (type)
      {
      case MEMMON_INFO_HELP:
	printer->print_help (buffer);
	break;
      case MEMMON_INFO_DEFAULT:
	printer->print_base (buffer);
	printer->print_default (buffer);
	break;
      case MEMMON_INFO_SHOWALL:
	printer->print_base (buffer);
	for (int i = 0; i < NUM_MODULES; i++)
	  {
	    modules[i]->print_stats (buffer);
	    //print_module_stats(modules[i], buffer);
	  }
	printer->print_transaction (buffer, display_size);
	break;
      case MEMMON_INFO_MODULE:
	printer->print_base (buffer);
	if (module_index != 0)
	  //print_module_stats(modules[index], buffer);
	  {
	    modules[module_index]->print_stats (buffer);
	  }
	else
	  {
	    index = get_module_index (module_name);
	    modules[index]->print_stats (buffer);
	    //print_module_stats(modules[index], buffer);
	  }
	break;
      case MEMMON_INFO_TRANSACTION:
	printer->print_base (buffer);
	printer->print_transaction (buffer, display_size);
	break;
      case MEMMON_INFO_MODTRANS:
	printer->print_base (buffer);
	if (module_index != 0)
	  //print_module_stats(modules[index], buffer);
	  {
	    modules[module_index]->print_stats (buffer);
	  }
	else
	  {
	    index = get_module_index (module_name);
	    modules[index]->print_stats (buffer);
	    //print_module_stats(modules[index], buffer);
	  }
	printer->print_transaction (buffer, display_size);
	break;
      }
    fprintf (stdout, "test_meminfo: %lu\n", test_meminfo.load());
    ret = (char *)malloc (strlen (buffer) + 1);
    memcpy (ret, buffer, strlen (buffer) + 1);
    free_and_init (buffer);
    ret = buffer;
    return error_code;
  }
#endif
} // namespace cubperf

#endif /* SERVER_MODE */

