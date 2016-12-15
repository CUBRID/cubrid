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