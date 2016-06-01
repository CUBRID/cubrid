#
# GDB Vacuum scripts.
#
 

#
# Vacuum data section
#

# vacuum_print_entries
# No arguments
#
# Print all vacuum data entries.
#
# Prerequisite:
# pgbuf_find_page
#
define vacuum_print_entries
  set $datap = vacuum_Data.first_page
  while 1
    p *$datap
    set $i = $datap->index_unvacuumed
    while $i < $datap->index_free
      set $entry = $datap->data + $i
      printf "blockid=%lld, flags = %llx, start_lsa=(%lld, %d), oldest=%d, newest=%d\n", \
              $entry->blockid & 0x1FFFFFFFFFFFFFFF, \
              $entry->blockid & 0xE000000000000000, \
              (long long int) $entry->start_lsa.pageid, \
              (int) $entry->start_lsa.offset, \
              $entry->oldest_mvccid, $entry->newest_mvccid
      set $i = $i + 1
      end
    set $np = &$datap->next_page
    if ($np->pageid == -1)
      loop_break
      end
    pgbuf_find_page $np.pageid $np.volid $page
    set $datap = (VACUUM_DATA_PAGE *) $page
    end
  end

#
# Dropped files section
#
 
# vacuum_print_tracked_drop_file
# $arg0 (in) : VFID *
#
# Print all dropped files with VFID from tracked dropped files.
# NOTE: Debug only
#
define vacuum_print_tracked_drop_file
  set $track=vacuum_Track_dropped_files
  while $track != 0
    set $i=0
    while $i < $track->dropped_data_page.n_dropped_files
        if $arg0->volid == $track->dropped_data_page.dropped_files[$i].vfid.volid \
           && $arg0->fileid == $track->dropped_data_page.dropped_files[$i].vfid.fileid
          p $track->dropped_data_page.dropped_files[$i]
        end
      set $i=$i+1
    end
    set $track=$track->next_tracked_page
  end
end

# vacuum_print_tracked_drop_file2
# $arg0 (in) : VFID *
# $arg1 (in) : MVCCID
#
# Print dropped files with VFID from tracked dropped files but only if their MVCCID succedes given MVCCID.
# NOTE: Debug only
#
define vacuum_print_tracked_drop_file2
  set $track=vacuum_Track_dropped_files
  while $track != 0
    set $i=0
    while $i < $track->dropped_data_page.n_dropped_files
        if $arg0->volid == $track->dropped_data_page.dropped_files[$i].vfid.volid 
           && $arg0->filed == $track->dropped_data_page.dropped_files[$i].vfid.fileid \
           && $arg1 <= $track->dropped_data_page.dropped_files[$i].mvccid
          p $track->dropped_data_page.dropped_files[$i]
        end
      set $i=$i+1
    end
    set $track=$track->next_tracked_page
  end
end

# vacuum_print_tracked_drop_files
# No arguments
#
# Print all tracked dropped files.
# NOTE: Debug only
#
define vacuum_print_tracked_drop_files
  set $track=vacuum_Track_dropped_files
  while $track != 0
    set $i=0
    while $i < $track->dropped_data_page.n_dropped_files
      p $track->dropped_data_page.dropped_files[$i]
      set $i=$i+1
    end
    set $track=$track->next_tracked_page
  end
end

# vacuum_drop_file_get_next_page
# $arg0 (in)  : PAGE_PTR
# $arg1 (out) : VPID * of next page.
#
# Get VPID of next page of dropped files.
#
define vacuum_drop_file_get_next_page
  set $drop_files_page = (VACUUM_DROPPED_FILES_PAGE*) $arg0
  set $arg1 = &$dropped_files_page->next_page
end

# vacuum_print_drop_file_in_page
# $arg0 (in) : PAGE_PTR
# $arg1 (in) : VFID *
#
# Find and print dropped file in given page.
#
define vacuum_print_drop_file_in_page
  set $drop_files_page = (VACUUM_DROPPED_FILES_PAGE*) $arg0
  set $i=0
  while $i < $drop_files_page->n_dropped_files
    if $arg1->volid == $drop_files_page->dropped_files[$i].vfid.volid \
       && $arg1->fileid == $drop_files_page->dropped_files[$i].vfid.fileid
      p $drop_files_page->dropped_files[$i]
    end
    set $i = $i + 1
  end
end

# vacuum_print_drop_file
# $arg0 (in) : VFID *
#
# Find and print dropped file in all dropped files pages.
#
# Prerequisite:
# vacuum_print_drop_file_in_page
# vacuum_drop_file_get_next_page
# pgbuf_find_page (page_buffer.gdb).
#
define vacuum_print_drop_file
  set $vpid = &vacuum_Dropped_files_vpid
  while $vpid->volid != -1 && $vpid->pageid != -1
    pgbuf_find $vpid $page
    if $page == 0
      printf "Couldn't find page (%d, %d) in page buffer.\n", $vpid->volid, $vpid->pageid
      loop_break
    end
    vacuum_print_drop_file_in_page $page
    vacuum_drop_file_get_next_page $page $vpid
  end
