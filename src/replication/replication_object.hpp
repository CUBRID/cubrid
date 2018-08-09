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
#include "dbtype_def.h"

#include <vector>
#include <string>

class string_buffer;

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
      virtual void stringify (string_buffer &str) = 0;
  };

  class sbr_repl_entry : public replication_object
  {
    private:
      std::string m_statement;
      std::string m_db_user;
      std::string m_sys_prm_context;

    public:
      static const int PACKING_ID = 1;

      sbr_repl_entry (const char *statement, const char *user, const char *sys_prm_ctx);

      sbr_repl_entry () = default;
      ~sbr_repl_entry () = default;

      int apply () override;

      bool is_equal (const cubpacking::packable_object *other);

      int pack (cubpacking::packer *serializator);
      int unpack (cubpacking::packer *serializator);

      std::size_t get_packed_size (cubpacking::packer *serializator, std::size_t start_offset = 0);

      void stringify (string_buffer &str) override final;
  };

  class single_row_repl_entry : public replication_object
  {
    public:
      int apply () override;

      virtual bool is_equal (const cubpacking::packable_object *other);

      void set_key_value (DB_VALUE *db_val);

    protected:
      single_row_repl_entry (const REPL_ENTRY_TYPE type, const char *class_name);

      single_row_repl_entry () = default;
      virtual ~single_row_repl_entry ();

      virtual int pack (cubpacking::packer *serializator);
      virtual int unpack (cubpacking::packer *serializator);
      virtual std::size_t get_packed_size (cubpacking::packer *serializator, std::size_t start_offset = 0);

      void stringify (string_buffer &str) override;

      REPL_ENTRY_TYPE m_type;
      std::string m_class_name;
      DB_VALUE m_key_value;

    private:
  };

  class rec_des_row_repl_entry : public single_row_repl_entry
  {
    public:
      static const int PACKING_ID = 2;

      rec_des_row_repl_entry (REPL_ENTRY_TYPE type, const char *class_name, RECDES *rec_des);

      rec_des_row_repl_entry () = default;
      ~rec_des_row_repl_entry ();

      virtual int pack (cubpacking::packer *serializator) override final;
      virtual int unpack (cubpacking::packer *serializator) override final;
      virtual std::size_t get_packed_size (cubpacking::packer *serializator, std::size_t start_offset = 0) override final;

      bool is_equal (const cubpacking::packable_object *other) override final;
      void stringify (string_buffer &str) override final;

    private:
      RECDES m_rec_des;
  };

  class changed_attrs_row_repl_entry : public single_row_repl_entry
  {
    public:
      static const int PACKING_ID = 3;

      changed_attrs_row_repl_entry (REPL_ENTRY_TYPE type, const char *class_name, const OID *inst_oid = NULL);

      changed_attrs_row_repl_entry () = default;
      ~changed_attrs_row_repl_entry ();

      void copy_and_add_changed_value (const ATTR_ID att_id,
				       DB_VALUE *db_val);

      virtual int pack (cubpacking::packer *serializator) override final;
      virtual int unpack (cubpacking::packer *serializator) override final;
      virtual std::size_t get_packed_size (cubpacking::packer *serializator, std::size_t start_offset = 0) override final;

      bool is_equal (const cubpacking::packable_object *other) override final;
      void stringify (string_buffer &str) override final;

      inline bool compare_inst_oid (const OID *other)
      {
	return m_inst_oid.pageid == other->pageid &&
	       m_inst_oid.slotid == other->slotid &&
	       m_inst_oid.volid == other->volid;
      }

    private:
      std::vector <int> m_changed_attributes;
      std::vector <DB_VALUE> m_new_values;

      OID m_inst_oid;
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_OBJECT_HPP_ */
