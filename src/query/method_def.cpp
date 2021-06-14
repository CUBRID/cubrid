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

#include "method_def.hpp"

#include "memory_private_allocator.hpp"

void
method_sig_list::freemem ()
{
  METHOD_SIG *sig = method_sig;
  while (sig)
    {
      METHOD_SIG *next = sig->next;

      db_private_free_and_init (NULL, sig->method_name);
      if (sig->class_name)
	{
	  db_private_free_and_init (NULL, sig->class_name);
	}
      db_private_free_and_init (NULL, sig->method_arg_pos);
      db_private_free_and_init (NULL, sig); /* itself */

      sig = next;
    }
}

void
method_sig_list::pack (cubpacking::packer &serializator) const
{
  serializator.pack_int (num_methods);

  METHOD_SIG *sig_p = method_sig;
  while (sig_p)
    {
      serializator.pack_c_string (sig_p->method_name, strlen (sig_p->method_name));

      serializator.pack_int (sig_p->method_type);
      serializator.pack_int (sig_p->num_method_args);

      for (int i = 0; i < sig_p->num_method_args + 1; i++)
	{
	  serializator.pack_int (sig_p->method_arg_pos[i]);
	}

      if (sig_p->method_type != METHOD_IS_JAVA_SP)
	{
	  serializator.pack_c_string (sig_p->class_name, strlen (sig_p->class_name));
	}

      sig_p = sig_p->next;
    }
}

size_t
method_sig_list::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
{
  size_t size = serializator.get_packed_int_size (start_offset); /* num_methods */

  METHOD_SIG *sig_p = method_sig;
  while (sig_p)
    {
      size += serializator.get_packed_c_string_size (sig_p->method_name, strlen (sig_p->method_name), size);

      /* method type and num_method_args */
      size += serializator.get_packed_int_size (size);
      size += serializator.get_packed_int_size (size);

      for (int i = 0; i < sig_p->num_method_args + 1; i++)
	{
	  size += serializator.get_packed_int_size (size); /* method_sig->method_arg_pos[i] */
	}

      if (sig_p->method_type != METHOD_IS_JAVA_SP)
	{
	  size += serializator.get_packed_c_string_size (sig_p->class_name, strlen (sig_p->class_name), size);
	}

      sig_p = sig_p->next;
    }
  return size;
}

void
method_sig_list::unpack (cubpacking::unpacker &deserializator)
{
  // assuming that METHOD_SIG_LIST is already allocated

  deserializator.unpack_int (num_methods);

  method_sig = nullptr;
  if (num_methods > 0)
    {
      method_sig = (METHOD_SIG *) db_private_alloc (NULL, sizeof (METHOD_SIG));
      METHOD_SIG *method_sig_p = method_sig;
      for (int i = 0; i < num_methods; i++)
	{
	  method_sig_p->next = nullptr;

	  cubmem::extensible_block method_name_blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
	  deserializator.unpack_string_to_memblock (method_name_blk);
	  method_sig_p->method_name = method_name_blk.release_ptr ();

	  int method_type;
	  deserializator.unpack_int (method_type);
	  method_sig_p->method_type = static_cast<METHOD_TYPE> (method_type);
	  deserializator.unpack_int (method_sig_p->num_method_args);

	  method_sig_p->method_arg_pos = (int *) db_private_alloc (NULL, sizeof (int) * (method_sig_p->num_method_args + 1));
	  for (int n = 0; n < method_sig->num_method_args + 1; n++)
	    {
	      deserializator.unpack_int (method_sig_p->method_arg_pos[n]);
	    }

	  method_sig_p->class_name = nullptr;
	  if (method_sig_p->method_type != METHOD_IS_JAVA_SP)
	    {
	      cubmem::extensible_block class_name_blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
	      deserializator.unpack_string_to_memblock (class_name_blk);
	      method_sig_p->class_name = class_name_blk.release_ptr ();
	    }

	  if (i != num_methods - 1) /* last */
	    {
	      method_sig_p->next = (METHOD_SIG *) db_private_alloc (NULL, sizeof (METHOD_SIG));
	      method_sig_p->next->next = nullptr;
	    }
	  method_sig_p = method_sig_p->next;
	}
    }
}