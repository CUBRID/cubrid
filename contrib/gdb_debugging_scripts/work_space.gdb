#
# Work space gdb scripts
#

# ws_hash
# $arg0 (in)  : VOLID
# $arg1 (in)  : PAGEID
# $arg2 (in)  : SLOTID
# $arg3 (out) : Hash value
#
# Get hash value for given OID.
#
define ws_hash
  set $volid = $arg0
  set $pageid = $arg1
  set $slotid = $arg2  
  if $pageid < 0
    set $hash = -$pageid
  else
    set $a = $slotid | (((unsigned int) $pageid) << 8)
    set $b = (((unsigned int) $pageid) >> 8) | (((unsigned int) $volid) << 24)
    set $hash = $a ^ $b
    end
  set $arg3 = $hash % ws_Mop_table_size
  end

# ws_find_mop
# $arg0 (in)  : VOLID
# $arg1 (in)  : PAGEID
# $arg2 (in)  : SLOTID
#
# Print all mops with given OID.
#
# Prerequisites:
# ws_hash
#
define ws_find_mop
  set $volid = $arg0
  set $pageid = $arg1
  set $slotid = $arg2
  ws_hash $volid $pageid $slotid $hash
  set $mop = ws_Mop_table[$hash].head
  while $mop != 0
    if $mop->oid_info.oid.pageid == $pageid \
       && $mop->oid_info.oid.volid == $volid \
       && $mop->oid_info.oid.slotid == $slotid
      p $mop
      end
    set $mop = $mop->hash_link
    end
  end
  
# ws_find_mop_in_commit_list
# $arg0 (in)  : VOLID
# $arg1 (in)  : PAGEID
# $arg2 (in)  : SLOTID
#
# Print mop in commit list for OID (if it exists).
#
define ws_find_mop_in_commit_list
  set $volid = $arg0
  set $pageid = $arg1
  set $slotid = $arg2
  
  set $mop = ws_Commit_mops
  while $mop != 0 && $mop != Null_object && $mop != $mop->commit_link
    if $mop->oid_info.oid.pageid == $pageid \
       && $mop->oid_info.oid.volid == $volid \
       && $mop->oid_info.oid.slotid == $slotid
      p $mop
      end
    set $mop = $mop->commit_link
    end
  end
 
# ws_find_cached_class
# $arg0 (in)  : Class name
#
# Print mop for class in class name cache (if it exists).
#
# Prerequisites:
# string_equal from lc_classname.gdb
#
define ws_find_cached_class
  set $cache_cls = Classname_cache->act_head
  set $found = 0
  while $cache_cls != 0
    string_equal $arg0 $cache_cls->key $found
    if $found != 0
      p (MOP) $cache_cls->data
      end
    set $cache_cls = $cache_cls->act_next
    end
  end
