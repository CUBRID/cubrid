#
# GDB representations scripts.
#

# or_mvcc_header_size_from_flags - print header size
# $arg0 (in)  : data
# $arg1 (out) : header_size
#
define or_mvcc_header_size_from_flags
  set $OR_MVCC_REP_SIZE=4
  set $OR_MVCC_FLAG_VALID_DELID=2
  set $OR_MVCC_FLAG_VALID_LONG_CHN=8
  set $OR_MVCCID_SIZE=8
  set $OR_INT_SIZE=4
  set $OR_MVCC_FLAG_VALID_NEXT_VERSION=4
  set $OR_MVCC_FLAG_VALID_PARTITION_OID=16
  set $OR_OID_SIZE=8
  set $OR_MVCC_FLAG_VALID_INSID=1
  
  set $data = $arg0
  
  set $mvcc_flags = (*((char *)($data))) & 0x1f
  
  set $mvcc_header_size = $OR_MVCC_REP_SIZE
  if ($mvcc_flags & $OR_MVCC_FLAG_VALID_INSID)
    set $mvcc_header_size = $mvcc_header_size + $OR_MVCCID_SIZE
  end
  
  if (($mvcc_flags & $OR_MVCC_FLAG_VALID_DELID) || ($mvcc_flags & $OR_MVCC_FLAG_VALID_LONG_CHN))
    set $mvcc_header_size = $mvcc_header_size + $OR_MVCCID_SIZE
  else
    set $mvcc_header_size = $mvcc_header_size + $OR_INT_SIZE
  end
  
  if ($mvcc_flags & $OR_MVCC_FLAG_VALID_NEXT_VERSION)
    set $mvcc_header_size = $mvcc_header_size + $OR_OID_SIZE
  end
  
  if ($mvcc_flags & $OR_MVCC_FLAG_VALID_PARTITION_OID)
    set $mvcc_header_size = $mvcc_header_size + $OR_OID_SIZE
  end
  
  set $arg1 = $mvcc_header_size
end




# or_var_table_size_internal -
# $arg0 (in)  : vars
# $arg1 (in)  : offset_size
# $arg2 (out)  : offset_size
#
define or_var_table_size_internal
  set $vars = $arg0
  set $offset_size = $arg1
  set $size_internal = 0
  
  if ($vars != 0)
    set $size_internal = $offset_size * ($vars + 1)
  end
  set $size_internal = ($size_internal + 4 - 1) & (~(4 - 1))
  set $arg2 = $size_internal
end


# or_fixed_attributes_offset_internal -
# $arg0 (in)  : ptr
# $arg1 (in)  : nvars
# $arg2 (out)  : offset_size
# $arg3 (out)  : offset_internal
#
define or_fixed_attributes_offset_internal
  set $ptr = $arg0
  set $nvars = $arg1
  set $offset_size = $arg2
  
  set $mvcc_header_size = 0
  set $var_table_size_internal = 0
  set $offset_internal = 0
  
  or_mvcc_header_size_from_flags $ptr $mvcc_header_size
  
  or_var_table_size_internal $nvars $offset_size $var_table_size_internal
  
  set $offset_internal = $mvcc_header_size + $var_table_size_internal
  set $arg3 = $offset_internal
end
  
  
# or_fixed_attributes_offset -
# $arg0 (in)  : ptr
# $arg1 (in)  : nvars
# $arg2 (out)  : offset
#
define or_fixed_attributes_offset  
  set $ptr = $arg0
  set $nvars = $arg1
  set $BIG_VAR_OFFSET_SIZE = 4
  set $offset = 0
  
  or_fixed_attributes_offset_internal $ptr $nvars $BIG_VAR_OFFSET_SIZE $offset
  set $arg2 = $offset
end
  

# or_class_rep_dir_ptr -
# $arg0 (in)  : record
#
define or_class_rep_dir_ptr
  set $record = $arg0
  set $ORC_CLASS_VAR_ATT_COUNT = 17
  set $ORC_REP_DIR_OFFSET = 8
  set $data = (char*) ($record->data)
  set $fixed_offset = 0
  
  or_fixed_attributes_offset $data $ORC_CLASS_VAR_ATT_COUNT $fixed_offset
  set $ptr = $data + $fixed_offset + $ORC_REP_DIR_OFFSET
  p $ptr
end


# or_class_hfid_ptr -
# $arg0 (in)  : record
#
define or_class_hfid_ptr
  set $record = $arg0
  set $ORC_CLASS_VAR_ATT_COUNT = 17
  set $ORC_REP_DIR_OFFSET = 8
  set $data = (char*) ($record->data)
  set $fixed_offset = 0
  
  or_fixed_attributes_offset $data $ORC_CLASS_VAR_ATT_COUNT $fixed_offset
  set $ptr = $data + $fixed_offset
  p $ptr + 16
end