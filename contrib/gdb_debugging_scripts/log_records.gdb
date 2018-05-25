#
# GDB scripts to process log records
#

#
# Log page buffer section
#

# logpb_index
# $arg0 (in)  : LOG_PAGEID
# $arg1 (out) : Hash value
#
# Get index value for LOG_PAGEID in log page buffer array
#
define logpb_index
  set $arg1 = $arg0 % log_Pb.num_buffers
end

# logpb_get_page
# $arg0 (in)  : LOG_PAGEID
# $arg1 (out) : LOG_PAGE *
#
# Get cached page for LOG_PAGEID.
# If page doesn't exist in cash, it will return 0
#
define logpb_get_page
  set $idx = $arg0 % log_Pb.num_buffers
  set $arg1 = 0
  if log_Pb.buffers[$idx].pageid == $arg0
	set $arg1 = log_Pb.buffers[$idx]->logpage
  end
end


# logpb_get_log_buffer
# $arg0 (in) : LOG_PAGE *
# $arg1 (out) : LOG_BUFFER *
#
# Get LOG_BUFFER for the given LOG_PAGE *
#
define logpb_get_log_buffer
  if $arg0 == log_Pb.header_page
    set $arg1 = &log_Pb.header_buffer
  else
    set $idx = (int) ((char *) $arg0 - (char *) log_Pb.pages_area) / db_Log_page_size
    set $arg1 = &log_Pb.buffers[$idx]
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
  
# log_sizeof_rectype_data
# arg0 (in)  : LOG_RECTYPE
# arg1 (out) : size of log data.
#
# Get log record data size
#
define log_sizeof_rectype_data
  set $rectype = $arg0
  set $sizeof = -1
  if $rectype == LOG_REDO_DATA
    set $sizeof = sizeof (LOG_REC_REDO)
    end
  if $rectype == LOG_UNDO_DATA
    set $sizeof = sizeof (LOG_REC_UNDO)
    end
  if $rectype == LOG_UNDOREDO_DATA || $rectype == LOG_DIFF_UNDOREDO_DATA
    set $sizeof = sizeof (LOG_REC_UNDOREDO)
    end
  if $rectype == LOG_MVCC_REDO_DATA
    set $sizeof = sizeof (LOG_REC_MVCC_REDO)
    end
  if $rectype == LOG_MVCC_UNDO_DATA
    set $sizeof = sizeof (LOG_REC_MVCC_UNDO)
    end
  if $rectype == LOG_MVCC_UNDOREDO_DATA || $rectype == LOG_DIFF_UNDOREDO_DATA
    set $sizeof = sizeof (LOG_REC_MVCC_UNDOREDO)
    end
    
  if $sizeof == -1
    #printf "Cannot handle rectype: %d\n", $rectype
    end

  set $arg1 = $sizeof
  end
  
# log_record_back
# $arg0 (in)  : LOG_RECORD_HEADER *
# $arg1 (out) : Previous PAGEID
# $arg2 (out) : Previous OFFSET
# $arg3 (out) : Previous LOG_RECORD_HEADER *
#
# Go to previous log record.
#
# Prerequisite:
# log_record_get_header
#
define log_record_back
  set $arg1 = $arg0->back_lsa.pageid
  set $arg2 = $arg0->back_lsa.offset
  if $arg1 != -1
    log_record_get_header $arg1 $arg2 $arg3
  else
    printf "Cannot go back\n"
    end
  end
  
# log_record_forward
# $arg0 (in)  : LOG_RECORD_HEADER *
# $arg1 (out) : Previous PAGEID
# $arg2 (out) : Previous OFFSET
# $arg3 (out) : Previous LOG_RECORD_HEADER *
#
# Go to next log record.
#
# Prerequisite:
# log_record_get_header
#
define log_record_forward
  set $arg1 = $arg0->forw_lsa.pageid
  set $arg2 = $arg0->forw_lsa.offset
  log_record_get_header $arg1 $arg2 $arg3
  if $arg1 != -1
    log_record_get_header $arg1 $arg2 $arg3
  else
    printf "Cannot go forward\n"
    end
  end

# log_advance_if_not_fit
# $arg0 (in/out) : LOG_PAGEID.
# $arg1 (in/out) : LOG_OFFSET.
# $arg2 (in/out) : LOG_PAGE
# $arg3 (in)     : Data size
#
# Advance to next page when given size doesn't fit current page.
#
# Prerequisite:
# logpb_get_page
#
define log_advance_if_not_fit
  set $lpid = $arg0
  set $loff = $arg1
  set $size = $arg3
  if $loff + $size >= 16368
    set $lpid = $lpid + 1
    set $offset = 0
    logpb_get_page $lpid $lpage
    end
  set $arg0 = $lpid
  set $arg1 = $loff
  set $arg2 = $lpage
  end

