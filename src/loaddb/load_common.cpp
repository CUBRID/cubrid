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

#include <fstream>

///////////////////// Function declarations /////////////////////
namespace cubload
{

  /*
   * A wrapper function for calling batch handler. Used by split function and does some extra checks
   */
  int handle_batch (batch_handler &handler, class_id class_id, std::string &batch_content, batch_id &batch_id,
		    bool handle_async = true);

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
    : m_class_id (NULL_CLASS_ID)
    , m_batch_id (NULL_BATCH_ID)
    , m_handle_async (true)
    , m_content ()
  {
    //
  }

  batch::batch (class_id class_id, batch_id batch_id, std::string &content, bool handle_async)
    : m_class_id (class_id)
    , m_batch_id (batch_id)
    , m_handle_async (handle_async)
    , m_content (content)
  {
    //
  }

  batch::batch (batch &&other) noexcept
    : m_class_id (other.m_class_id)
    , m_batch_id (other.m_batch_id)
    , m_content (std::move (other.m_content))
  {
    //
  }

  batch &
  batch::operator= (batch &&other) noexcept
  {
    m_class_id = other.m_class_id;
    m_batch_id = other.m_batch_id;
    m_content = std::move (other.m_content);

    return *this;
  }

  int
  batch::pack (cubpacking::packer *serializator)
  {
    serializator->pack_int (m_class_id);
    serializator->pack_int (m_batch_id);
    short handle_async = static_cast<short> (m_handle_async ? 1 : 0);
    serializator->pack_short (&handle_async);
    serializator->pack_string (m_content);

    return NO_ERROR;
  }

  int
  batch::unpack (cubpacking::packer *serializator)
  {
    serializator->unpack_int (&m_class_id);
    serializator->unpack_int (&m_batch_id);
    short handle_async;
    serializator->unpack_short (&handle_async);
    m_handle_async = handle_async == 1;
    serializator->unpack_string (m_content);

    return NO_ERROR;
  }

  bool
  batch::is_equal (const packable_object *other)
  {
    (void) other;

    // used only in test context
    assert (false);
    return true;
  }

  size_t
  batch::get_packed_size (cubpacking::packer *serializator)
  {
    size_t size = 0;

    size += serializator->get_packed_int_size (size);
    size += serializator->get_packed_int_size (size);
    size += serializator->get_packed_short_size (size);
    size += serializator->get_packed_string_size (m_content, size);

    return size;
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

  int
  stats::pack (cubpacking::packer *serializator)
  {
    serializator->pack_bigint (&total_objects);
    serializator->pack_bigint (&last_commit);
    serializator->pack_int (errors);
    serializator->pack_string (error_message);
    serializator->pack_int ((int) is_failed);
    serializator->pack_int ((int) is_completed);

    return NO_ERROR;
  }

  int
  stats::unpack (cubpacking::packer *serializator)
  {
    serializator->unpack_bigint (&total_objects);
    serializator->unpack_bigint (&last_commit);
    serializator->unpack_int (&errors);
    serializator->unpack_string (error_message);

    int is_failed_;
    serializator->unpack_int (&is_failed_);
    is_failed = (bool) is_failed_;

    int is_completed_;
    serializator->unpack_int (&is_completed_);
    is_completed = (bool) is_completed_;

    return NO_ERROR;
  }

  bool
  stats::is_equal (const packable_object *other)
  {
    (void) other;

    // used only in test context
    assert (false);
    return true;
  }

  size_t
  stats::get_packed_size (cubpacking::packer *serializator)
  {
    size_t size = 0;

    size += serializator->get_packed_bigint_size (size); // total_objects
    size += serializator->get_packed_bigint_size (size); // last_commit
    size += serializator->get_packed_int_size (size); // errors
    size += serializator->get_packed_string_size (error_message, size);
    size += serializator->get_packed_int_size (size); // is_failed
    size += serializator->get_packed_int_size (size); // is_completed

    return size;
  }

  void
  free_string (string_type **str)
  {
    if (str == NULL || *str == NULL)
      {
	return;
      }

    string_type *str_ = *str;

    if (str_->need_free_val)
      {
	free_and_init (str_->val);
      }
    if (str_->need_free_self)
      {
	free_and_init (str_);
	*str = NULL;
      }
  }

  void
  free_class_command_spec (class_command_spec_type **class_cmd_spec)
  {
    if (class_cmd_spec == NULL || *class_cmd_spec == NULL)
      {
	return;
      }

    string_type *attr, *arg;
    class_command_spec_type *class_cmd_spec_ = *class_cmd_spec;

    if (class_cmd_spec_->ctor_spec)
      {
	for (arg = class_cmd_spec_->ctor_spec->arg_list; arg; arg = arg->next)
	  {
	    free_string (&arg);
	  }

	free_string (&class_cmd_spec_->ctor_spec->id_name);
	free_and_init (class_cmd_spec_->ctor_spec);
      }

    for (attr = class_cmd_spec_->attr_list; attr; attr = attr->next)
      {
	free_string (&attr);
      }

    free_and_init (class_cmd_spec_);
    *class_cmd_spec = NULL;
  }

  int
  split (int batch_size, std::string &object_file_name, batch_handler &handler)
  {
    int error_code;
    int rows = 0;
    batch_id batch_id = 0;
    class_id class_id = 0;
    std::string batch_buffer;
    std::ifstream object_file (object_file_name, std::fstream::in);

    if (!object_file)
      {
	// file does not exists on server, let client do the split operation
	return ER_FAILED;
      }

    assert (batch_size > 0);

    for (std::string line; std::getline (object_file, line);)
      {
	bool is_id_line = starts_with (line, "%id");
	bool is_class_line = starts_with (line, "%class");

	if (is_id_line || is_class_line)
	  {
	    if (is_class_line)
	      {
		// in case of class line collect remaining for current class
		// and start new batch for the new class
		error_code = handle_batch (handler, class_id, batch_buffer, batch_id);
		if (error_code != NO_ERROR)
		  {
		    object_file.close ();
		    return error_code;
		  }

		++class_id;

		// rewind forward rows counter until batch is full
		for (; (rows % batch_size) != 0; rows++)
		  ;
	      }

	    line.append ("\n"); // feed lexer with new line
	    error_code = handle_batch (handler, class_id, line, batch_id, false);
	    if (error_code != NO_ERROR)
	      {
		object_file.close ();
		return error_code;
	      }

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
		error_code = handle_batch (handler, class_id, batch_buffer, batch_id);
		if (error_code != NO_ERROR)
		  {
		    object_file.close ();
		    return error_code;
		  }
	      }
	  }
      }

    // collect remaining rows
    error_code = handle_batch (handler, class_id, batch_buffer, batch_id);

    object_file.close ();

    return error_code;
  }

  int
  handle_batch (batch_handler &handler, class_id class_id, std::string &batch_content, batch_id &batch_id,
		bool handle_async)
  {
    if (batch_content.empty ())
      {
	// batch is empty, therefore do nothing and return
	return NO_ERROR;
      }

    batch batch_ (class_id, ++batch_id, batch_content, handle_async);
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
