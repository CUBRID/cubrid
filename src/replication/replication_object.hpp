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

  class replication_object : public cubpacking::packable_object
  {
    public:
      replication_object ();
      replication_object (LOG_LSA &lsa_stamp);
      virtual int apply (void) = 0;
      virtual void stringify (string_buffer &str) = 0;
      virtual bool is_instance_changing_attr (const OID &inst_oid);

      void get_lsa_stamp (LOG_LSA &lsa_stamp);
      void set_lsa_stamp (const LOG_LSA &lsa_stamp);

    protected:
      /* Used to detect whether an object was created inside of a sysop. */
      LOG_LSA m_lsa_stamp;
  };

  class sbr_repl_entry : public replication_object
  {
    private:
      std::string m_statement;
      std::string m_db_user;
      std::string m_sys_prm_context;

    public:
      static const int PACKING_ID = 1;

      sbr_repl_entry (const char *statement, const char *user, const char *sys_prm_ctx, LOG_LSA &lsa_stamp);

      sbr_repl_entry () = default;
      ~sbr_repl_entry () = default;

      int apply () override;

      bool is_equal (const cubpacking::packable_object *other);

      void pack (cubpacking::packer &serializator) const;
      void unpack (cubpacking::unpacker &deserializator);

      std::size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const;

      void stringify (string_buffer &str) override final;
  };

  class single_row_repl_entry : public replication_object
  {
    public:
      static const int PACKING_ID = 2;

      virtual int apply () override;

      virtual bool is_equal (const cubpacking::packable_object *other);

      void set_key_value (const DB_VALUE &db_val);
      single_row_repl_entry (const repl_entry_type type, const char *class_name, LOG_LSA &lsa_stamp);
      single_row_repl_entry () = default;

    protected:
      virtual ~single_row_repl_entry ();

      virtual void pack (cubpacking::packer &serializator) const;
      virtual void unpack (cubpacking::unpacker &deserializator);
      virtual std::size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const;

      void stringify (string_buffer &str) override;

      repl_entry_type m_type;
      std::string m_class_name;
      DB_VALUE m_key_value;

    private:
  };

  class rec_des_row_repl_entry : public single_row_repl_entry
  {
    public:
      static const int PACKING_ID = 3;

      rec_des_row_repl_entry (repl_entry_type type, const char *class_name, const RECDES &rec_des, LOG_LSA &lsa_stamp);

      rec_des_row_repl_entry () = default;
      ~rec_des_row_repl_entry ();

      int apply () override;

      virtual void pack (cubpacking::packer &serializator) const final;
      virtual void unpack (cubpacking::unpacker &deserializator) final;
      virtual std::size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const final;

      bool is_equal (const cubpacking::packable_object *other) final;
      void stringify (string_buffer &str) final;

    private:
      RECDES m_rec_des;
  };

  class changed_attrs_row_repl_entry : public single_row_repl_entry
  {
    public:
      static const int PACKING_ID = 4;

      changed_attrs_row_repl_entry (repl_entry_type type, const char *class_name, const OID &inst_oid, LOG_LSA &lsa_stamp);

      changed_attrs_row_repl_entry () = default;
      ~changed_attrs_row_repl_entry ();

      void copy_and_add_changed_value (const ATTR_ID att_id, const DB_VALUE &db_val);

      int apply () override;

      virtual void pack (cubpacking::packer &serializator) const override final;
      virtual void unpack (cubpacking::unpacker &deserializator) override final;
      virtual std::size_t get_packed_size (cubpacking::packer &serializator,
					   std::size_t start_offset = 0) const override final;

      bool is_equal (const cubpacking::packable_object *other) override final;
      void stringify (string_buffer &str) override final;
      bool is_instance_changing_attr (const OID &inst_oid) override final;

      inline bool compare_inst_oid (const OID &other)
      {
	return (m_inst_oid.pageid == other.pageid && m_inst_oid.slotid == other.slotid
		&& m_inst_oid.volid == other.volid);
      }

    private:
      std::vector <int> m_changed_attributes;
      std::vector <DB_VALUE> m_new_values;

      OID m_inst_oid;
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_OBJECT_HPP_ */
