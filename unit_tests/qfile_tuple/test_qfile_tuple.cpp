/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "config.h"

#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include "catch2/catch.hpp"

#include "qfile_tuple_layout.h"

TEST_CASE ("Tuple record initializer leaves the position cache unstarted", "[qfile_tuple]")
{
  QFILE_TUPLE_RECORD slot = QFILE_TUPLE_RECORD_INITIALIZER;

  REQUIRE (slot.tpl == nullptr);
  REQUIRE (slot.size == 0);
  REQUIRE (slot.type_list == nullptr);
  REQUIRE (slot.cached_column_index_in_tuple == -1);
  REQUIRE (slot.fixed_length_col_cnt == 0);
  REQUIRE (slot.data_off == 0);
  REQUIRE_FALSE (slot.has_null);
  REQUIRE (slot.cached_byte_offset_in_tuple == 0);
}

TEST_CASE ("Bound tuple records read fixed and variable columns and reset on tuple changes", "[qfile_tuple]")
{
  // A fixed INTEGER followed by a VAR/DIRECT body; the second tuple has a NULL INTEGER.
  QFILE_COL_LAYOUT columns[] =
  {
    { 0, 4, QFILE_COL_FIXED, QFILE_VALUE_DIRECT, 4, DB_TYPE_INTEGER },
    { -1, -1, QFILE_COL_VAR, QFILE_VALUE_DIRECT, 1, DB_TYPE_VARCHAR }
  };
  QFILE_TUPLE_VALUE_TYPE_LIST layout = {};
  layout.type_cnt = 2;
  layout.column_layout_array = columns;
  layout.max_fixed_length_col_cnt = 1;
  layout.data_off[0] = 4;
  layout.data_off[1] = 8;
  layout.bitmap_size = 1;
  layout.hdr_size = QFILE_TUPLE_HDR_SIZE_FORWARD;
  layout.layout_ready = true;

  alignas (4) char first[12] = {};
  alignas (4) char second[16] = {};
  QFILE_PUT_TUPLE_LENGTH (first, sizeof (first), false);
  OR_PUT_INT (first + 4, 42);
  qfile_var_hdr_encode (first + 8, 3, 1);
  memcpy (first + 9, "abc", 3);
  QFILE_PUT_TUPLE_LENGTH (second, sizeof (second), true);
  QFILE_BITMAP_SET_BOUND (second + layout.hdr_size, 1);
  qfile_var_hdr_encode (second + 8, 6, 1);
  memcpy (second + 9, "longer", 6);

  QFILE_TUPLE_RECORD slot = QFILE_TUPLE_RECORD_INITIALIZER;
  int length;
  bool is_null;
  const char *data;

  SECTION ("Bind layout and tuple separately")
  {
    qfile_slot_set_layout (&slot, &layout);
    qfile_slot_set_tuple_ptr (&slot, first);
  }
  SECTION ("Bind tuple and layout together")
  {
    qfile_slot_set_tuple_ptr_and_layout (&slot, first, &layout);
  }

  REQUIRE (slot.cached_column_index_in_tuple == -1);
  data = qfile_slot_get_column_data (&slot, 0, &length, &is_null);
  REQUIRE_FALSE (is_null);
  REQUIRE (length == 4);
  REQUIRE (data == first + 4);
  REQUIRE (OR_GET_INT (data) == 42);
  REQUIRE (slot.fixed_length_col_cnt == 1);
  data = qfile_slot_get_column_data (&slot, 1, &length, &is_null);
  REQUIRE_FALSE (is_null);
  REQUIRE (length == 3);
  REQUIRE (memcmp (data, "abc", length) == 0);
  REQUIRE (slot.cached_column_index_in_tuple == 1);

  // Changing tuples invalidates the old no-NULL prefix and cached offset.
  qfile_slot_set_tuple_ptr (&slot, second);
  REQUIRE (slot.type_list == &layout);
  REQUIRE (slot.cached_column_index_in_tuple == -1);
  data = qfile_slot_get_column_data (&slot, 0, &length, &is_null);
  REQUIRE (is_null);
  REQUIRE (length == 0);
  REQUIRE (slot.has_null);
  REQUIRE (slot.data_off == 8);
  REQUIRE (slot.fixed_length_col_cnt == 0);
  REQUIRE (slot.cached_column_index_in_tuple == 0); // Zero is a valid started cache position.
  data = qfile_slot_get_column_data (&slot, 1, &length, &is_null);
  REQUIRE_FALSE (is_null);
  REQUIRE (length == 6);
  REQUIRE (memcmp (data, "longer", length) == 0);

  // Start with a VAR read after rebinding, then read backwards into the fixed prefix.
  qfile_slot_set_tuple_ptr_and_layout (&slot, first, &layout);
  REQUIRE (slot.cached_column_index_in_tuple == -1);
  data = qfile_slot_get_column_data (&slot, 1, &length, &is_null);
  REQUIRE_FALSE (is_null);
  REQUIRE (length == 3);
  REQUIRE (memcmp (data, "abc", length) == 0);
  data = qfile_slot_get_column_data (&slot, 0, &length, &is_null);
  REQUIRE_FALSE (is_null);
  REQUIRE (length == 4);
  REQUIRE (OR_GET_INT (data) == 42);
}
