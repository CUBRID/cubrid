#
# GDB page buffer scripts.
#


# pgbuf_print_holders
# No arguments -
#
# Prints all holders of any pages.
#
define pgbuf_print_holders
  set $i = 0
  while $i < thread_Manager.num_total
    if pgbuf_Pool.thrd_holder_info[$i].thrd_hold_list != 0
      print $i
      print pgbuf_Pool.thrd_holder_info[$i]
      set $phold = pgbuf_Pool.thrd_holder_info[$i].thrd_hold_list
      while $phold != 0
        p $phold->bufptr->vpid
        set $phold = $phold->thrd_link
        end
      end
    set $i=$i+1
    end
  end

# pgbuf_print_holders_and_waiters
# No arguments -
#
# Print all holders and waiters based on pgbuf_Pool.thrd_holder_info.
#
# Should help building a graph with thread dependencies (based on who waits for whom)
# and finding dead-latch cycles.
#
# NOTE: Information here should be completed with waiters that timedout.
#       They are removed from waiting lists but should be found by consulting the
#       server log files (error -836).
#       Careful some may still be missing and can only be found by consulting thread
#       callstacks (thread can be in pgbuf_timed_sleep_error_handling or in
#       pgbuf_timed_sleep right after er_set_return label).
#
define pgbuf_print_holders_and_waiters
  set $i = 0
  while $i < thread_Manager.num_total
    if pgbuf_Pool.thrd_holder_info[$i].thrd_hold_list != 0
      printf "Holder: %d.\n", $i
      set $phold = pgbuf_Pool.thrd_holder_info[$i].thrd_hold_list
      while $phold != 0
        printf "Waiters for %d|%d: ", $phold->bufptr->vpid.volid, $phold->bufptr->vpid.pageid
        set $buf = $phold->bufptr
        set $thr = $buf->next_wait_thrd
        while $thr != 0
          printf "%d ", $thr->index
          set $thr = $thr->next_wait_thrd
          end
        printf "\n"
        set $phold = $phold->thrd_link
        end
      printf "\n"
      end
    set $i=$i+1
    end
  end


# pgbuf_print_page_holders
# $arg0 (in) : VOLID
# $arg1 (in) : PAGEID
#
# Prints all holder of the give page.
#
define find_page_holders
  set $i=0
  while $i < thread_Manager.num_total
    if pgbuf_Pool.thrd_holder_info[$i].thrd_hold_list != 0
      set $th = pgbuf_Pool.thrd_holder_info[$i].thrd_hold_list
      while $th != 0
        if $th->bufptr->vpid.pageid == $arg1 && $th->bufptr->vpid.volid == $arg0
          print $i
          print pgbuf_Pool.thrd_holder_info[$i]
          end
        set $th = $th->thrd_link
        end
      end
    set $i=$i+1
    end
  end


# pgbuf_get_vpid
# $arg0 (in)  : PAGE_PTR
# $arg1 (out) : VPID *
#
# Get a pointer to page VPID.
#
define pgbuf_get_vpid
  set $arg1 = (VPID*)($arg0 - 24)
end

# pgbuf_print_vpid
# $arg0 (in) : PAGE_PTR
#
# Print page VPID.
#
define pgbuf_print_vpid
  p *(VPID*)($arg0 - 24)
end


# pgbuf_get_lsa
# $arg0 (in)  : PAGE_PTR
# $arg1 (out) : LOG_LSA *
#
# Get a pointer to page LSA.
#
define pgbuf_get_lsa
  set $arg1 = (LOG_LSA *)($arg0 - 32)
end

# pgbuf_print_lsa
# $arg0 (in) : PAGE_PTR
#
# Print page LOG_LSA.
#
define pgbuf_print_lsa
  p *(LOG_LSA*) ($arg0 - 32)
end


# pgbuf_hash
# $arg0 (in)  : PAGEID
# $arg1 (in)  : VOLID
# $arg2 (out) : hash value
#
# Get hash value for give VPID.
#
define pgbuf_hash
  set $volid_lsb = $arg1
  set $lsb_mask = 1
  set $reverse_mask = 1 << 19
  set $reverse_volid_lsb = 0
  set $i = 8
  while $i > 0
    if $volid_lsb & $lsb_mask
      set $reverse_volid_lsb = $reverse_volid_lsb | $reverse_mask
    end
    set $reverse_mask = $reverse_mask >> 1
    set $lsb_mask = $lsb_mask << 1
    set $i=$i - 1
  end
  set $hash_val = $arg0 ^ $reverse_volid_lsb
  set $hash_val = $hash_val & ((1 << 20) - 1)
  set $arg2 = $hash_val
