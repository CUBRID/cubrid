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
 * load_common.hpp - common code used by loader
 */

#ifndef _LOAD_COMMON_HPP_
#define _LOAD_COMMON_HPP_

#include "packable_object.hpp"

#include <cassert>
#include <functional>

#define NUM_LDR_TYPES (LDR_TYPE_MAX + 1)
#define NUM_DB_TYPES (DB_TYPE_LAST + 1)

namespace cubload
{

  using batch_id = int;
  using class_id = int;

  const class_id NULL_CLASS_ID = 0;
  const batch_id NULL_BATCH_ID = 0;
  const batch_id FIRST_BATCH_ID = 1;

  struct batch : public cubpacking::packable_object
  {
    class_id m_clsid;
    batch_id m_batch_id;
    std::string m_content;

    batch ();
    batch (class_id clsid, batch_id batch_id, std::string &content);

    batch (batch &&other) noexcept; // MoveConstructible
    batch &operator= (batch &&other) noexcept; // MoveAssignable

    batch (const batch &copy) = default; // Not CopyConstructible // TODO CBRD-22660 we should not allow copy
    batch &operator= (const batch &copy) = delete; // Not CopyAssignable

    int pack (cubpacking::packer *serializator) override;
    int unpack (cubpacking::packer *serializator) override;
    bool is_equal (const packable_object *other) override;
    size_t get_packed_size (cubpacking::packer *serializator) override;
  };

  using batch_handler = std::function<int (const batch &)>;
  using class_install_handler = std::function<int (const class_id, const std::string &)>;

  /*
   * loaddb executables command line arguments
   */
  struct load_args
  {
    char *volume;
    char *input_file;
    char *user_name;
    char *password;
    bool syntax_check;
    bool load_only;
    int estimated_size;
    bool verbose;
    bool disable_statistics;
    int periodic_commit;
    bool verbose_commit;
    bool no_oid_hint;
    char *schema_file;
    char *index_file;
    char *object_file;
    char *error_file;
    bool ignore_logging;
    char *table_name;
    char *ignore_class_file;
    bool compare_storage_order;
  };

  /*
   * These are the "types" of strings that the lexer recognizes.  The
   * loader can specialize on each type.
   * These values are used to set up a vector of type setting functions, based
   * on information about each attribute parsed in the %CLASS line.
   * The setter functions are invoked using the enumerated type as an index into
   * the function vector. This gives us a significant saving when processing
   * values in the instance line, over the previous loader.
   */

  enum data_type
  {
    LDR_NULL,
    LDR_INT,
    LDR_STR,
    LDR_NSTR,
    LDR_NUMERIC,                 /* Default real */
    LDR_DOUBLE,                  /* Reals specified with scientific notation, 'e', or 'E' */
    LDR_FLOAT,                   /* Reals specified with 'f' or 'F' notation */
    LDR_OID,                     /* Object references */
    LDR_CLASS_OID,               /* Class object reference */
    LDR_DATE,
    LDR_TIME,
    LDR_TIMESTAMP,
    LDR_TIMESTAMPLTZ,
    LDR_TIMESTAMPTZ,
    LDR_COLLECTION,
    LDR_ELO_INT,                 /* Internal ELO's */
    LDR_ELO_EXT,                 /* External ELO's */
    LDR_SYS_USER,
    LDR_SYS_CLASS,               /* This type is not allowed currently. */
    LDR_MONETARY,
    LDR_BSTR,                    /* Binary bit strings */
    LDR_XSTR,                    /* Hexidecimal bit strings */
    LDR_DATETIME,
    LDR_DATETIMELTZ,
    LDR_DATETIMETZ,
    LDR_JSON,

    LDR_TYPE_MAX = LDR_JSON
  };

  /*
   * attribute_type
   *
   * attribute type identifiers for ldr_act_restrict_attributes().
   * These attributes are handled specially since there modify the class object
   * directly.
   */

  enum attribute_type
  {
    LDR_ATTRIBUTE_ANY = 0,
    LDR_ATTRIBUTE_SHARED,
    LDR_ATTRIBUTE_CLASS,
    LDR_ATTRIBUTE_DEFAULT
  };

  enum interrupt_type
  {
    LDR_NO_INTERRUPT,
    LDR_STOP_AND_ABORT_INTERRUPT,
    LDR_STOP_AND_COMMIT_INTERRUPT
  };

  struct string_type
  {
    string_type *next;
    string_type *last;
    char *val;
    size_t size;
    bool need_free_val;
    bool need_free_self;
  };

  struct constructor_spec_type
  {
    string_type *id_name;
    string_type *arg_list;
  };

  struct class_command_spec_type
  {
    int qualifier;
    string_type *attr_list;
    constructor_spec_type *ctor_spec;
  };

