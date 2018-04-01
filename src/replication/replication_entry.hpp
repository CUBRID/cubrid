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
 * replication_entry.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_ENTRY_HPP_
#define _REPLICATION_ENTRY_HPP_

#include "packable_object.hpp"
#include "dbtype.h"
#include "storage_common.h"
#include <vector>
#include <string>

/* TODO[arnia] : change these as constants */
#define STREAM_ENTRY_RBR 0
#define STREAM_ENTRY_SBR 1

class cubpacking::packer;

enum repl_entry_type
{
  REPL_UPDATE = 0,
  REPL_INSERT,
  REPL_DELETE
};
typedef enum repl_entry_type REPL_ENTRY_TYPE;

class sbr_repl_entry : public cubpacking::packable_object, public cubpacking::self_creating_object
{
private:
  std::string m_statement;

public:
  sbr_repl_entry () {};
  ~sbr_repl_entry () {};
  sbr_repl_entry (const std::string &str) { set_statement (str); };

  bool is_equal (const cubpacking::packable_object *other);

  void set_statement (const std::string &str) { m_statement = str; };

  int get_create_id (void) { return STREAM_ENTRY_SBR; };
  cubpacking::self_creating_object *create (void) { return new sbr_repl_entry (); };

  int pack (cubpacking::packer *serializator);
  int unpack (cubpacking::packer *serializator);

  size_t get_packed_size (cubpacking::packer *serializator);
};

class single_row_repl_entry : public cubpacking::packable_object, public cubpacking::self_creating_object
{
private:
  REPL_ENTRY_TYPE m_type;
  std::vector <int> changed_attributes;
  char m_class_name [SM_MAX_IDENTIFIER_LENGTH + 1];
  DB_VALUE m_key_value;
  std::vector <DB_VALUE> new_values;

public:
  single_row_repl_entry () {};
  ~single_row_repl_entry ();
  single_row_repl_entry (const REPL_ENTRY_TYPE m_type, const char *class_name);

  bool is_equal (const cubpacking::packable_object *other);

  void set_class_name (const char *class_name);

  void set_key_value (DB_VALUE *db_val);

  void add_changed_value (const int att_id, DB_VALUE *db_val);


  int get_create_id (void) { return STREAM_ENTRY_RBR; };
  cubpacking::self_creating_object *create (void) { return new single_row_repl_entry (); };

  int pack (cubpacking::packer *serializator);
  int unpack (cubpacking::packer *serializator);

  size_t get_packed_size (cubpacking::packer *serializator);
};


class replication_object_builder : public cubpacking::object_builder
{
public:
  replication_object_builder ();
};

#endif /* _REPLICATION_ENTRY_HPP_ */