end

# pgbuf_find_page
# $arg0 (in)  : PAGEID
# $arg1 (in)  : VOLID
# $arg2 (out) : PAGE_PTR
#
# Find page in page buffer for give VPID.
#
# Prerequisite:
# pgbuf_hash
#
define pgbuf_find_page
  pgbuf_hash $arg0 $arg1 $hash
  set $bcb = pgbuf_Pool.buf_hash_table[$hash].hash_next
  set $arg2 = 0
  while $bcb != 0
    if $bcb->vpid.volid == $arg1 && $bcb->vpid.pageid == $arg0
      set $arg2 = $bcb->iopage_buffer->iopage.page
      loop_break
    end
    set $bcb = $bcb->hash_next
  end
end

# pgbuf_cast_btop
# $arg0 (in)  : PGBUF_BCB *
# $arg1 (out) : PAGE_PTR
#
# Cast PGBUF_BCB pointer to PAGE_PTR (CAST_BFPTR_TO_PGPTR)
#
define pgbuf_cast_btop
  set $arg1 = $arg0->iopage_buffer->iopage.page
  end

# pgbuf_cast_ptob
# $arg0 (in)  : PAGE_PTR
# $arg1 (out) : PGBUF_BCB *
# Cast PAGE_PTR to PGBUF_BCB pointer (CAST_PGPTR_TO_BFPTR)
#
define pgbuf_cast_ptob
  set $arg1 = ((PGBUF_IOPAGE_BUFFER *) (((PAGE_PTR) $arg0) - sizeof (FILEIO_PAGE_RESERVED) - sizeof (PGBUF_BCB *)))->bcb
  end

# pgbuf_lru_print
# arg0  - lru index
#
# Prints info on lru
#  
define pgbuf_lru_print
  set $lru_idx = $arg0
  set $lru_list = &pgbuf_Pool.buf_LRU_list[$lru_idx]
  set $bcb = $lru_list->LRU_bottom
  
  set $cnt3 = 0
  set $cnt2 = 0
  set $cnt1 = 0
  set $first_victim = 0
  set $cntv = 0
  set $cntdrt = 0
  set $cntisflsh = 0
  set $cntdirv = 0
  set $cntfix = 0
  set $dist_vh = -1
  set $dist_fv = -1
  set $dist = 0
  
  while $bcb != 0
    if ($bcb->flags & PGBUF_ZONE_MASK) == PGBUF_LRU_1_ZONE
      set $cnt1 = $cnt1 + 1
      end
    if ($bcb->flags & PGBUF_ZONE_MASK) == PGBUF_LRU_2_ZONE
      set $cnt2 = $cnt2 + 1
      end
    if ($bcb->flags & PGBUF_ZONE_MASK) == PGBUF_LRU_3_ZONE
      set $cnt3 = $cnt3 + 1
      if ($bcb->flags & 0xF0000000) == 0
        set $cntv = $cntv + 1
        if $first_victim == 0
          set $first_victim = $bcb
          set $dist_fv = $dist
          end
        end
      end
    if ($bcb->flags & 0x80000000) != 0
      set $cntdrt = $cntdrt + 1
      end
    if ($bcb->flags & 0x40000000) != 0
      set $cntisflsh = $cntisflsh + 1
      end
    if ($bcb->flags & 0x20000000) != 0
      set $cntdirv = $cntdirv + 1
      end
    if $bcb->fcnt > 0 || $bcb->latch_mode != PGBUF_NO_LATCH || $bcb->next_wait_thrd != 0
      set $cntfix = $cntfix + 1
      end
    if $bcb == $lru_list->LRU_victim_hint
      set $dist_vh = $dist
      end
      
    set $bcb = $bcb->prev_BCB
    set $dist = $dist + 1
    end
  
  if $lru_idx < pgbuf_Pool.num_LRU_list
    printf "LRU shared list %d: \n", $lru_idx
    printf "Total count:  %d \n", $cnt1 + $cnt2 + $cnt3
  else
    printf "LRU private list %d: \n", $lru_idx
    printf "Total count:  %d; quota = %d \n", $cnt1 + $cnt2 + $cnt3, $lru_list->quota
    end
  printf "Zone 1: %d (%d); threshold = %d \n", $cnt1, $lru_list->count_lru1, $lru_list->threshold_lru1
  printf "Zone 2: %d (%d); threshold = %d \n", $cnt2, $lru_list->count_lru2, $lru_list->threshold_lru2
  printf "Zone 3: %d (%d) \n", $cnt3, $lru_list->count_lru3
  printf "Victim count: %d (%d) \n", $cntv, $lru_list->count_vict_cand
  printf "First victim: %p, distance: %d \n", $first_victim, $dist_fv
  printf "Victim hint: %p, distance: %d \n", pgbuf_Pool.buf_LRU_list[$lru_idx]->LRU_victim_hint, $dist_vh
  printf "Dirties = %d, Flushing = %d, Direct victims = %d, Fixed = %d \n", $cntdrt, $cntisflsh, $cntdirv, $cntfix
  end

