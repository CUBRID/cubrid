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

#include "pl_signature.hpp"

#include "memory_alloc.h"
#include "memory_private_allocator.hpp"

#define CHECK_NULL_AND_FREE(val)                        \
  do {                                                  \
    if (val != nullptr)                                 \
    {                                                   \
      db_private_free_and_init (NULL, val);             \
    }                                                   \
  } while(0)

namespace cubpl
{
  static const char *EMPTY_STRING = "";

  pl_arg::pl_arg ()
    : arg_size {0}
    , arg_mode {nullptr}
    , arg_type {nullptr}
    , arg_default_value_size {nullptr}
    , arg_default_value {nullptr}
  {}

  pl_arg::pl_arg (int num_args)
    : arg_size {num_args}
  {
    arg_mode = (int *) db_private_alloc (NULL, (num_args) * sizeof (int));
    arg_type = (int *) db_private_alloc (NULL, (num_args) * sizeof (int));
    arg_default_value_size = (char **) db_private_alloc (NULL, (param_cnt) * sizeof (char *));
    arg_default_value = (int *) db_private_alloc (NULL, (param_cnt) * sizeof (int));
  }

  pl_arg::~pl_arg ()
  {
    if (arg_size > 0)
      {
	CHECK_NULL_AND_FREE (arg_mode);
	CHECK_NULL_AND_FREE (arg_type);
	CHECK_NULL_AND_FREE (arg_default_value_size);
	for (int i = 0; i < arg_size; i++)
	  {
	    if (arg_default_value_size[i] > 0)
	      {
		CHECK_NULL_AND_FREE (arg_default_value[i]);
	      }
	  }
	CHECK_NULL_AND_FREE (arg_default_value);
      }
  }

  void
  pl_arg::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (arg_size);

    if (arg_size > 0)
      {
	serializator.pack_int_array (arg_mode, arg_size);
	serializator.pack_int_array (arg_type, arg_size);
	serializator.pack_int_array (arg_default_value_size, arg_size);
	for (int i = 0; i < arg_size; i++)
	  {
	    if (arg_default_value_size[i] > 0)
	      {
		serializator.pack_c_string (arg_default_value[i], arg_default_value_size[i]);
	      }
	    else
	      {
		serializator.pack_c_string (EMPTY_STRING, 0);
	      }
	  }
      }
  }

  void
  pl_arg::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (arg_size);

    if (arg_size > 0)
      {
	arg_mode = (int *) db_private_alloc (NULL, sizeof (int) * (arg_size));
	arg_type = (int *) db_private_alloc (NULL, sizeof (int) * (arg_size));
	arg_default_value_size = (int *) db_private_alloc (NULL, sizeof (int) * (arg_size));
	arg_default_value = (char **) db_private_alloc (NULL, sizeof (char *) * (arg_size));

	int cnt;
	deserializator.unpack_int_array (arg_mode, cnt);
	assert (arg_size == cnt);

	deserializator.unpack_int_array (arg_type, cnt);
	assert (arg_size == cnt);

	deserializator.unpack_int_array (arg_default_value_size, cnt);
	assert (arg_size == cnt);

	for (int i = 0; i < arg_size; i++)
	  {
	    if (arg_default_value_size[i] > 0)
	      {
		arg_default_value[i] = (char *) db_private_alloc (NULL, sizeof (char) * (arg_default_value_size[i]));
		deserializator.unpack_c_string (arg_default_value[i], arg_default_value_size[i]);
	      }
	    else
	      {
		arg_default_value[i] = (char *) EMPTY_STRING;
	      }
	  }
      }
  }

  size_t
  pl_arg::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = 0;
    size += serializator.get_packed_int_size (size); // arg_size
    if (arg_size > 0)
      {
	size += serializator.get_packed_int_vector_size (size, arg_size); // arg_mode
	size += serializator.get_packed_int_vector_size (size, arg_size); // arg_type
	size += serializator.get_packed_int_vector_size (size, arg_size); // arg_default_value_size

	for (int i = 0; i < arg_size; i++)
	  {
	    if (arg_default_value_size[i] > 0)
	      {
		size += serializator.get_packed_c_string_size ((const char *) arg_default_value[i],
			(size_t) arg_default_value_size[i], size);
	      }
	    else
	      {
		size += serializator.get_packed_c_string_size (EMPTY_STRING, 0, size);
	      }
	  }
      }
    return size;
  }

  pl_signature::pl_signature ()
    : name {nullptr}
    , auth {nullptr}
    , pl_type {0}
    , result_type {0}
    , arg {nullptr}
  {}

  pl_signature::~pl_signature ()
  {
    CHECK_NULL_AND_FREE (name);
    CHECK_NULL_AND_FREE (auth);
    CHECK_NULL_AND_FREE (arg);
  }

  void
  pl_signature::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (pl_type);
    serializator.pack_c_string (name, strlen (name));

    serializator.pack_bool (auth != nullptr);
    if (auth)
      {
	serializator.pack_c_string (auth, strlen (auth));
      }

    serializator.pack_int (result_type);

    serializator.pack_bool (arg != nullptr);
    if (arg)
      {
	arg->pack (serializator);
      }
  }

  void
  pl_signature::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (pl_type);

    cubmem::extensible_block name_blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
    deserializator.unpack_string_to_memblock (name_blk);
    name = name_blk.release_ptr ();

    bool has_auth = false;
    deserializator.unpack_bool (has_auth);
    if (has_auth)
      {
	cubmem::extensible_block auth_name_blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
	deserializator.unpack_string_to_memblock (auth_name_blk);
	auth = auth_name_blk.release_ptr ();
      }
    else
      {
	auth = nullptr;
      }

    deserializator.unpack_int (result_type);

    bool has_arg = false;
    deserializator.unpack_bool (has_arg);
    if (has_auth)
      {
	arg->unpack (deserializator);
      }
    else
      {
	arg = nullptr;
      }
  }

  size_t
  pl_signature::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = 0;
    size += serializator.get_packed_int_size (size); /* pl_type */

    size += serializator.get_packed_c_string_size (name, strlen (name), size); // name
    size += serializator.get_packed_bool_size (size); // has auth
    if (auth)
      {
	size += serializator.get_packed_c_string_size (auth, strlen (auth), size);
      }

    size += serializator.get_packed_int_size (size); /* result_type */

    size += serializator.get_packed_bool_size (size); // has arg
    if (arg)
      {
	size += arg->get_packed_size (serializator, size); // arg
      }

    return size;
  }

  pl_signature_array::pl_signature_array ()
    : num_sigs {0}
    , sigs {nullptr}
  {}

  pl_signature_array::~pl_signature_array ()
  {
    CHECK_NULL_AND_FREE (sigs);
  }

  void
  pl_signature_array::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (num_sigs);
    for (int i = 0; i < num_sigs; i++)
      {
	sigs[i].pack (serializator);
      }
  }

  void
  pl_signature_array::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (num_sigs);
    if (num_sigs > 0)
      {
	sigs = (pl_signature *) db_private_alloc (NULL, sizeof (pl_signature) * (num_sigs));
	for (int i = 0; i < num_sigs; i++)
	  {
	    sigs[i].unpack (deserializator);
	  }
      }
  }

  size_t
  pl_signature_array::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = 0;
    size += serializator.get_packed_int_size (size); /* num_sigs */
    for (int i = 0; i < num_sigs; i++)
      {
	size += sigs[i].get_packed_size (serializator, size);
      }
    return size;
  }
}
