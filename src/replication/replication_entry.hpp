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

#include <vector>
#include "dbtype.h"
#include "storage_common.h"

class replication_serialization;

typedef enum repl_entry_type REPL_ENTRY_TYPE;
enum repl_entry_type
{
  REPL_UPDATE = 0,
  REPL_INSERT,
  REPL_DELETE
};

class replication_entry
{
protected:
  size_t packed_size;
public:
  virtual int pack (replication_serialization *serializator) = 0;
  virtual int unpack (replication_serialization *serializator) = 0;
  virtual size_t get_packed_size (replication_serialization *serializator) = 0;

  static const size_t UNDEFINED_SIZE = -1;
};

class sbr_repl_entry : public replication_entry
{
private:
  std::string statement;

public:
  sbr_repl_entry () { packed_size = -1; };
  virtual size_t get_packed_size (replication_serialization *serializator) = 0;
  int pack (replication_serialization *serializator);
  int unpack (replication_serialization *serializator);
};

class single_row_repl_entry : public replication_entry
{
private:
  DB_VALUE key_value;
  char class_name [SM_MAX_IDENTIFIER_LENGTH];
  std::vector <int> changed_attributes;
  std::vector <DB_VALUE> new_values;
  REPL_ENTRY_TYPE m_type;

public:
  single_row_repl_entry () { packed_size = -1; };
  virtual size_t get_packed_size (replication_serialization *serializator) = 0;
  int pack (replication_serialization *serializator);
  int unpack (replication_serialization *serializator);
};

#endif /* _REPLICATION_ENTRY_HPP_ */