end

# vacuum_worker_copy_log_page
# $arg0 (in)  : LOG_PAGEID
# $arg1 (in)  : BLOCK_LOG_BUFFER *
# $arg2 (in)  : VACUUM_WORKER *
# $arg3 (out) : LOG_PAGE *
#
# Equivalent for vacuum_copy_log_page.
# NOTE: NULL is returned if page is not prefetched.
# NOTE: Function works only if prefetch is done by workers.
#
define vacuum_worker_copy_log_page
  set $pageid = $arg0
  set $buffer = $arg1
  set $worker = $arg2
  set $arg3 = 0
  if $pageid >= $buffer->start_page && $pageid <= $buffer->last_page
    set $arg3 = (LOG_PAGE *) ($worker->prefetch_log_buffer + (($pageid - $buffer->start_page) * 16384))
  end
end

# vacuum_worker_process_log_record
# $arg0 (in)  : LOG_PAGEID
# $arg1 (in)  : LOG_OFFSET
# $arg2 (in)  : BLOCK_LOG_BUFFER *
# $arg3 (in)  : VACUUM_WORKER *
# $arg4 (out) : LOG_RECORD_HEADER *
# $arg5 (out) : LOG_DATA *
# $arg6 (out) : MVCCID
# $arg7 (out) : LOG_VACUUM_INFO *
#
# Process log record for vacuum
#
# Prerequisite:
# vacuum_worker_copy_log_page
#
define vacuum_worker_process_log_record
  set $pageid = $arg0
  set $offset = $arg1
  set $buffer = $arg2
  set $worker = $arg3
  vacuum_worker_copy_log_page $pageid $buffer $worker $lp
  set $arg4 = (LOG_RECORD_HEADER *) ($lp->area + $offset)
  set $offset = $offset + sizeof (LOG_RECORD_HEADER)
  if $offset >= 16368
    set $pageid = $pageid + 1
    vacuum_worker_copy_log_page $pageid $buffer $worker $lp
    set $offset = $offset - 16368
  end
  if $arg4->type == LOG_MVCC_UNDO_DATA
    if $offset + sizeof (struct log_mvcc_undo) >= 16368
      set $pageid = $pageid + 1
      vacuum_worker_copy_log_page $pageid $buffer $worker $lp
      set $offset = 0
    end
    set $arg5 = &(((struct log_mvcc_undo *) ($lp->area + $offset))->undo.data)
    set $arg6 = ((struct log_mvcc_undo *) ($lp->area + $offset))->mvccid
    set $arg7 = &(((struct log_mvcc_undo *) ($lp->area + $offset))->vacuum_info)
  else
    if $offset + sizeof (struct log_mvcc_undoredo) >= 16368
      set $pageid = $pageid + 1
      vacuum_worker_copy_log_page $pageid $buffer $worker $lp
      set $offset = 0
    end
    set $arg5 = &(((struct log_mvcc_undoredo *) ($lp->area + $offset))->undoredo.data)
    set $arg6 = ((struct log_mvcc_undoredo *) ($lp->area + $offset))->mvccid
    set $arg7 = &(((struct log_mvcc_undoredo *) ($lp->area + $offset))->vacuum_info)
  end
end

# vacuum_worker_print_log_data_on_page
# $arg0 (in) : Start LOG_PAGEID
# $arg1 (in) : Start LOG_OFFSET
# $arg2 (in) : BLOCK_LOG_BUFFER *
# $arg3 (in) : VACUUM_WORKER *
# $arg4 (in) : PAGEID
# $arg5 (in) : VOLID
#
# Print all records in current block belonging to given page
#
# Prerequisite:
# vacuum_worker_copy_log_page
# vacuum_worker_process_log_record
#
define vacuum_worker_print_log_data_on_page
  set $log_pageid = $arg0
  set $log_offset = $arg1
  set $buffer = $arg2
  set $worker = $arg3
  set $page_id = $arg4
  set $vol_id = $arg5
  set $start_blockid = $log_pageid / vacuum_Data->log_block_npages
  set $blockid = $start_blockid
  while $blockid == $start_blockid
    vacuum_worker_process_log_record $log_pageid $log_offset $buffer $worker $header $log_data $mvccid $vacinfo
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

