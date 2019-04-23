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
 * replication_db_copy.cpp
 */

#ident "$Id$"

#include "replication_db_copy.hpp"
#include "heap_file.h"
#include "locator_sr.h"
#include "object_representation.h"
#include "packer.hpp"
#include "record_descriptor.hpp"
#include "scan_manager.h"
#include "string_buffer.hpp"
#include "thread_entry.hpp"

#include <algorithm>
#include <cstddef>


namespace cubreplication
{

  void copy_context::pack_and_add_object (row_object &obj)
    {
      stream_entry stream_entry (m_stream);

      stream_entry.add_packable_entry (&obj);

      stream_entry.pack ();
    }



  row_object::row_object (const char *class_name)
    {
      m_class_name = class_name;
      m_copyarea = NULL;
      m_data_size = 0;
    }

  row_object::~row_object ()
    {
      reset ();
    }

  void row_object::reset (void)
    {
      m_data_size = 0;
      if (m_copyarea != NULL)
        {
          locator_free_copy_area (m_copyarea);
          m_copyarea = NULL;
        }

      for (auto rec : m_rec_des_list)
        {
          rec.~record_descriptor ();
        }
      m_rec_des_list.clear ();
    }


  int row_object::apply (void)
  {
    /* TODO[replication] */
    return NO_ERROR;
  }

  void row_object::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (row_object::PACKING_ID);
    serializator.pack_string (m_class_name);    
    serializator.pack_int ((int) m_rec_des_list.size ());

