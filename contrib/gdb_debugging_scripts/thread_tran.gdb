#
# GDB Thread and Transaction scripts.
#


# thread_find_by_tran_index
# $arg0 (in)  : TRAN_INDEX
# $arg1 (out) : THREAD_ENTRY *
#
# Get THREAD_ENTRY with TRAN_INDEX
#
define thread_find_by_tran_index
  set $i = 0
  set $arg1 = 0
  set $found = 0
  while $i < thread_Manager.num_total && $found == 0
    if thread_Manager.thread_array[$i]->tran_index == $arg0
      set $arg1 = thread_Manager.thread_array[$i]
      loop_break
    end
    set $i=$i+1
  end
end

# thread_find_by_tid
# $arg0 (in)  : TID
# $arg1 (out) : THREAD_ENTRY *
#
# Get THREAD_ENTRY with TID.
#
define thread_find_by_tid
  set $i = 0
  set $arg1 = 0
  while $i < thread_Manager.num_total
    if thread_Manager.thread_array[$i]->get_posix_id () == $arg0
      set $arg1 = thread_Manager.thread_array[$i]
      loop_break
	end
  set $i=$i+1
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
