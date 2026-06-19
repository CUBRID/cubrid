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
#include "dblink_scan.h"
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

  while (dblink)
    {
      if (dblink->is_2pc_participant)
	{
	  num_ids++;
	}

      dblink = dblink->next;
    }

  *block_particps_ids = NULL;

  if (num_ids > 0)
    {
      int nth = 0;

      ids = (char *) calloc (num_ids, id_size);
      if (ids == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, num_ids * id_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      dblink = dblink_conn;
      while (dblink)
	{
	  if (dblink->is_2pc_participant)
	    {
	      memcpy (ids + (nth++) * id_size, &(dblink->conn_info), id_size);
	    }
	  dblink = dblink->next;
	}

      *block_particps_ids = (void *) ids;
    }

  *partid_len = id_size;

  return num_ids;
}

#ifdef CCI_XA
bool
dblink_2pc_send_prepare (THREAD_ENTRY * thread_p, int gtrid, int num_particps, void *block_particps_ids)
{
  int i;
  XID xid;
  T_CCI_ERROR err_buf;
  DBLINK_CONN_INFO *dblink;

  xid.formatID = MAJOR_VERSION * 100 + MINOR_VERSION;
  xid.gtrid_length = sizeof (int);
  xid.bqual_length = sizeof (int);

  dblink = (DBLINK_CONN_INFO *) block_particps_ids;
  for (i = 0; i < num_particps; i++)
    {
      memcpy (xid.data, &gtrid, xid.gtrid_length);
      memcpy (xid.data + xid.gtrid_length, &(dblink[i].conn_handle), xid.bqual_length);

      if (cci_xa_prepare (dblink[i].conn_handle, &xid, &err_buf) != NO_ERROR)
	{
	  return false;
	}
    }

  return true;
}

void
dblink_2pc_end_tran (THREAD_ENTRY * thread_p, int gtrid, int num_particps, bool is_commit, void *block_particps_ids)
{
  int i, err, conn_handle;
  XID xid;
  T_CCI_ERROR err_buf;
  DBLINK_CONN_INFO *dblink;
  char type;			/* for COMMIT or ABORT */

  xid.formatID = MAJOR_VERSION * 100 + MINOR_VERSION;
  xid.gtrid_length = sizeof (int);
  xid.bqual_length = sizeof (int);

  if (is_commit)
    {
      type = CCI_TRAN_COMMIT;
    }
  else
    {
      type = CCI_TRAN_ROLLBACK;
    }

  dblink = (DBLINK_CONN_INFO *) block_particps_ids;
  for (i = 0; i < num_particps; i++)
    {
      conn_handle = dblink[i].conn_handle;
      memcpy (xid.data, &gtrid, xid.gtrid_length);
      memcpy (xid.data + xid.gtrid_length, &(dblink[i].conn_handle), xid.bqual_length);

      do
	{
	  err = cci_xa_end_tran (conn_handle, &xid, type, &err_buf);
	  if (err != NO_ERROR)
	    {
	      do
		{
		  /* TODO: remove the sleep and sending decision repeatedly */
		  thread_sleep (1000);	/* wait 1 second for retry */
		  conn_handle =
		    cci_connect_with_url_ex (dblink[i].conn_url, dblink[i].user_name, dblink[i].password, &err_buf);
		}
	      while (conn_handle < 0);
	    }
	}
      while (err != NO_ERROR);
    }

  qmgr_dblink_clear_conn_entry (thread_p);

  return;
}

void
dblink_2pc_dump_participants (FILE * fp, int block_length, void *block_particps_ids)
{
  int i, participant_num = block_length / sizeof (DBLINK_CONN_INFO);
  DBLINK_CONN_INFO *dblink = (DBLINK_CONN_INFO *) block_particps_ids;

  assert (participant_num > 0);

  for (i = 0; i < participant_num; i++)
    {
      fprintf (fp, "  CONN-HANDLE = %d, CONN-URL = %s, USER = %s\n", dblink[i].conn_handle, dblink[i].conn_url,
	       dblink[i].user_name);
    }
}

/*
 * dblink_2pc_send_decision_one_participant - For coordinator recovery: send commit/abort to one participant.
 *   Reconnects using conn_url, user_name, password and sends XA end_tran with (gtrid, bqual).
 *   Returns NO_ERROR on success, ER_* on failure.
 */
int
dblink_2pc_send_decision_one_participant (int gtrid, DBLINK_CONN_INFO * participant, bool is_commit)
{
  int err, bqual, conn_handle;
  XID xid;
  T_CCI_ERROR err_buf;
  char type = is_commit ? CCI_TRAN_COMMIT : CCI_TRAN_ROLLBACK;
  char conn_url_gateway[MAX_LEN_CONNECTION_URL + 16];

  char *conn_url = participant->conn_url;
  char *user_name = participant->user_name;
  char *password = participant->password;

  conn_handle = bqual = participant->conn_handle;

  if (conn_url == NULL || user_name == NULL || password == NULL)
    {
      return ER_FAILED;
    }

  /* try to connect with conntion handle first */
  xid.formatID = MAJOR_VERSION * 100 + MINOR_VERSION;
  xid.gtrid_length = sizeof (int);
  xid.bqual_length = sizeof (int);
  memcpy (xid.data, &gtrid, xid.gtrid_length);
  memcpy (xid.data + xid.gtrid_length, &bqual, xid.bqual_length);

  err = cci_xa_end_tran (conn_handle, &xid, type, &err_buf);
  (void) cci_disconnect (conn_handle, &err_buf);

  if (err == CCI_ER_NO_ERROR)
    {
      return NO_ERROR;
    }

  /* try to connect for cci_xa_end_tran, maybe recoverying */
  if (strstr (conn_url, ":?"))
    {
      snprintf (conn_url_gateway, sizeof (conn_url_gateway), "%s%s", conn_url, "&__gateway=true");
    }
  else
    {
      snprintf (conn_url_gateway, sizeof (conn_url_gateway), "%s%s", conn_url, "?__gateway=true");
    }

  conn_handle = cci_connect_with_url_ex (conn_url_gateway, user_name, password, &err_buf);
  if (conn_handle < 0)
    {
      return ER_DBLINK;
    }

  err = cci_xa_end_tran (conn_handle, &xid, type, &err_buf);
  (void) cci_disconnect (conn_handle, &err_buf);

  if (err == CCI_ER_NO_ERROR)
    {
      return NO_ERROR;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);

  return ER_DBLINK;
}
#endif
