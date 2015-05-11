#
# GDB heap scripts.
#

# heap_hfid_table_entry_key_hash - print hash of OID (for HFID cache)
# $arg0 (in)  : OID volid
# $arg1 (in)  : OID pageid
# $arg2 (in)  : OID slotid
#
define heap_hfid_table_entry_key_hash
  set $oid_volid = $arg0
  set $oid_pageid = $arg1
  set $oid_slotid = $arg2
  set $ht_size = heap_Hfid_table->hfid_hash->hash_size
  set $oid_pseudo_key = -1
  if $oid_pageid < -1
    set $oid_pseudo_key = -$oid_pageid
  else
    set  $oid_pseudo_key = ($oid_slotid | ($oid_slotid << 8)) ^ (($oid_pageid >> 8) | ($oid_volid << 24))
  end
  set $hash_val = $oid_pseudo_key % $ht_size
  p $hash_val 
end



# heap_print_hfid_from_cache - print HFID based OID cache
# $arg0 (in)  : OID volid
# $arg1 (in)  : OID pageid
# $arg2 (in)  : OID slotid
#
define heap_hfid_table_entry_key_hash
  set $oid_volid = $arg0
  set $oid_pageid = $arg1
  set $oid_slotid = $arg2
  set $ht_size = heap_Hfid_table->hfid_hash->hash_size
  set $oid_pseudo_key = -1
  if $oid_pageid < -1
    set $oid_pseudo_key = -$oid_pageid
  else
    set  $oid_pseudo_key = ($oid_slotid | ($oid_slotid << 8)) ^ (($oid_pageid >> 8) | ($oid_volid << 24))
  end
  set $hash_val = $oid_pseudo_key % $ht_size
  p $hash_val 
  
  p heap_Hfid_table->hfid_hash->buckets[$hash_val]
end
