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
 * replication_schema_extract.cpp
 */

#include "replication_schema_extract.hpp"
#include "extract_schema.hpp"
#include "network.h"
#include "network_interface_cl.h"
#include "unloaddb.h" // for extract_schema, extract_triggers, emit_indexes

namespace cubreplication
{

  net_print_output::net_print_output (const int buffer_type, const size_t flush_size)
    : m_buffer_type (buffer_type)
    , m_flush_size (flush_size)
  {
    m_send_error_cnt = 0;
  }

  int net_print_output::flush ()
  {
    if (m_sb.len () > m_flush_size)
      {
	return send_to_network ();
      }
    return 0;
  }

  void net_print_output::end_item (const char *item)
  {
     m_id = item;
    (void) send_to_network ();
  }

  int net_print_output::send_to_network ()
  {
    int res = (int) m_sb.len ();
    int error;

    error = locator_send_proxy_buffer (m_buffer_type, m_sb.len (), m_sb.get_buffer ());
    if (error != NO_ERROR)
      {
	m_send_error_cnt++;
	return 0;
      }

    m_sb.clear ();

    return res;
  }

} /* namespace cubreplication */


int send_class_list (DB_OBJLIST *classes)
{
  DB_OBJLIST *cl;
  int cnt_classes = 0;
  int error = NO_ERROR;

  cubmem::extensible_block blk;
  cubpacking::packer packer;

  for (cl = classes; cl != NULL; cl = cl->next)
    {
      ++cnt_classes;
    }

  blk.extend_to (OR_INT_SIZE + cnt_classes * OR_OID_SIZE);
  packer.set_buffer_and_pack_all (blk, cnt_classes);

  for (cl = classes; cl != NULL; cl = cl->next)
    {
      OID oid = *ws_oid (cl->op);
      packer.append_to_buffer_and_pack_all (blk, oid);
    }

  error = locator_send_proxy_buffer (NET_PROXY_BUF_TYPE_OID_LIST, blk.get_size (), blk.get_read_ptr ());

  return error;
}

int replication_schema_extract (const char *program_name)
{
  int error = NO_ERROR;

  extract_context copy_schema_context;
  copy_schema_context.do_auth = 1;
  copy_schema_context.storage_order = FOLLOW_ATTRIBUTE_ORDER;
  copy_schema_context.exec_name = program_name;

  cubreplication::net_print_output output_net_schema (NET_PROXY_BUF_TYPE_EXTRACT_CLASSES);
  cubreplication::net_print_output output_net_trigger (NET_PROXY_BUF_TYPE_EXTRACT_TRIGGER);
  cubreplication::net_print_output output_net_index (NET_PROXY_BUF_TYPE_EXTRACT_INDEX);

  if (extract_classes (copy_schema_context, output_net_schema) != 0)
    {
      error = er_errid ();
    }
  output_net_schema.set_buffer_type (NET_PROXY_BUF_TYPE_EXTRACT_CLASSES_END);
  output_net_schema.send_to_network ();

  if (error == NO_ERROR && extract_triggers (copy_schema_context, output_net_trigger) != 0)
    {
      error = er_errid ();
    }
  output_net_trigger.set_buffer_type (NET_PROXY_BUF_TYPE_EXTRACT_TRIGGERS_END);
  output_net_trigger.send_to_network ();

  if (error == NO_ERROR && emit_indexes (output_net_index, copy_schema_context.classes,
					 copy_schema_context.has_indexes,
					 copy_schema_context.vclass_list_has_using_index) != 0)
    {
      error = er_errid ();
    }
  output_net_index.set_buffer_type (NET_PROXY_BUF_TYPE_EXTRACT_INDEXES_END);
  output_net_index.send_to_network ();

  if (error == NO_ERROR)
    {
      error = send_class_list (copy_schema_context.classes);
    }

  copy_schema_context.clear_schema_workspace ();

  return error;
}
