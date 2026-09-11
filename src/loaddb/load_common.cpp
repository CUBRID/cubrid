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

#include "db_client_type.hpp"
#include "dbtype_def.h"
#include "error_code.h"
#include "error_manager.h"
#include "compressor.hpp"
#include "internal_lob_file.hpp"
#include "intl_support.h"
#include "language_support.h"
#include "object_representation.h"
#include "utility.h"
#if defined (CS_MODE)
#include "network_interface_cl.h"
#include "stream_session.hpp"
#endif

#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <algorithm>
#include <vector>
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

  int expand_internal_lob_refs (std::string &row, const internal_lob_sidecar_map &sidecar, bool sidecar_available,
				class_id clsid);
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
    , no_logging_index (false)
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
    : m_load_client_type (DB_CLIENT_TYPE_LOADDB_UTILITY)
    , m_load_completed (false)
    , m_load_failed (false)
    , m_load_stats ()
  {
  }

  load_status::load_status (int load_client_type, bool load_completed, bool load_failed, std::vector<stats> &load_stats)
    : m_load_client_type (load_client_type)
    , m_load_completed (load_completed)
    , m_load_failed (load_failed)
    , m_load_stats (load_stats)
  {
  }

  load_status::load_status (load_status &&other) noexcept
    : m_load_client_type (other.m_load_client_type)
    , m_load_completed (other.m_load_completed)
    , m_load_failed (other.m_load_failed)
    , m_load_stats (std::move (other.m_load_stats))
  {
  }

  load_status &
  load_status::operator= (load_status &&other) noexcept
  {
    m_load_client_type = other.m_load_client_type;
    m_load_completed = other.m_load_completed;
    m_load_failed = other.m_load_failed;
    m_load_stats = std::move (other.m_load_stats);

    return *this;
  }

  int
  load_status::get_load_client_type ()
  {
    return m_load_client_type;
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
    serializator.pack_int (m_load_client_type);
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
    deserializator.unpack_int (m_load_client_type);
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
    size_t size = serializator.get_packed_int_size (start_offset); // m_load_client_type
    size += serializator.get_packed_bool_size (size); // m_load_completed
    size += serializator.get_packed_bool_size (size); // m_load_failed
    size += serializator.get_packed_bigint_size (size); // m_load_stats size
    for (const stats &s : m_load_stats)
      {
	size += s.get_packed_size (serializator, size);
      }

    return size;
  }


