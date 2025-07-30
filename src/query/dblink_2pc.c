/*
 *
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

#ident "$Id$"

// dblink connection handling for distributed transaction
#include "connection_defs.h"
#include "thread_manager.hpp"
#include "query_manager.h"
#include "dblink_2pc.h"

#ifndef DBDEF_HEADER_
#define DBDEF_HEADER_
#endif

#include <cas_cci.h>
#include <cci_xa.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

int
dblink_2pc_get_participants (THREAD_ENTRY * thread_p, int *partid_len, void **block_particps_ids)
{
  int num_ids = 0, id_size = sizeof (DBLINK_CONN_INFO);
  char *ids;

  DBLINK_CONN_ENTRY *dblink_conn = qmgr_dblink_get_conn_entry (thread_p);
  DBLINK_CONN_ENTRY *dblink = dblink_conn;

  while (dblink && dblink->is_2pc_participant)
    {
      dblink = dblink->next;
      num_ids++;
    }

  *block_particps_ids = NULL;

  if (num_ids > 0)
    {
      int nth = 0;

      ids = (char *) calloc (num_ids, id_size);
      if (ids == NULL)
	{
	  return -1;
	}

      dblink = dblink_conn;
      while (dblink && dblink->is_2pc_participant)
	{
	  memcpy (ids + (nth++) * id_size, &(dblink->conn_info), id_size);
	  dblink = dblink->next;
	}

      *block_particps_ids = (void *) ids;
    }

  *partid_len = id_size;

  return num_ids;
}

bool
dblink_2pc_send_prepare (THREAD_ENTRY * thread_p, int gtrid, int num_particps, void *block_particps_ids)
{
  int i;
  XID xid;
  T_CCI_ERROR err_buf;
  DBLINK_CONN_INFO *dblink;

  xid.formatID = 1105;
  xid.gtrid_length = sizeof (int);
  xid.bqual_length = sizeof (int);

  dblink = (DBLINK_CONN_INFO *) block_particps_ids;
  for (i = 0; i < num_particps; i++)
    {
      memcpy (xid.data, &gtrid, xid.gtrid_length);
      memcpy (xid.data + xid.gtrid_length, &(dblink[i].conn_handle) + i * xid.bqual_length, xid.bqual_length);
      if (cci_xa_prepare (dblink[i].conn_handle, &xid, &err_buf) != NO_ERROR)
	{
	  int conn_handle =
	    cci_connect_with_url_ex (dblink[i].conn_url, dblink[i].user_name, dblink[i].password, &err_buf);

	  if (conn_handle != NO_ERROR)
	    {
	      return false;
	    }

	  if (cci_xa_prepare (dblink[i].conn_handle, &xid, &err_buf) != NO_ERROR)
	    {
	      return false;
	    }
	}
    }

  return true;
}

void
dblink_2pc_send_commit (THREAD_ENTRY * thread_p, int gtrid, int num_particps, bool * particps_ack,
			void *block_particps_ids)
{
  int i, ack;
  XID xid;
  T_CCI_ERROR err_buf;
  DBLINK_CONN_INFO *dblink;

  xid.formatID = 1105;		/* for ver. 11.5 */
  xid.gtrid_length = sizeof (int);
  xid.bqual_length = sizeof (int);

  assert (particps_ack != NULL);

  dblink = (DBLINK_CONN_INFO *) block_particps_ids;
  for (i = 0; i < num_particps; i++)
    {
      memcpy (xid.data, &gtrid, xid.gtrid_length);
      memcpy (xid.data + xid.gtrid_length, &(dblink[i].conn_handle) + i * xid.bqual_length, xid.bqual_length);
      ack = cci_xa_end_tran (dblink[i].conn_handle, &xid, CCI_TRAN_COMMIT, &err_buf);
      /* while recovery conn_handle would be invaild, so retry once */
      if (ack != NO_ERROR)
	{
	  int conn_handle =
	    cci_connect_with_url_ex (dblink[i].conn_url, dblink[i].user_name, dblink[i].password, &err_buf);

	  ack = cci_xa_end_tran (conn_handle, &xid, CCI_TRAN_COMMIT, &err_buf);
	}

      if (ack == NO_ERROR)
	{
	  particps_ack[i] = true;
	}
    }

  qmgr_dblink_clear_conn_entry (thread_p);

  return;
}

void
dblink_2pc_send_abort (THREAD_ENTRY * thread_p, int gtrid, int num_particps, bool * particps_ack,
		       void *block_particps_ids, bool collect)
{
  int i, ack;
  XID xid;
  T_CCI_ERROR err_buf;
  DBLINK_CONN_INFO *dblink;

  xid.formatID = 1105;		/* for ver. 11.5 */
  xid.gtrid_length = sizeof (int);
  xid.bqual_length = sizeof (int);

  if (collect)
    {
      assert (particps_ack != NULL);
    }

  dblink = (DBLINK_CONN_INFO *) block_particps_ids;
  for (i = 0; i < num_particps; i++)
    {
      memcpy (xid.data, &gtrid, xid.gtrid_length);
      memcpy (xid.data + xid.gtrid_length, &(dblink[i].conn_handle) + i * xid.bqual_length, xid.bqual_length);
      ack = cci_xa_end_tran (dblink[i].conn_handle, &xid, CCI_TRAN_ROLLBACK, &err_buf);
      /* while recovery conn_handle would be invaild, so retry once */
      if (ack != NO_ERROR)
	{
	  int conn_handle =
	    cci_connect_with_url_ex (dblink[i].conn_url, dblink[i].user_name, dblink[i].password, &err_buf);

	  ack = cci_xa_end_tran (conn_handle, &xid, CCI_TRAN_ROLLBACK, &err_buf);
	}

      /* for participant, no check for commit whether a participant is fail or not */
      if (collect && ack == NO_ERROR)
	{
	  particps_ack[i] = true;
	}
    }

  qmgr_dblink_clear_conn_entry (thread_p);

  return;
}
