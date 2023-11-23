#
# GDB Thread and Transaction scripts.
#

# thread_num_total
# $arg0 (out) : output total thread count
#
# Output total thread count
#
define thread_num_total
  set $arg0 = cubthread::Manager.m_max_threads + 1
  end

# thread_find_by_index
# $arg0 (in)  : THREAD_INDEX
# $arg1 (out) : THREAD_ENTRY *
#
# Get THREAD_ENTRY with THREAD_INDEX
#
define thread_find_by_index
  set $arg1 = 0
  set $thread_index = $arg0
  thread_num_total $num_total_threads

  if $thread_index == 0
    set $arg1 = (THREAD_ENTRY *) cubthread::Main_entry_p
  else
    set $arg1 = (THREAD_ENTRY *) &cubthread::Manager->m_all_entries[$thread_index - 1]
  end
end

# Print some info related to the thread.
# The function is supposed to be called as an argument to:
#   thread apply <all|thread list> ....
#
define thread_info
  printf "index: %d\n", (cubthread::get_entry ())->index
  printf "type: %d\n", (cubthread::get_entry ())->type
end

# thread_find_by_tran_index
# $arg0 (in)  : TRAN_INDEX
# $arg1 (out) : THREAD_ENTRY *
#
# Get THREAD_ENTRY with TRAN_INDEX
#
define thread_find_by_tran_index
  set $i = 0
  set $arg1 = 0
  thread_num_total $num_total_threads

  while $i < $num_total_threads
    thread_find_by_index $i $thread_entry

    if $thread_entry->tran_index == $arg0
      set $arg1 = $thread_entry
      loop_break
    end
    set $i = $i + 1
  end
end

# thread_find_by_tid
# $arg0 (in)  : THREAD_ID
# $arg1 (out) : THREAD_ENTRY *
#
# Get THREAD_ENTRY with THREAD_ID.
#
define thread_find_by_id
  set $i = 0
  set $arg1 = 0
  thread_num_total $num_total_threads

  while $i < $num_total_threads
    thread_find_by_index $i $thread_entry

    if $thread_entry->m_id._M_thread == $arg0
      set $arg1 = $thread_entry
      loop_break
    end
    set $i = $i + 1
  end
end

# tdes_find_by_tran_index
# $arg0 (in)  : TRAN_INDEX
# $arg1 (out) : TDES *
#
# Get TDES with TRAN_INDEX
#
define tdes_find_by_tran_index
  set $arg1 = log_Gl.trantable.all_tdes[$arg0]
end

# thread_wp_print_info
# $arg0 (in) : thread worker pool
#
# Print all thread worker pool info
#
define thread_wp_print_info
  # init
  set $wp = $arg0
  set $active_workers = 0
  set $inactive_workers = 0
  set $queued_tasks = 0

  # loop through cores
  set $core = $wp.m_core_array
  while $core < $wp.m_core_array + $wp.m_core_count

    #count active
    set $active_workers = $active_workers + $core.m_free_active_list.size ()

    # to count inactive, forward_list must be traversed
    set $elem = $core.m_inactive_list._M_impl._M_head._M_next
    while $elem != 0
      set $inactive_workers = $inactive_workers + 1
      set $elem = $elem._M_next
      end

    # queued tasks
    set $queued_tasks = $queued_tasks + $core.m_task_queue.c._M_impl._M_finish._M_cur - $core.m_task_queue.c._M_impl._M_start._M_cur

    # next core
    set $core = $core + 1
    end

  # print info
  printf "active workers count: %d\n", $active_workers
  printf "inactive workers count: %d\n", $inactive_workers
  printf "running workers count: %d\n", $wp.m_max_workers - $active_workers - $inactive_workers
  printf "queue tasks: %d\n", $queued_tasks
  end

# thread_wp_stats_alloc
# $arg0 (out) : allocated statistics value array
#
# Allocate an array to hold statistics for a worker pool
#
define thread_wp_stats_alloc
  set $statsp = (cubperf::stat_value *) malloc (cubthread::Worker_pool_statdef.m_value_count * sizeof (cubperf::stat_value))
  #output
  set $arg0 = $statsp
  end

# thread_wp_stats_add
# $arg0 (in)     : stats to add
# $arg1 (in/out) : stats accumulator
#
# Add first arguments statistics array to second argument accumulator
#
define thread_wp_stats_add
  set $what = $arg0
  set $where = $arg1
  set $idx = 0
  while $idx < cubthread::Worker_pool_statdef.m_value_count
    set $where[$idx] = $where[$idx] + $what[$idx]
    set $idx = $idx + 1
    end
  end

# thread_wp_stats_print
# $arg0 (in) : array of statistics values
# $arg1 (in)
define thread_wp_stats_print
  set $stats = $arg0
  set $idx = 0
  while $idx < cubthread::Worker_pool_statdef.m_value_count
    printf "%s: %d\n", cubthread::Worker_pool_statdef.get_value_name ($idx), $stats[$idx]
    set $idx = $idx + 1
    end
  end

# thread_wp_collect_stats
# $arg0 (in)  : thread worker pool
# $arg1 (out) : stats container; use thread_wp_alloc_stats
#
# Collect thread worker pool statistics
#
# prerequisites: thread_wp_stats_add
#
define thread_wp_collect_stats
  # init
  set $wp = $arg0
  set $stats = $arg1

  # init stats
  set $idx = 0
  while $idx < cubthread::Worker_pool_statdef.m_value_count
    set $stats[$idx] = 0
    set $idx = $idx + 1
    end

  # collect stats for all workers
  set $core = $wp.m_core_array
  while $core < $wp.m_core_array + $wp.m_core_count
    set $wrk = $core.m_worker_array
    while $wrk < $core.m_worker_array + $core.m_max_workers
      thread_wp_stats_add $wrk.m_statistics.m_values $stats
      set $wrk = $wrk + 1
      end
    set $core = $core + 1
    end
  end

# thread_wp_print_agg_stats
# $arg0 (in) : worker pool
#
# Allocate, aggregate and print worker pool statistics
#
# prerequisites: thread_wp_stats_alloc, thread_wp_collect_stats, thread_wp_stats_print
#
define thread_wp_print_agg_stats
  set $wp = $arg0
  thread_wp_stats_alloc $stat_array
  thread_wp_collect_stats $wp $stat_array
  thread_wp_stats_print $stat_array
  end

