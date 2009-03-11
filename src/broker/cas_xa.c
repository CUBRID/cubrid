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
 * cas_xa.c -
 */

#ident "$Id$"

#include "cas_common.h"
#include "cas.h"
#include "cas_net_buf.h"
#include "cas_network.h"
#include "cas_log.h"
#include "cas_function.h"

#include "cas_execute.h"
#include "cas_db_inc.h"

#include "xa.h"
#define CAS_SUPPORT_XA

#define MAX_GTRIDS	100

#ifdef CAS_SUPPORT_XA
static int net_arg_get_xid (XID * xid, char *buf);
static void net_buf_cp_xid (T_NET_BUF * net_buf, XID * xid);
static int compare_xid (XID * xid1, XID * xid2);
#endif

int xa_prepare_flag = 0;

CAS_FUNC_PROTOTYPE (fn_xa_prepare)
{
#ifdef CAS_SUPPORT_XA
  XID xid;
  int err_code, gtrid;

  if ((argc < 1) || (net_arg_get_xid (&xid, (char *) argv[0]) < 0))
    {
      net_buf_cp_int (net_buf, CAS_ER_ARGS, NULL);
      return 0;
    }

  gtrid = db_2pc_start_transaction ();
  if (gtrid < 0)
    {
      DB_ERR_MSG_SET (net_buf, gtrid);
      return 0;
    }

  err_code =
    db_set_global_transaction_info (gtrid, (void *) &xid, sizeof (XID));
  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      return 0;
    }

  err_code = db_2pc_prepare_transaction ();
  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      return 0;
    }

  xa_prepare_flag = 1;

  net_buf_cp_int (net_buf, 0, NULL);

  cas_log_write (0, TRUE, "xa_prepare");
#else
  net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_NOT_IMPLEMENTED, NULL);
#endif
  return 0;
}

CAS_FUNC_PROTOTYPE (fn_xa_recover)
{
#ifdef CAS_SUPPORT_XA
  int count;
  int gtrids[MAX_GTRIDS];
  int i, err_code;
  XID xid;

  count = db_2pc_prepared_transactions (gtrids, MAX_GTRIDS);
  if (count < 0)
    {
      DB_ERR_MSG_SET (net_buf, count);
      return 0;
    }

  net_buf_cp_int (net_buf, count, NULL);

  for (i = 0; i < count; i++)
    {
      err_code =
	db_get_global_transaction_info (gtrids[i], (void *) &xid,
					sizeof (XID));
      if (err_code < 0)
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return 0;
	}
      net_buf_cp_xid (net_buf, &xid);
    }

  cas_log_write (0, TRUE, "xa_recover");
#else
  net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_NOT_IMPLEMENTED, NULL);
#endif
  return 0;
}

CAS_FUNC_PROTOTYPE (fn_xa_end_tran)
{
#ifdef CAS_SUPPORT_XA
  int tran_type;
  XID xid, tmp_xid;
  int gtrids[MAX_GTRIDS];
  int i, err_code, gtrid, count;

  if ((argc < 2) || (net_arg_get_xid (&xid, (char *) argv[0]) < 0))
    {
      net_buf_cp_int (net_buf, CAS_ER_ARGS, NULL);
      return 0;
    }
  NET_ARG_GET_CHAR (tran_type, argv[1]);
  if (tran_type != CCI_TRAN_COMMIT && tran_type != CCI_TRAN_ROLLBACK)
    {
      net_buf_cp_int (net_buf, CAS_ER_TRAN_TYPE, NULL);
      return 0;
    }

  count = db_2pc_prepared_transactions (gtrids, MAX_GTRIDS);
  if (count < 0)
    {
      DB_ERR_MSG_SET (net_buf, count);
      return 0;
    }

  for (gtrid = -1, i = 0; i < count; i++)
    {
      err_code =
	db_get_global_transaction_info (gtrids[i], (void *) &tmp_xid,
					sizeof (XID));
      if (err_code < 0)
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return 0;
	}
      if (compare_xid (&xid, &tmp_xid) == 0)
	{
	  gtrid = gtrids[i];
	  break;
	}
    }

  if (gtrid >= 0)
    {
#if 0
      ux_end_tran (CCI_TRAN_ROLLBACK);
#endif

      err_code = db_2pc_attach_transaction (gtrid);
      if (err_code < 0)
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return 0;
	}

      ux_end_tran (tran_type, TRUE);
      xa_prepare_flag = 1;
    }

  net_buf_cp_int (net_buf, 0, NULL);
  cas_log_write (0, TRUE, "xa_end_tran %d", tran_type);
#else
  net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_NOT_IMPLEMENTED, NULL);
#endif
  return -1;
}

#ifdef CAS_SUPPORT_XA
static int
net_arg_get_xid (XID * xid, char *buf)
{
  int data_size;
  int i, id[3];

  memset (xid, 0, sizeof (XID));

  memcpy (&data_size, buf, 4);
  data_size = ntohl (data_size);
  buf += 4;

  for (i = 0; i < 3; i++)
    {
      if (data_size < 4)
	return -1;
      memcpy (&id[i], buf, 4);
      id[i] = ntohl (id[i]);
      buf += 4;
      data_size -= 4;
    }

  if (data_size < id[1] + id[2])
    return -1;

  xid->formatID = id[0];
  xid->gtrid_length = id[1];
  xid->bqual_length = id[2];
  memcpy (xid->data, buf, id[1] + id[2]);
  return 0;
}

static void
net_buf_cp_xid (T_NET_BUF * net_buf, XID * xid)
{
  net_buf_cp_int (net_buf, 12 + xid->gtrid_length + xid->bqual_length, NULL);
  net_buf_cp_int (net_buf, xid->formatID, NULL);
  net_buf_cp_int (net_buf, xid->gtrid_length, NULL);
  net_buf_cp_int (net_buf, xid->bqual_length, NULL);
  net_buf_cp_str (net_buf, xid->data, xid->gtrid_length + xid->bqual_length);
}

static int
compare_xid (XID * xid1, XID * xid2)
{
  if (xid1->formatID != xid2->formatID)
    return -1;
  if (xid1->gtrid_length != xid2->gtrid_length)
    return -1;
  if (xid1->bqual_length != xid2->bqual_length)
    return -1;
  return (memcmp
	  (xid1->data, xid2->data, xid1->gtrid_length + xid1->bqual_length));
}
#endif
