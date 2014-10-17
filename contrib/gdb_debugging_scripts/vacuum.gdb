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
define vacuum_print_entries
  set $n = vacuum_Data->n_table_entries
  set $i = 0
  while $i < $n
    set $entry = vacuum_Data->vacuum_data_table + $i
    printf "blockid=%lld, start_lsa=(%lld, %d), oldest=%d, newest=%d\n", \
           $entry->blockid & 0x3FFFFFFFFFFFFFFF, \
           (long long int) $entry->start_lsa.pageid, \
           (int) $entry->start_lsa.offset, \
           $entry->oldest_mvccid, $entry->newest_mvccid
    set $i=$i+1
  end
end

# vacuum_print_entries_raw
# No arguments
#
# Print all vacuum data entries. Blockids flags are not cleared.
#
define vacuum_print_entries_raw
 p *(VACUUM_DATA_ENTRY *) vacuum_Data->vacuum_data_table@vacuum_Data->n_table_entries
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