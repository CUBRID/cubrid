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

# pgbuf_lru_print_vict
# arg0  - lru index
#
# Prints all BCB which may be victimized from LRU, count of elements in LRU, count in LRU2 and LRU1 sections, count of dirty BCBs,  count of BCBs with fixed, count of BCBs with latch
# Last is count of BCBs which may be victimized
#  
define pgbuf_lru_print_vict
  set $lru_idx = $arg0
  set $bcb = pgbuf_Pool.buf_LRU_list[$lru_idx]->LRU_bottom
  set $cnt_dirty = 0
  set $cnt_fcnt = 0
  set $cnt_avoid_victim = 0
  set $cnt_latch_mode = 0
  set $cnt_victim_candidate = 0
  set $cnt_can_victim = 0
  set $cnt_zone_lru2 = 0
  set $cnt_zone_lru1 = 0
  set $elem = 0
  set $zone_lru2 = ($lru_idx << 3) | 1
  set $zone_lru1 = ($lru_idx << 3)
  set $found = 0
  set $first_lru1 = -1
  set $last_lru2 = -1

  p $zone_lru2
  p $zone_lru1
  
  while $bcb != 0
	if $bcb->dirty == 0 && $bcb->fcnt == 0 && $bcb->avoid_victim == 0 && $bcb->latch_mode == 0 && $bcb->victim_candidate == 0 && $bcb->zone_lru == $zone_lru2
    if $found == 0
      p $elem
      set $found = 1
      end

	  p $bcb
	  set $cnt_can_victim = $cnt_can_victim + 1
	end

	if $bcb->zone_lru == $zone_lru2
      set $cnt_zone_lru2 = $cnt_zone_lru2 + 1
      set $last_lru2 = $elem
	end

	if $bcb->zone_lru == $zone_lru1
      if $first_lru1 == -1
        set $first_lru1 = $elem
        end
      set $cnt_zone_lru1= $cnt_zone_lru1 + 1
	end

	if  $bcb->dirty != 0 
	  set $cnt_dirty = $cnt_dirty + 1
	end

	if  $bcb->fcnt != 0 
	  set $cnt_fcnt = $cnt_fcnt + 1
	end

	if  $bcb->avoid_victim != 0 
	  set $cnt_avoid_victim = $cnt_avoid_victim + 1
	end

	if  $bcb->latch_mode != 0 
	  set $cnt_latch_mode = $cnt_latch_mode + 1
	end
	if $bcb->victim_candidate != 0 
	  set $cnt_victim_candidate = $cnt_victim_candidate + 1
	end
	
    set $bcb = $bcb->prev_BCB
	set $elem = $elem + 1
  end
  
  p $elem
  p $cnt_zone_lru2
  p $cnt_zone_lru1  
  p $cnt_dirty
  p $cnt_fcnt
  p $cnt_avoid_victim
  p $cnt_latch_mode
  p $cnt_victim_candidate
  p $cnt_can_victim
  p $first_lru1
  p $last_lru2
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
  
  set $i = pgbuf_Pool.num_LRU_list + pgbuf_Pool.quota.num_garbage_LRU_list
  while $i < pgbuf_Pool.num_LRU_list + pgbuf_Pool.quota.num_garbage_LRU_list + pgbuf_Pool.quota.num_private_LRU_list
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
      printf "(thr = %d, bcb = %p), ", $i, pgbuf_Pool.direct_victims.bcb_victims[$i]
      end
    set $i = $i + 1
    end
  printf "\n"
  printf "waiter_threads_latch_with_waiters: \n"
  set $lfcq = pgbuf_Pool.direct_victims.waiter_threads_latch_with_waiters
  set $i = $lfcq->consume_cursor
  set $thrar = (THREAD_ENTRY **) $lfcq->data
  while $i < $lfcq->produce_cursor
    printf "%d ", $thrar[$i % $lfcq->capacity]->index
    set $i = $i + 1
    end
  printf "\n"
  printf "waiter_threads_latch_without_waiters: \n"
  set $lfcq = pgbuf_Pool.direct_victims.waiter_threads_latch_without_waiters
  set $i = $lfcq->consume_cursor
  set $thrar = (THREAD_ENTRY **) $lfcq->data
  while $i < $lfcq->produce_cursor
    printf "%d ", $thrar[$i % $lfcq->capacity]->index
    set $i = $i + 1
    end
  printf "\n"
  printf "waiter_threads_latch_none: \n"
  set $lfcq = pgbuf_Pool.direct_victims.waiter_threads_latch_none
  set $i = $lfcq->consume_cursor
  set $thrar = (THREAD_ENTRY **) $lfcq->data
  while $i < $lfcq->produce_cursor
    printf "%d ", $thrar[$i % $lfcq->capacity]->index
    set $i = $i + 1
    end
  printf "\n"
  end
  
define pgbuf_find_alloc_bcb_wait_thread
  printf "bcb = %p \n", pgbuf_Pool.direct_victims.bcb_victims[$arg0]
  
  set $lfcq = pgbuf_Pool.direct_victims.waiter_threads_latch_with_waiters
  set $i = $lfcq->consume_cursor
  set $thrar = (THREAD_ENTRY **) $lfcq->data
  set $found = 0
  while $i < $lfcq->produce_cursor
    if $thrar[$i % $lfcq->capacity]->index == $arg0
      printf "waiter_threads_latch_with_waiters, cursor = %d, thread_p = %p \n", $i, $thrar[$i % $lfcq->capacity]
      set $found = 1
      loop_break
      end
    set $i = $i + 1
    end
  if !$found
    set $lfcq = pgbuf_Pool.direct_victims.waiter_threads_latch_without_waiters
    set $i = $lfcq->consume_cursor
    set $thrar = (THREAD_ENTRY **) $lfcq->data
    set $found = 0
    while $i < $lfcq->produce_cursor
      if $thrar[$i % $lfcq->capacity]->index == $arg0
        printf "waiter_threads_latch_without_waiters, cursor = %d, thread_p = %p \n", $i, $thrar[$i % $lfcq->capacity]
        set $found = 1
        loop_break
        end
      set $i = $i + 1
      end
    end
  if !$found
    set $lfcq = pgbuf_Pool.direct_victims.waiter_threads_latch_none
    set $i = $lfcq->consume_cursor
    set $thrar = (THREAD_ENTRY **) $lfcq->data
    set $found = 0
    while $i < $lfcq->produce_cursor
      if $thrar[$i % $lfcq->capacity]->index == $arg0
        printf "waiter_threads_latch_none, cursor = %d, thread_p = %p \n", $i, $thrar[$i % $lfcq->capacity]
        set $found = 1
        loop_break
        end
      set $i = $i + 1
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
    printf "BCB is victim candidate \n"
    end
  if $flags & 0x10000000
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