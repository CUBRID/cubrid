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
define file_header_part_ftab
  set $fhead = $arg0
  set $off = $fhead->offset_to_full_ftab
  if $off < 0
    printf "no full table"
  else
    set $arg1 = (FILE_EXTENSIBLE_DATA *) (((char *) $fhead) + $off)
    end
  end
  
# file_header_part_ftab
# $arg0 (in)  : FILE HEADER
# $arg1 (out) : Partiable table extensible data
#
define file_header_part_ftab
  set $fhead = $arg0
  set $off = $fhead->offset_to_user_page_ftab
  if $off < 0
    printf "no user page table"
  else
    set $arg1 = (FILE_EXTENSIBLE_DATA *) (((char *) $fhead) + $off)
    end
  end