# vacuum_worker_print_log_block_records
# $arg0 (in) : Start LOG_PAGEID
# $arg1 (in) : Start LOG_OFFSET
# $arg2 (in) : BLOCK_LOG_BUFFER *
# $arg3 (in) : VACUUM_WORKER *
#
# Print all records in current block
#
# Prerequisite:
# vacuum_worker_copy_log_page
# vacuum_worker_process_log_record
# WARNING: Untested.
#
define vacuum_worker_print_log_block_records
  set $log_pageid = $arg0
  set $log_offset = $arg1
  set $buffer = $arg2
  set $worker = $arg3
  set $start_blockid = $log_pageid / vacuum_Data->log_block_npages
  set $blockid = $start_blockid
  while $blockid == $start_blockid
    vacuum_worker_process_log_record $log_pageid $log_offset $buffer $worker $header $log_data $mvccid $vacinfo
    p *$log_data
    p $mvccid
    set $log_pageid = $vacinfo->prev_mvcc_op_log_lsa.pageid
    set $log_offset = $vacinfo->prev_mvcc_op_log_lsa.offset
    if $log_pageid == -1
      set $blockid = -1
    else
      set $blockid = $log_pageid / vacuum_Data->log_block_npages
      end
    end
  end

# vacuum_process_log_record
# $arg0 (in)  : LOG_PAGEID
# $arg1 (in)  : LOG_OFFSET
# $arg4 (out) : LOG_RECORD_HEADER *
# $arg5 (out) : LOG_DATA *
# $arg6 (out) : MVCCID
# $arg7 (out) : LOG_VACUUM_INFO *
#
# Process log record for vacuum
#
# Prerequisite:
# logpb_get_page
#
define vacuum_process_log_record
  set $pageid = $arg0
  set $offset = $arg1
  logpb_get_page $pageid $lp
  set $arg4 = (LOG_RECORD_HEADER *) ($lp->area + $offset)
  set $offset = $offset + sizeof (LOG_RECORD_HEADER)
  if $offset >= 16368
    set $pageid = $pageid + 1
    logpb_get_page $pageid $lp
    set $offset = $offset - 16368
  end
  if $arg4->type == LOG_MVCC_UNDO_DATA
    if $offset + sizeof (struct log_mvcc_undo) >= 16368
      set $pageid = $pageid + 1
      logpb_get_page $pageid $lp
      set $offset = 0
    end
    set $arg5 = &(((struct log_mvcc_undo *) ($lp->area + $offset))->undo.data)
    set $arg6 = ((struct log_mvcc_undo *) ($lp->area + $offset))->mvccid
    set $arg7 = &(((struct log_mvcc_undo *) ($lp->area + $offset))->vacuum_info)
  else
    if $offset + sizeof (struct log_mvcc_undoredo) >= 16368
      set $pageid = $pageid + 1
      logpb_get_page $pageid $lp
      set $offset = 0
    end
    set $arg5 = &(((struct log_mvcc_undoredo *) ($lp->area + $offset))->undoredo.data)
    set $arg6 = ((struct log_mvcc_undoredo *) ($lp->area + $offset))->mvccid
    set $arg7 = &(((struct log_mvcc_undoredo *) ($lp->area + $offset))->vacuum_info)
  end
end

# vacuum_print_log_records_on_page
# $arg0 (in) : Start LOG_PAGEID
# $arg1 (in) : Start LOG_OFFSET
# $arg4 (in) : PAGEID
# $arg5 (in) : VOLID
#
# Print all records in current block belonging to given page
#
# Prerequisite:
# logpb_get_page
# vacuum_process_log_record
#
define vacuum_print_log_data_on_page
  set $log_pageid = $arg0
  set $log_offset = $arg1
  set $page_id = $arg4
  set $vol_id = $arg5
  set $start_blockid = $log_pageid / vacuum_Data->log_block_npages
  set $blockid = $start_blockid
  while $blockid == $start_blockid
    vacuum_process_log_record $log_pageid $log_offset $header $log_data $mvccid $vacinfo
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

# vacuum_print_log_block_records
# $arg0 (in) : Start LOG_PAGEID
# $arg1 (in) : Start LOG_OFFSET
#
# Print all records in current block
#
# Prerequisite:
# logpb_get_page
# vacuum_process_log_record
# WARNING: Untested.
#
define vacuum_print_log_block_records
  set $log_pageid = $arg0
  set $log_offset = $arg1
  set $start_blockid = $log_pageid / vacuum_Data->log_block_npages
  set $blockid = $start_blockid
  while $blockid == $start_blockid
    vacuum_process_log_record $log_pageid $log_offset $header $log_data $mvccid $vacinfo
    p *$log_data
    p $mvccid
    set $log_pageid = $vacinfo->prev_mvcc_op_log_lsa.pageid
    set $log_offset = $vacinfo->prev_mvcc_op_log_lsa.offset
    if $log_pageid == -1
      set $blockid = -1
    else
      set $blockid = $log_pageid / vacuum_Data->log_block_npages
      end
    end
  end