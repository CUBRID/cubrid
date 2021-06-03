/*
 *
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

#include "jsp_struct.hpp"

namespace cubprocedure
{

//////////////////////////////////////////////////////////////////////////
// header
//////////////////////////////////////////////////////////////////////////

  sp_header::sp_header ()
    : command (0)
    , size (0)
  {

  }

  void
  sp_header::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (command);
    serializator.pack_int (size);
  }

  size_t
  sp_header::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset);
    size += serializator.get_packed_int_size (size);
    return size;
  }

  void
  sp_header::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (command);
    deserializator.unpack_int (size);
  }

//////////////////////////////////////////////////////////////////////////
// args
//////////////////////////////////////////////////////////////////////////

  sp_args::sp_args ()
    : name (nullptr),
      args (nullptr),
      arg_count (0),
      return_type (0)
  {
    memset (arg_mode, 0, sizeof (int) * MAX_ARG_COUNT);
    memset (arg_type, 0, sizeof (int) * MAX_ARG_COUNT);
  }

  int
  sp_args::get_argument_count () const
  {
    int count = 0;
    for (db_arg_list *p = args; p != NULL; p = p->next)
      {
	count++;
      }
    return count;
  }

  void
  sp_args::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_c_string (name, strlen (name));
    serializator.pack_int (get_argument_count ());

    sp_value sp_val;
    db_arg_list *p = args;
    int i = 0;
    while (p != NULL)
      {
	serializator.pack_int (arg_mode[i]);
	serializator.pack_int (arg_type[i]);

	sp_val.value = p->val;
	sp_val.pack (serializator);

	i++;
	p = p->next;
      }

    serializator.pack_int (return_type);
  }

  size_t
  sp_args::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_c_string_size (name, strlen (name), start_offset);
    size += serializator.get_packed_int_size (size); /* argument count */

    sp_value sp_val;
    db_arg_list *p = args;
    while (p != NULL)
      {
	size += serializator.get_packed_int_size (size); /* arg_mode */
	size += serializator.get_packed_int_size (size); /* arg_type */

	sp_val.value = p->val;
	size += sp_val.get_packed_size (serializator, size); /* value */
	p = p->next;
      }

    size += serializator.get_packed_int_size (size); /* return_type */
    return size;
  }

  void
  sp_args::unpack (cubpacking::unpacker &deserializator)
  {
    // TODO : Future work in another subtasks
    assert (false);
  }
}
