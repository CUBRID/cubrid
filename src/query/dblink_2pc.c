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
  int num_ids = 0, id_size = sizeof (int);
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

      ids = (char *) malloc (num_ids * id_size);
      if (ids == NULL)
	{
	  return -1;
	}

      dblink = dblink_conn;
      while (dblink && dblink->is_2pc_participant)
	{
	  memcpy (ids + (nth++) * id_size, &(dblink->conn_handle), id_size);
	  dblink = dblink->next;
	}

      *block_particps_ids = (void *) ids;
    }

  *partid_len = num_ids * id_size;

  return num_ids;
}

bool
dblink_2pc_send_prepare (THREAD_ENTRY * thread_p, int gtrid, int num_particps, void *block_particps_ids)
{
  int nth = 0;
  XID xid;
  T_CCI_ERROR err_buf;
  DBLINK_CONN_ENTRY *dblink = qmgr_dblink_get_conn_entry (thread_p);

  xid.formatID = 1105;
  xid.gtrid_length = sizeof (int);
  xid.bqual_length = sizeof (int);

  while (dblink && dblink->is_2pc_participant)
    {
      memcpy (xid.data, &gtrid, xid.gtrid_length);
      memcpy (xid.data + xid.gtrid_length, (char *) block_particps_ids + (nth++) * xid.bqual_length, xid.bqual_length);
      if (cci_xa_prepare (dblink->conn_handle, &xid, &err_buf) != NO_ERROR)
	{
	  return false;
	}
      dblink = dblink->next;
    }

  assert (nth <= num_particps);

  return true;
}

void
dblink_2pc_send_commit (THREAD_ENTRY * thread_p, int gtrid, int num_particps, bool * particps_ack,
			void *block_particps_ids)
{
  int nth = 0, ack;
  XID xid;
  T_CCI_ERROR err_buf;
  DBLINK_CONN_ENTRY *dblink = qmgr_dblink_get_conn_entry (thread_p);

  xid.formatID = 1105;		/* for ver. 11.5 */
  xid.gtrid_length = sizeof (int);
  xid.bqual_length = sizeof (int);

  assert (particps_ack != NULL);

  while (dblink)
    {
      if (dblink->is_2pc_participant)
	{
	  memcpy (xid.data, &gtrid, xid.gtrid_length);
	  memcpy (xid.data + xid.gtrid_length, (char *) block_particps_ids + nth * xid.bqual_length, xid.bqual_length);
	  /* for participant, no check for commit whether a participant is fail or not */
	  ack = cci_xa_end_tran (dblink->conn_handle, &xid, CCI_TRAN_COMMIT, &err_buf);
	  if (ack == NO_ERROR)
	    {
	      particps_ack[nth] = true;
	    }

	  nth++;
	}

      dblink = dblink->next;
    }

  qmgr_dblink_clear_conn_entry (thread_p);

  assert (nth <= num_particps);

  return;
}

void
dblink_2pc_send_abort (THREAD_ENTRY * thread_p, int gtrid, int num_particps, bool * particps_ack,
		       void *block_particps_ids, bool collect)
{
  int nth = 0, ack;
  XID xid;
  T_CCI_ERROR err_buf;
  DBLINK_CONN_ENTRY *dblink = qmgr_dblink_get_conn_entry (thread_p);

  xid.formatID = 1105;		/* for ver. 11.5 */
  xid.gtrid_length = sizeof (int);
  xid.bqual_length = sizeof (int);

  if (collect)
    {
      assert (particps_ack != NULL);
    }

  while (dblink)
    {
      if (dblink->is_2pc_participant)
	{
	  memcpy (xid.data, &gtrid, xid.gtrid_length);
	  memcpy (xid.data + xid.gtrid_length, (char *) block_particps_ids + nth * xid.bqual_length, xid.bqual_length);
	  /* for participant, no check for abort whether a participant is fail or not */
	  ack = cci_xa_end_tran (dblink->conn_handle, &xid, CCI_TRAN_ROLLBACK, &err_buf);
	  if (collect && ack == NO_ERROR)
	    {
	      particps_ack[nth] = true;
	    }

	  nth++;
	}

      dblink = dblink->next;
    }

  qmgr_dblink_clear_conn_entry (thread_p);

  assert (nth <= num_particps);

  return;
}