define pgbuf_lru_print_victim_status
  set $i = 0
  
  set $shared_1 = 0
  set $shared_2 = 0
  set $shared_3 = 0
  set $shared_vc = 0
  
  while $i < pgbuf_Pool.num_LRU_list
    set $lru_list = &pgbuf_Pool.buf_LRU_list[$i]
    set $shared_1 = $shared_1 + $lru_list->count_lru1
    set $shared_2 = $shared_2 + $lru_list->count_lru2
    set $shared_3 = $shared_3 + $lru_list->count_lru3
    set $shared_vc = $shared_vc + $lru_list->count_vict_cand
    set $i = $i + 1
    end
    
  set $private_1 = 0
  set $private_2 = 0
  set $private_3 = 0
  set $private_vc = 0
  set $private_quota = 0
  set $oq_lists = 0
  set $oq_bcbs = 0
  set $oq_with_vc = 0
  set $oq_with_vc_bcbs = 0
  
  set $i = pgbuf_Pool.num_LRU_list
  while $i < pgbuf_Pool.num_LRU_list + pgbuf_Pool.quota.num_private_LRU_list
    set $lru_list = &pgbuf_Pool.buf_LRU_list[$i]
    set $private_1 = $private_1 + $lru_list->count_lru1
    set $private_2 = $private_2 + $lru_list->count_lru2
    set $private_3 = $private_3 + $lru_list->count_lru3
    set $private_vc = $private_vc + $lru_list->count_vict_cand
    set $private_quota = $private_quota + $lru_list->quota
    set $diff = $lru_list->count_lru1 + $lru_list->count_lru2 + $lru_list->count_lru3 - $lru_list->quota
    if $diff > 0
      set $oq_lists = $oq_lists + 1
      set $oq_bcbs = $oq_bcbs + $diff
      if $lru_list->count_vict_cand > 0
        set $oq_with_vc = $oq_with_vc + 1
        set $oq_with_vc_bcbs = $oq_with_vc_bcbs + $lru_list->count_vict_cand
        end
      end
    set $i = $i + 1
    end
  
  printf "\n"
  printf "Shared lists: \n"
  printf "Total bcbs: %d \n", $shared_1 + $shared_2 + $shared_3
  printf "Zone 1: %d \n", $shared_1
  printf "Zone 2: %d \n", $shared_2
  printf "Zone 3: %d \n", $shared_3
  printf "Victim candidates: %d \n", $shared_vc
  printf "\n"
  printf "Private lists: \n"
  printf "Total bcbs: %d \n", $private_1 + $private_2 + $private_3
  printf "Zone 1: %d \n", $private_1
  printf "Zone 2: %d \n", $private_2
  printf "Zone 3: %d \n", $private_3
  printf "Victim candidates: %d \n", $private_vc
  printf "Over quota: lists = %d, bcb's = %d \n", $oq_lists, $oq_bcbs
  printf "Candidates in over quota: lists = %d, bcb's = %d \n", $oq_with_vc, $oq_with_vc_bcbs
  printf "\n"
  end
  
