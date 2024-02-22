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

method_sig_node::method_sig_node ()
{
  next = nullptr;
  method_name = nullptr;
  auth_name = nullptr;
  method_type = METHOD_TYPE_NONE;
  num_method_args = 0;
  method_arg_pos = nullptr;
  class_name = nullptr;
  arg_info = nullptr;
}

method_sig_node::~method_sig_node ()
{
  freemem ();
}

method_sig_node::method_sig_node (method_sig_node &&obj)
{
  next = std::move (obj.next);

  method_name = obj.method_name;
  auth_name = obj.auth_name;
  method_type = obj.method_type;
  method_arg_pos = obj.method_arg_pos;
  num_method_args = obj.num_method_args;

  obj.method_name = nullptr;
  obj.auth_name = nullptr;
  obj.method_arg_pos = nullptr;

  if (obj.class_name != nullptr)
    {
      class_name = obj.class_name;
      obj.class_name = nullptr;
    }

  if (obj.arg_info != nullptr)
    {
      arg_info->arg_mode = obj.arg_info->arg_mode;
      arg_info->arg_type = obj.arg_info->arg_type;
      arg_info->result_type = obj.arg_info->result_type;

      obj.arg_info->arg_mode = nullptr;
      obj.arg_info->arg_type = nullptr;
      obj.arg_info = nullptr;
    }
}

method_sig_node::method_sig_node (const method_sig_node &obj)
{
  if (obj.next != nullptr)
    {
      next = (METHOD_SIG *) db_private_alloc (NULL, sizeof (METHOD_SIG));
      *next = * (obj.next);
    }
  else
    {
      next = nullptr;
    }

  if (obj.method_name != nullptr)
    {
      int method_name_len = strlen (obj.method_name);
      method_name = (char *) db_private_alloc (NULL, method_name_len + 1);
      strncpy (method_name, obj.method_name, method_name_len);
    }

  if (obj.auth_name != nullptr)
    {
      int auth_name_len = strlen (obj.auth_name);
      auth_name = (char *) db_private_alloc (NULL, auth_name_len + 1);
      strncpy (auth_name, obj.auth_name, auth_name_len);
    }

  method_type = obj.method_type;
  num_method_args = obj.num_method_args;

  if (obj.num_method_args > 0)
    {
      method_arg_pos = (int *) db_private_alloc (NULL, sizeof (int) * (num_method_args + 1));
      for (int n = 0; n < num_method_args + 1; n++)
	{
	  method_arg_pos[n] = obj.method_arg_pos[n];
	}
    }

  if (obj.class_name != nullptr)
    {
      int class_name_len = strlen (obj.class_name);
      class_name = (char *) db_private_alloc (NULL, class_name_len + 1);
      strncpy (class_name, obj.class_name, class_name_len);
    }
  else
    {
      class_name = nullptr;
    }

  if (obj.arg_info != nullptr)
    {
      if (num_method_args > 0)
	{
	  arg_info->arg_mode = (int *) db_private_alloc (NULL, sizeof (int) * (num_method_args));
	  arg_info->arg_type = (int *) db_private_alloc (NULL, sizeof (int) * (num_method_args));
	  for (int n = 0; n < num_method_args; n++)
	    {
	      arg_info->arg_mode[n] = obj.arg_info->arg_mode[n];
	      arg_info->arg_type[n] = obj.arg_info->arg_type[n];
	    }
	}
      else
	{
	  arg_info->arg_mode = nullptr;
	  arg_info->arg_type = nullptr;
	}
      arg_info->result_type = obj.arg_info->result_type;
    }
}

void
method_sig_node::pack (cubpacking::packer &serializator) const
{
  serializator.pack_c_string (method_name, strlen (method_name));

  if (auth_name)
    {
      serializator.pack_bool (true);
      serializator.pack_c_string (auth_name, strlen (auth_name));
    }
  else
    {
      serializator.pack_bool (false);
    }

  serializator.pack_int (method_type);
  serializator.pack_int (num_method_args);

  for (int i = 0; i < num_method_args + 1; i++)
    {
      serializator.pack_int (method_arg_pos[i]);
    }

  if (class_name)
    {
      serializator.pack_bool (true);
      serializator.pack_c_string (class_name, strlen (class_name));
    }
  else
    {
      serializator.pack_bool (false);
    }

  if (arg_info)
    {
      serializator.pack_bool (true);
      for (int i = 0; i < num_method_args; i++)
	{
	  serializator.pack_int (arg_info->arg_mode[i]);
	}
      for (int i = 0; i < num_method_args; i++)
	{
	  serializator.pack_int (arg_info->arg_type[i]);
	}
      serializator.pack_int (arg_info->result_type);
    }
  else
    {
      serializator.pack_bool (false);
    }
}

