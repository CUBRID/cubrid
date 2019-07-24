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

#include "dbtype_def.h"
#include "dbtype.h"
#include "log_lsa.hpp"
#include "packable_object.hpp"
#include "record_descriptor.hpp"
#include "storage_common.h"
#include "transaction_group.hpp"

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
      replication_object (const LOG_LSA &lsa_stamp);
      virtual int apply (void) = 0;
      // todo: const
      virtual void stringify (string_buffer &str) = 0;
      // todo: unreferenced
      virtual bool is_instance_changing_attr (const OID &inst_oid);
      // todo: unreferenced
      virtual bool is_statement_replication ();

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

      sbr_repl_entry (const char *statement, const char *user, const char *sys_prm_ctx, const LOG_LSA &lsa_stamp);

      sbr_repl_entry () = default;
      ~sbr_repl_entry () = default;

      int apply () override;

      void set_params (const char *statement, const char *user, const char *sys_prm_ctx);

      void append_statement (const char *buffer, const size_t buf_size);

      bool is_equal (const cubpacking::packable_object *other);
      bool is_statement_replication ();

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
      void set_class_name (const char *class_name);
      single_row_repl_entry (const repl_entry_type type, const char *class_name, const LOG_LSA &lsa_stamp);
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

      rec_des_row_repl_entry (repl_entry_type type, const char *class_name, const RECDES &rec_des,
			      const LOG_LSA &lsa_stamp);

      rec_des_row_repl_entry () = default;
      ~rec_des_row_repl_entry ();

      int apply () override;

      virtual void pack (cubpacking::packer &serializator) const final;
      virtual void unpack (cubpacking::unpacker &deserializator) final;
      virtual std::size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const final;

      bool is_equal (const cubpacking::packable_object *other) final;
      void stringify (string_buffer &str) final;

    private:
      record_descriptor m_rec_des;
  };

  class changed_attrs_row_repl_entry : public single_row_repl_entry
  {
    public:
      static const int PACKING_ID = 4;

      changed_attrs_row_repl_entry (repl_entry_type type, const char *class_name, const OID &inst_oid,
				    const LOG_LSA &lsa_stamp);

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

  class repl_gc_info : public replication_object
  {
    public:
      struct tran_info
      {
	MVCCID m_mvccid;
	TRAN_STATE m_tran_state;
      };

      static const int PACKING_ID = 5;

      explicit repl_gc_info (const tx_group &tx_group_node);
      repl_gc_info () = default;
      ~repl_gc_info () = default;

      tx_group as_tx_group () const;

      int apply () override;
      void pack (cubpacking::packer &serializator) const;
      void unpack (cubpacking::unpacker &deserializator);
      std::size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const;
      void stringify (string_buffer &str) override final;

    private:
      std::vector<tran_info> m_gc_trans;
  };

  /* packs multiple records (record_descriptor/recdes) from heap to be copied into a new replicated server */
  class multirow_object : public replication_object
  {
    public:
      static const size_t DATA_PACK_THRESHOLD_SIZE = 16384;
      static const size_t DATA_PACK_THRESHOLD_CNT = 100;

      static const int PACKING_ID = 6;

      multirow_object (const char *class_name);

      multirow_object () = default;

      ~multirow_object ();

      void reset ();

      int apply () override;

      void pack (cubpacking::packer &serializator) const final;
      void unpack (cubpacking::unpacker &deserializator) final;
      std::size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const final;

      bool is_equal (const cubpacking::packable_object *other) final;
      void stringify (string_buffer &str) final;

      void move_record (record_descriptor &&record);

      bool is_pack_needed (void)
      {
	return m_rec_des_list.size () >= DATA_PACK_THRESHOLD_CNT || m_data_size >= DATA_PACK_THRESHOLD_SIZE;
      }

      size_t get_rec_cnt () const
      {
	return m_rec_des_list.size ();
      }

    private:
      std::vector<record_descriptor> m_rec_des_list;
      std::string m_class_name;

      /* non-serialized data members: */
      /* total size of all record_descriptor buffers */
      std::size_t m_data_size;
  };

  class savepoint_object : public replication_object
  {
    public:
      static const int PACKING_ID = 7;

      enum event_type
      {
	CREATE_SAVEPOINT,
	ROLLBACK_TO_SAVEPOINT
      };

      savepoint_object () = default;
      savepoint_object (const char *savepoint_name, event_type event);
      ~savepoint_object () = default;

      int apply () override;
      void stringify (string_buffer &str) final;

      void pack (cubpacking::packer &serializator) const final;
      void unpack (cubpacking::unpacker &deserializator) final;
      std::size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const final;

    private:
      std::string m_savepoint_name;
      event_type m_event;
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_OBJECT_HPP_ */
