/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * load_common.cpp - common code used by loader
 */

#include "load_common.hpp"

#include "error_code.h"

#include <fstream>

///////////////////// Function declarations /////////////////////
namespace cubload
{

  /*
   * A wrapper function for calling batch handler. Used by split function and does some extra checks
   */
  int handle_batch (batch_handler &handler, class_id clsid, std::string &batch_content, batch_id &batch_id,
		    int line_offset);

  /*
   * Check if a given string starts with a given prefix
   */
  bool starts_with (const std::string &str, const std::string &prefix);

  /*
   * Check if a given string ends with a given suffix
   */
  bool ends_with (const std::string &str, const std::string &suffix);

  /*
   * Trim whitespaces on the right of the string. String is passed as reference and it will be modified
   */
  void rtrim (std::string &str);
}

///////////////////// Function definitions /////////////////////
namespace cubload
{

  batch::batch ()
    : m_id (NULL_BATCH_ID)
    , m_clsid (NULL_CLASS_ID)
    , m_content ()
    , m_line_offset (0)
  {
    //
  }

  batch::batch (batch_id id, class_id clsid, std::string &content, int line_offset)
    : m_id (id)
    , m_clsid (clsid)
    , m_content (content)
    , m_line_offset (line_offset)
  {
    //
  }

  batch::batch (batch &&other) noexcept
    : m_id (other.m_id)
    , m_clsid (other.m_clsid)
    , m_content (std::move (other.m_content))
    , m_line_offset (other.m_line_offset)
  {
    //
  }

  batch &
  batch::operator= (batch &&other) noexcept
  {
    m_id = other.m_id;
    m_clsid = other.m_clsid;
    m_content = std::move (other.m_content);
    m_line_offset = other.m_line_offset;

    return *this;
  }

  batch_id
  batch::get_id () const
  {
    return m_id;
  }

  class_id
  batch::get_class_id () const
  {
    return m_clsid;
  }

  int
  batch::get_line_offset () const
  {
    return m_line_offset;
  }

  const std::string &
  batch::get_content () const
  {
    return m_content;
  }