size_t
method_sig_node::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
{
  size_t size = serializator.get_packed_int_size (start_offset); /* num_methods */

  size += serializator.get_packed_c_string_size (method_name, strlen (method_name), size);

  size += serializator.get_packed_bool_size (size); // has auth_name
  if (auth_name)
    {
      size += serializator.get_packed_c_string_size (auth_name, strlen (auth_name), size);
    }

  /* method type and num_method_args */
  size += serializator.get_packed_int_size (size);
  size += serializator.get_packed_int_size (size);

  for (int i = 0; i < num_method_args + 1; i++)
    {
      size += serializator.get_packed_int_size (size); /* method_sig->method_arg_pos[i] */
    }

  size += serializator.get_packed_bool_size (size); // has class_name
  if (class_name)
    {
      size += serializator.get_packed_c_string_size (class_name, strlen (class_name), size);
    }

  size += serializator.get_packed_bool_size (size); // has arg_info
  if (arg_info)
    {
      for (int i = 0; i < num_method_args; i++)
	{
	  size += serializator.get_packed_int_size (size); /* method_sig->arg_info->arg_mode[i] */
	  size += serializator.get_packed_int_size (size); /* method_sig->arg_info->arg_type[i] */
	}
      size += serializator.get_packed_int_size (size); /* method_sig->arg_info->result_type */
    }

  return size;
}

method_sig_node &
method_sig_node::operator= (const method_sig_node &obj)
{
  if (this != &obj)
    {
      if (obj.next != nullptr)
	{
	  next = (METHOD_SIG *) db_private_alloc (NULL, sizeof (METHOD_SIG));
	  *next = * (obj.next);
	}
      else
	{
	  next = nullptr;
	}

      if (obj.method_name != nullptr)
	{
	  int method_name_len = strlen (obj.method_name);
	  method_name = (char *) db_private_alloc (NULL, method_name_len + 1);
	  strncpy (method_name, obj.method_name, method_name_len);
	}

      if (obj.auth_name != nullptr)
	{
	  int auth_name_len = strlen (obj.auth_name);
	  auth_name = (char *) db_private_alloc (NULL, auth_name_len + 1);
	  strncpy (auth_name, obj.auth_name, auth_name_len);
	}
      else
	{
	  auth_name = nullptr;
	}

      method_type = obj.method_type;
      num_method_args = obj.num_method_args;
      if (obj.num_method_args > 0)
	{
	  method_arg_pos = (int *) db_private_alloc (NULL, sizeof (int) * (num_method_args + 1));
	  for (int n = 0; n < num_method_args + 1; n++)
	    {
	      method_arg_pos[n] = obj.method_arg_pos[n];
	    }
	}
      else
	{
	  method_arg_pos = nullptr;
	}

      if (obj.class_name != nullptr)
	{
	  int class_name_len = strlen (obj.class_name);
	  class_name = (char *) db_private_alloc (NULL, class_name_len + 1);
	  strncpy (class_name, obj.class_name, class_name_len);
	}
      else
	{
	  class_name = nullptr;
	}

      if (obj.arg_info != nullptr)
	{
	  if (num_method_args > 0)
	    {
	      arg_info->arg_mode = (int *) db_private_alloc (NULL, sizeof (int) * (num_method_args));
	      arg_info->arg_type = (int *) db_private_alloc (NULL, sizeof (int) * (num_method_args));
	      for (int n = 0; n < num_method_args; n++)
		{
		  arg_info->arg_mode[n] = obj.arg_info->arg_mode[n];
		  arg_info->arg_type[n] = obj.arg_info->arg_type[n];
		}
	    }
	  arg_info->result_type = obj.arg_info->result_type;
	}
      else
	{
	  arg_info = nullptr;
	}
    }
  return *this;
}

