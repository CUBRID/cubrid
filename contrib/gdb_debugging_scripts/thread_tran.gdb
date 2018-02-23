#
# GDB Thread and Transaction scripts.
#


# thread_find_by_index
# $arg0 (in)  : THREAD_INDEX
# $arg1 (out) : THREAD_ENTRY *
#
# Get THREAD_ENTRY with THREAD_INDEX
#
define thread_find_by_index
  set $arg1 = 0
  set $thread_index = $arg0
  set $num_total_threads = thread_Manager.num_total + cubthread::Manager->m_max_threads + 1

  if $thread_index >= 0 && $thread_index < $num_total_threads
    if $thread_index == 0
      set $arg1 = (THREAD_ENTRY *) cubthread::Main_entry_p
    else
      if $thread_index <= thread_Manager.num_total
        set $arg1 = (THREAD_ENTRY *) &thread_Manager.thread_array[$thread_index - 1]
      else
        set $arg1 = (THREAD_ENTRY *) &cubthread::Manager->m_all_entries[$thread_index - thread_Manager.num_total - 1]
      end
    end
  end
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
  set $num_total_threads = thread_Manager.num_total + cubthread::Manager->m_max_threads + 1

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
  set $num_total_threads = thread_Manager.num_total + cubthread::Manager->m_max_threads + 1

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
