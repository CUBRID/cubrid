#
# GDB debugging scripts for file manager
#

# file_get_header
# $arg0 (in)  : VOLID
# $arg1 (in)  : FILEID
# $arg2 (out) : File header page
# $arg3 (out) : File header
#
# Find volume header page and get volume header
#
# Prerequisite:
# pgbuf_find_page
#
define file_get_header
  pgbuf_find_page $arg1 $arg0 $arg2
  set $arg3 = (FLRE_HEADER *) $arg2
  p *$arg3
  end

# file_header_part_ftab
# $arg0 (in)  : FILE HEADER
# $arg1 (out) : Partiable table extensible data
#
define file_header_part_ftab
  set $fhead = $arg0
  set $off = $fhead->offset_to_partial_ftab
  set $arg1 = (FILE_EXTENSIBLE_DATA *) (((char *) $fhead) + $off)
  end

# file_header_full_ftab
# $arg0 (in)  : FILE HEADER
# $arg1 (out) : Partiable table extensible data
#
define file_header_full_ftab
  set $fhead = $arg0
  set $off = $fhead->offset_to_full_ftab
  if $off < 0
    printf "no full table"
  else
    set $arg1 = (FILE_EXTENSIBLE_DATA *) (((char *) $fhead) + $off)
    end
  end
  
# file_header_user_ftab
# $arg0 (in)  : FILE HEADER
# $arg1 (out) : Partiable table extensible data
#
define file_header_user_ftab
  set $fhead = $arg0
  set $off = $fhead->offset_to_user_page_ftab
  if $off < 0
    printf "no user page table"
  else
    set $arg1 = (FILE_EXTENSIBLE_DATA *) (((char *) $fhead) + $off)
    end
  end
  
# file_print_tracker
#
# Prerequisite:
# pgbuf_find_page
#
define file_print_tracker
  pgbuf_find_page flre_Tracker_vpid.pageid flre_Tracker_vpid.volid $trkp
  set $ext = (FILE_EXTENSIBLE_DATA *) $trkp
  while 1
    p *$ext
    set $itemp = (char *) ($ext + 1)
    set $endp = $itemp + $ext->size_of_item * $ext->n_items
    while $itemp < $endp
      set $item = (FLRE_TRACK_ITEM *) $itemp
      set $ftype = $item->type
      set $is_markdel = $ftype == 1 && $item->metadata.heap.is_marked_deleted
      printf "VFID = %d|%d, type = %d, marked_deleted = %d\n", $item->volid, $item->fileid, $ftype, $is_markdel
      set $itemp = $itemp + $ext->size_of_item
      end
    set $vpid = &$ext->vpid_next
    if $vpid->pageid == -1
      loop_break
      end
    pgbuf_find_page $vpid->pageid $vpid->volid $trkop
    set $ext = (FILE_EXTENSIBLE_DATA *) $trkop
    end
  end