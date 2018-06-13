#
# Lock-free module scripts
#

#
# Lock-free hashes
#

# lf_print_counters
# No arguments -
#
# Prints all lock-free counters
#
define lf_print_counters
  printf "lf_hash_size                   %d\n", lf_hash_size
  printf "lf_inserts                     %d\n", lf_inserts
  printf "lf_inserts_restart             %d\n", lf_inserts_restart
  printf "lf_list_inserts                %d\n", lf_list_inserts
  printf "lf_list_inserts_found          %d\n", lf_list_inserts_found
  printf "lf_list_inserts_save_temp_1    %d\n", lf_list_inserts_save_temp_1
  printf "lf_list_inserts_save_temp_2    %d\n", lf_list_inserts_save_temp_2
  printf "lf_list_inserts_claim          %d\n", lf_list_inserts_claim
  printf "lf_list_inserts_fail_link      %d\n", lf_list_inserts_fail_link
  printf "lf_list_inserts_success_link   %d\n", lf_list_inserts_success_link
  printf "lf_deletes                     %d\n", lf_deletes
  printf "lf_deletes_restart             %d\n", lf_deletes_restart
  printf "lf_list_deletes                %d\n", lf_list_deletes
  printf "lf_list_deletes_found          %d\n", lf_list_deletes_found
  printf "lf_list_deletes_fail_mark_next %d\n", lf_list_deletes_fail_mark_next
  printf "lf_list_deletes_fail_unlink    %d\n", lf_list_deletes_fail_unlink
  printf "lf_list_deletes_success_unlink %d\n", lf_list_deletes_success_unlink
  printf "lf_list_deletes_not_found      %d\n", lf_list_deletes_not_found
  printf "lf_list_deletes_not_match      %d\n", lf_list_deletes_not_match
  printf "lf_clears                      %d\n", lf_clears
  printf "lf_retires                     %d\n", lf_retires
  printf "lf_claims                      %d\n", lf_claims
  printf "lf_claims_temp                 %d\n", lf_claims_temp
  printf "lf_transports                  %d\n", lf_transports
  printf "lf_temps                       %d\n", lf_temps
  end

#
# Lock-free circular queues
#

# lfcq_print
# $arg0 (in) : lock-free circular queue
#
# Print all entry states
#
define lfcq_print
  set $lfcq = $arg0
  # print info
  printf "capacity = %d\n", $lfcq.m_capacity
  printf "produce_cursor = %d", $lfcq.m_produce_cursor
  printf "consume_cursor = %d", $lfcq.m_consume_cursor
  end

# lfcq_at_index
# $arg0 (in) : lock-free circular queue
# $arg1 (in) : data index
#
# Print data in lfcq at index
#
define lfcq_at_index
  set $lfcq = $arg0
  set $index = $arg1
  p $lfcq.m_data[$index]
  end

# lfcq_get_cursor_index
# $arg0 (in)  : lfcq
# $arg1 (in)  : cursor
# $arg2 (out) : index
#
# Get index of cursor
#
define lfcq_get_cursor_index
  set $lfcq = $arg0
  set $cursor = $arg1
  set $index = $cursor & $lfcq.m_index_mask
  #output
  set $arg2 = $index
  end

# lfcq_print_data
# $arg0 (in) : lfcq
#
# Print all data in lfcq
#
define lfcq_print_data
  set $lfcq = $arg0
  set $cursor = $lfcq.m_produce_cursor
  printf "Data between %d-%d:\n", $lfcq.m_produce_cursor, $lfcq.m_consume_cursor
  while $cursor < $lfcq.m_consume_cursor
    lfcq_get_cursor_index $lfcq $cursor $index
    lfcq_at_index $lfcq $index
    set $cursor = $cursor + 1
    end
  end