#define LOADDB_BUFFER_SIZE_LIMIT ((size_t)(((1024 * 1024 * 2) - 1) * 1024LL)) /* (2GB - 1K) */
  static const char *LDR_INTERNAL_LOB_SIDE_CAR_SUFFIX = "_internal_lob";
  /* Format 1: hex body, no metadata line. Format 2: LZ4 block body plus a charset / collation line.
   * Both are accepted; unloaddb only writes format 2. */
  static const char *LDR_INTERNAL_LOB_SIDE_CAR_MAGIC_V1 = "CUBRID_INTERNAL_LOB_UNLOAD 1";
  static const char *LDR_INTERNAL_LOB_SIDE_CAR_MAGIC_V2 = "CUBRID_INTERNAL_LOB_UNLOAD 2";
  static const int LDR_INTERNAL_LOB_SIDE_CAR_BLOCK_SIZE = 1024 * 1024;

  static std::string
  internal_lob_sidecar_path (const std::string &object_file)
  {
    const std::string object_suffix = "_objects";

    if (object_file.size () >= object_suffix.size ()
	&& object_file.compare (object_file.size () - object_suffix.size (), object_suffix.size (), object_suffix) == 0)
      {
	return object_file.substr (0, object_file.size () - object_suffix.size ()) + LDR_INTERNAL_LOB_SIDE_CAR_SUFFIX;
      }

    return object_file + LDR_INTERNAL_LOB_SIDE_CAR_SUFFIX;
  }

  static int
  internal_lob_sidecar_set_error ()
  {
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
    return ER_FAILED;
  }

  /* Format 2 records the unload-time charset and collation. CLOB / BLOB keep no per-value charset,
   * so the stored bytes are read with whatever the target database uses: a different charset would
   * turn them into different characters. Charset conversion during load is out of scope here and is
   * tracked as a separate follow-up issue; a charset mismatch is refused outright. A different
   * collation only changes comparison and ordering, so it never blocks a load. */
  static int
  internal_lob_sidecar_check_charset (std::ifstream &sidecar_file)
  {
    std::string line;
    std::string file_charset;
    std::size_t tab;
    const char *db_charset;

    if (!std::getline (sidecar_file, line))
      {
	return internal_lob_sidecar_set_error ();
      }

    tab = line.find ('\t');
    file_charset = (tab == std::string::npos) ? line : line.substr (0, tab);
    db_charset = lang_charset_name (LANG_SYS_CODESET);
    if (file_charset.empty () || db_charset == NULL)
      {
	return internal_lob_sidecar_set_error ();
      }
    if (file_charset == db_charset)
      {
	return NO_ERROR;
      }

    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_INTERNAL_LOB_CHARSET_MISMATCH, 2, file_charset.c_str (),
	    db_charset);
    return ER_LDR_INTERNAL_LOB_CHARSET_MISMATCH;
  }

  static int
  internal_lob_sidecar_hex_value (char ch)
  {
    if (ch >= '0' && ch <= '9')
      {
	return ch - '0';
      }
    if (ch >= 'a' && ch <= 'f')
      {
	return ch - 'a' + 10;
      }
    if (ch >= 'A' && ch <= 'F')
      {
	return ch - 'A' + 10;
      }

    return -1;
  }

  static bool
  internal_lob_sidecar_read_field (std::ifstream &sidecar_file, std::string &field, char delimiter)
  {
    char ch;

    field.clear ();
    while (sidecar_file.get (ch))
      {
	if (ch == delimiter)
	  {
	    return true;
	  }
	field.push_back (ch);
      }

    return !field.empty ();
  }

  static int
  internal_lob_sidecar_parse_entry (std::ifstream &sidecar_file, const std::string &path, int format_version,
				    internal_lob_sidecar_map &sidecar)
  {
    std::string type_field, key_len_field, key, data_len_field, bit_len_field;
    char *endptr = NULL;
    unsigned long key_len;
    long long data_len;
    long long bit_length;
    std::streamoff hex_offset;
    std::streamoff hex_size;
    internal_lob_sidecar_entry entry;

    if (!internal_lob_sidecar_read_field (sidecar_file, type_field, '\t'))
      {
	return sidecar_file.eof () ? NO_ERROR : internal_lob_sidecar_set_error ();
      }
    if (!internal_lob_sidecar_read_field (sidecar_file, key_len_field, '\t')
	|| !internal_lob_sidecar_read_field (sidecar_file, key, '\t')
	|| !internal_lob_sidecar_read_field (sidecar_file, data_len_field, '\t')
	|| !internal_lob_sidecar_read_field (sidecar_file, bit_len_field, '\t'))
      {
	return internal_lob_sidecar_set_error ();
      }

    if (type_field.size () != 1 || (type_field[0] != 'B' && type_field[0] != 'C'))
      {
	return internal_lob_sidecar_set_error ();
      }

    key_len = strtoul (key_len_field.c_str (), &endptr, 10);
    if (endptr == key_len_field.c_str () || *endptr != '\0' || key_len != key.size ())
      {
	return internal_lob_sidecar_set_error ();
      }

    data_len = strtoll (data_len_field.c_str (), &endptr, 10);
    if (endptr == data_len_field.c_str () || *endptr != '\0' || data_len < 0 || data_len > DB_BIGINT_MAX / 2)
      {
	return internal_lob_sidecar_set_error ();
      }

    bit_length = strtoll (bit_len_field.c_str (), &endptr, 10);
    if (endptr == bit_len_field.c_str () || *endptr != '\0' || bit_length < 0)
      {
	return internal_lob_sidecar_set_error ();
      }
    if ((type_field[0] == 'C' && bit_length != 0)
	|| (type_field[0] == 'B' && !internal_lob_is_valid_blob_bit_length ((DB_BIGINT) data_len,
	    (DB_BIGINT) bit_length)))
      {
	return internal_lob_sidecar_set_error ();
      }

    if (format_version >= 2)
      {
	std::string comp_field, stored_len_field;
	long long stored_len;

	if (!internal_lob_sidecar_read_field (sidecar_file, comp_field, '\t')
	    || !internal_lob_sidecar_read_field (sidecar_file, stored_len_field, '\t'))
	  {
	    return internal_lob_sidecar_set_error ();
	  }
	if (comp_field.size () != 1 || (comp_field[0] != 'N' && comp_field[0] != 'L'))
	  {
	    return internal_lob_sidecar_set_error ();
	  }
	stored_len = strtoll (stored_len_field.c_str (), &endptr, 10);
	if (endptr == stored_len_field.c_str () || *endptr != '\0' || stored_len < 0)
	  {
	    return internal_lob_sidecar_set_error ();
	  }
	entry.compression = comp_field[0];
	entry.stored_length = (DB_BIGINT) stored_len;
      }

    hex_offset = sidecar_file.tellg ();
    if (hex_offset < 0)
      {
	return internal_lob_sidecar_set_error ();
      }

    hex_size = (format_version >= 2) ? (std::streamoff) entry.stored_length : (std::streamoff) data_len * 2;
    sidecar_file.seekg (hex_size, std::ios_base::cur);
    if (!sidecar_file.good ())
      {
	return internal_lob_sidecar_set_error ();
      }

    char newline = '\0';
    if (sidecar_file.get (newline))
      {
	if (newline != '\n')
	  {
	    return internal_lob_sidecar_set_error ();
	  }
      }
    else if (!sidecar_file.eof ())
      {
	return internal_lob_sidecar_set_error ();
      }

    entry.type = type_field[0];
    entry.data_length = (DB_BIGINT) data_len;
    entry.bit_length = (DB_BIGINT) bit_length;
    entry.body_offset = hex_offset;
    entry.path = path;

    sidecar[key] = std::move (entry);
    return NO_ERROR;
  }

  int
  load_internal_lob_sidecar (const std::string &object_file_name, internal_lob_sidecar_map &sidecar,
			     bool &sidecar_available)
  {
    std::string path;
    std::ifstream sidecar_file;
    std::string line;
    int format_version = 1;

    sidecar.clear ();
    sidecar_available = false;

    if (object_file_name.empty ())
      {
	return NO_ERROR;
      }

    path = internal_lob_sidecar_path (object_file_name);
    sidecar_file.open (path, std::ios::in | std::ios::binary);
    if (!sidecar_file.is_open ())
      {
	return NO_ERROR;
      }

    if (!std::getline (sidecar_file, line))
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	return ER_FAILED;
      }
    if (line == LDR_INTERNAL_LOB_SIDE_CAR_MAGIC_V1)
      {
	format_version = 1;
      }
    else if (line == LDR_INTERNAL_LOB_SIDE_CAR_MAGIC_V2)
      {
	format_version = 2;
      }
    else
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	return ER_FAILED;
      }

    if (format_version >= 2)
      {
	int error = internal_lob_sidecar_check_charset (sidecar_file);

	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    while (!sidecar_file.eof ())
      {
	int error = internal_lob_sidecar_parse_entry (sidecar_file, path, format_version, sidecar);
	if (error != NO_ERROR)
	  {
	    return ER_FAILED;
	  }
      }

    sidecar_available = true;
    return NO_ERROR;
  }

  /* Block index of a format-2 body: (first plain byte, file position of the block header) per block, built by
   * one pass over the block headers the first time an entry is read and kept for that entry. A read then starts
   * at the block that contains the requested offset whatever the direction of the walk: the internal LOB writer
   * consumes a payload from its end toward offset 0. The last decompressed block is kept so consecutive reads
   * inside one block do not decompress it again. Thread-local: one loader thread reads one entry at a time. */
  struct internal_lob_sidecar_block_index
  {
    std::string path;
    std::streamoff body_offset = -1;
    std::vector<std::pair<DB_BIGINT, std::streamoff>> blocks;	/* plain start -> header position */
    DB_BIGINT cached_block_start = -1;
    std::vector<char> cached_plain;
  };

  static int
  internal_lob_sidecar_read_block_header (std::ifstream &sidecar_file, std::streamoff pos, int &plain_size,
					  int &compressed_size)
  {
    char header[OR_INT_SIZE * 2];

    sidecar_file.seekg (pos, std::ios_base::beg);
    if (!sidecar_file.good ())
      {
	return internal_lob_sidecar_set_error ();
      }
    sidecar_file.read (header, sizeof (header));
    if (sidecar_file.gcount () != (std::streamsize) sizeof (header))
      {
	return internal_lob_sidecar_set_error ();
      }
    plain_size = OR_GET_INT (header);
    compressed_size = OR_GET_INT (header + OR_INT_SIZE);
    if (plain_size <= 0 || plain_size > LDR_INTERNAL_LOB_SIDE_CAR_BLOCK_SIZE || compressed_size <= 0)
      {
	return internal_lob_sidecar_set_error ();
      }
    return NO_ERROR;
  }

  static int
  internal_lob_sidecar_build_block_index (std::ifstream &sidecar_file, const internal_lob_sidecar_entry &entry,
					  internal_lob_sidecar_block_index &index)
  {
    DB_BIGINT block_start = 0;
    std::streamoff pos = entry.body_offset;

    index.path.clear ();
    index.body_offset = -1;
    index.blocks.clear ();
    index.cached_block_start = -1;
    index.cached_plain.clear ();

    while (block_start < entry.data_length)
      {
	int plain_size;
	int compressed_size;
	int error = internal_lob_sidecar_read_block_header (sidecar_file, pos, plain_size, compressed_size);

	if (error != NO_ERROR)
	  {
	    return error;
	  }
	index.blocks.emplace_back (block_start, pos);
	pos += (std::streamoff) (OR_INT_SIZE * 2) + compressed_size;
	block_start += plain_size;
      }

    index.path = entry.path;
    index.body_offset = entry.body_offset;
    return NO_ERROR;
  }

  static int
  internal_lob_sidecar_read_blocks (const internal_lob_sidecar_entry &entry, DB_BIGINT byte_offset, char *buf,
				    int read_size)
  {
    static thread_local internal_lob_sidecar_block_index index;

    std::ifstream sidecar_file;
    std::vector<char> compressed;
    int copied = 0;

    sidecar_file.open (entry.path, std::ios::in | std::ios::binary);
    if (!sidecar_file.is_open ())
      {
	return internal_lob_sidecar_set_error ();
      }

    if (index.path != entry.path || index.body_offset != entry.body_offset)
      {
	int error = internal_lob_sidecar_build_block_index (sidecar_file, entry, index);

	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    /* the block whose plain range contains byte_offset */
    auto block = std::upper_bound (index.blocks.begin (), index.blocks.end (), byte_offset,
				   [] (DB_BIGINT offset, const std::pair<DB_BIGINT, std::streamoff> &candidate)
    {
      return offset < candidate.first;
    });
    if (block == index.blocks.begin ())
      {
	return internal_lob_sidecar_set_error ();
      }
    --block;

    while (copied < read_size)
      {
	if (block == index.blocks.end ())
	  {
	    return internal_lob_sidecar_set_error ();
	  }

	const DB_BIGINT block_start = block->first;

	if (index.cached_block_start != block_start)
	  {
	    int plain_size;
	    int compressed_size;
	    int error = internal_lob_sidecar_read_block_header (sidecar_file, block->second, plain_size, compressed_size);

	    if (error != NO_ERROR)
	      {
		return error;
	      }
	    compressed.resize ((std::size_t) compressed_size);
	    sidecar_file.read (compressed.data (), (std::streamsize) compressed_size);
	    if (sidecar_file.gcount () != (std::streamsize) compressed_size)
	      {
		return internal_lob_sidecar_set_error ();
	      }
	    index.cached_block_start = -1;
	    index.cached_plain.resize ((std::size_t) plain_size);
	    if (cubcompress::decompress<cubcompress::LZ4> (compressed.data (), compressed_size, index.cached_plain.data (),
		plain_size) != plain_size)
	      {
		index.cached_plain.clear ();
		return internal_lob_sidecar_set_error ();
	      }
	    index.cached_block_start = block_start;
	  }

	{
	  const int plain_size = (int) index.cached_plain.size ();
	  const int in_block = (int) (byte_offset + copied - block_start);
	  int take = plain_size - in_block;

	  if (in_block < 0 || in_block >= plain_size)
	    {
	      return internal_lob_sidecar_set_error ();
	    }
	  if (take > read_size - copied)
	    {
	      take = read_size - copied;
	    }
	  std::memcpy (buf + copied, index.cached_plain.data () + in_block, (std::size_t) take);
	  copied += take;
	}
	++block;
      }

    return NO_ERROR;
  }

  int
  internal_lob_sidecar_read_raw_chunk (const internal_lob_sidecar_entry &entry, DB_BIGINT byte_offset, char *buf,
				       int buf_size, int *nread)
  {
    std::ifstream sidecar_file;
    std::vector<char> hex_data;
    DB_BIGINT remaining;
    int read_size;

    if (buf == NULL || buf_size < 0 || nread == NULL || byte_offset < 0 || byte_offset > entry.data_length)
      {
	return internal_lob_sidecar_set_error ();
      }
    *nread = 0;
    if (buf_size == 0 || byte_offset == entry.data_length)
      {
	return NO_ERROR;
      }

    remaining = entry.data_length - byte_offset;
    read_size = (remaining > (DB_BIGINT) buf_size) ? buf_size : (int) remaining;
    if (read_size < 0 || read_size > INT_MAX / 2)
      {
	return internal_lob_sidecar_set_error ();
      }

    if (entry.compression == 'L')
      {
	int error = internal_lob_sidecar_read_blocks (entry, byte_offset, buf, read_size);

	if (error != NO_ERROR)
	  {
	    return error;
	  }
	*nread = read_size;
	return NO_ERROR;
      }

    sidecar_file.open (entry.path, std::ios::in | std::ios::binary);
    if (!sidecar_file.is_open ())
      {
	return internal_lob_sidecar_set_error ();
      }

    sidecar_file.seekg (entry.body_offset + (std::streamoff) byte_offset * 2, std::ios_base::beg);
    if (!sidecar_file.good ())
      {
	return internal_lob_sidecar_set_error ();
      }

    hex_data.resize ((std::size_t) read_size * 2);
    sidecar_file.read (hex_data.data (), (std::streamsize) hex_data.size ());
    if (sidecar_file.gcount () != (std::streamsize) hex_data.size ())
      {
	return internal_lob_sidecar_set_error ();
      }

    for (int i = 0; i < read_size; i++)
      {
	int hi = internal_lob_sidecar_hex_value (hex_data[ (std::size_t) i * 2]);
	int lo = internal_lob_sidecar_hex_value (hex_data[ (std::size_t) i * 2 + 1]);
	if (hi < 0 || lo < 0)
	  {
	    return internal_lob_sidecar_set_error ();
	  }
	buf[i] = (char) ((hi << 4) | lo);
      }

    *nread = read_size;
    return NO_ERROR;
  }

  static void
  internal_lob_append_hex_byte (std::string &literal, unsigned char byte)
  {
    static const char HEX[] = "0123456789ABCDEF";

    literal.push_back (HEX[byte >> 4]);
    literal.push_back (HEX[byte & 0x0f]);
  }

  static int
  internal_lob_make_literal_from_sidecar_entry (const internal_lob_sidecar_entry &entry, std::string &literal)
  {
    char buffer[64 * 1024];
    DB_BIGINT offset = 0;
    DB_BIGINT remaining_bits = entry.bit_length;

    literal.clear ();
    if (entry.type == 'C')
      {
	literal.push_back ('\'');
      }
    else if (entry.type == 'B' && (entry.bit_length % 8) == 0)
      {
	literal.append ("X'");
      }
    else if (entry.type == 'B')
      {
	literal.append ("B'");
      }
    else
      {
	return internal_lob_sidecar_set_error ();
      }

    while (offset < entry.data_length)
      {
	int nread = 0;
	int error = internal_lob_sidecar_read_raw_chunk (entry, offset, buffer, (int) sizeof (buffer), &nread);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
	if (nread <= 0)
	  {
	    return internal_lob_sidecar_set_error ();
	  }

	if (entry.type == 'C')
	  {
	    for (int i = 0; i < nread; i++)
	      {
		if (buffer[i] == '\'')
		  {
		    literal.append ("''");
		  }
		else
		  {
		    literal.push_back (buffer[i]);
		  }
	      }
	  }
	else if ((entry.bit_length % 8) == 0)
	  {
	    for (int i = 0; i < nread; i++)
	      {
		internal_lob_append_hex_byte (literal, (unsigned char) buffer[i]);
	      }
	  }
	else
	  {
	    for (int i = 0; i < nread && remaining_bits > 0; i++)
	      {
		unsigned char byte = (unsigned char) buffer[i];
		for (int bit = 0; bit < 8 && remaining_bits > 0; bit++, remaining_bits--)
		  {
		    int shift = 7 - bit;
		    literal.push_back (((byte >> shift) & 1) ? '1' : '0');
		  }
	      }
	  }

	offset += nread;
      }

    literal.push_back ('\'');
    return NO_ERROR;
  }

  static int
  internal_lob_make_ref_from_sidecar_entry (class_id clsid, const internal_lob_sidecar_entry &entry, std::string &ref)
  {
#if defined (CS_MODE)
    char buffer[64 * 1024];
    DB_BIGINT offset = 0;
    INT64 token = 0;
    OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT64_SIZE * 2) a_config;
    char *config = OR_ALIGNED_BUF_START (a_config);
    DB_TYPE lob_type = entry.type == 'B' ? DB_TYPE_BLOB : DB_TYPE_CLOB;
    DB_BIGINT logical_length = entry.type == 'B' ? entry.bit_length : entry.data_length;
    int error;
    bool abort_token = false;

    (void) or_pack_int (config, entry.type == 'B' ? INTERNAL_LOB_STREAM_TYPE_BLOB : INTERNAL_LOB_STREAM_TYPE_CLOB);
    OR_PUT_INT64 (config + OR_INT_SIZE, &entry.data_length);
    OR_PUT_INT64 (config + OR_INT_SIZE + OR_INT64_SIZE, &logical_length);
    error = stream_from_init (STREAM_KIND_INTERNAL_LOB, config, OR_ALIGNED_BUF_SIZE (a_config));
    if (error != NO_ERROR)
      {
	return error;
      }
    abort_token = true;

    while (offset < entry.data_length)
      {
	int nread = 0;

	error = internal_lob_sidecar_read_raw_chunk (entry, offset, buffer, (int) sizeof (buffer), &nread);
	if (error != NO_ERROR)
	  {
	    goto cleanup;
	  }
	if (nread <= 0)
	  {
	    error = internal_lob_sidecar_set_error ();
	    goto cleanup;
	  }

	error = stream_from_send_data (buffer, nread);
	if (error != NO_ERROR)
	  {
	    goto cleanup;
	  }

	offset += nread;
      }

    error = stream_from_end (&token);
    if (error != NO_ERROR)
      {
	goto cleanup;
      }
    abort_token = false;

    ref = "^L'";
    ref.push_back (entry.type);
    ref.push_back ('|');
    ref.append (std::to_string ((long long) token));
    ref.push_back ('\'');
    return NO_ERROR;

cleanup:
    if (abort_token)
      {
	(void) stream_from_abort ();
      }
    return error;
#else
    (void) clsid;
    return internal_lob_make_literal_from_sidecar_entry (entry, ref);
#endif
  }

  int
  expand_internal_lob_refs (std::string &row, const internal_lob_sidecar_map &sidecar, bool sidecar_available,
			    class_id clsid)
  {
    std::string expanded;
    bool in_quote = false;
    bool changed = false;

    for (size_t pos = 0; pos < row.size (); pos++)
      {
	char ch = row[pos];

	if (ch == '\'')
	  {
	    if (in_quote && pos + 1 < row.size () && row[pos + 1] == '\'')
	      {
		expanded.append ("''");
		pos++;
		continue;
	      }
	    in_quote = !in_quote;
	    expanded.push_back (ch);
	    continue;
	  }

	if (!in_quote && ch == '^' && pos + 2 < row.size () && (row[pos + 1] == 'L' || row[pos + 1] == 'l')
	    && row[pos + 2] == '\'')
	  {
	    size_t token_start = pos + 3;
	    size_t token_end = row.find ('\'', token_start);
	    if (token_end == std::string::npos)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
		return ER_FAILED;
	      }

	    std::string token = row.substr (token_start, token_end - token_start);
	    if (token.size () < 3 || token[1] != '|')
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
		return ER_FAILED;
	      }

	    if (!sidecar_available)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
		return ER_FAILED;
	      }

	    char type = token[0];
	    std::string key = token.substr (2);
	    auto found = sidecar.find (key);
	    if (found == sidecar.end () || found->second.type != type)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
		return ER_FAILED;
	      }

	    if (type != 'C' && type != 'B')
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
		return ER_FAILED;
	      }

	    std::string replacement;
	    int error = internal_lob_make_ref_from_sidecar_entry (clsid, found->second, replacement);
	    if (error != NO_ERROR)
	      {
		return error;
	      }
	    expanded.append (replacement);

	    pos = token_end;
	    changed = true;
	    continue;
	  }

	expanded.push_back (ch);
      }

    if (changed)
      {
	row.swap (expanded);
      }

    return NO_ERROR;
  }

  static int
  append_incomplete_row (std::string &batch_buffer, std::string &one_row_buffer, batch_handler &b_handler,
			 class_id &clsid, batch_id &batch_id, int lineno,
			 int &one_row_lineno, int &batch_start_offset, int64_t &batch_rows,
			 const internal_lob_sidecar_map &internal_lob_sidecar, bool internal_lob_sidecar_available)
  {
    int error_code = NO_ERROR;

    assert (one_row_buffer.empty() == false);

    error_code = expand_internal_lob_refs (one_row_buffer, internal_lob_sidecar, internal_lob_sidecar_available, clsid);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    // The content contained in one_row_buffer may not be a complete row.
    // TODO: How about handling errors right away without having to send them to the server?
    if ((one_row_buffer.size() + batch_buffer.size()) >= LOADDB_BUFFER_SIZE_LIMIT)
      {
	error_code = handle_batch (b_handler, clsid, batch_buffer, batch_id, batch_start_offset, batch_rows);
	if (error_code != NO_ERROR)
	  {
	    return error_code;
	  }
	// Next batch should start from the following line.
	batch_start_offset = lineno - one_row_lineno + 1;
      }

    batch_buffer.append (one_row_buffer);
    one_row_buffer.clear();
    one_row_lineno = 0;

    return error_code;
  }

  int
  split (int batch_size, const std::string &object_file_name, class_handler &c_handler,
	 batch_handler &b_handler)
  {
    int error_code;
    int64_t batch_rows = 0;
    int lineno = 0;
    int one_row_lineno = 0;
    int batch_start_offset = 0;
    class_id clsid = FIRST_CLASS_ID;
    batch_id batch_id = NULL_BATCH_ID;
    std::string batch_buffer;
    std::string one_row_buffer;
    internal_lob_sidecar_map internal_lob_sidecar;
    bool internal_lob_sidecar_available = false;
    bool class_is_ignored = false;
    short single_quote_checker = 0;
    bool  size_over = false;
#define DEFAULT_STRING_SZ (4096)
#define DEFAULT_ONEROW_BUF_SZ (1024*1024*1) // 1MB
    size_t size_bk = DEFAULT_STRING_SZ;

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

    /* one_row_buffer reuses allocated space.
     * batch_buffer does not reuse allocated space. Instead, it reallocates to the maximum size it used.
     */
    one_row_buffer.reserve (DEFAULT_ONEROW_BUF_SZ);
    batch_buffer.reserve (DEFAULT_STRING_SZ);

    error_code = load_internal_lob_sidecar (object_file_name, internal_lob_sidecar,
					    internal_lob_sidecar_available);
    if (error_code != NO_ERROR)
      {
	object_file.close ();
	return error_code;
      }

    for (std::string line; std::getline (object_file, line); ++lineno, ++one_row_lineno)
      {
	if (single_quote_checker == 0)
	  {
	    bool is_id_line = starts_with (line, "%id") || starts_with (line, "%ID");
	    bool is_class_line = starts_with (line, "%class") || starts_with (line, "%CLASS");

	    if (is_id_line || is_class_line)
	      {
		if (one_row_buffer.empty() == false)
		  {
		    error_code = append_incomplete_row (batch_buffer, one_row_buffer, b_handler, clsid, batch_id,
							lineno, one_row_lineno, batch_start_offset, batch_rows,
							internal_lob_sidecar, internal_lob_sidecar_available);
		    if (error_code != NO_ERROR)
		      {
			object_file.close ();
			return error_code;
		      }
		  }

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
		    batch_buffer.reserve (DEFAULT_STRING_SZ);
		    size_bk = DEFAULT_STRING_SZ;
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
	  }

	if (class_is_ignored)
	  {
	    assert (single_quote_checker == 0);
	    // Skip the remaining lines until we find another class.
	    continue;
	  }

	// check for matching single quotes
	for (const char &c: line)
	  {
	    if (c == '\'')
	      {
		single_quote_checker ^= 1;
	      }
	  }

	if (single_quote_checker == 0)
	  {
	    // strip trailing whitespace
	    rtrim (line);

	    if (line.empty ())
	      {
		continue;
	      }
	  }

	// it is a line containing row data so append it
	one_row_buffer.append (line);

	// since std::getline eats end line character, add it back in order to make loaddb lexer happy
	one_row_buffer.append ("\n");

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

	error_code = expand_internal_lob_refs (one_row_buffer, internal_lob_sidecar, internal_lob_sidecar_available, clsid);
	if (error_code != NO_ERROR)
	  {
	    object_file.close ();
	    return error_code;
	  }

	if ((one_row_buffer.size() + batch_buffer.size()) >= LOADDB_BUFFER_SIZE_LIMIT)
	  {
	    size_over = true;
	  }
	else
	  {
	    batch_buffer.append (one_row_buffer);
	    one_row_buffer.clear();
	    one_row_lineno = 0;

	    ++batch_rows;
	    size_over = false;
	  }

	// check if we have a full batch
	if (batch_rows == batch_size || size_over)
	  {
	    if (size_bk < batch_buffer.size())
	      {
		size_bk = batch_buffer.size();
	      }

	    error_code = handle_batch (b_handler, clsid, batch_buffer, batch_id, batch_start_offset, batch_rows);
	    if (error_code != NO_ERROR)
	      {
		object_file.close ();
		return error_code;
	      }
	    batch_buffer.reserve (size_bk);
	    // Next batch should start from the following line.
	    batch_start_offset =  size_over ?  (lineno - one_row_lineno + 1) : (lineno + 1);
	  }

	if (size_over)
	  {
	    if (one_row_buffer.empty() == false)
	      {
		batch_buffer.append (one_row_buffer);
		one_row_buffer.clear();
	      }
	    one_row_lineno = 0;
	    ++batch_rows;
	  }
      }

    if (one_row_buffer.empty() == false)
      {
	error_code = append_incomplete_row (batch_buffer, one_row_buffer, b_handler, clsid, batch_id,
					    lineno, one_row_lineno, batch_start_offset, batch_rows, internal_lob_sidecar,
					    internal_lob_sidecar_available);
	if (error_code != NO_ERROR)
	  {
	    object_file.close ();
	    return error_code;
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
