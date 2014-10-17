#
# GDB slotted page scripts.
#
 
 
# spage_print_header
# $arg0 (in) : PAGE_PTR
#
# Print slotted page header.
#
define spage_print_header
  p *(SPAGE_HEADER*) $arg0
end


# spage_print_slot
# $arg0 (in) : PAGE_PTR
# $arg1 (in) : Slot ID.
#
# Print page slot at slot ID.
#
define spage_print_slot
  p *(((SPAGE_SLOT*) ($arg0 + db_User_page_size - sizeof (SPAGE_SLOT))) - $arg1)
end

# spage_get_slot
# $arg0 (in)  : PAGE_PTR
# $arg1 (in)  : Slot ID.
# $arg2 (out) : SPAGE_SLOT *
#
# Get page slot at slot ID.
#
define spage_get_slot
  set $arg2 = (((SPAGE_SLOT*) ($arg0 + db_User_page_size - sizeof (SPAGE_SLOT))) - $arg1)
end

# spage_print_all_slots
# $arg0 (in) : PAGE_PTR
#
# Print all slots in page.
#
define spage_print_all_slots
  set $num_slots=((SPAGE_HEADER*) $arg0)->num_slots
  p *(((SPAGE_SLOT*) ($arg0 + db_User_page_size)) - $num_slots)@$num_slots
end