void
method_sig_node::unpack (cubpacking::unpacker &deserializator)
{
  next = nullptr;

  cubmem::extensible_block method_name_blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
  deserializator.unpack_string_to_memblock (method_name_blk);
  method_name = method_name_blk.release_ptr ();

  bool has_attr = false;

  deserializator.unpack_bool (has_attr);
  if (has_attr)
    {
      cubmem::extensible_block auth_name_blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
      deserializator.unpack_string_to_memblock (auth_name_blk);
      auth_name = auth_name_blk.release_ptr ();
    }
  else
    {
      auth_name = nullptr;
    }

  int type;
  deserializator.unpack_int (type);
  method_type = static_cast<METHOD_TYPE> (type);
  deserializator.unpack_int (num_method_args);

  method_arg_pos = (int *) db_private_alloc (NULL, sizeof (int) * (num_method_args + 1));
  for (int n = 0; n < num_method_args + 1; n++)
    {
      deserializator.unpack_int (method_arg_pos[n]);
    }


  deserializator.unpack_bool (has_attr);
  if (has_attr)
    {
      cubmem::extensible_block class_name_blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
      deserializator.unpack_string_to_memblock (class_name_blk);
      class_name = class_name_blk.release_ptr ();
    }
  else
    {
      class_name = nullptr;
    }

  deserializator.unpack_bool (has_attr);
  if (has_attr)
    {
      arg_info = (METHOD_ARG_INFO *) db_private_alloc (NULL, sizeof (METHOD_ARG_INFO));

      if (num_method_args > 0)
	{
	  arg_info->arg_mode = (int *) db_private_alloc (NULL, sizeof (int) * (num_method_args));
	  arg_info->arg_type = (int *) db_private_alloc (NULL, sizeof (int) * (num_method_args));

	  for (int i = 0; i < num_method_args; i++)
	    {
	      deserializator.unpack_int (arg_info->arg_mode[i]);
	    }

	  for (int i = 0; i < num_method_args; i++)
	    {
	      deserializator.unpack_int (arg_info->arg_type[i]);
	    }
	}
      else
	{
	  arg_info->arg_mode = nullptr;
	  arg_info->arg_type = nullptr;
	}

      deserializator.unpack_int (arg_info->result_type);
    }
  else
    {
      arg_info = nullptr;
    }


}

void
method_sig_node::freemem ()
{
  if (method_name != nullptr)
    {
      db_private_free_and_init (NULL, method_name);
    }

  if (auth_name != nullptr)
    {
      db_private_free_and_init (NULL, auth_name);
    }

  if (method_arg_pos != nullptr)
    {
      db_private_free_and_init (NULL, method_arg_pos);
    }


  if (class_name != nullptr)
    {
      db_private_free_and_init (NULL, class_name);
    }

  if (arg_info != nullptr)
    {
      if (arg_info->arg_mode != nullptr)
	{
	  db_private_free_and_init (NULL, arg_info->arg_mode);
	}
      if (arg_info->arg_type != nullptr)
	{
	  db_private_free_and_init (NULL, arg_info->arg_type);
	}
      db_private_free_and_init (NULL, arg_info);
    }
}

void
method_sig_list::freemem ()
{
  METHOD_SIG *sig = method_sig;
  while (sig)
    {
      METHOD_SIG *next = sig->next;

      sig->freemem ();
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
      sig_p->pack (serializator);
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
      size += sig_p->get_packed_size (serializator, size);
      sig_p = sig_p->next;
    }
  return size;
}

void
method_sig_list::unpack (cubpacking::unpacker &deserializator)
{
  deserializator.unpack_int (num_methods);

  method_sig = nullptr;
  if (num_methods > 0)
    {
      method_sig = (METHOD_SIG *) db_private_alloc (NULL, sizeof (METHOD_SIG));
      METHOD_SIG *method_sig_p = method_sig;
      for (int i = 0; i < num_methods; i++)
	{
	  method_sig_p->unpack (deserializator);

	  if (i != num_methods - 1) /* last */
	    {
	      method_sig_p->next = (METHOD_SIG *) db_private_alloc (NULL, sizeof (METHOD_SIG));
	    }

	  method_sig_p = method_sig_p->next;
	}
    }
}