# log_record_data
# $arg0 (in)  : LOG_PAGEID
# $arg1 (in)  : LOG_OFFSET
# $arg2 (out) : struct log_data * OR 0
#
# Get log record data.
#
# Prerequisite:
# logpb_get_page
# log_record_get_header
# log_sizeof_rectype_data
# log_advance_if_not_fit
#
define log_record_data
  set $lpgid = $arg0
  set $loff = $arg1
  logpb_get_page $lpgid $lpage
  log_record_get_header $lpgid $loff $lheader
  log_sizeof_rectype_data $lheader->type $data_size
  if $data_size == 0
    p "Cannot handle rectype:"
    p $lheader->type
    set $arg2 = 0
  else
    log_advance_if_not_fit $lpgid $loff $lpage $data_size
    set $arg2 = (struct log_data *) $lpage->area + $loff
    end
  end

# log_next_record_for_page
# $arg0 (in)  : VOLID
# $arg1 (in)  : PAGEID
# $arg2 (in)  : LOG_PAGEID
# $arg3 (in)  : LOG_OFFSET
# $arg4 (in)  : 0 for backward direction, otherwise forward direction
#
# Get previous/next record which belongs to given page.
#
# Prerequisites:
# log_record_get_header
# log_record_forward
# log_record_back
# log_record_data
#
define log_next_record_for_page
  set $volid = $arg0
  set $pgid = $arg1
  set $lpgid = $arg2
  set $loff = $arg3
  set $fwd_dir = $arg4
  set $ldata = 0
  log_record_get_header $lpgid $loff $lheader

  while 1
    if $fwd_dir != 0
      log_record_forward $lheader $lpgid $loff $lheader
    else
      log_record_back $lheader $lpgid $loff $lheader
      end

    if $lpgid == -1
      printf "Search ended"
      loop_break
      end

    set $lpgid_save = $lpgid
    set $loff_save = $loff
    log_record_data $lpgid $loff $ldata

    if $ldata != 0 && $ldata->pageid == $pgid && $ldata->volid == $volid
      p *$ldata
      p $lpgid_save
      p $loff_save
      p *$lheader
      loop_break
      end
    end
  end

# log_next_record_for_oid
# $arg0 (in)  : VOLID
# $arg1 (in)  : PAGEID
# $arg2 (in)  : SLOTID
# $arg3 (in)  : LOG_PAGEID
# $arg4 (in)  : LOG_OFFSET
# $arg5 (in)  : 0 for backward direction, otherwise forward direction
#
# Get previous/next record which belongs to given OID.
#
# Prerequisites:
# log_record_get_header
# log_record_forward
# log_record_back
# log_record_data
#  
define log_next_record_for_oid
  set $volid = $arg0
  set $pgid = $arg1
  set $slotid = $arg2
  set $lpgid = $arg3
  set $loff = $arg4
  set $fwd_dir = $arg5
  set $ldata = 0
  log_record_get_header $lpgid $loff $lheader

  while 1
    if $fwd_dir != 0
      log_record_forward $lheader $lpgid $loff $lheader
    else
      log_record_back $lheader $lpgid $loff $lheader
      end

    if $lpgid == -1
      printf "Search ended"
      loop_break
      end

    set $lpgid_save = $lpgid
    set $loff_save = $loff
    log_record_data $lpgid $loff $ldata

    if $ldata != 0 && $ldata->pageid == $pgid && $ldata->volid == $volid \
       && ($ldata->offset == $slotid || $ldata->offset == ($slotid | 0x8000))
      p *$ldata
      p $lpgid_save
      p $loff_save
      p *$lheader
      loop_break
      end
    end
  end

# log_find_prior_node
# $arg0 (in) : LOG_PAGEID
# #arg1 (in) : LOG_OFFSET
#
# Find a node in log prior list to match the given LSA
#
define log_find_prior_node
  set $log_pageid = $arg0
  set $log_offset = $arg1
  
  set $node = log_Gl.prior_info.prior_list_header
  while $node != 0
    if $node.start_lsa.pageid == $log_pageid && $node.start_lsa.offset == $log_offset
      p *$node
      loop_break
      end
    set $node = $node->next
    end
  end

# log_find_prior_node
# $arg0 (in) : VOLID
# #arg1 (in) : PAGEID
#
# Print all nodes in log prior list for the given page
#
define log_find_prior_node_by_page
  set $node = log_Gl.prior_info.prior_list_header
  while $node != 0
    set $log_data = (LOG_DATA *) $node->data_header
    if $log_data != 0 && $log_data.pageid == $arg1 && $log_data.volid == $arg0
      p *$node
      end
    set $node = $node->next
    end
  end