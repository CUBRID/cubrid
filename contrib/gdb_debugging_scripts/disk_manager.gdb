#
# GDB debugging scripts for disk manager
#

# disk_get_volheader
# $arg0 (in)  : VOLID
# $arg1 (out) : Volheader page
# $arg2 (out) : Volheader
#
# Find volume header page and get volume header
#
# Prerequisite:
# pgbuf_find_page
#
define disk_get_volheader
  pgbuf_find_page 0 $arg0 $arg1
  set $arg2 = (DISK_VAR_HEADER *) $arg1
  p *$arg2
  end

# disk_print_stab
# $arg0 (in)  : VOLID
#
# Print sector table map
#
# Prerequisite:
# disk_get_volheader
# pgbuf_find_page
#
define disk_print_stab
  set $volid = $arg0
  disk_get_volheader $volid $volh_page $volh
  set $nunits = $volh->nsect_total / 64
  set $one_page_units = db_User_page_size / 8
  set $unit = 0
  set $next_page_start = 0
  set $pageid = 1
  while $unit < $nunits
    set $page_start = $next_page_start
    set $next_page_start = $next_page_start + $one_page_units
    pgbuf_find_page $pageid $volid $stab_page
    while $unit < $nunits && $unit < $next_page_start
      printf "sectid = %d, bitmap = 0x%016llx \n", $unit * 64, *(unsigned long long) ($stab_page + ($unit - $page_start))
      set $unit = $unit + 1
      end
    set $pageid = $pageid + 1
    end
  end