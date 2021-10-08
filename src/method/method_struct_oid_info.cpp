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

#include "method_struct_oid_info.hpp"

namespace cubmethod
{

//////////////////////////////////////////////////////////////////////////
// OID GET
//////////////////////////////////////////////////////////////////////////

  void
  oid_get_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_string (class_name);
    serializator.pack_int (attr_names.size());
    for (int i = 0; i < (int) attr_names.size(); i++)
      {
	serializator.pack_string (attr_names[i]);
      }

    serializator.pack_int (column_infos.size());
    for (int i = 0; i < (int) column_infos.size(); i++)
      {
	column_infos[i].pack (serializator);
      }

    serializator.pack_int (db_values.size());
    for (int i = 0; i < (int) db_values.size(); i++)
      {
	serializator.pack_db_value (db_values[i]);
      }
  }

  void
  oid_get_info::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_string (class_name);

    int num_attr_name;
    deserializator.unpack_int (num_attr_name);
    if (num_attr_name > 0)
      {
	attr_names.resize (num_attr_name);
	for (int i = 0; i < (int) num_attr_name; i++)
	  {
	    deserializator.unpack_string (attr_names[i]);
	  }
      }

    int num_column_info;
    deserializator.unpack_int (num_column_info);
    if (num_column_info > 0)
      {
	column_infos.resize (num_column_info);
	for (int i = 0; i < (int) num_column_info; i++)
	  {
	    column_infos[i].unpack (deserializator);
	  }
      }

    int num_db_values;
    deserializator.unpack_int (num_db_values);
    if (num_db_values > 0)
      {
	db_values.resize (num_db_values);
	for (int i = 0; i < (int) num_db_values; i++)
	  {
	    deserializator.unpack_db_value (db_values[i]);
	  }
      }
  }

  size_t
  oid_get_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_string_size (class_name, start_offset); // class_name

    size += serializator.get_packed_int_size (size); // attr_names.size()
    if (attr_names.size() > 0)
      {
	for (int i = 0; i < (int) attr_names.size(); i++)
	  {
	    size += serializator.get_packed_string_size (attr_names[i], size);
	  }
      }

    size += serializator.get_packed_int_size (size); // num_columns
    if (column_infos.size() > 0)
      {
	for (int i = 0; i < (int) column_infos.size(); i++)
	  {
	    size += column_infos[i].get_packed_size (serializator, size);
	  }
      }

    size += serializator.get_packed_int_size (size); // num_values
    for (int i = 0; i < (int) db_values.size(); i++)
      {
	size += serializator.get_packed_db_value_size (db_values[i], size);
      }

    return size;
  }

//////////////////////////////////////////////////////////////////////////
// OID PUT
//////////////////////////////////////////////////////////////////////////

  void
  oid_put_request::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_oid (oid);
    serializator.pack_int (attr_names.size());
    for (int i = 0; i < (int) attr_names.size(); i++)
      {
	serializator.pack_string (attr_names[i]);
      }

    serializator.pack_int (db_values.size());
    for (int i = 0; i < (int) db_values.size(); i++)
      {
	serializator.pack_db_value (db_values[i]);
      }
  }

  void
  oid_put_request::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_oid (oid);

    int num_attr_name;
    deserializator.unpack_int (num_attr_name);
    if (num_attr_name > 0)
      {
	attr_names.resize (num_attr_name);
	for (int i = 0; i < (int) num_attr_name; i++)
	  {
	    deserializator.unpack_string (attr_names[i]);
	  }
      }

    int num_db_values;
    deserializator.unpack_int (num_db_values);
    if (num_db_values > 0)
      {
	db_values.resize (num_db_values);
	for (int i = 0; i < (int) num_db_values; i++)
	  {
	    deserializator.unpack_db_value (db_values[i]);
	  }
      }
  }

  size_t
  oid_put_request::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {

    size_t size = serializator.get_packed_oid_size (start_offset); // attr_names.size()

    size += serializator.get_packed_int_size (size); // attr_names.size()
    if (attr_names.size() > 0)
      {
	for (int i = 0; i < (int) attr_names.size(); i++)
	  {
	    size += serializator.get_packed_string_size (attr_names[i], size);
	  }
      }

    size += serializator.get_packed_int_size (size); // num_values
    for (int i = 0; i < (int) db_values.size(); i++)
      {
	size += serializator.get_packed_db_value_size (db_values[i], size);
      }

    return size;
  }
}
