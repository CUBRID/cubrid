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

# string_equal
# $arg0 (in)  : First string
# $arg1 (in)  : Second string
#
# Compare two given strings.
# NOTE: Arguments should be real pointers. Giving constant string (e.g. "t1") might not work.
# TODO: Move to a proper script file.
#
define string_equal
  set $str1 = (char *) $arg0
  set $str2 = (char *) $arg1
  set $ch = 0
  while $str1[$ch] != 0 && $str2[$ch] != 0 && $str1[$ch] == $str2[$ch]
    set $ch = $ch + 1
    end
  if $str1[$ch] == 0 && $str2[$ch] == 0
    set $arg2 = 1
  else
    set $arg2 = 0
    end
  end

# lc_names_find
# $arg0 (in)  : Table name (char *)
#
# Find locator classname entry for given name
#
define lc_names_find
  set $i = 0
  set $found = 0
  while $i < locator_Mht_classnames->size && $found == 0
    set $he = locator_Mht_classnames->table[$i]
      while $he != 0 && $found == 0
        set $he_name = (char *) $he->key
        string_equal $he_name $arg0 $found
        if $found == 1
          p (char *) $he->key
          p (LOCATOR_CLASSNAME_ENTRY *) $he->data
          p *(LOCATOR_CLASSNAME_ENTRY *) $he->data
          end
        set $he = $he->next
        end
    set $i = $i + 1
    end
  end
