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
 * load_common.hpp - common code used by loader
 */

#ifndef _LOAD_COMMON_HPP_
#define _LOAD_COMMON_HPP_

#include "packable_object.hpp"

#include <atomic>
#include <cassert>
#include <functional>
#include <vector>

#define NUM_LDR_TYPES (LDR_TYPE_MAX + 1)
#define NUM_DB_TYPES (DB_TYPE_LAST + 1)

namespace cubload
{

  using batch_id = int64_t;
  using class_id = int;

  const class_id NULL_CLASS_ID = 0;
  const batch_id NULL_BATCH_ID = 0;
  const class_id FIRST_CLASS_ID = 1;
  const batch_id FIRST_BATCH_ID = 1;

  class batch : public cubpacking::packable_object
  {
    public:
      batch ();
      batch (batch_id id, class_id clsid, std::string &content, int64_t line_offset, int64_t rows);

      batch (batch &&other) noexcept; // MoveConstructible
      batch &operator= (batch &&other) noexcept; // MoveAssignable

      batch (const batch &copy) = delete; // Not CopyConstructible
      batch &operator= (const batch &copy) = delete; // Not CopyAssignable

      batch_id get_id () const;
      class_id get_class_id () const;
      int64_t get_line_offset () const;
      const std::string &get_content () const;
      int64_t get_rows_number () const;

      void pack (cubpacking::packer &serializator) const override;
      void unpack (cubpacking::unpacker &deserializator) override;
      size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    private:
      batch_id m_id;
      class_id m_clsid;
      std::string m_content;
      int64_t m_line_offset;
      int64_t m_rows;
  };

  using batch_handler = std::function<int64_t (const batch &)>;
  using class_handler = std::function<int64_t (const batch &, bool &)>;

  /*
   * loaddb executables command line arguments
   */
  struct load_args : public cubpacking::packable_object
  {
    load_args ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    int parse_ignore_class_file ();

    std::string volume;
    std::string input_file;
    std::string user_name;
    std::string password;
    bool syntax_check;
    bool load_only;
    int estimated_size;
    bool verbose;
    bool disable_statistics;
    int periodic_commit;
    bool verbose_commit;
    bool no_oid_hint;
    std::string schema_file;
    std::string index_file;
    std::string trigger_file;
    std::string object_file;
    std::string error_file;
    bool ignore_logging;
    bool compare_storage_order;
    std::string table_name;
    std::string ignore_class_file;
    std::vector<std::string> ignore_classes;
    std::vector<int> m_ignored_errors;
    static const int PERIODIC_COMMIT_DEFAULT_VALUE = 10240;
    bool no_user_specified_name;
    std::string multiload_schema_file;
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
    string_type ();
    string_type (char *val, std::size_t size, bool need_free_val);
    ~string_type ();

    void destroy ();

    string_type *next;
    string_type *last;
    char *val;
    size_t size;
    bool need_free_val;
  };

  struct constructor_spec_type
  {
    constructor_spec_type () = delete;
    constructor_spec_type (string_type *id_name, string_type *arg_list);
    ~constructor_spec_type () = default;

    string_type *id_name;
    string_type *arg_list;
  };

  struct class_command_spec_type
  {
    class_command_spec_type () = delete;
    class_command_spec_type (int attr_type, string_type *attr_list, constructor_spec_type *ctor_spec);
    ~class_command_spec_type () = default;

    attribute_type attr_type;
    string_type *attr_list;
    constructor_spec_type *ctor_spec;
  };

  struct constant_type
  {
    constant_type ();
    constant_type (data_type type, void *val);
    ~constant_type () = default;

    constant_type *next;
    constant_type *last;
    void *val;
    data_type type;
  };

  struct object_ref_type
  {
    object_ref_type () = delete;
    object_ref_type (string_type *class_id, string_type *class_name);
    ~object_ref_type () = default;

    string_type *class_id;
    string_type *class_name;
    string_type *instance_number;
  };

  struct monetary_type
  {
    monetary_type () = delete;
    monetary_type (string_type *amount, int currency_type);
    ~monetary_type () = default;

    string_type *amount;
    int currency_type;
  };

  struct stats : public cubpacking::packable_object
  {
    int64_t rows_committed; // equivalent of 'last_commit' from SA_MODE
    std::atomic<int64_t> current_line;
    int64_t last_committed_line;
    int rows_failed; // // equivalent of 'errors' from SA_MODE
    std::string error_message;
    std::string log_message;

    // Default constructor
    stats ();
    // Copy constructor
    stats (const stats &copy);
    // Assignment operator
    stats &operator= (const stats &other);

    void clear ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };

  class load_status : public cubpacking::packable_object
  {
    public:
      load_status ();
      load_status (bool is_load_completed, bool is_session_failed, std::vector<stats> &load_stats);

      load_status (load_status &&other) noexcept;
      load_status &operator= (load_status &&other) noexcept;

      load_status (const load_status &copy) = delete; // Not CopyConstructible
      load_status &operator= (const load_status &copy) = delete; // Not CopyAssignable

      bool is_load_completed ();
      bool is_load_failed ();
      std::vector<stats> &get_load_stats ();

      void pack (cubpacking::packer &serializator) const override;
      void unpack (cubpacking::unpacker &deserializator) override;
      size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    private:
      bool m_load_completed;
      bool m_load_failed;
      std::vector<stats> m_load_stats;
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
       * Function set class_id for class installer instance
       *
       *    return: void
       *    clsid(in): generated id of the class
       */
      virtual void set_class_id (class_id clsid) = 0;

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

      virtual void flush_records () = 0;

      virtual std::size_t get_rows_number () = 0;
  };

///////////////////// common global functions /////////////////////

  /*
   * Splits a loaddb object file into batches of a given size.
   *
   *    return: NO_ERROR in case of success or ER_FAILED if file does not exists
   *    batch_size(in)      : batch size
   *    object_file_name(in): loaddb object file name (absolute path is required)
   *    c_handler(in)       : a function for handling/process a %class or %id line from object file
   *    b_handler(in)       : a function for handling/process a batch of objects
   */
  int split (int batch_size, const std::string &object_file_name, class_handler &c_handler, batch_handler &b_handler);

} // namespace cubload

// alias declaration for legacy C files
using load_stats = cubload::stats;
using load_status = cubload::load_status;

#define IS_OLD_GLO_CLASS(class_name)                    \
	 (strncasecmp ((class_name), "glo", MAX(strlen(class_name), 3)) == 0      || \
	  strncasecmp ((class_name), "glo_name", MAX(strlen(class_name), 8)) == 0  || \
	  strncasecmp ((class_name), "glo_holder", MAX(strlen(class_name), 10)) == 0)

#endif /* _LOAD_COMMON_HPP_ */
