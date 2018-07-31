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
 * common.hpp - common code used by loader
 */

#ifndef _COMMON_HPP_
#define _COMMON_HPP_

#ident "$Id$"

#include <cassert>
#include <cstddef>
#include <fstream>

#include "error_code.h"

namespace cubload
{

  /*
   * These are the "types" of strings that the lexer recognizes.  The
   * loader can specialize on each type.
   * These values are used to set up a vector of type setting functions, based
   * on information about each attribute parsed in the %CLASS line.
   * The setter functions are invoked using the enumerated type as an index into
   * the function vector. This gives us a significant saving when processing
   * values in the instance line, over the previous loader.
   */

  enum LDR_TYPE
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
    LDR_TIMELTZ,
    LDR_TIMETZ,
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
    LDR_BIGINT,
    LDR_DATETIME,
    LDR_DATETIMELTZ,
    LDR_DATETIMETZ,
    LDR_JSON,

    LDR_TYPE_MAX = LDR_JSON
  };

  /*
   * LDR_ATTRIBUTE_TYPE
   *
   * attribute type identifiers for ldr_act_restrict_attributes().
   * These attributes are handled specially since there modify the class object
   * directly.
   */

  enum LDR_ATTRIBUTE_TYPE
  {
    LDR_ATTRIBUTE_ANY = 0,
    LDR_ATTRIBUTE_SHARED,
    LDR_ATTRIBUTE_CLASS,
    LDR_ATTRIBUTE_DEFAULT
  };

  enum LDR_INTERRUPT_TYPE
  {
    LDR_NO_INTERRUPT,
    LDR_STOP_AND_ABORT_INTERRUPT,
    LDR_STOP_AND_COMMIT_INTERRUPT
  };

  struct LDR_STRING
  {
    LDR_STRING *next;
    LDR_STRING *last;
    char *val;
    size_t size;
    bool need_free_val;
    bool need_free_self;
  };

  struct LDR_CONSTRUCTOR_SPEC
  {
    LDR_STRING *id_name;
    LDR_STRING *arg_list;
  };

  struct LDR_CLASS_COMMAND_SPEC
  {
    int qualifier;
    LDR_STRING *attr_list;
    LDR_CONSTRUCTOR_SPEC *ctor_spec;
  };

  struct LDR_CONSTANT
  {
    LDR_CONSTANT *next;
    LDR_CONSTANT *last;
    void *val;
    int type;
    bool need_free;
  };

  struct LDR_OBJECT_REF
  {
    LDR_STRING *class_id;
    LDR_STRING *class_name;
    LDR_STRING *instance_number;
  };

  struct LDR_MONETARY_VALUE
  {
    LDR_STRING *amount;
    int currency_type;
  };

  // type aliases
  using string_t = LDR_STRING;
  using constant_t = LDR_CONSTANT;
  using object_ref_t = LDR_OBJECT_REF;
  using monetary_t = LDR_MONETARY_VALUE;
  using ctor_spec_t = LDR_CONSTRUCTOR_SPEC;
  using class_cmd_spec_t = LDR_CLASS_COMMAND_SPEC;

  class loader
  {
    public:
      virtual ~loader () = default;

      virtual void act_setup_class_command_spec (string_t **class_name, class_cmd_spec_t **cmd_spec) = 0;
      virtual void act_start_id (char *name) = 0;
      virtual void act_set_id (int id) = 0;
      virtual void act_start_instance (int id, constant_t *cons) = 0;
      virtual void process_constants (constant_t *cons) = 0;
      virtual void act_finish_line () = 0;
      virtual void act_finish () = 0;

      virtual void load_failed_error () = 0;
      virtual void increment_err_total () = 0;
      virtual void increment_fails () = 0;
  };

  ///////////////////// common global functions /////////////////////
  void ldr_string_free (string_t **str);
  void ldr_class_command_spec_free (class_cmd_spec_t **class_cmd_spec);

  template <typename Func>
  int split (int batch_size, std::string &object_file_name, Func &&func);

  bool starts_with (const std::string &str, const std::string &prefix);
  bool ends_with (const std::string &str, const std::string &suffix);

} // namespace cubload

namespace cubload
{

  template <typename Func>
  int
  split (int batch_size, std::string &object_file_name, Func &&func)
  {
    int rows = 0;
    std::string class_line;
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
	if (starts_with (line, "%id"))
	  {
	    // do nothing for now
	    continue;
	  }

	if (starts_with (line, "%class"))
	  {
	    // store class line
	    class_line = line;

	    // close current batch
	    func (batch_buffer);

	    // rewind forward rows counter until batch is full
	    for (; (rows % batch_size) != 0; rows++)
	      ;

	    // start new batch for the class
	    batch_buffer.clear ();
	    batch_buffer.append (class_line);
	    batch_buffer.append ("\n");
	    continue;
	  }

	// it is a line containing row data so append it
	batch_buffer.append (line);

	// it could be that a row is wrapped on the next line,
	// this means that the row ends on the last line that does not end with '+' (plus) character
	if (!ends_with (line, "+"))
	  {
	    // since std::getline eats end line character, add it back in order to make loaddb lexer happy
	    batch_buffer.append ("\n");

	    rows++;

	    // check if we have a full batch
	    if ((rows % batch_size) == 0)
	      {
		func (batch_buffer);

		// start new batch for the class
		batch_buffer.clear ();
		batch_buffer.append (class_line);
		batch_buffer.append ("\n");
	      }
	  }
      }

    // collect remaining rows
    func (batch_buffer);
    batch_buffer.clear ();

    object_file.close ();

    return NO_ERROR;
  }
} // namespace cubload

#endif //_COMMON_HPP_