  void
  batch::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (m_id);
    serializator.pack_int (m_clsid);
    serializator.pack_string (m_content);
    serializator.pack_int (m_line_offset);
  }

  void
  batch::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (m_id);
    deserializator.unpack_int (m_clsid);
    deserializator.unpack_string (m_content);
    deserializator.unpack_int (m_line_offset);
  }

  size_t
  batch::get_packed_size (cubpacking::packer &serializator) const
  {
    size_t size = 0;

    size += serializator.get_packed_int_size (size); // m_id
    size += serializator.get_packed_int_size (size); // m_clsid
    size += serializator.get_packed_string_size (m_content, size);
    size += serializator.get_packed_int_size (size); // m_line_offset

    return size;
  }

  string_type::string_type ()
    : next (NULL)
    , last (NULL)
    , val (NULL)
    , size (0)
    , need_free_val (false)
  {
    //
  }

  string_type::~string_type ()
  {
    destroy ();
  }

  string_type::string_type (char *val, std::size_t size, bool need_free_val)
    : next (NULL)
    , last (NULL)
    , val (val)
    , size (size)
    , need_free_val (need_free_val)
  {
    //
  }

  void
  string_type::destroy ()
  {
    if (need_free_val)
      {
	delete [] val;

	val = NULL;
	size = 0;
	need_free_val = false;
      }
  }

  constructor_spec_type::constructor_spec_type (string_type *id_name, string_type *arg_list)
    : id_name (id_name)
    , arg_list (arg_list)
  {
    //
  }

  class_command_spec_type::class_command_spec_type (int qualifier, string_type *attr_list,
      constructor_spec_type *ctor_spec)
    : qualifier (qualifier)
    , attr_list (attr_list)
    , ctor_spec (ctor_spec)
  {
    //
  }

  constant_type::constant_type ()
    : next (NULL)
    , last (NULL)
    , val (NULL)
    , type (LDR_NULL)
  {
    //
  }

  constant_type::constant_type (data_type type, void *val)
    : next (NULL)
    , last (NULL)
    , val (val)
    , type (type)
  {
    //
  }

  object_ref_type::object_ref_type (string_type *class_id, string_type *class_name)
    : class_id (class_id)
    , class_name (class_name)
    , instance_number (NULL)
  {
    //
  }

  monetary_type::monetary_type (string_type *amount, int currency_type)
    : amount (amount)
    , currency_type (currency_type)
  {
    //
  }

  stats::stats ()
    : defaults (0)
    , total_objects (0)
    , last_commit (0)
    , errors (0)
    , error_message ()
    , is_failed (false)
    , is_completed (false)
  {
    //
  }

  // Copy constructor
  stats::stats (const stats &copy)
    : defaults (copy.defaults)
    , total_objects (copy.total_objects)
    , last_commit (copy.last_commit)
    , errors (copy.errors)
    , error_message (copy.error_message)
    , is_failed (copy.is_failed)
    , is_completed (copy.is_completed)
  {
    //
  }

  stats &
  stats::operator= (const stats &other)
  {
    this->defaults = other.defaults;
    this->total_objects = other.total_objects;
    this->last_commit = other.last_commit;
    this->errors = other.errors;
    this->error_message = other.error_message;
    this->is_failed = other.is_failed;
    this->is_completed = other.is_completed;

    return *this;
  }

  void
  stats::clear ()
  {
    defaults = 0;
    total_objects = 0;
    last_commit = 0;
    errors = 0;
    error_message.clear ();
    is_failed = false;
    is_completed = false;
  }

  void
  stats::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (defaults);
    serializator.pack_bigint (total_objects);
    serializator.pack_bigint (last_commit);
    serializator.pack_int (errors);
    serializator.pack_string (error_message);
    serializator.pack_bool (is_failed);
    serializator.pack_bool (is_completed);
  }

  void
  stats::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (defaults);
    deserializator.unpack_bigint (total_objects);
    deserializator.unpack_bigint (last_commit);
    deserializator.unpack_int (errors);
    deserializator.unpack_string (error_message);
    deserializator.unpack_bool (is_failed);
    deserializator.unpack_bool (is_completed);
  }

  size_t
  stats::get_packed_size (cubpacking::packer &serializator) const
  {
    size_t size = 0;

    size += serializator.get_packed_int_size (size); // defaults
    size += serializator.get_packed_bigint_size (size); // total_objects
    size += serializator.get_packed_bigint_size (size); // last_commit
    size += serializator.get_packed_int_size (size); // errors
    size += serializator.get_packed_string_size (error_message, size);
    size += serializator.get_packed_bool_size (size); // is_failed
    size += serializator.get_packed_bool_size (size); // is_completed

    return size;
  }

  int
  split (int batch_size, const std::string &object_file_name, batch_handler &c_handler, batch_handler &b_handler)
  {
    int error_code;
    int rows = 0;
    int lineno = 0;
    int line_offset = 0;
    class_id clsid = 0;
    batch_id batch_id = 0;
    std::string batch_buffer;
    std::ifstream object_file (object_file_name, std::fstream::in);

    if (!object_file)
      {
	// file does not exists on server, let client do the split operation
	return ER_FILE_UNKNOWN_FILE;
      }

    assert (batch_size > 0);

    for (std::string line; std::getline (object_file, line); ++lineno)
      {
	bool is_id_line = starts_with (line, "%id");
	bool is_class_line = starts_with (line, "%class");

	if (is_id_line || is_class_line)
	  {
	    if (is_class_line)
	      {
		// in case of class line collect remaining for current class
		// and start new batch for the new class
		error_code = handle_batch (b_handler, clsid, batch_buffer, batch_id, line_offset);
		if (error_code != NO_ERROR)
		  {
		    object_file.close ();
		    return error_code;
		  }

		++clsid;

		// rewind forward rows counter until batch is full
		for (; (rows % batch_size) != 0; rows++)
		  ;
	      }

	    line.append ("\n"); // feed lexer with new line
	    batch c_batch (batch_id, clsid, line, lineno);
	    error_code = c_handler (c_batch);
	    if (error_code != NO_ERROR)
	      {
		object_file.close ();
		return error_code;
	      }

	    line_offset = lineno;
	    continue;
	  }

	// strip trailing whitespace
	rtrim (line);

	// it is a line containing row data so append it
	batch_buffer.append (line);

	// since std::getline eats end line character, add it back in order to make loaddb lexer happy
	batch_buffer.append ("\n");

	// it could be that a row is wrapped on the next line,
	// this means that the row ends on the last line that does not end with '+' (plus) character
	if (!ends_with (line, "+"))
	  {
	    ++rows;

	    // check if we have a full batch
	    if ((rows % batch_size) == 0)
	      {
		error_code = handle_batch (b_handler, clsid, batch_buffer, batch_id, line_offset);
		line_offset = lineno;
		if (error_code != NO_ERROR)
		  {
		    object_file.close ();
		    return error_code;
		  }
	      }
	  }
      }

    // collect remaining rows
    error_code = handle_batch (b_handler, clsid, batch_buffer, batch_id, line_offset);

    object_file.close ();

    return error_code;
  }

  int
  handle_batch (batch_handler &handler, class_id clsid, std::string &batch_content, batch_id &batch_id, int line_offset)
  {
    if (batch_content.empty ())
      {
	// batch is empty, therefore do nothing and return
	return NO_ERROR;
      }

    batch batch_ (++batch_id, clsid, batch_content, line_offset);
    int error_code = handler (batch_);

    // prepare to start new batch for the class
    batch_content.clear ();

    return error_code;
  }

  inline bool
  starts_with (const std::string &str, const std::string &prefix)
  {
    return str.size () >= prefix.size () && 0 == str.compare (0, prefix.size (), prefix);
  }

  inline bool
  ends_with (const std::string &str, const std::string &suffix)
  {
    return str.size () >= suffix.size () && 0 == str.compare (str.size () - suffix.size (), suffix.size (), suffix);
  }

  inline void
  rtrim (std::string &str)
  {
    str.erase (str.find_last_not_of (" \t\f\v\n\r") + 1);
  }

} // namespace cubload