define pgbuf_print_alloc_bcb_waits

  set $i = 0
  
  printf "Direct victim array: \n"
  while $i < thread_Manager.num_total
    if pgbuf_Pool.direct_victims.bcb_victims[$i] != 0
      printf "(thr = %d, bcb = %p) \n", $i, pgbuf_Pool.direct_victims.bcb_victims[$i]
      end
    set $i = $i + 1
    end
  printf "\n"

  printf "waiter_threads_high_priority: "
  set $lfcq = pgbuf_Pool.direct_victims.waiter_threads_high_priority
  set $i = $lfcq->consume_cursor
  set $thrar = (THREAD_ENTRY **) $lfcq->data
  printf "%d \n", $lfcq->produce_cursor - $i
  while $i < $lfcq->produce_cursor
    printf "%d ", $thrar[$i % $lfcq->capacity]->index
    set $i = $i + 1
    end
  printf "\n"

  printf "waiter_threads_low_priority: "
  set $lfcq = pgbuf_Pool.direct_victims.waiter_threads_low_priority
  set $i = $lfcq->consume_cursor
  set $thrar = (THREAD_ENTRY **) $lfcq->data
  printf "%d \n", $lfcq->produce_cursor - $i
  while $i < $lfcq->produce_cursor
    printf "%d ", $thrar[$i % $lfcq->capacity]->index
    set $i = $i + 1
    end
  printf "\n"
  end
  
define pgbuf_find_alloc_bcb_wait_thread
  printf "bcb = %p \n", pgbuf_Pool.direct_victims.bcb_victims[$arg0]
  
  set $lfcq = pgbuf_Pool.direct_victims.waiter_threads_high_priority
  set $i = $lfcq->consume_cursor
  set $thrar = (THREAD_ENTRY **) $lfcq->data
  set $found = 0
  set $dist = 0
  while $i < $lfcq->produce_cursor
    if $thrar[$i % $lfcq->capacity]->index == $arg0
      printf "waiter_threads_high_priority, cursor = %d, distance = %d, thread_p = %p \n", $i, $dist, $thrar[$i % $lfcq->capacity]
      set $found = 1
      loop_break
      end
    set $i = $i + 1
    set $dist = $dist + 1
    end

  if !$found
    set $lfcq = pgbuf_Pool.direct_victims.waiter_threads_low_priority
    set $i = $lfcq->consume_cursor
    set $thrar = (THREAD_ENTRY **) $lfcq->data
    set $found = 0
    while $i < $lfcq->produce_cursor
      if $thrar[$i % $lfcq->capacity]->index == $arg0
        printf "waiter_threads_low_priority, cursor = %d, distance = %d, thread_p = %p \n", $i, $dist, $thrar[$i % $lfcq->capacity]
        set $found = 1
        loop_break
        end
      set $i = $i + 1
      set $dist = $dist + 1
      end
    end

  if !$found
    printf "not found \n"
    end
  end
  
define pgbuf_read_bcb_flags
  set $flags = $arg0
  
  printf "Flags: %x \n", ($flags & 0xF0000000)
  if $flags & 0x80000000
    printf "BCB is dirty \n"
    end
  if $flags & 0x40000000
    printf "BCB is being flushed to disk \n"
    end
  if $flags & 0x20000000
    printf "BCB is direct victim \n"
    end
  
  if ($flags & PGBUF_ZONE_MASK) == PGBUF_LRU_1_ZONE
    printf "BCB is in lru 1 \n"
    end
  if ($flags & PGBUF_ZONE_MASK) == PGBUF_LRU_2_ZONE
    printf "BCB is in lru 2 \n"
    end
  if ($flags & PGBUF_ZONE_MASK) == PGBUF_LRU_3_ZONE
    printf "BCB is in lru 3 \n"
    end
  if ($flags & PGBUF_ZONE_MASK) == PGBUF_VOID_ZONE
    printf "BCB is in void zone \n"
    end
  if ($flags & PGBUF_ZONE_MASK) == PGBUF_INVALID_ZONE
    printf "BCB is in invalid zone \n"
    end
  if ($flags & PGBUF_ZONE_MASK) == PGBUF_AIN_ZONE
    printf "BCB is in ain \n"
    end
  
  if $flags & PGBUF_LRU_ZONE_MASK
    printf "BCB is in lru list with index %d \n", $flags & 0xFFFF
    end
  end