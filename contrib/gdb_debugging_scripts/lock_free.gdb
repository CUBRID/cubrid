
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

define lfcq_print_entry_states
  set $lfcq = $arg0
  set $i = 0
  while $i < $lfcq->capacity
    set $flag = $lfcq->entry_state[$i] & 0xC000000000000000
    if $flag == 0x0000000000000000
      printf "%10d\tready for produce \n", $lfcq->entry_state[$i] & ~0xC000000000000000
      end
    if $flag == 0x8000000000000000
      printf "%10d\treserved for produce \n", $lfcq->entry_state[$i] & ~0xC000000000000000
      end
    if $flag == 0x4000000000000000
      printf "%10d\tready for consume \n", $lfcq->entry_state[$i] & ~0xC000000000000000
      end
    if $flag == 0xC000000000000000
      printf "%10d\treserved for consume \n", $lfcq->entry_state[$i] & ~0xC000000000000000
      end
    set $i = $i+1
    end
  end