  struct constant_type
  {
    constant_type *next;
    constant_type *last;
    void *val;
    int type;
    bool need_free;
  };

  struct object_ref_type
  {
    string_type *class_id;
    string_type *class_name;
    string_type *instance_number;
  };

  struct monetary_type
  {
    string_type *amount;
    int currency_type;
  };

  struct stats : public cubpacking::packable_object
  {
    int defaults;
    std::int64_t total_objects;
    std::int64_t last_commit;
    int errors;
    std::string error_message;
    bool is_failed;
    bool is_completed;

    // Default constructor
    stats ();
    // Copy constructor
    stats (const stats &copy);
    // Assignment operator
    stats &operator= (const stats &other);

    void clear ();

    int pack (cubpacking::packer *serializator) override;
    int unpack (cubpacking::packer *serializator) override;
    bool is_equal (const packable_object *other) override;
    size_t get_packed_size (cubpacking::packer *serializator) override;
  };

  /*
   * cubload::class_installer
   *
   * description
   *
   * how to use
   */
  class class_installer
  {
    public:
      virtual ~class_installer () = default; // Destructor

      /*
       * Function to initialize class installer instance
       *
       *    return: void
       *    clsid(in): generated id of the class
       */
      virtual void init (class_id clsid) = 0;

      /*
       * Function to check a class, it is called when a line of the following form "%id foo 42" is reached
       *    in loaddb object file (where class_name will be "foo" and class_id will be 42)
       *
       *    return: void
       *    class_name(in): name of the class
       *    class_id(in)  : id of the class from the object file
       */
      virtual void check_class (const char *class_name, int class_id) = 0;

      /*
       * Function to set up a class and class attributes list. Should be used when loaddb object file doesn't contain
       *     a %class line but instead "-t TABLE or --table=TABLE" parameter was passed to loaddb executable
       *     In this case class attributes list and their order will be fetched from class schema representation
       *
       *    return: NO_ERROR in case of success or error code otherwise
       *    class_name(in): name of the class pass to loaddb executable
       */
      virtual int install_class (const char *class_name) = 0;

      /*
       * Function to set up a class, class attributes and class constructor. It is called when a line of the following
       *    form "%class foo (id, name)" is reached in loaddb object file
       *
       *    return: void
       *    class_name(in): loader string type which contains name of the class
       *    cmd_spec(in)  : class command specification which contains
       *                        attribute list and class constructor specification
       */
      virtual void install_class (string_type *class_name, class_command_spec_type *cmd_spec) = 0;
  };

  /*
   * cubload::object_loader
   *
   * description
   *    A pure virtual class that serves as an interface for inserting rows by the loaddb. Currently there are two
   *    implementations of this class: server loader and client loader.
   *        * server_object_loader: A object loader that is running on the cub_server on multi-threaded environment
   *        * sa_object_loader    : Contains old loaddb code base and is running
   *                                on SA mode (single threaded environment)
   *
   * how to use
   *    Loader is used by the cubload::driver, which later is passed to the cubload::parser. The parser class will then
   *    call specific functions on different grammar rules.
   */
  class object_loader
  {
    public:
      virtual ~object_loader () = default; // Destructor

      /*
       * Function to initialize object loader instance
       *
       *    return: void
       *    clsid(in): generated id of the class
       */
      virtual void init (class_id clsid) = 0;

      /*
       * Destroy function called when loader grammar reached the end of the loaddb object file
       */
      virtual void destroy () = 0;

      /*
       * Function called by the loader grammar before every line with row data from loaddb object file.
       *
       *    return: void
       *    object_id(in): id of the referenced object instance
       */
      virtual void start_line (int object_id) = 0;

      /*
       * Process and inserts a row. constant_type contains the value and the type for each column from the row.
       *
       *    return: void
       *    cons(in): array of constants
       */
      virtual void process_line (constant_type *cons) = 0;

      /*
       * Called after process_line, should implement login for cleaning up data after insert if required.
       */
      virtual void finish_line () = 0;
  };

  ///////////////////// common global functions /////////////////////
  void free_string (string_type **str);
  void free_class_command_spec (class_command_spec_type **class_cmd_spec);

  /*
   * Splits a loaddb object file into batches of a given size.
   *
   *    return: NO_ERROR in case of success or ER_FAILED if file does not exists
   *    batch_size(in)      : batch size
   *    object_file_name(in): loaddb object file name (absolute path is required)
   *    handler(in)         : a function for handling/process a batch
   */
  int split (int batch_size, const std::string &object_file_name, class_install_handler &c_handler,
	     batch_handler &b_handler);

} // namespace cubload

// alias declaration for legacy C files
using load_stats = cubload::stats;

#endif /* _LOAD_COMMON_HPP_ */
