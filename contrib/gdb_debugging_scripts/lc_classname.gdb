#
# GDB Locator class name scripts
#

# lc_names_print_all
#
# Print all locator class name entries
#
define lc_names_print_all
  set $i = 0
  while $i < locator_Mht_classnames->size
    set $he = locator_Mht_classnames->table[$i]
      while $he != 0
        p (char *) $he->key
        p (LOCATOR_CLASSNAME_ENTRY *) $he->data
        p *(LOCATOR_CLASSNAME_ENTRY *) $he->data
        set $he = $he->next
        end
    set $i = $i + 1
    end
  end