    for (auto rec : m_rec_des_list)
      {
        rec.pack (serializator);
      }
  }

  void row_object::unpack (cubpacking::unpacker &deserializator)
  {
    int entry_type_not_used;

    deserializator.unpack_int (entry_type_not_used);

    deserializator.unpack_string (m_class_name);
    int rec_des_cnt = 0;

    deserializator.unpack_int (rec_des_cnt);

    for (int i = 0; i < rec_des_cnt; i++)
      {
        m_rec_des_list.emplace_back ();
        record_descriptor &rec = m_rec_des_list.back ();
        rec.unpack (deserializator);
      }
  }

  std::size_t row_object ::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    std::size_t entry_size = start_offset;

    entry_size += serializator.get_packed_int_size (0);

    entry_size += serializator.get_packed_string_size (m_class_name, entry_size);
    entry_size += serializator.get_packed_int_size (entry_size);
    for (auto rec : m_rec_des_list)
      {
        entry_size += rec.get_packed_size (serializator, entry_size);
      }

    return entry_size;
  }


  bool row_object::is_equal (const cubpacking::packable_object *other)
  {
    const row_object *other_t = dynamic_cast<const row_object *> (other);

    if (other_t == NULL)
      {
	return false;
      }

    if (m_class_name.compare (other_t->m_class_name) != 0)
      {
	return false;
      }

    if (m_rec_des_list.size () != other_t->m_rec_des_list.size ())
      {
        return false;
      }

    for (int i = 0; i < m_rec_des_list.size (); i++)
      {
        if (m_rec_des_list[i].get_size () != other_t->m_rec_des_list[i].get_size ())
          {
            return false;
          }

        if (m_rec_des_list[i].get_recdes ().type != other_t->m_rec_des_list[i].get_recdes ().type)
          {
            return false;
          }

        if (std::memcmp (m_rec_des_list[i].get_recdes ().data, other_t->m_rec_des_list[i].get_recdes().data, m_rec_des_list[i].get_size ()) != 0)
          {
            return false;
          }
      }
    return true;
    }

  void row_object::stringify (string_buffer &str)
  {
    str ("row_object::row_object table=%s records_cnt:%d\n", m_class_name.c_str (), m_rec_des_list.size ());
    for (int i = 0; i < m_rec_des_list.size (); i++)
      {
	string_buffer sb_hex;

	size_t buf_size = m_rec_des_list[i].get_size ();

	sb_hex.hex_dump (m_rec_des_list[i].get_recdes ().data, buf_size);
        str ("\trecord:%d, size:%d\n", i, buf_size);
        buf_size = std::min (buf_size, (size_t) 256);
        str.add_bytes (sb_hex.len (), sb_hex.get_buffer ());
      }
  }
  
  void row_object::add_record (const record_descriptor &record)
  {
    m_rec_des_list.push_back (record);
    m_data_size += record.get_recdes ().length;
  }

  int row_object::convert_to_last_representation (cubthread::entry *thread_p, record_descriptor &record,
                                                  const OID &inst_oid, HEAP_CACHE_ATTRINFO &attr_info)
  {
    int error_code = NO_ERROR;
    RECDES *old_recdes = const_cast <RECDES *>(&(record.get_recdes ()));
    const int reprid = or_rep_id (old_recdes);
    RECDES new_recdes;

    if (reprid == attr_info.last_classrepr->id)
      {
        /* nothing to to */
        return error_code;
      }


    error_code = heap_attrinfo_read_dbvalues (thread_p, &inst_oid, old_recdes, NULL, &attr_info);
    if (error_code != NO_ERROR)
      {
        return error_code;
      }

    assert (m_copyarea == NULL);

    /* TODO[replication] : to reuse copyarea */
    m_copyarea =
	locator_allocate_copy_area_by_attr_info (thread_p, &attr_info, old_recdes, &new_recdes, -1,
						 LOB_FLAG_EXCLUDE_LOB);
    if (m_copyarea == NULL)
      {
	error_code = ER_FAILED;
        goto end;
      }

    record.~record_descriptor ();
    new (&record) record_descriptor (new_recdes);

  end:
    if (m_copyarea != NULL)
      {
        locator_free_copy_area (m_copyarea);
        m_copyarea = NULL;
      }

    return error_code;
  }

  static void
  create_scan_for_replication_copy (cubthread::entry *thread_p, SCAN_ID &s_id, OID &class_oid, HFID &class_hfid)
  {
    const bool mvcc_select_lock_needed = false;
    SCAN_OPERATION_TYPE scan_op_type = S_SELECT;
    int fixed = true;
    int grouped = true;
    QPROC_SINGLE_FETCH single_fetch = QPROC_NO_SINGLE_INNER;
    SCAN_TYPE scan_type = S_HEAP_SCAN;
    int error = NO_ERROR;

    error = scan_open_heap_scan (thread_p, &s_id, mvcc_select_lock_needed, scan_op_type, fixed, grouped, single_fetch,
			       NULL, /* join_dbval */
                               NULL, /* val_list */
                               NULL, /* vd */
                               &class_oid, &class_hfid,
                               NULL, /* regu_variable_list_node*/
    			       NULL, /* pr */
                               NULL, /* regu_list_rest */
                               0, /* num_attrs_pred */
                               NULL, /* attrids_pred */
                               NULL, /* cache_pred */
                               0, /* num_attrs_rest */
                               NULL, /* attrids_rest*/
                               NULL, /* cache_rest */
			       scan_type,
                               NULL, /* cache_recordinfo */
                               NULL /* regu_list_recordinfo*/
      );

    assert (error == NO_ERROR);
    }

  int copy_class (cubthread::entry *thread_p, OID &class_oid)
  {
    SCAN_ID s_id;
    SCAN_CODE sc_scan;
    HEAP_CACHE_ATTRINFO attr_info;
    HFID class_hfid;
    bool attr_info_inited = false;
    int error_code = NO_ERROR;
    char *class_name;
    LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

    assert (tdes != NULL);

    error_code = heap_get_class_name (thread_p, &class_oid, &class_name);
    if (error_code != NO_ERROR)
      {
        return error_code;
      }

    row_object heap_objects (class_name);

    error_code = heap_get_hfid_from_class_oid (thread_p, &class_oid, &class_hfid);
    if (error_code != NO_ERROR)
      {
        goto end;
      }

    error_code = heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info);
    if (error_code != NO_ERROR)
      {
        goto end;
      }

    attr_info_inited = true;

    create_scan_for_replication_copy (thread_p, s_id, class_oid, class_hfid);

    do
      {
        sc_scan = scan_next_scan (thread_p, &s_id);
        if (sc_scan == S_END)
          {
            break;
          }

        if (sc_scan != S_SUCCESS)
          {
            error_code = ER_FAILED;
	    goto end;
	  }

        error_code = heap_objects.convert_to_last_representation (thread_p, *s_id.s.hsid.row_record,
                                                                  s_id.s.hsid.curr_oid, attr_info);
        if (error_code != NO_ERROR)
          {
	    goto end;
	  }
        heap_objects.add_record (*s_id.s.hsid.row_record);

        if (heap_objects.is_pack_needed ())
          {
            /* pack and add to stream */
            tdes->replication_copy_context->pack_and_add_object (heap_objects);
            heap_objects.reset ();
          }
      }
    while (1);

    end:
      if (attr_info_inited)
        {
          heap_attrinfo_end (thread_p, &attr_info);
          attr_info_inited = false;
        }

      scan_end_scan (thread_p, &s_id);

      return error_code;
  }

} /* namespace cubreplication */
