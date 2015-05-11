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
    end
    set $i=$i+1
  end
end


# pgbuf_print_page_holders
# $arg0 (in) : VPID *
#
# Prints all holder of the give page.
#
define find_page_holders
  set $i=0
  while $i < thread_Manager.num_total
    if pgbuf_Pool.thrd_holder_info[$i].thrd_hold_list != 0
      set $th = pgbuf_Pool.thrd_holder_info[$i].thrd_hold_list
      while $th != 0
        if $th->bufptr->vpid.pageid == $arg0->pageid && $th->bufptr->vpid.volid == $arg0->volid
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