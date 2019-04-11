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

#include "dbtype_def.h"
#include "error_code.h"

#include <fstream>

///////////////////// Function declarations /////////////////////
namespace cubload
{

  /*
   * A wrapper function for calling batch handler. Used by split function and does some extra checks
   */
  int handle_batch (batch_handler &handler, class_id clsid, std::string &batch_content, batch_id &batch_id,
		    int line_offset, int &rows);

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
    , m_rows (0)
  {
    //
  }

  batch::batch (batch_id id, class_id clsid, std::string &content, int line_offset, int rows)
    : m_id (id)
    , m_clsid (clsid)
    , m_content (std::move (content))
    , m_line_offset (line_offset)
    , m_rows (rows)
  {
    //
  }

  batch::batch (batch &&other) noexcept
    : m_id (other.m_id)
    , m_clsid (other.m_clsid)
    , m_content (std::move (other.m_content))
    , m_line_offset (other.m_line_offset)
    , m_rows (other.m_rows)
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
    m_rows = other.m_rows;

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

  int
  batch::get_rows_number () const
  {
    return m_rows;
  }

  void
  batch::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (m_id);
    serializator.pack_int (m_clsid);
    serializator.pack_string (m_content);
    serializator.pack_int (m_line_offset);
    serializator.pack_int (m_rows);
  }

  void
  batch::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (m_id);
    deserializator.unpack_int (m_clsid);
    deserializator.unpack_string (m_content);
    deserializator.unpack_int (m_line_offset);
    deserializator.unpack_int (m_rows);
  }

  size_t
  batch::get_packed_size (cubpacking::packer &serializator) const
  {
    size_t size = 0;

    size += serializator.get_packed_int_size (size); // m_id
    size += serializator.get_packed_int_size (size); // m_clsid
    size += serializator.get_packed_string_size (m_content, size);
    size += serializator.get_packed_int_size (size); // m_line_offset
    size += serializator.get_packed_int_size (size); // m_rows

    return size;
  }

  load_args::load_args ()
    : volume ()
    , input_file ()
    , user_name ()
    , password ()
    , syntax_check (false)
    , load_only (false)
    , estimated_size (0)
    , verbose (false)
    , disable_statistics (false)
    , periodic_commit (0)
    , verbose_commit (false)
    , no_oid_hint (false)
    , schema_file ()
    , index_file ()
    , object_file ()
    , server_object_file ()
    , error_file ()
    , ignore_logging (false)
    , compare_storage_order (false)
    , table_name ()
    , ignore_class_file ()
    , ignore_classes ()
    , ignored_errors ()
  {
    //
  }

  void
  load_args::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_string (volume);
    serializator.pack_string (input_file);
    serializator.pack_string (user_name);
    serializator.pack_string (password);
    serializator.pack_bool (syntax_check);
    serializator.pack_bool (load_only);
    serializator.pack_int (estimated_size);
    serializator.pack_bool (verbose);
    serializator.pack_bool (disable_statistics);
    serializator.pack_int (periodic_commit);
    serializator.pack_bool (verbose_commit);
    serializator.pack_bool (no_oid_hint);
    serializator.pack_string (schema_file);
    serializator.pack_string (index_file);
    serializator.pack_string (object_file);
    serializator.pack_string (server_object_file);
    serializator.pack_string (error_file);
    serializator.pack_bool (ignore_logging);
    serializator.pack_bool (compare_storage_order);
    serializator.pack_string (table_name);
    serializator.pack_string (ignore_class_file);

    serializator.pack_bigint (ignore_classes.size ());
    for (const std::string &ignore_class : ignore_classes)
      {
	serializator.pack_string (ignore_class);
      }
    serializator.pack_bigint (ignored_errors.size ());
    for (int error :ignored_errors)
      {
	serializator.pack_int (error);
      }
  }

  void
  load_args::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_string (volume);
    deserializator.unpack_string (input_file);
    deserializator.unpack_string (user_name);
    deserializator.unpack_string (password);
    deserializator.unpack_bool (syntax_check);
    deserializator.unpack_bool (load_only);
    deserializator.unpack_int (estimated_size);
    deserializator.unpack_bool (verbose);
    deserializator.unpack_bool (disable_statistics);
    deserializator.unpack_int (periodic_commit);
    deserializator.unpack_bool (verbose_commit);
    deserializator.unpack_bool (no_oid_hint);
    deserializator.unpack_string (schema_file);
    deserializator.unpack_string (index_file);
    deserializator.unpack_string (object_file);
    deserializator.unpack_string (server_object_file);
    deserializator.unpack_string (error_file);
    deserializator.unpack_bool (ignore_logging);
    deserializator.unpack_bool (compare_storage_order);
    deserializator.unpack_string (table_name);
    deserializator.unpack_string (ignore_class_file);

    size_t ignore_classes_size = 0;
    deserializator.unpack_bigint (ignore_classes_size);
    ignore_classes.reserve (ignore_classes_size);

    for (size_t i = 0; i < ignore_classes_size; ++i)
      {
	std::string ignore_class;
	deserializator.unpack_string (ignore_class);
	ignore_classes.push_back (ignore_class);
      }
    size_t ignore_errors_size = 0;

    deserializator.unpack_bigint (ignore_errors_size);
    ignored_errors.reserve (ignore_errors_size);
    for (size_t i = 0; i < ignore_errors_size; i++)
      {
	int error;
	deserializator.unpack_int (error);
	ignored_errors.push_back (error);
      }

  }

  size_t
  load_args::get_packed_size (cubpacking::packer &serializator) const
  {
    size_t size = 0;

    size += serializator.get_packed_string_size (volume, size);
    size += serializator.get_packed_string_size (input_file, size);
    size += serializator.get_packed_string_size (user_name, size);
    size += serializator.get_packed_string_size (password, size);
    size += serializator.get_packed_bool_size (size); // syntax_check
    size += serializator.get_packed_bool_size (size); // load_only
    size += serializator.get_packed_int_size (size); // estimated_size
    size += serializator.get_packed_bool_size (size); // verbose
    size += serializator.get_packed_bool_size (size); // disable_statistics
    size += serializator.get_packed_int_size (size); // periodic_commit
    size += serializator.get_packed_bool_size (size); // verbose_commit
    size += serializator.get_packed_bool_size (size); // no_oid_hint
    size += serializator.get_packed_string_size (schema_file, size);
    size += serializator.get_packed_string_size (index_file, size);
    size += serializator.get_packed_string_size (object_file, size);
    size += serializator.get_packed_string_size (server_object_file, size);
    size += serializator.get_packed_string_size (error_file, size);
    size += serializator.get_packed_bool_size (size); // ignore_logging
    size += serializator.get_packed_bool_size (size); // compare_storage_order
    size += serializator.get_packed_string_size (table_name, size);
    size += serializator.get_packed_string_size (ignore_class_file, size);

    size += serializator.get_packed_bigint_size (size); // ignore_classes size
    for (const std::string &ignore_class : ignore_classes)
      {
	size += serializator.get_packed_string_size (ignore_class, size);
      }

    size += serializator.get_packed_bigint_size (size);
    for (const int i : ignored_errors)
      {
	size += serializator.get_packed_int_size (size);
      }

    return size;
  }

  int
  load_args::parse_ignore_class_file ()
  {
    if (ignore_class_file.empty ())
      {
	// it means that ignore class file was not provided, just exit without error
	return NO_ERROR;
      }

    std::ifstream file (ignore_class_file, std::fstream::in);
    if (!file)
      {
	return ER_FILE_UNKNOWN_FILE;
      }

    for (std::string line; std::getline (file, line);)
      {
	rtrim (line);
	if (line.size () > DB_MAX_IDENTIFIER_LENGTH)
	  {
	    file.close ();
	    ignore_classes.clear ();
	    return ER_FAILED;
	  }

	const char *fmt= "%s";
	std::string class_name (line.size (), '\0');

	// scan first string, and ignore rest of the line
	sscanf (line.c_str (), fmt, class_name.c_str ());
	ignore_classes.emplace_back (class_name.c_str (), strlen (class_name.c_str ()));
      }

    file.close ();
    return NO_ERROR;
  }

  int
  load_args::set_ignored_errors_array (std::vector <int> &ignored_errors)
  {
    this->ignored_errors = ignored_errors;
    return NO_ERROR;
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

  class_command_spec_type::class_command_spec_type (int attr_type, string_type *attr_list,
      constructor_spec_type *ctor_spec)
    : attr_type ((attribute_type) attr_type)
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
    : rows_committed (0)
    , current_line {0}
    , last_committed_line (0)
    , rows_failed (0)
    , error_message ()
    , is_failed (false)
    , is_completed (false)
  {
    //
  }

  // Copy constructor
  stats::stats (const stats &copy)
    : rows_committed (copy.rows_committed)
    , current_line {copy.current_line.load ()}
    , last_committed_line (copy.last_committed_line)
    , rows_failed (copy.rows_failed)
    , error_message (copy.error_message)
    , is_failed (copy.is_failed)
    , is_completed (copy.is_completed)
  {
    //
  }

  stats &
  stats::operator= (const stats &other)
  {
    this->rows_committed = other.rows_committed;
    this->current_line.store (other.current_line.load ());
    this->last_committed_line = other.last_committed_line;
    this->rows_failed = other.rows_failed;
    this->error_message = other.error_message;
    this->is_failed = other.is_failed;
    this->is_completed = other.is_completed;

    return *this;
  }

  void
  stats::clear ()
  {
    rows_committed = 0;
    current_line.store (0);
    last_committed_line = 0;
    rows_failed = 0;
    error_message.clear ();
    is_failed = false;
    is_completed = false;
  }

  void
  stats::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (rows_committed);
    serializator.pack_int (current_line.load ());
    serializator.pack_int (last_committed_line);
    serializator.pack_int (rows_failed);
    serializator.pack_string (error_message);
    serializator.pack_bool (is_failed);
    serializator.pack_bool (is_completed);
  }

  void
  stats::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (rows_committed);

    int current_line_;
    deserializator.unpack_int (current_line_);
    current_line.store (current_line_);

    deserializator.unpack_int (last_committed_line);
    deserializator.unpack_int (rows_failed);
    deserializator.unpack_string (error_message);
    deserializator.unpack_bool (is_failed);
    deserializator.unpack_bool (is_completed);
  }

  size_t
  stats::get_packed_size (cubpacking::packer &serializator) const
  {
    size_t size = 0;

    size += serializator.get_packed_int_size (size); // rows_committed
    size += serializator.get_packed_int_size (size); // current_line
    size += serializator.get_packed_int_size (size); // last_committed_line
    size += serializator.get_packed_int_size (size); // rows_failed
    size += serializator.get_packed_string_size (error_message, size);
    size += serializator.get_packed_bool_size (size); // is_failed
    size += serializator.get_packed_bool_size (size); // is_completed

    return size;
  }

  int
  split (int batch_size, const std::string &object_file_name, batch_handler &c_handler, batch_handler &b_handler)
  {
    int error_code;
    int total_rows = 0;
    int batch_rows = 0;
    int lineno = 0;
    int line_offset = 0;
    class_id clsid = FIRST_CLASS_ID;
    batch_id batch_id = NULL_BATCH_ID;
    std::string batch_buffer;

    if (object_file_name.empty ())
      {
	return ER_FILE_UNKNOWN_FILE;
      }

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
		error_code = handle_batch (b_handler, clsid, batch_buffer, batch_id, line_offset, batch_rows);
		if (error_code != NO_ERROR)
		  {
		    object_file.close ();
		    return error_code;
		  }

		++clsid;

		// rewind forward total_rows counter until batch is full
		for (; (total_rows % batch_size) != 0; total_rows++)
		  ;
	      }

	    line.append ("\n"); // feed lexer with new line
	    batch *c_batch = new batch (batch_id, clsid, line, lineno, 1);
	    error_code = c_handler (*c_batch);
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
	    ++total_rows;
	    ++batch_rows;

	    // check if we have a full batch
	    if ((total_rows % batch_size) == 0)
	      {
		error_code = handle_batch (b_handler, clsid, batch_buffer, batch_id, line_offset, batch_rows);
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
    error_code = handle_batch (b_handler, clsid, batch_buffer, batch_id, line_offset, batch_rows);

    object_file.close ();

    return error_code;
  }

  int
  handle_batch (batch_handler &handler, class_id clsid, std::string &batch_content, batch_id &batch_id, int line_offset,
		int &rows)
  {
    if (batch_content.empty ())
      {
	// batch is empty, therefore do nothing and return
	return NO_ERROR;
      }

    batch *batch_ = new batch (++batch_id, clsid, batch_content, line_offset, rows);
    int error_code = handler (*batch_);

    // prepare to start new batch for the class
    batch_content.clear ();
    rows = 0;

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
