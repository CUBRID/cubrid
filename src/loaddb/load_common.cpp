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

/*
 * load_common.cpp - common code used by loader
 */

#include "load_common.hpp"

#include "dbtype_def.h"
#include "error_code.h"
#include "intl_support.h"

#include <fstream>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

///////////////////// Function declarations /////////////////////
namespace cubload
{

  /*
   * A wrapper function for calling batch handler. Used by split function and does some extra checks
   */
  int handle_batch (batch_handler &handler, class_id clsid, std::string &batch_content, batch_id &batch_id,
		    int64_t line_offset, int64_t &rows);

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

  batch::batch (batch_id id, class_id clsid, std::string &content, int64_t line_offset, int64_t rows)
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

  int64_t
  batch::get_line_offset () const
  {
    return m_line_offset;
  }

  const std::string &
  batch::get_content () const
  {
    return m_content;
  }

  int64_t
  batch::get_rows_number () const
  {
    return m_rows;
  }

  void
  batch::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_bigint (m_id);
    serializator.pack_int (m_clsid);
    serializator.pack_string (m_content);
    serializator.pack_bigint (m_line_offset);
    serializator.pack_bigint (m_rows);
  }

  void
  batch::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_bigint (m_id);
    deserializator.unpack_int (m_clsid);
    deserializator.unpack_string (m_content);
    deserializator.unpack_bigint (m_line_offset);
    deserializator.unpack_bigint (m_rows);
  }

  size_t
  batch::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_bigint_size (start_offset); // m_id
    size += serializator.get_packed_int_size (size); // m_clsid
    size += serializator.get_packed_string_size (m_content, size);
    size += serializator.get_packed_bigint_size (size); // m_line_offset
    size += serializator.get_packed_bigint_size (size); // m_rows

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
    , error_file ()
    , ignore_logging (false)
    , compare_storage_order (false)
    , table_name ()
    , ignore_class_file ()
    , ignore_classes ()
    , m_ignored_errors ()
    , no_user_specified_name (false)
    , schema_file_list ()
    , cs_mode (false)
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
    serializator.pack_bigint (m_ignored_errors.size ());
    for (const int error : m_ignored_errors)
      {
	serializator.pack_int (error);
      }

    serializator.pack_bool (no_user_specified_name);
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
    m_ignored_errors.resize (ignore_errors_size);
    for (size_t i = 0; i < ignore_errors_size; i++)
      {
	deserializator.unpack_int (m_ignored_errors[i]);
      }

    deserializator.unpack_bool (no_user_specified_name);
  }

  size_t
  load_args::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_string_size (volume, start_offset);
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
    for (const int i : m_ignored_errors)
      {
	size += serializator.get_packed_int_size (size);
      }

    size += serializator.get_packed_bool_size (size); // no_user_specified_name

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

	char lower_case_string[DB_MAX_IDENTIFIER_LENGTH] = { 0 };

	assert (intl_identifier_lower_string_size (class_name.c_str ()) <= DB_MAX_IDENTIFIER_LENGTH);

	// Make the string to be lower case and take into consideration all types of characters.
	intl_identifier_lower (class_name.c_str (), lower_case_string);

	ignore_classes.emplace_back (lower_case_string);
      }

    file.close ();
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
    , log_message ()
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
    , log_message (copy.log_message)
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
    this->log_message = other.log_message;

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
    log_message.clear ();
  }

  void
  stats::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_bigint (rows_committed);
    serializator.pack_bigint (current_line.load ());
    serializator.pack_bigint (last_committed_line);
    serializator.pack_int (rows_failed);
    serializator.pack_string (error_message);
    serializator.pack_string (log_message);
  }

  void
  stats::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_bigint (rows_committed);

    int64_t current_line_;
    deserializator.unpack_bigint (current_line_);
    current_line.store (current_line_);

    deserializator.unpack_bigint (last_committed_line);
    deserializator.unpack_int (rows_failed);
    deserializator.unpack_string (error_message);
    deserializator.unpack_string (log_message);
  }

  size_t
  stats::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_bigint_size (start_offset); // rows_committed
    size += serializator.get_packed_bigint_size (size); // current_line
    size += serializator.get_packed_bigint_size (size); // last_committed_line
    size += serializator.get_packed_int_size (size); // rows_failed
    size += serializator.get_packed_string_size (error_message, size);
    size += serializator.get_packed_string_size (log_message, size);

    return size;
  }

  load_status::load_status ()
    : m_load_completed (false)
    , m_load_failed (false)
    , m_load_stats ()
  {
  }

  load_status::load_status (bool load_completed, bool load_failed, std::vector<stats> &load_stats)
    : m_load_completed (load_completed)
    , m_load_failed (load_failed)
    , m_load_stats (load_stats)
  {
  }

  load_status::load_status (load_status &&other) noexcept
    : m_load_completed (other.m_load_completed)
    , m_load_failed (other.m_load_failed)
    , m_load_stats (std::move (other.m_load_stats))
  {
  }

  load_status &
  load_status::operator= (load_status &&other) noexcept
  {
    m_load_completed = other.m_load_completed;
    m_load_failed = other.m_load_failed;
    m_load_stats = std::move (other.m_load_stats);

    return *this;
  }

  bool
  load_status::is_load_completed ()
  {
    return m_load_completed;
  }

  bool
  load_status::is_load_failed ()
  {
    return m_load_failed;
  }

  std::vector<stats> &
  load_status::get_load_stats ()
  {
    return m_load_stats;
  }

  void
  load_status::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_bool (m_load_completed);
    serializator.pack_bool (m_load_failed);

    serializator.pack_bigint (m_load_stats.size ());
    for (const stats &s : m_load_stats)
      {
	s.pack (serializator);
      }
  }

  void
  load_status::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_bool (m_load_completed);
    deserializator.unpack_bool (m_load_failed);

    size_t load_stats_size = 0;
    deserializator.unpack_bigint (load_stats_size);
    m_load_stats.resize (load_stats_size);

    for (size_t i = 0; i < load_stats_size; ++i)
      {
	m_load_stats[i].unpack (deserializator);
      }
  }

  size_t
  load_status::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_bool_size (start_offset); // m_load_completed
    size += serializator.get_packed_bool_size (size); // m_load_failed
    size += serializator.get_packed_bigint_size (size); // m_load_stats size
    for (const stats &s : m_load_stats)
      {
	size += s.get_packed_size (serializator, size);
      }

    return size;
  }

  int
  split (int batch_size, const std::string &object_file_name, class_handler &c_handler, batch_handler &b_handler)
  {
    int error_code;
    int64_t batch_rows = 0;
    int lineno = 0;
    int batch_start_offset = 0;
    class_id clsid = FIRST_CLASS_ID;
    batch_id batch_id = NULL_BATCH_ID;
    std::string batch_buffer;
    bool class_is_ignored = false;
    short single_quote_checker = 0;

    if (object_file_name.empty ())
      {
	return ER_FILE_UNKNOWN_FILE;
      }

    std::ifstream object_file (object_file_name, std::fstream::in | std::fstream::binary);
    if (!object_file)
      {
	// file does not exists
	return ER_FILE_UNKNOWN_FILE;
      }

    assert (batch_size > 0);

    for (std::string line; std::getline (object_file, line); ++lineno)
      {
	bool is_id_line = starts_with (line, "%id") || starts_with (line, "%ID");
	bool is_class_line = starts_with (line, "%class") || starts_with (line, "%CLASS");

	if (is_id_line || is_class_line)
	  {
	    if (is_class_line)
	      {
		// in case of class line collect remaining for current class
		// and start new batch for the new class

		error_code = handle_batch (b_handler, clsid, batch_buffer, batch_id, batch_start_offset, batch_rows);
		if (error_code != NO_ERROR)
		  {
		    object_file.close ();
		    return error_code;
		  }

		++clsid;
	      }

	    // New class so we check if the previous one was ignored.
	    // If so, then we should empty the current batch since we do not send it to the server.

	    line.append ("\n"); // feed lexer with new line
	    batch c_batch (batch_id, clsid, line, lineno, 1);
	    error_code = c_handler (c_batch, class_is_ignored);
	    if (error_code != NO_ERROR)
	      {
		object_file.close ();
		return error_code;
	      }

	    // Next batch should start from the following line.
	    batch_start_offset = lineno + 1;
	    continue;
	  }

	if (class_is_ignored)
	  {
	    // Skip the remaining lines until we find another class.
	    continue;
	  }

	// strip trailing whitespace
	rtrim (line);

	if (line.empty ())
	  {
	    continue;
	  }

	// it is a line containing row data so append it
	batch_buffer.append (line);

	// since std::getline eats end line character, add it back in order to make loaddb lexer happy
	batch_buffer.append ("\n");

	// check for matching single quotes
	for (const char &c: line)
	  {
	    if (c == '\'')
	      {
		single_quote_checker ^= 1;
	      }
	  }

	// it could be that a row is wrapped on the next line,
	// this means that the row ends on the last line that does not end with '+' (plus) character
	if (ends_with (line, "+"))
	  {
	    continue;
	  }

	// if single_quote_checker is 1, it means that a single quote was opened but not closed
	if (single_quote_checker == 1)
	  {
	    continue;
	  }

	++batch_rows;

	// check if we have a full batch
	if (batch_rows == batch_size)
	  {
	    error_code = handle_batch (b_handler, clsid, batch_buffer, batch_id, batch_start_offset, batch_rows);
	    // Next batch should start from the following line.
	    batch_start_offset = lineno + 1;
	    if (error_code != NO_ERROR)
	      {
		object_file.close ();
		return error_code;
	      }
	  }
      }

    // collect remaining rows
    error_code = handle_batch (b_handler, clsid, batch_buffer, batch_id, batch_start_offset, batch_rows);

    object_file.close ();

    return error_code;
  }

  int
  handle_batch (batch_handler &handler, class_id clsid, std::string &batch_content, batch_id &batch_id,
		int64_t line_offset, int64_t &rows)
  {
    if (batch_content.empty ())
      {
	// batch is empty, therefore do nothing and return
	return NO_ERROR;
      }

    batch batch_ (++batch_id, clsid, batch_content, line_offset, rows);
    int error_code = handler (batch_);

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
