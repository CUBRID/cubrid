#
# GDB scripts to process log records
#

#
# Log page buffer section
#

# logpb_hash
# $arg0 (in)  : LOG_PAGEID
# $arg1 (out) : Hash value
#
# Get hash value for LOG_PAGEID in log page buffer hash
#
define logpb_hash
  set $arg1 = $arg0 % log_Pb.ht->size
end

# logpb_get_page
# $arg0 (in)  : LOG_PAGEID
# $arg1 (out) : LOG_PAGE *
#
# Get cached page for LOG_PAGEID.
# If page doesn't exist in cash, it will return 0
#
define logpb_get_page
  set $hash = $arg0 % log_Pb.ht->size
  set $hte = log_Pb.ht->table[$hash]
  set $arg1 = 0
  while $hte != 0
    if *((LOG_PAGEID *) $hte->key) == $arg0
      set $arg1 = &(((struct log_buffer *) ($hte->data))->logpage)
      loop_break
    end
  end
end


#
# Log records section
#

# log_record_get_header
# $arg0 (in)  : LOG_PAGEID
# $arg1 (in)  : LOG_OFFSET
# $arg2 (out) : LOG_RECORD_HEADER *
# 
# Get header of log record
#
define log_record_get_header
  logpb_get_page $arg0 $lp
  set $arg2 = (LOG_RECORD_HEADER *) ($lp->area + $arg1)
end

#
# Vacuum process log records section
#

# log_record_process_for_vacuum
# $arg0 (in)  : LOG_PAGEID
# $arg1 (in)  : LOG_OFFSET
# $arg2 (out) : LOG_RECORD_HEADER *
# $arg3 (out) : LOG_DATA *
# $arg4 (out) : MVCCID
# $arg5 (out) : LOG_VACUUM_INFO *
#
# Process log record for vacuum.
#
# Prerequisites:
# logpb_get_page
#
define log_record_process_for_vacuum
  set $pageid = $arg0
  set $offset = $arg1
  logpb_get_page $pageid $lp
  set $arg2 = (LOG_RECORD_HEADER *) ($lp->area + $offset)
  set $offset = $offset + sizeof (LOG_RECORD_HEADER)
  if $offset >= 16368
    set $pageid = $pageid + 1
    logpb_get_page $pageid $lp
    set $offset = $offset - 16368
  end
  if $arg2->type == LOG_MVCC_UNDO_DATA
    if $offset + sizeof (struct log_mvcc_undo) >= 16368
      set $pageid = $pageid + 1
      logpb_get_page $pageid $lp
      set $offset = 0
    end
    set $arg3 = &(((struct log_mvcc_undo *) ($lp->area + $offset))->undo.data)
    set $arg4 = ((struct log_mvcc_undo *) ($lp->area + $offset))->mvccid
    set $arg5 = &(((struct log_mvcc_undo *) ($lp->area + $offset))->vacuum_info)
  else
    if $offset + sizeof (struct log_mvcc_undoredo) >= 16368
      set $pageid = $pageid + 1
      logpb_get_page $pageid $lp
      set $offset = 0
    end
    set $arg3 = &(((struct log_mvcc_undoredo *) ($lp->area + $offset))->undoredo.data)
    set $arg4 = ((struct log_mvcc_undoredo *) ($lp->area + $offset))->mvccid
    set $arg5 = &(((struct log_mvcc_undoredo *) ($lp->area + $offset))->vacuum_info)
  end
end

# log_block_find_page_records_for_vacuum
# $arg0 (in)  : Start LOG_PAGEID
# $arg1 (in)  : Start LOG_OFFSET
# $arg2 (in)  : PAGEID
# $arg3 (in)  : VOLID
#
# Print all log records in current log block belonging to given page
# Colateral effect - first log record info before current block will be output to:
# $header - LOG_RECORD_HEADER *
# $log_data - LOG_DATA *
# $mvccid - MVCCID
# $vacinfo - LOG_VACUUM_INFO *
#
# Prerequisites:
# log_record_process_for_vacuum
# logpb_get_page
#
define log_block_find_page_records_for_vacuum
  set $log_pageid = $arg0
  set $log_offset = $arg1
  set $page_id = $arg2
  set $vol_id = $arg3
  set $start_blockid = $log_pageid / vacuum_Data->log_block_npages
  set $blockid = $start_blockid
  while $blockid == $start_blockid
    log_record_process_for_vacuum $log_pageid $log_offset $header $log_data $mvccid $vacinfo
    if $log_data->pageid == $page_id && $log_data->volid == $vol_id
      p *$log_data
      p $mvccid
    end
    set $log_pageid = $vacinfo->prev_mvcc_op_log_lsa.pageid
    set $log_offset = $vacinfo->prev_mvcc_op_log_lsa.offset
    if $log_pageid == -1
      set $blockid = -1
    else
      set $blockid = $log_pageid / vacuum_Data->log_block_npages
    end
  end
end
  
