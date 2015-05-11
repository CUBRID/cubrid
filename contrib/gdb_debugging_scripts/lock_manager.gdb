#
# GDB lock manager scripts.
#

# lock_get_hash_value - prints hash value in lock object table
# $arg0 (in)  : OID volid
# $arg1 (in)  : OID pageid
# $arg2 (in)  : OID slotid
#
#
define lock_get_hash_value
  set $oid_volid = $arg0
  set $oid_pageid = $arg1
  set $oid_slotid = $arg2
  set $ht_size = lk_Gl.obj_hash_table.hash_size
  
  if $oid_slotid  <= 0
    set $addr = $oid_pageid - $oid_slotid
  else
    set $next_base_slotid = 2
	while $next_base_slotid <= $oid_slotid
	  set $next_base_slotid = $next_base_slotid * 2
	end
    set $addr = $oid_pageid + ($ht_size / $next_base_slotid) * (2 * $oid_slotid - $next_base_slotid + 1)
  end
  p ($addr % $ht_size)
end

# lock_get_lock_res - print lock resource
# $arg0 (in)  : OID volid
# $arg1 (in)  : OID pageid
# $arg2 (in)  : OID slotid
#
define lock_get_lock_res
  set $oid_volid = $arg0
  set $oid_pageid = $arg1
  set $oid_slotid = $arg2
  set $ht_size = lk_Gl.obj_hash_table.hash_size
  
  if $oid_slotid  <= 0
    set $addr = $oid_pageid - $oid_slotid
  else
    set $next_base_slotid = 2
	while $next_base_slotid <= $oid_slotid
	  set $next_base_slotid = $next_base_slotid * 2
	end
    set $addr = $oid_pageid + ($ht_size / $next_base_slotid) * (2 * $oid_slotid - $next_base_slotid + 1)
  end
  set $index = $addr % $ht_size
  p *(LK_RES *)(lk_Gl.obj_hash_table.buckets[$index])
end
