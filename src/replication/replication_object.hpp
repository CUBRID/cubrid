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
 * replication_object.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_OBJECT_HPP_
#define _REPLICATION_OBJECT_HPP_

#include "packable_object.hpp"
#include "dbtype.h"
#include "storage_common.h"
#include <vector>
#include <string>


namespace cubreplication
{

  enum repl_entry_type
  {
    REPL_UPDATE = 0,
    REPL_INSERT,
    REPL_DELETE,

    REPL_UNKNOWN
  };
  typedef enum repl_entry_type REPL_ENTRY_TYPE;

  class replication_object : public cubpacking::packable_object
  {
    public:
      virtual int apply (void) = 0;
  };

  class sbr_repl_entry : public replication_object
  {
    private:
      std::string m_statement;

    public:
      static const int PACKING_ID = 1;

      sbr_repl_entry ()
      {
      };

      ~sbr_repl_entry ()
      {
      };

      sbr_repl_entry (const std::string &str)
      {
	set_statement (str);
      };

      int apply ();

      bool is_equal (const cubpacking::packable_object *other);

      void set_statement (const std::string &str)
      {
	m_statement = str;
      };

      void pack (cubpacking::packer &serializator) const;
      void unpack (cubpacking::unpacker &deserializator);

      size_t get_packed_size (cubpacking::packer &serializator) const;
  };

  class single_row_repl_entry : public replication_object
  {
    private:
      REPL_ENTRY_TYPE m_type;
      std::vector <int> changed_attributes;
      char m_class_name [SM_MAX_IDENTIFIER_LENGTH + 1];
      DB_VALUE m_key_value;
      std::vector <DB_VALUE> m_new_values;

    public:
      static const int PACKING_ID = 2;

      single_row_repl_entry () : m_type (REPL_UNKNOWN)
      {
	m_class_name[0] = '\0';
	db_value_clear (&m_key_value);
      };

      ~single_row_repl_entry ();

      single_row_repl_entry (const REPL_ENTRY_TYPE m_type, const char *class_name);

      int apply ();

      bool is_equal (const cubpacking::packable_object *other);

      void set_class_name (const char *class_name);

      void set_key_value (DB_VALUE *db_val);

      void copy_and_add_changed_value (const int att_id, DB_VALUE *db_val);

      void pack (cubpacking::packer &serializator) const;
      void unpack (cubpacking::unpacker &deserializator);

      size_t get_packed_size (cubpacking::packer &serializator) const;
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_OBJECT_HPP_ */
