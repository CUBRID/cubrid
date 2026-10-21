/*
 * Copyright 2016 CUBRID Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _TEST_OOS_PERSISTED_IDENTITY_HPP_
#define _TEST_OOS_PERSISTED_IDENTITY_HPP_

#include <array>

/* Meaningful stored fields only. This fixed six-row crash fixture crosses a pipe;
 * object padding is never an equality oracle and an unparsed reference stays absent. */
namespace test_oos_persisted_identity
{
  struct reference
  {
    int is_oos;
    int width;
    bool parsed;
    int volume;
    int page;
    int slot;
    long long length;
    long long stamp_page;
    int stamp_offset;
    std::array<unsigned char, 8> vot;
    bool has_stub_bytes;
    std::array<unsigned char, 24> stub;
    int attribute_id;
    int var_index;
    int schema_position;
    int schema_def_order;
    int schema_type;
  };

  struct row
  {
    int id;
    int volume;
    int page;
    int slot;
    bool has_oos;
    std::array<reference, 2> refs;
    std::array<unsigned char, 4> header;
    std::array<unsigned char, 4> fixed_id;
    int header_size;
    int offset_size;
    int record_length;
    std::array<int, 3> class_oid;
    int representation_id;
    int n_attributes;
    int n_variable;
    int id_attribute_id;
    int id_fixed_location;
  };

  using snapshot = std::array<row, 6>;

  inline bool valid_reference (const reference &ref, int serialized_bytes)
  {
    return ref.is_oos == 1 && ref.width == 24 && ref.parsed && ref.has_stub_bytes
	   && ref.volume >= 0 && ref.page >= 0 && ref.slot >= 0 && ref.length == serialized_bytes;
  }

  inline bool same_reference (const reference &before, const reference &after)
  {
    return before.parsed && after.parsed && before.is_oos == after.is_oos && before.width == after.width
	   && before.volume == after.volume && before.page == after.page && before.slot == after.slot
	   && before.length == after.length && before.stamp_page == after.stamp_page
	   && before.stamp_offset == after.stamp_offset && before.attribute_id == after.attribute_id
	   && before.var_index == after.var_index && before.schema_position == after.schema_position
	   && before.schema_def_order == after.schema_def_order && before.schema_type == after.schema_type;
  }

  inline bool valid_binding (const row &current)
  {
    if (current.class_oid[0] < 0 || current.class_oid[1] < 0 || current.class_oid[2] < 1
	|| current.representation_id < 1 || current.n_attributes != 3 || current.n_variable != 2
	|| current.id_attribute_id < 0 || current.id_fixed_location != 0)
      {
	return false;
      }
    for (int logical = 0; logical < 2; ++logical)
      {
	const reference &ref = current.refs[logical];
	if (ref.attribute_id < 0 || ref.attribute_id == current.id_attribute_id || ref.var_index < 0
	    || ref.var_index >= current.n_variable || ref.schema_position != 1 + ref.var_index
	    || ref.schema_def_order != logical + 1)
	  {
	    return false;
	  }
      }
    return current.refs[0].attribute_id != current.refs[1].attribute_id
	   && current.refs[0].var_index != current.refs[1].var_index;
  }

  inline bool valid_rows (const snapshot &rows)
  {
    for (int i = 0; i < 6; ++i)
      {
	const row &current = rows[i];
	if (!valid_binding (current) || current.id != i + 1 || current.volume < 0 || current.page < 0
	    || current.slot < 1 || !current.has_oos
	    || !valid_reference (current.refs[0], 30008) || !valid_reference (current.refs[1], 4508))
	  {
	    return false;
	  }
	for (int j = 0; j < i; ++j)
	  {
	    if (current.volume == rows[j].volume && current.page == rows[j].page && current.slot == rows[j].slot)
	      {
		return false;
	      }
	  }
      }
    return true;
  }
}

#endif // _TEST_OOS_PERSISTED_IDENTITY_HPP_
